# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
from typing import TYPE_CHECKING, Any

from ._plot_style import _backend

if TYPE_CHECKING:
    from ._planex import QuickSketch, Sketch

# the sketch view: its plot area at most this many pixels
SKETCH_BOX = (600, 450)
GRID_COL, MINOR_GRID_COL = "#d9d9d9", "#ececec"
# the parent solid under the sketch: its projected faces in the face colour, see-through, its edges black
PARENT_FILL, PARENT_EDGE = "rgba(221, 230, 240, 0.6)", "black"
# the sizes of the sketch look are CSS pixels (plotly's), 96 to the inch
CSS_PX = 96
# the frame around the plot area, in CSS pixels: left, right, top, bottom
MARGIN = (40, 10, 10, 30)
NOT_DRAWN = ("H", "V", "origin", "px.origin")


def Plot(sketch: Sketch | QuickSketch, /, ax: Any = None, show_constraints: bool | None = None, show_ends: bool = False,
         show_direction: bool = False, show_parent: Any = None, backend: str | None = None) -> Any:
    """Draws the sketch with matplotlib or plotly, every element in the colour of its state.

    Green is fully constrained, black free, pink over-constrained (a constraint that contradicts the others and the elements it
    contradicts; all free while the constraints are disabled);
    construction elements are dashed, projected elements gold; in plotly an element shows its id when hovered. The
    signs, pieces and arrows keep their size on screen when zooming.

    Args:
        ax: a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.
        show_constraints: True adds a sign per constraint (in plotly what it is when hovered; not while the
            constraints are disabled); a QuickSketch has none and warns.
        show_ends: True adds a blue start and an orange end piece on every line and arc.
        show_direction: True adds an arrow from start to end on every line and arc.
        show_parent: an OCC solid (e.g. the one whose face the sketch lies on) drawn projected into the sketch's plane
            under the sketch, its faces lightly filled, its edges black (in plotly with their names when hovered);
            only in the plot.
        backend: "matplotlib" or "plotly" (hover) without ax; None is matplotlib.

    Returns:
        the matplotlib Axes or the plotly Figure.

    Raises:
        ValueError: backend is unknown.

    Examples:
        sk.Plot(show_constraints=True)
        sk.Plot(show_parent=box, backend="plotly")
    """
    draw = _plotly if _backend(ax, backend) == "plotly" else _matplotlib
    return draw(sketch, ax, show_constraints, show_ends, show_direction, show_parent)


def _elements(sketch):
    """the sketch's drawing by id, the elements drawn, and the polyline of every curve by id"""
    from ._svg import CURVES, _path

    everything = {g["id"]: g for g in sketch._Drawing()}
    drawn = [g for i, g in everything.items() if i not in NOT_DRAWN]
    return everything, drawn, {g["id"]: _path(g) for g in drawn if g["kind"] in CURVES}


def _shadow(face, normal):
    """the area of the surface triangles of an OCC face projected along normal, signed so that the front and back of
    a curved face cancel: about 0 for a face seen edge-on, also a cylinder seen end-on"""
    import numpy as np
    from netgen.meshing import MeshingStep
    from netgen.occ import OCCGeometry

    a, b = face.bounding_box
    size = max(b.x - a.x, b.y - a.y, b.z - a.z) / 12
    mesh = OCCGeometry(face).GenerateMesh(maxh=size, perfstepsend=MeshingStep.MESHSURFACE)
    points = np.array([p.p for p in mesh.Points()])
    tris = np.array([[points[v.nr - 1] for v in el.vertices] for el in mesh.Elements2D()])
    if not len(tris):
        return 0.0
    return abs(float((np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]) @ np.asarray(normal)).sum())) / 2


