# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
from typing import TYPE_CHECKING, Any

from . import _planex as px
from ._sketches import sketch_types
from . import _plot_sketch
from ._geom import closed_loops, edge_views, parts, points, scale, xy
from ._plot_fields import _checked_mesh, _dim, _field_library, _is_mesh, _mesh_library
from ._plot_mesh import (_MeshRegion, _cut_lines, _field_mesh, _flat, _is_occ, _is_plane, _mesh_cut, _mesh_field,
                         _mesh_region, _on_xy, _plane_of, _region)
from ._plot_style import (OVERLAY_COL, _arrowheads, _colormap, _colorscale, _contours_arg, _css, _density,
                          _levels, _limits, _offscreen, _overlays, _polylines, _separated, _title, _backend)
from ._auto import Auto, is_auto
from ._caller import warn

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire
    from .types import Color, Ids, Plane

# the plotly figure takes the aspect ratio of the drawing, its plot area at most this many pixels
PLOTLY_BOX = (1000, 600)
# tab:orange, tab:green, tab:red, tab:purple
LINE_COL, ARC_COL, SPLINE_COL = "black", "#ff7f0e", "#2ca02c"
SMALL_COL, FREE_COL, MARK_COL = "#d62728", "#9467bd", "black"
# planex's face colour, as in the docs
FACE_COL = "#dde6f0"
# tab:gray, thin: a mesh's element edges
MESH_COL = "#7f7f7f"


def _length(edge):
    pts = points(edge)
    return sum(math.dist(p, q) for p, q in zip(pts, pts[1:]))


def _free_edges(edges):
    """edges with an endpoint that no other edge shares exactly"""
    count = {}
    for e in edges:
        for v in (e.start, e.end):
            count[xy(v)] = count.get(xy(v), 0) + 1
    return [i for i, e in enumerate(edges) if count[xy(e.start)] == 1 or count[xy(e.end)] == 1]


def _sampling(views):
    """how far a drawn chord may lie from its arc: 1e-4 of the bounding box's diagonal, below a pixel up to about 10x
    zoom"""
    boxes = [v.bounding_box for v in views]
    if not boxes:
        return None
    lo = (min(b[0][0] for b in boxes), min(b[0][1] for b in boxes))
    hi = (max(b[1][0] for b in boxes), max(b[1][1] for b in boxes))
    return 1e-4 * math.dist(lo, hi) or None


def _scene(obj, line_col, arc_col, spline_col, face_col, mark_edges_smaller, mark_free_edges, highlight):
    """polylines by (color, label), face loops, and marks as (points, color, label)"""
    all_parts = parts(obj)
    all_views = edge_views(obj)
    edges = [v.edge for v in all_views]
    eps = 1e-9 * scale(edges)
    tol = _sampling(all_views)

    by_type, by_property = line_col == "type", line_col == "property"
    plain = LINE_COL if by_type or by_property else line_col
    polylines = {}
    for v, e in zip(all_views, edges):
        if not by_type:
            col, label = plain, None
        elif isinstance(e, px.LineEdge):
            col, label = plain, "lines"
        elif isinstance(e, (px.ArcEdge, px.CircleEdge)):
            col, label = arc_col, "arcs"
        else:
            col, label = spline_col, "splines"
        if by_property and v.col is not None:
            col, label = tuple(v.col), None
        polylines.setdefault((col, label), []).append(points(e, tol))

    # a wire inside a compound is a wire, a wire on its own may be meant as an outline
    faces = []
    if face_col is not None:
        for part in all_parts:
            if (part is obj and face_col != FACE_COL) or not isinstance(part, px.Wire):
                faces.extend(pts for pts, depth in closed_loops(part, eps, tol))

    marks = []
    if mark_edges_smaller is not None:
        small = [i for i, e in enumerate(edges) if _length(e) < mark_edges_smaller]
        marks.append((small, SMALL_COL, f"edges < {mark_edges_smaller:g}"))
    if mark_free_edges:
        marks.append((_free_edges(edges), FREE_COL, "free edges"))
    if highlight is not None:
        marks.append((_highlighted(obj, all_views, highlight), MARK_COL, "marked edges"))
    marks = [([xy(edges[i].MidPoint()) for i in indices], col, label) for indices, col, label in marks if indices]
    return polylines, faces, marks