def _parent_geometry(sketch, parent):
    """the OCC solid projected straight into the sketch's plane: for each face seen with an area its loops (outer
    counterclockwise, holes clockwise: one region to fill with its holes empty), and its edges as (name, polyline),
    or None when nothing of it projects"""
    import warnings

    import planex as px

    normal = px.Plane(sketch).normal
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", px.PlanexWarning)
        edges = px.Project(parent, sketch)
        faces = []
        for f in parent.faces:
            if _shadow(f, normal) < 1e-3 * f.mass:
                continue
            try:
                region = px.Shape(px.Project(f, sketch))
            except (ValueError, RuntimeError):
                continue
            if region.mass > 0:
                faces.append(region)
    box = [w.bounding_box for w in edges if w.bounding_box]
    if not box:
        return None
    size = max(max(b[1][0] for b in box) - min(b[0][0] for b in box), max(b[1][1] for b in box) - min(b[0][1] for b in box))
    tol = 2e-4 * size
    regions = [region.Polylines(tol) for region in faces]
    runs = [(e.name or "", run) for w in edges for e in w.edges for run in e.Wire().Polylines(tol)]
    return regions, runs


def _axis_line(g, mid, reach):
    """an axis element as the line through the foot of mid on it, reach to both sides"""
    t = math.radians(g["normal_angle"])
    n, u = (math.cos(t), math.sin(t)), (-math.sin(t), math.cos(t))
    f = n[0] * mid[0] + n[1] * mid[1] - g["d"]
    foot = (mid[0] - f * n[0], mid[1] - f * n[1])
    return foot, (foot[0] - reach * u[0], foot[1] - reach * u[1]), (foot[0] + reach * u[0], foot[1] + reach * u[1])