def _highlighted(obj, views, highlight):
    """the indices of the drawn edges highlight names: indices, a boolean mask over them, or ids and selected edges or
    faces (one, a list or a selection; a face its edges)"""
    import numbers

    one = isinstance(highlight, (str, px.EdgeView, px.FaceView)) or type(highlight).__name__ == "Selection"
    items = [highlight] if one else list(highlight)
    if items and all(isinstance(x, (bool, numbers.Integral)) for x in items):
        return [k for k, m in enumerate(items) if m] if isinstance(items[0], bool) else items
    ids = set()
    for x in items:
        for item in (obj(x) if isinstance(x, str) else x if type(x).__name__ == "Selection" else [x]):
            ids.update(e.id for e in (item.edges if isinstance(item, px.FaceView) else [item]))
    return [k for k, v in enumerate(views) if v.id in ids]


def _draw_matplotlib(ax, polylines, faces, face_col, marks, legend, mesh_lines, face_label=None):
    import matplotlib.pyplot as plt
    from matplotlib.collections import LineCollection
    from matplotlib.patches import PathPatch
    from matplotlib.path import Path

    if ax is None:
        _, ax = plt.subplots()
    for (col, label), lines in polylines.items():
        ax.add_collection(LineCollection(lines, colors=[col], label=label or "_nolegend_"))
    if mesh_lines is not None:
        # above the edges (zorder 2)
        ax.add_collection(LineCollection(mesh_lines, colors=[MESH_COL], linewidths=0.5, zorder=2.5, label="mesh"))
    if faces:
        verts, codes = [], []
        for pts in faces:
            verts.extend(pts + [pts[0]])
            codes.extend([Path.MOVETO] + [Path.LINETO] * (len(pts) - 1) + [Path.CLOSEPOLY])
        ax.add_patch(PathPatch(Path(verts, codes), facecolor=face_col, edgecolor="none",
                               label=None if face_col == FACE_COL else face_label or "face"))
    for pts, col, label in marks:
        ax.plot([p[0] for p in pts], [p[1] for p in pts], "x", color=col, linestyle="none", label=label)
    # limits the caller fixed (and axes shared both ways) stay: the box gets the equal scale instead of the limits
    fixed = not ax.get_autoscalex_on() or not ax.get_autoscaley_on()
    shared = len(ax.get_shared_x_axes().get_siblings(ax)) > 1 and len(ax.get_shared_y_axes().get_siblings(ax)) > 1
    ax.set_aspect("equal", adjustable="box" if fixed or shared else "datalim")
    ax.autoscale_view()
    if legend:
        entries = {}
        for handle, label in zip(*ax.get_legend_handles_labels()):
            entries.setdefault(label, handle)
        if entries:
            ax.legend(entries.values(), entries.keys(), loc="upper left", bbox_to_anchor=(1.02, 1.0))
    return ax


def _draw_plotly(fig, polylines, faces, face_col, marks, legend, mesh_lines, face_label=None):
    import plotly.graph_objects as go

    if fig is None:
        fig = go.Figure()
    shown = {t.name for t in fig.data if t.showlegend} | {s.name for s in fig.layout.shapes if s.showlegend}

    def entry(label):
        if not legend or label is None or label in shown:
            return False
        shown.add(label)
        return True

    for (col, label), lines in polylines.items():
        xs, ys = _polylines(lines)
        fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", line=dict(color=_css(col)), name=label or "edges",
                                 legendgroup=label or "edges", showlegend=entry(label), hoverinfo="skip"))
    if mesh_lines is not None:
        xs, ys = _separated(mesh_lines)
        fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", line=dict(color=MESH_COL, width=1), name="mesh",
                                 legendgroup="mesh", showlegend=entry("mesh"), hoverinfo="skip"))
    if faces:
        path = " ".join("M %r %r %s Z" % (*pts[0], " ".join("L %r %r" % p for p in pts[1:])) for pts in faces)
        # outer loops counterclockwise, holes clockwise: nonzero keeps the holes of a face and fills where parts overlap
        label = face_label or "face"
        fig.add_shape(type="path", path=path, fillcolor=_css(face_col), fillrule="nonzero", line_width=0,
                      layer="below", name=label, legendgroup=label, showlegend=face_col != FACE_COL and entry(label))
    for pts, col, label in marks:
        fig.add_trace(go.Scatter(x=[p[0] for p in pts], y=[p[1] for p in pts], mode="markers",
                                 marker=dict(symbol="x", color=_css(col), size=8), name=label, legendgroup=label,
                                 showlegend=entry(label)))
    pts = [(x, y) for t in fig.data for x, y in zip(t.x, t.y) if x is not None]
    if pts:
        dx = max(max(p[0] for p in pts) - min(p[0] for p in pts), 1e-12)
        dy = max(max(p[1] for p in pts) - min(p[1] for p in pts), 1e-12)
        s = min(PLOTLY_BOX[0] / dx, PLOTLY_BOX[1] / dy)
        margin = dict(l=50, r=160, t=30, b=50)
        fig.update_layout(width=max(dx * s, 200) + margin["l"] + margin["r"],
                          height=max(dy * s, 150) + margin["t"] + margin["b"], margin=margin)
        fig.update_yaxes(scaleanchor="x", scaleratio=1)
    return fig