def _plotly(sketch, fig, show_constraints, show_ends, show_direction, show_parent):
    import plotly.graph_objects as go

    from ._svg import CURVES, END, PROJECTED, START, _along, _colour, _ends, _shown_constraints, _signs

    show_constraints = _shown_constraints(sketch, show_constraints)

    if fig is None:
        fig = go.Figure()
    if show_parent is not None:
        parent = _parent_geometry(sketch, show_parent)
        if parent is not None:
            for region in parent[0]:
                xs, ys = [], []
                for loop in region:
                    xs.extend(list(loop[:, 0]) + [None])
                    ys.extend(list(loop[:, 1]) + [None])
                fig.add_trace(go.Scatter(x=xs, y=ys, mode="none", fill="toself", fillcolor=PARENT_FILL,
                                         hoverinfo="skip", showlegend=False))
            for name, run in parent[1]:
                fig.add_trace(go.Scatter(x=run[:, 0], y=run[:, 1], mode="lines", line=dict(color=PARENT_EDGE, width=0.8),
                                         hovertext=name, hoverinfo="text", showlegend=False))
    everything, drawn, paths = _elements(sketch)
    ends = _ends(drawn)
    lines, hover = {}, ([], [], [])
    for g in drawn:
        if g["kind"] not in CURVES:
            continue
        path = paths[g["id"]]
        xs, ys = lines.setdefault((_colour(g), g["construction"]), ([], []))
        xs.extend([p[0] for p in path] + [None])
        ys.extend([p[1] for p in path] + [None])
        (mx, my), _ = _along(path, 0.5)
        hover[0].append(mx)
        hover[1].append(my)
        hover[2].append(g["id"] + (" (construction)" if g["construction"] else ""))
    # the projections under the sketch's own lines
    for (colour, construction), (xs, ys) in sorted(lines.items(), key=lambda item: item[0][0] != PROJECTED):
        fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", hoverinfo="skip", showlegend=False,
                                 line=dict(color=colour, width=1.2 if construction else 2, dash="dash" if construction else "solid")))
    if paths:
        xs_ = [p[0] for path in paths.values() for p in path]
        ys_ = [p[1] for path in paths.values() for p in path]
        mid, reach = ((min(xs_) + max(xs_)) / 2, (min(ys_) + max(ys_)) / 2), 10 * max(max(xs_) - min(xs_), max(ys_) - min(ys_), 1e-12)
        for g in drawn:
            if g["kind"] != "Axis":
                continue
            foot, a, b = _axis_line(g, mid, reach)
            fig.add_trace(go.Scatter(x=[a[0], b[0]], y=[a[1], b[1]], mode="lines", hoverinfo="skip", showlegend=False,
                                     meta="unbounded", line=dict(color=_colour(g), width=1.2, dash="dashdot")))
            hover[0].append(foot[0])
            hover[1].append(foot[1])
            hover[2].append(g["id"])
    lone = [g for g in drawn if g["kind"] == "Point" and tuple(g["point"]) not in ends]
    fig.add_trace(go.Scatter(x=[g["point"][0] for g in lone] + hover[0], y=[g["point"][1] for g in lone] + hover[1],
                             mode="markers", hovertext=[g["id"] for g in lone] + hover[2], hoverinfo="text", showlegend=False,
                             marker=dict(size=[6] * len(lone) + [10] * len(hover[0]),
                                         color=[_colour(g) for g in lone] + ["black"] * len(hover[0]),
                                         opacity=[1] * len(lone) + [0] * len(hover[0]))))
    annotations, shapes = list(fig.layout.annotations or []), list(fig.layout.shapes or [])
    curves = [g for g in drawn if g["kind"] in ("Line", "Arc", "Spline")]

    def shape(at, kind, colour, width, opacity=1, **kwargs):
        """a shape in pixels (y up) at the sketch point at: its size on screen stays when zooming"""
        shapes.append(dict(type=kind, xsizemode="pixel", ysizemode="pixel", xanchor=at[0], yanchor=at[1],
                           line=dict(color=colour, width=width), fillcolor="rgba(0,0,0,0)", layer="above",
                           opacity=opacity, **kwargs))

    def path(at, points, colour, width=1.1):
        shape(at, "path", colour, width, path="M " + " L ".join("%.3f %.3f" % p for p in points))

    def circle(at, centre, r, colour, width):
        shape(at, "circle", colour, width, x0=centre[0] - r, x1=centre[0] + r, y0=centre[1] - r, y1=centre[1] + r)

    def hover_box(at, shift, title, size=16):
        annotations.append(dict(x=at[0], y=at[1], xshift=shift[0], yshift=shift[1], text=" ", width=size, height=size,
                                showarrow=False, hovertext=title, bgcolor="rgba(0,0,0,0)"))

    if show_direction and curves:
        mids = [_along(paths[g["id"]], 0.5) for g in curves]
        fig.add_trace(go.Scatter(x=[m[0][0] for m in mids], y=[m[0][1] for m in mids], mode="markers", hoverinfo="skip",
                                 showlegend=False, marker=dict(symbol="triangle-right", size=11, color=[_colour(g) for g in curves],
                                                               angle=[-math.degrees(math.atan2(t[1], t[0])) for _, t in mids])))
    if show_ends:
        at = {}
        points = {tuple(g["point"]): g["id"] for g in drawn if g["kind"] == "Point"}
        for g in curves:
            line = paths[g["id"]]
            for p, t, colour, end in ((line[0], _along(line, 0.0)[1], START, ".start"), (line[-1], _along(line, 1.0)[1], END, ".end")):
                sense = 1 if end == ".start" else -1
                path(p, [(0, 0), (14 * sense * t[0], 14 * sense * t[1])], colour, 3)
                at.setdefault(tuple(p), []).append(g["id"] + end)
        where = list(at)
        fig.add_trace(go.Scatter(x=[p[0] for p in where], y=[p[1] for p in where], mode="markers", hoverinfo="text",
                                 showlegend=False, marker=dict(size=14, opacity=0),
                                 hovertext=[" · ".join(at[p]) + (" (%s)" % points[p] if p in points else "") for p in where]))
    scale = _fit(fig)
    if show_constraints:
        leaders = {}
        for group in _signs(sketch, everything, scale, 12):
            colour, title = group["colour"], group["title"]
            for item in group["items"]:
                kind = item[0]
                if kind in ("text", "circle", "anchor") and math.hypot(*item[2]) > 8:
                    # plotly shows nothing only while hovered: a faint line from the sign to its place
                    f = (math.hypot(*item[2]) - 7) / math.hypot(*item[2])
                    shape(item[1], "path", colour, 0.6, 0.35, path="M 0 0 L %.3f %.3f" % (f * item[2][0], f * item[2][1]))
                if kind == "text":
                    annotations.append(dict(x=item[1][0], y=item[1][1], xshift=item[2][0], yshift=item[2][1], showarrow=False,
                                            text="<b>%s</b>" % item[3], textangle=item[4], hovertext=title,
                                            font=dict(color=colour, family="sans-serif", size=12)))
                elif kind == "circle":
                    circle(item[1], item[2], 4.8, colour, 1.2)
                    hover_box(item[1], item[2], title)
                elif kind == "leader":
                    xs, ys = leaders.setdefault(colour, ([], []))
                    xs.extend([item[1][0], item[2][0], None])
                    ys.extend([item[1][1], item[2][1], None])
                else:
                    for points, width in _glyph(item):
                        path(item[1], points, colour, width)
                    if kind == "anchor":
                        (sx, sy), k = item[2], 1.2
                        circle(item[1], (sx, sy + 4.8 * k), 1.6 * k, colour, 1)
                    hover_box(item[1], _hover_at(item), title, {"corner": 12, "tangent": 14, "chevron": 12}.get(kind, 16))
        for colour, (xs, ys) in leaders.items():
            fig.add_trace(go.Scatter(x=xs, y=ys, mode="lines", hoverinfo="skip", showlegend=False,
                                     line=dict(color=colour, width=0.7, dash="dot")))
    fig.update_layout(shapes=shapes)
    fig.update_layout(annotations=annotations)
    _fit(fig)
    return fig


def _glyph(item):
    """the strokes of a corner, anchor, tangent or chevron sign as (points in pixels around its place, width)"""
    kind = item[0]
    if kind == "corner":
        u, v, size = item[2], item[3], 8.4
        return [([(size * u[0], size * u[1]), (size * (u[0] + v[0]), size * (u[1] + v[1])), (size * v[0], size * v[1])], 1)]
    if kind == "anchor":
        (sx, sy), k = item[2], 1.2
        strokes = ([(0, 3.2), (0, -5.5)], [(-3, 1.5), (3, 1.5)], [(-6.2, -3), (-5, -1.5), (-3.4, -2.4)],
                   [(6.2, -3), (5, -1.5), (3.4, -2.4)],
                   [(5 * math.cos(a), -1.5 - 3.5 * math.sin(a)) for a in [math.pi * i / 12 for i in range(13)]])
        return [([(sx + k * x, sy + k * y) for x, y in stroke], 1.1) for stroke in strokes]
    if kind == "tangent":
        t = item[2]
        n = (-t[1], t[0])
        out = []
        for side in (4.2, -4.2):
            c = (side * n[0], side * n[1])
            out.append(([(c[0] - 7.2 * t[0], c[1] - 7.2 * t[1]), (c[0] + 7.2 * t[0], c[1] + 7.2 * t[1])], 1.2))
        return out
    d = item[2]
    n = (-d[1], d[0])
    return [([(4.8 * n[0], 4.8 * n[1]), (6 * d[0], 6 * d[1]), (-4.8 * n[0], -4.8 * n[1])], 1.2)]


def _hover_at(item):
    """where a sign's hover box sits, in pixels from its place"""
    if item[0] == "corner":
        u, v, size = item[2], item[3], 8.4
        return size * (u[0] + v[0]) / 2, size * (u[1] + v[1]) / 2
    return item[2] if item[0] == "anchor" else (0, 0)