def _field(obj, f, samples):
    """the data a measurement draws: f(obj, plot=True, samples=n)"""
    kwargs = {"plot": True}
    if samples is not None:
        kwargs["samples"] = samples
    try:
        data = f(obj, **kwargs)
    except TypeError as e:
        raise TypeError("Plot: a col function is a measurement that takes plot=True (and samples=) and returns its "
                        "data, see planex.measurements (%s)" % e) from e
    if not isinstance(data, dict) or "points" not in data or "values" not in data:
        raise TypeError("Plot: with plot=True the measurement returns {'points': [(h, v), ...], 'values': [...], ...}")
    return data



def _raster(data):
    return data["raster"] if "raster" in data else _disc_raster(data)


def _triangulation(obj, data):
    """the points triangulated, the triangles outside the faces masked: the faces filled by linear interpolation"""
    import numpy as np
    from matplotlib.tri import Triangulation

    pts = np.asarray(data["points"], dtype=float)
    tri = Triangulation(pts[:, 0], pts[:, 1])
    tri.set_mask(~_region(obj).Contains(pts[tri.triangles].mean(axis=1)))
    return tri, np.asarray(data["values"], dtype=float)


def _disc_raster(data, n=400):
    """a grid over the discs: each point the largest value of the discs covering it, NaN where none does"""
    import numpy as np

    discs = np.asarray(data["discs"], dtype=float)
    x0, x1 = (discs[:, 0] - discs[:, 2]).min(), (discs[:, 0] + discs[:, 2]).max()
    y0, y1 = (discs[:, 1] - discs[:, 2]).min(), (discs[:, 1] + discs[:, 2]).max()
    step = max(x1 - x0, y1 - y0) / n
    xs = np.arange(x0, x1 + step, step)
    ys = np.arange(y0, y1 + step, step)
    z = np.full((len(ys), len(xs)), np.nan)
    for h, v, r, value in discs:
        i0, i1 = np.searchsorted(xs, h - r), np.searchsorted(xs, h + r, side="right")
        j0, j1 = np.searchsorted(ys, v - r), np.searchsorted(ys, v + r, side="right")
        gx, gy = np.meshgrid(xs[i0:i1], ys[j0:j1])
        inside = (gx - h) ** 2 + (gy - v) ** 2 <= r * r
        window = z[j0:j1, i0:i1]
        z[j0:j1, i0:i1] = np.where(inside, np.fmax(window, value), window)
    return xs, ys, z


def _surface(obj, data, faces, clim, contours):
    """what both backends draw of a measurement or field: its kind ("contour" for a pixel grid, "tricontour" for a
    triangulation, "points"), its arguments, the values, the color range and the contour levels (None without)"""
    import numpy as np

    if faces and ("raster" in data or "discs" in data):
        kind, args = "contour", _raster(data)
        values = args[2]
    elif faces:
        kind, args = "tricontour", _triangulation(obj, data)
        values = args[1]
    else:
        kind, args, values = "points", np.asarray(data["points"], dtype=float).reshape(-1, 2), data["values"]
    levels = _levels(values, clim, contours) if faces and contours is not None else None
    return kind, args, values, _limits(values, clim), levels