def _matplotlib(sketch, ax, show_constraints, show_ends, show_direction, show_parent):
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.collections import LineCollection
    from matplotlib.lines import Line2D
    from matplotlib.patches import Circle, PathPatch, Polygon
    from matplotlib.path import Path
    from matplotlib.ticker import MultipleLocator
    from matplotlib.transforms import Affine2D, ScaledTranslation

    from ._svg import CURVES, END, PROJECTED, START, _along, _colour, _ends, _shown_constraints, _signs

    show_constraints = _shown_constraints(sketch, show_constraints)
    everything, drawn, paths = _elements(sketch)
    ends = _ends(drawn)
    lone = [g for g in drawn if g["kind"] == "Point" and tuple(g["point"]) not in ends]
    parent = _parent_geometry(sketch, show_parent) if show_parent is not None else None
    pts = [p for path in paths.values() for p in path] + [tuple(g["point"]) for g in lone]
    if parent is not None:
        pts += [tuple(p) for region in parent[0] for loop in region for p in loop] + [tuple(p) for _, run in parent[1] for p in run]
    if not pts:
        pts = [(0.0, 0.0)]
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    pad = 0.03 * max(x1 - x0, y1 - y0, 1e-12)
    dx, dy = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    if ax is None:
        s = min(SKETCH_BOX[0] / dx, SKETCH_BOX[1] / dy)
        left, right, top, bottom = MARGIN
        width, height = dx * s + left + right, dy * s + top + bottom
        fig = plt.figure(figsize=(width / CSS_PX, height / CSS_PX))
        ax = fig.add_axes((left / width, bottom / height, dx * s / width, dy * s / height))
    else:
        box = ax.get_position()
        w, h = ax.figure.get_size_inches()
        s = min(box.width * w * CSS_PX / dx, box.height * h * CSS_PX / dy)
    pt = 72 / CSS_PX

    def at(p):
        """CSS pixels (y up) around the sketch point p: shapes in it keep their size on screen when zooming"""
        return Affine2D().scale(ax.figure.dpi / CSS_PX) + ScaledTranslation(p[0], p[1], ax.transData)

    def path(p, points, colour, width=1.1, alpha=1.0):
        ax.add_line(Line2D([q[0] for q in points], [q[1] for q in points], color=colour, linewidth=width * pt,
                           alpha=alpha, transform=at(p), solid_capstyle="round", solid_joinstyle="round", zorder=4))

    if parent is not None:
        for region in parent[0]:
            outline = Path(np.concatenate(region), np.concatenate([[Path.MOVETO] + [Path.LINETO] * (len(loop) - 1) for loop in region]))
            ax.add_patch(PathPatch(outline, facecolor=(221 / 255, 230 / 255, 240 / 255, 0.6), linewidth=0, zorder=1))
        ax.add_collection(LineCollection([run for _, run in parent[1]], colors=PARENT_EDGE, linewidths=0.8 * pt, zorder=1))
    lines = {}
    for g in drawn:
        if g["kind"] in CURVES:
            lines.setdefault((_colour(g), g["construction"]), []).append(paths[g["id"]])
    # the projections under the sketch's own lines
    for (colour, construction), group in sorted(lines.items(), key=lambda item: item[0][0] != PROJECTED):
        ax.add_collection(LineCollection(group, colors=colour, linewidths=(1.2 if construction else 2) * pt,
                                         linestyles="dashed" if construction else "solid", capstyle="round", zorder=2))
    if paths:
        mid, reach = ((x0 + x1) / 2, (y0 + y1) / 2), 10 * max(x1 - x0, y1 - y0, 1e-12)
        for g in drawn:
            if g["kind"] == "Axis":
                _, a, b = _axis_line(g, mid, reach)
                ax.plot([a[0], b[0]], [a[1], b[1]], color=_colour(g), linewidth=1.2 * pt, linestyle="dashdot", zorder=1.5)
    if lone:
        ax.scatter([g["point"][0] for g in lone], [g["point"][1] for g in lone], s=(6 * pt) ** 2,
                   c=[_colour(g) for g in lone], linewidths=0, zorder=3)
    curves = [g for g in drawn if g["kind"] in ("Line", "Arc", "Spline")]
    if show_direction:
        for g in curves:
            (m, t) = _along(paths[g["id"]], 0.5)
            n = (-t[1], t[0])
            tip = [(5.5 * t[0], 5.5 * t[1]), (-4.5 * t[0] + 4 * n[0], -4.5 * t[1] + 4 * n[1]),
                   (-4.5 * t[0] - 4 * n[0], -4.5 * t[1] - 4 * n[1])]
            ax.add_patch(Polygon(tip, closed=True, facecolor=_colour(g), edgecolor="none", transform=at(m), zorder=3))
    if show_ends:
        for g in curves:
            line = paths[g["id"]]
            for p, t, colour, sense in ((line[0], _along(line, 0.0)[1], START, 1), (line[-1], _along(line, 1.0)[1], END, -1)):
                path(p, [(0, 0), (14 * sense * t[0], 14 * sense * t[1])], colour, 3)
    if show_constraints:
        leaders = {}
        for group in _signs(sketch, everything, s, 12):
            colour = group["colour"]
            for item in group["items"]:
                kind = item[0]
                if kind in ("text", "circle", "anchor") and math.hypot(*item[2]) > 8:
                    f = (math.hypot(*item[2]) - 7) / math.hypot(*item[2])
                    path(item[1], [(0, 0), (f * item[2][0], f * item[2][1])], colour, 0.6, 0.35)
                if kind == "text":
                    ax.text(item[2][0], item[2][1], item[3], transform=at(item[1]), ha="center", va="center",
                            rotation=-item[4], color=colour, fontsize=12 * pt, fontweight="bold", family="sans-serif",
                            zorder=4)
                elif kind == "circle":
                    ax.add_patch(Circle(item[2], 4.8, transform=at(item[1]), fill=False, edgecolor=colour,
                                        linewidth=1.2 * pt, zorder=4))
                elif kind == "leader":
                    leaders.setdefault(colour, []).append([item[1], item[2]])
                else:
                    for points, width in _glyph(item):
                        path(item[1], points, colour, width)
                    if kind == "anchor":
                        (sx, sy), k = item[2], 1.2
                        ax.add_patch(Circle((sx, sy + 4.8 * k), 1.6 * k, transform=at(item[1]), fill=False,
                                            edgecolor=colour, linewidth=pt, zorder=4))
        for colour, segments in leaders.items():
            ax.add_collection(LineCollection(segments, colors=colour, linewidths=0.7 * pt, linestyles="dotted", zorder=4))
    step, minor = _grid_steps(dx, dy)
    for axis in (ax.xaxis, ax.yaxis):
        axis.set_major_locator(MultipleLocator(step))
        axis.set_minor_locator(MultipleLocator(minor))
    ax.grid(which="major", color=GRID_COL, linewidth=pt)
    ax.grid(which="minor", color=MINOR_GRID_COL, linewidth=pt, linestyle=(0, (2, 3)))
    ax.set_axisbelow(True)
    ax.set_xlim(x0 - pad, x1 + pad)
    ax.set_ylim(y0 - pad, y1 + pad)
    ax.set_aspect("equal", adjustable="box")
    return ax