def _field_matplotlib(ax, obj, data, faces, clim, contours, density, colormap):
    import matplotlib.pyplot as plt
    import numpy as np

    cmap = _colormap(colormap)
    kind, args, values, (vmin, vmax), levels = _surface(obj, data, faces, clim, contours)
    if ax is None:
        _, ax = plt.subplots()
    if kind == "contour":
        xs, ys, z = args
        step = xs[1] - xs[0] if len(xs) > 1 else 1.0
        shown = ax.imshow(z, origin="lower", cmap=cmap, zorder=0, interpolation="nearest", vmin=vmin, vmax=vmax,
                          extent=(xs[0] - step / 2, xs[-1] + step / 2, ys[0] - step / 2, ys[-1] + step / 2))
    elif kind == "tricontour":
        shown = ax.tripcolor(args[0], values, shading="gouraud", cmap=cmap, zorder=0, vmin=vmin, vmax=vmax)
    else:
        shown = ax.scatter(args[:, 0], args[:, 1], c=values, cmap=cmap, s=10, zorder=3, vmin=vmin, vmax=vmax)
    _overlays(ax, kind, args, levels, data, density)
    at, uv = data.get("arrows", (np.empty((0, 2)), None))
    if len(at):
        ax.quiver(at[:, 0], at[:, 1], uv[:, 0], uv[:, 1], angles="xy", scale_units="xy", scale=1.0)
    plt.colorbar(shown, ax=ax, label=data.get("label"))
    for (h, v), text in data.get("marks", []):
        ax.plot([h], [v], "x", color="red", zorder=4)
        if text:
            ax.annotate(text, (h, v), textcoords="offset points", xytext=(4, 4), color="red")
    if _title(data):
        ax.set_title(_title(data))
    return ax


def _field_plotly(fig, obj, data, faces, clim, contours, density, colormap):
    import numpy as np
    import plotly.graph_objects as go

    scale = _colorscale(colormap)
    kind, args, values, (zmin, zmax), levels = _surface(obj, data, faces, clim, contours)
    if fig is None:
        fig = go.Figure()
    colorbar = dict(title=data.get("label"))
    smooth = {}
    if kind == "tricontour":
        from matplotlib.tri import LinearTriInterpolator

        tri = args[0]
        n = 300
        xs = np.linspace(tri.x.min(), tri.x.max(), n)
        ys = np.linspace(tri.y.min(), tri.y.max(),
                         max(2, int(n * (tri.y.max() - tri.y.min()) / max(tri.x.max() - tri.x.min(), 1e-300))))
        gx, gy = np.meshgrid(xs, ys)
        z = np.ma.filled(LinearTriInterpolator(tri, values)(gx, gy).astype(float), np.nan)
        zmin, zmax = _limits(z, clim)
        smooth = {"zsmooth": "best"}
    elif kind == "contour":
        xs, ys, z = args
    if kind == "points":
        fig.add_trace(go.Scatter(x=args[:, 0], y=args[:, 1], mode="markers", name=data.get("label"),
                                 marker=dict(color=values, colorscale=scale, size=5, colorbar=colorbar,
                                             showscale=True, cmin=zmin, cmax=zmax)))
    else:
        fig.add_trace(go.Heatmap(x=xs, y=ys, z=z, colorscale=scale, colorbar=colorbar, name=data.get("label"),
                                 hoverinfo="z", zmin=zmin, zmax=zmax, **smooth))
    contour, stream = _overlays(_offscreen(), kind, args, levels, data, density)
    if contour is not None:
        pieces = [(float(level), s) for level, segs in zip(contour.levels, contour.allsegs) for s in segs if len(s)]
        xs, ys = _polylines([s for _, s in pieces])
        level = [v for at, s in pieces for v in [at] * len(s) + [None]]
        fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", line=dict(color=OVERLAY_COL, width=1), customdata=level,
                                 hovertemplate="%{customdata:.6g}<extra></extra>", name="contours",
                                 showlegend=False))
    if stream is not None:
        lines = stream.lines.get_segments()
        xs, ys = _polylines(lines)
        fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", line=dict(color=OVERLAY_COL, width=1), hoverinfo="skip",
                                 name="field lines", showlegend=False))
        heads, angles = _arrowheads(lines)
        fig.add_trace(go.Scatter(x=heads[:, 0], y=heads[:, 1], mode="markers", hoverinfo="skip",
                                 marker=dict(symbol="triangle-right", size=7, color=OVERLAY_COL, angle=angles),
                                 name="field line arrows", showlegend=False))
    at, uv = data.get("arrows", (np.empty((0, 2)), None))
    if len(at):
        import plotly.figure_factory as ff

        quiver = ff.create_quiver(at[:, 0], at[:, 1], uv[:, 0], uv[:, 1], scale=1.0, name="vectors",
                                  line=dict(color="black", width=1), hoverinfo="skip", showlegend=False)
        fig.add_trace(quiver.data[0])
    marks = data.get("marks", [])
    if marks:
        fig.add_trace(go.Scatter(x=[m[0][0] for m in marks], y=[m[0][1] for m in marks], mode="markers+text",
                                 text=[m[1] for m in marks], textposition="top right",
                                 marker=dict(symbol="x", color="red", size=9), name="marks", showlegend=False))
    if _title(data):
        fig.update_layout(title=_title(data))
    return fig


_DRAWERS = {"plotly": (_draw_plotly, _field_plotly), "matplotlib": (_draw_matplotlib, _field_matplotlib)}


def _subject(obj, plane, call):
    """what a plot draws of obj: a planex object (or what an id or a selection names), an OCC shape cut at the plane or,
    flat, in its own plane"""
    if _is_occ(obj):
        return px.Intersect3d(plane, obj) if plane is not None else _flat(obj)
    if plane is not None:
        raise ValueError(f"{call}: the plane cuts an OCC shape; a planex object is drawn in its own plane")
    if _is_plane(obj):
        raise ValueError(f"{call}: a plane has no bounds: px.PlotMesh(mesh, plane) draws where it cuts a mesh, "
                         "px.PlotField(field, mesh, plane) a field there")
    if isinstance(obj, str):
        from ._select import Find

        obj = Find(obj)
    from ._compound import Compound

    if type(obj).__name__ == "Selection":
        if any(isinstance(x, px.FaceView) for x in obj):
            obj = Compound(x.Copy() for x in obj)
        else:
            obj = Compound(x.Wire() if isinstance(x, px.EdgeView) else x for x in obj._edge_parts(call))
    if isinstance(obj, px.DeformableShape):
        obj = Compound(obj.Wires())
    return obj


def _region_of(obj, mesh, call):
    """the region of a field or mesh plot: the object given, what a plane cuts from the mesh's geometry, left out
    the mesh's own (see _mesh_region)"""
    if obj is None:
        region = _mesh_region(mesh)
    elif _is_plane(obj):
        region = _mesh_cut(obj, mesh)
    else:
        region = _subject(obj, None, call)
    if _dim(mesh) == 2 and not _on_xy(region):
        raise ValueError(f"{call}: a 2d mesh is drawn on the XY plane only for now (origin (0, 0, 0), h along x, v "
                         "along y); this object lies on another plane (a 3d mesh takes any)")
    return region


def _draw(obj, ax, backend, legend, line_col=LINE_COL, arc_col=ARC_COL, spline_col=SPLINE_COL, face_col=FACE_COL,
          small=None, free=False, highlight=None, mesh_lines=None, field=None, face_label=None):
    """obj into ax with the backend: its edges and faces, the mesh's element edges, a field's colors (field: the
    arguments of the backend's field drawer after ax and obj); face_label: the legend entry of a fill that is not the
    default"""
    draw_scene, draw_field = _DRAWERS[_backend(ax, backend)]
    if field is not None:
        ax = draw_field(ax, obj, *field)
        face_col = None
    if isinstance(obj, _MeshRegion):
        outline = {(LINE_COL, None): [[tuple(p) for p in line] for line in obj.outline]} if len(obj.outline) else {}
        return draw_scene(ax, outline, [], None, [], legend, mesh_lines)
    polylines, faces, marks = _scene(obj, line_col, arc_col, spline_col, face_col, small, free, highlight)
    return draw_scene(ax, polylines, faces, face_col, marks, legend, mesh_lines, face_label)