def _grid_steps(dx, dy):
    """a round major step (1, 2 or 5 times a power of ten): about four lines on the short side, at most about twelve
    on the long side; and the minor step"""
    raw = max(max(dx, dy) / 12, min(dx, dy) / 4)
    power = 10 ** math.floor(math.log10(raw))
    for m, parts in ((1, 5), (2, 4), (5, 5), (10, 5)):
        if m * power >= raw:
            return m * power, m * power / parts


def _fit(fig):
    """axes with equal scaling, tight ranges and square grid cells, the figure sized to the drawing (infinite lines,
    traces with meta "unbounded", left out)"""
    pts = [(x, y) for t in fig.data if t.meta != "unbounded" for x, y in zip(t.x, t.y) if x is not None]
    if not pts:
        return
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    pad = 0.03 * max(x1 - x0, y1 - y0, 1e-12)
    dx, dy = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    s = min(SKETCH_BOX[0] / dx, SKETCH_BOX[1] / dy)
    margin = dict(l=MARGIN[0], r=MARGIN[1], t=MARGIN[2], b=MARGIN[3])
    fig.update_layout(width=dx * s + margin["l"] + margin["r"], height=dy * s + margin["t"] + margin["b"],
                      margin=margin, showlegend=False, template="simple_white", hovermode="closest")
    step, minor = _grid_steps(dx, dy)
    grid = dict(showgrid=True, gridcolor=GRID_COL, tick0=0, dtick=step,
                minor=dict(showgrid=True, gridcolor=MINOR_GRID_COL, griddash="2px,3px", tick0=0, dtick=minor))
    fig.update_xaxes(range=[x0 - pad, x1 + pad], constrain="domain", **grid)
    fig.update_yaxes(range=[y0 - pad, y1 + pad], scaleanchor="x", scaleratio=1, constrain="domain", **grid)
    return s