def Plot(obj: Face | Shape | Wire | Compound | Sketch | QuickSketch | str | Any, plane: Plane | None = None, /,
         ax: Any = None, line_col: Color = LINE_COL, arc_col: Color = ARC_COL, spline_col: Color = SPLINE_COL,
         face_col: Color | None = FACE_COL, mark_edges_smaller: float | None = None, mark_free_edges: bool = False,
         highlight: Ids | Any = None, legend: bool = True, show_constraints: bool | None = None,
         show_ends: bool = False, show_direction: bool = False, show_diagnostics: bool | dict = False,
         show_free_motions: bool | int = False, show_parent: bool | None = None, backend: str | None = None) -> Any:
    """Draws a planex object in its plane with matplotlib or plotly.

    A sketch is drawn as sk.Plot draws it (show_constraints, show_ends, show_direction, show_diagnostics,
    show_free_motions, show_parent); the other options
    are for the other objects, e.g. px.Plot(sk.Shape(), face_col="C0"). An OCC shape is cut at the plane
    (px.Intersect3d(plane, shape)), or without one drawn flat in its own plane as px.FromOCC gives it. Fields:
    px.PlotField; meshes: px.PlotMesh.

    Args:
        obj: a Face, Shape, Wire, Compound, sketch, selection, DeformableShape or OCC shape, or an id that names
            one ("Sketch.1", "Shape.2.Line.3").
        plane: the plane that cuts an OCC shape; None draws it flat in its own plane.
        ax: a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.
        line_col: the color of every edge; "type" draws lines in black, arcs in arc_col and splines in spline_col,
            with a legend entry each; "property" takes each edge's own col where it has one.
        arc_col: the color of arcs with line_col="type".
        spline_col: the color of splines with line_col="type".
        face_col: fills the closed loops (faces and shapes; a wire on its own stays an outline unless face_col is
            given); a fill other than the default gets a legend entry labeled with the object's id ('Face.3'), the
            default fill none; None draws the outlines only.
        mark_edges_smaller: marks the edges shorter than this at their midpoints.
        mark_free_edges: True marks the edges with an end no other edge shares.
        highlight: marks the edges given: ids ("Line.3", "Shape.1.Line.3"), selected edges or faces (a face: its
            edges), a selection, or indices or a boolean mask over obj.edges (for a Compound those of its parts in
            turn).
        legend: True shows the legend.
        show_constraints: for a sketch, as sk.Plot.
        show_ends: for a sketch, as sk.Plot.
        show_direction: for a sketch, as sk.Plot.
        show_diagnostics: for a sketch, as sk.Plot: True or the dict of sk.Diagnose() marks its geometry faults.
        show_free_motions: for a sketch, as sk.Plot: True draws an arrow per point each free motion moves, a number k
            motion k only.
        show_parent: for a sketch, as sk.Plot.
        backend: "matplotlib" or "plotly" without ax; None is matplotlib.

    Returns:
        the matplotlib Axes or the plotly Figure.

    Raises:
        ValueError: a sketch with an option for other objects, an option for sketches with another object, a plane
            with a planex object, obj is a plane, or backend is unknown.

    Examples:
        px.Plot(face, line_col="type")
        px.Plot(solid, px.XY)
    """
    if isinstance(obj, str):
        from ._select import Find

        obj = Find(obj)
    style = (("plane", plane, None), ("line_col", line_col, LINE_COL), ("arc_col", arc_col, ARC_COL),
             ("spline_col", spline_col, SPLINE_COL), ("face_col", face_col, FACE_COL),
             ("mark_edges_smaller", mark_edges_smaller, None), ("mark_free_edges", mark_free_edges, False),
             ("highlight", highlight, None), ("legend", legend, True))
    marks = (("show_constraints", show_constraints, None), ("show_ends", show_ends, False),
             ("show_direction", show_direction, False), ("show_diagnostics", show_diagnostics, False),
             ("show_free_motions", show_free_motions, False), ("show_parent", show_parent, None))
    if isinstance(obj, sketch_types):
        given = [name for name, value, default in style if _given(value, default)]
        if given:
            raise ValueError(f"Plot: a sketch is drawn in its own look, {', '.join(given)} draw other objects: "
                             f"px.Plot(sk.Shape(), ...) or px.Plot(sk.Wires(), ...)")
        return _plot_sketch.Plot(obj, ax=ax, show_constraints=show_constraints, show_ends=show_ends,
                                 show_direction=show_direction, show_diagnostics=show_diagnostics,
                                 show_free_motions=show_free_motions, show_parent=show_parent, backend=backend)
    given = [name for name, value, default in marks if _given(value, default)]
    if given:
        raise ValueError(f"Plot: {', '.join(given)} draw a sketch, this is a {type(obj).__name__}")
    label = getattr(obj, "id", None)
    return _draw(_subject(obj, plane, "Plot"), ax, backend, legend, line_col, arc_col, spline_col, face_col,
                 mark_edges_smaller, mark_free_edges, highlight, face_label=label if isinstance(label, str) else None)


def _given(value, default):
    """an option set to something else than its default (a mask or a solid is never a default)"""
    if default is None:
        return value is not None
    return not (isinstance(value, type(default)) and value == default)


def PlotField(field: Any, mesh: Any = None, obj: Any = None, /, on: str = "faces", ax: Any = None,
              backend: str | None = None, samples: int | None = None, vectors: bool = False, min: float | Auto = Auto,
              max: float | Auto = Auto, contours: int | list[float] | None = None, field_lines: bool | float = False,
              colormap: str | Any = "default", legend: bool = True) -> Any:
    """Draws a field over a planex object with matplotlib or plotly.

    An NGSolve or dolfinx field is evaluated at the centers of square pixels; pixels the mesh does not cover stay empty
    (NGSolve's 2d search still counts a point up to about 1e-6 of an element's size outside it); a vector or complex
    field shows its magnitude. A 2d mesh needs the object on the XY plane; on a 3d mesh (with volume elements) every
    pixel is evaluated at its 3d point on the object's plane and a vector field is colored by its full length. A
    measurement is called on obj with plot=True and samples=samples; its values color the faces (interpolated between
    its points) or the edges, with a color bar, its value in the title and its marks.

    Args:
        field: an NGSolve GridFunction or CoefficientFunction, a dolfinx.fem.Function (scalar or vector, real or
            complex; interpolate a UFL expression into one first), or a measurement that can be drawn
            (planex.measurements, e.g. MinThickness()).
        mesh: the ngsolve.Mesh or dolfinx mesh to evaluate on; a GridFunction or dolfinx Function brings its own, a
            CoefficientFunction needs one; a dolfinx mesh must not be distributed over MPI processes. Left out, the
            second positional argument is obj.
        obj: the region: a 2d planex object (the pixels inside its faces are evaluated, its outlines drawn over the
            colors), an OCC shape flat in its own plane, or a plane (px.Plane, netgen Axes) whose cut of the mesh's
            OCC geometry is the region; None: the XY cut of a 3d mesh's geometry, a 2d mesh's geometry itself, or a
            mesh without an OCC geometry (dolfinx, read from a file) with its boundary as the outline.
        on: "faces" or "edges": what a measurement colors; NGSolve and dolfinx fields color the faces.
        ax: as px.Plot.
        backend: as px.Plot.
        samples: the pixels along the longer side of the region's box (default 400); for a measurement its samples.
        vectors: True adds the arrows of a real vector field (its part in the plane) on a coarse grid.
        min: the low end of the color range; px.Auto takes it from the values.
        max: the high end of the color range; px.Auto takes it from the values.
        contours: thin dark lines over the colors of the faces: a number n of levels evenly spaced inside the color
            range, or a list of levels.
        field_lines: True or a density (as matplotlib's streamplot: 2 is twice as dense) draws the field lines of a
            real vector field with small arrowheads.
        colormap: "default" (planex's map, low values red to high values blue; "default_r" the other way round), a
            matplotlib colormap or its name ("viridis", "coolwarm_r"), or a list of colors.
        legend: True shows the legend.

    Returns:
        the matplotlib Axes or the plotly Figure.

    Raises:
        TypeError: mesh and obj are given and mesh is no mesh; a measurement that does not take plot=True.
        ValueError: on is neither "faces" nor "edges", min > max, a measurement with a mesh, without obj or with
            vectors, on="edges" with a field, vectors or field_lines with a complex field or one of the wrong number
            of components, a 2d mesh with an object off the XY plane, or contours, field_lines or colormap of
            another form.

    Examples:
        px.PlotField(gf, face, contours=10)
        px.PlotField(MinThickness(), shape, on="edges")
    """
    if mesh is not None and not _is_mesh(mesh):
        if obj is not None:
            raise TypeError(f"PlotField(field, mesh, obj): mesh is an ngsolve.Mesh or a dolfinx.mesh.Mesh, not "
                            f"{type(mesh).__name__}")
        mesh, obj = None, mesh
    if on not in ("faces", "edges"):
        raise ValueError(f"PlotField: on is 'faces' or 'edges', not {on!r}")
    min = None if is_auto(min, "PlotField", "min") else min
    max = None if is_auto(max, "PlotField", "max") else max
    if min is not None and max is not None and min > max:
        raise ValueError(f"PlotField: min {min} > max {max}")
    library = _field_library(field)
    contours, density = _contours_arg(contours), _density(field_lines)
    if library is None:
        if mesh is not None:
            raise ValueError("PlotField: a measurement (planex.measurements) is drawn on the object it measures, "
                             "without a mesh: px.PlotField(MinThickness(), shape)")
        if obj is None:
            raise ValueError("PlotField: a measurement needs the object it measures: px.PlotField(MinThickness(), "
                             "shape)")
        if vectors:
            raise ValueError("PlotField: vectors=True draws the arrows of an NGSolve vector field or a dolfinx vector "
                             "Function")
        if contours is not None and on == "edges":
            warn("PlotField: contours are drawn over the colors of faces (on='faces'), there are none: no "
                          "contours", px.PlanexWarning)
            contours = None
        if density is not None:
            warn("PlotField: field lines need a vector field (an NGSolve field or dolfinx Function of two or "
                          "more components): no field lines", px.PlanexWarning)
            density = None
        obj = _subject(obj, None, "PlotField")
        data = _field(obj, field, samples)
        return _draw(obj, ax, backend, legend, field=(data, on == "faces", (min, max), contours, density, colormap))
    if on == "edges":
        raise ValueError("PlotField: an NGSolve or dolfinx field colors the faces; on='edges' takes planex "
                         "measurements")
    if density is not None and library.size(field) <= 1:
        warn("PlotField: field lines need a vector field (an NGSolve field or dolfinx Function of two or more "
                      "components): no field lines", px.PlanexWarning)
        density = None
    where = _field_mesh(field, mesh)
    dim = _dim(where)
    obj = _region_of(obj, where, "PlotField")
    plane = _plane_of(obj) if dim == 3 else None
    in_plane = vectors or density is not None
    if in_plane and library.is_complex(field):
        raise ValueError("PlotField: vectors=True and field_lines draw real fields; draw a complex one with "
                         "ngsolve_webgpu, netgen.webgui or ParaView")
    if in_plane and library.size(field) != dim:
        raise ValueError(f"PlotField: vectors=True and field_lines need a field of {('two', 'three')[dim - 2]}"
                         f" components on a {dim}d mesh, this one has {library.size(field)}")
    data = _mesh_field(obj, field, where, plane, samples, vectors, density is not None)
    return _draw(obj, ax, backend, legend, field=(data, True, (min, max), contours, density, colormap))


def PlotMesh(mesh: Any, obj: Any = None, /, ax: Any = None, backend: str | None = None, legend: bool = True) -> Any:
    """Draws the element edges of an NGSolve or dolfinx mesh with matplotlib or plotly.

    A 2d mesh is drawn as it is (curved on a curved NGSolve mesh, straight on a dolfinx one), a 3d one where the
    object's plane cuts its elements (curved elements cut straight).

    Args:
        mesh: an ngsolve.Mesh or a dolfinx mesh.
        obj: a planex object (its outlines drawn under the mesh; with a 2d mesh on the XY plane), an OCC
            shape flat in its own plane, or a plane (px.Plane, netgen Axes) with its cut of the mesh's OCC geometry;
            None as px.PlotField.
        ax: as px.Plot.
        backend: as px.Plot.
        legend: True shows the legend.

    Returns:
        the matplotlib Axes or the plotly Figure.

    Raises:
        TypeError: mesh is no mesh.
        ValueError: the mesh is not 2d or 3d with volume elements, or a 2d mesh with an object off the XY plane.

    Examples:
        px.PlotMesh(mesh)
        px.PlotMesh(mesh, px.Plane(normal=(0, 1, 0)), backend="plotly")
    """
    _checked_mesh(mesh)
    obj = _region_of(obj, mesh, "PlotMesh")
    lines = _mesh_library(mesh).lines(mesh) if _dim(mesh) == 2 else _cut_lines(mesh, _plane_of(obj))
    return _draw(obj, ax, backend, legend, face_col=None, mesh_lines=lines)
