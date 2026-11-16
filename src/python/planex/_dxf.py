# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
import os
from typing import TYPE_CHECKING

from . import _planex as px
from ._geom import edge_views, parts, read_name
from ._sketches import sketch_types
from ._units import cm, inch, m, mil, mm, um
from ._auto import Auto, is_auto
from ._fileunits import check_length, named, read_factor, write_factor
from ._caller import warn
from ._paths import with_extension

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire
    from .types import Plane


def _rgb(ent, doc):
    if ent.dxf.hasattr("true_color"):
        import ezdxf.colors
        return tuple(c / 255 for c in ezdxf.colors.int2rgb(ent.dxf.true_color))
    aci = ent.dxf.color
    if aci == 256 and ent.dxf.layer in doc.layers:  # BYLAYER
        aci = abs(doc.layers.get(ent.dxf.layer).color)
    if 1 <= aci <= 255:
        import ezdxf.colors
        return tuple(c / 255 for c in ezdxf.colors.aci2rgb(aci))
    return None


def _arc_from_bulge(p, q, bulge, plane):
    # bulge = tan(sweep / 4), positive counterclockwise; the center sits r - sagitta left of the chord midpoint
    chord = (q[0] - p[0], q[1] - p[1])
    length = math.hypot(*chord)
    sagitta = abs(bulge) * length / 2
    radius = length * (1 + bulge * bulge) / (4 * abs(bulge))
    n = (-chord[1] / length, chord[0] / length)
    h = (radius - sagitta) * (1 if bulge > 0 else -1)
    center = ((p[0] + q[0]) / 2 + n[0] * h, (p[1] + q[1]) / 2 + n[1] * h)
    return px._Arc(p, q, center, bulge > 0, plane=plane)


def _polyline(pts, closed, plane):
    """lines and bulge arcs between consecutive (x, y, bulge) points"""
    out = []
    pairs = list(zip(pts, pts[1:] + (pts[:1] if closed else [])))
    for (x0, y0, b), (x1, y1, _) in pairs:
        if (x0, y0) == (x1, y1):
            continue
        out.append(_arc_from_bulge((x0, y0), (x1, y1), b, plane) if b else px.Line((x0, y0), (x1, y1), plane=plane))
    return out


def _bspline_pieces(poles, knots, weights, degree):
    """(poles, knots, weights) of the pieces between interior knots of multiplicity degree + 1: SVG conversions store
    chains of Bezier pieces as one spline, which is not one curve"""
    p = degree
    pieces = []
    a = 0
    i = p + 1
    while i < len(knots) - (p + 1):
        j = i
        while j < len(knots) and knots[j] == knots[i]:
            j += 1
        if j - i >= p + 1:
            pieces.append((a, i, i + p + 1))
            a = j - (p + 1)
            i = a + p + 1
        else:
            i = j
    pieces.append((a, len(knots) - p - 1, len(knots)))
    return [(poles[a:b], knots[a:c], weights[a:b] if weights else None) for a, b, c in pieces]


def _splines(bs, s, plane):
    """one wire per piece; a piece whose poles all coincide is nothing, one OCC rejects is interpolated through samples"""
    poles = [(p.x * s, p.y * s) for p in bs.control_points]
    weights = list(bs.weights())
    out = []
    for pp, kk, ww in _bspline_pieces(poles, list(bs.knots()), weights, bs.degree):
        extent = max(abs(p[i] - pp[0][i]) for p in pp for i in (0, 1))
        if extent <= 1e-12 * max(1.0, max(abs(c) for p in pp for c in p)):
            continue
        try:
            out.append(px._BSpline(pp, kk, bs.degree, ww, plane=plane))
        except (ValueError, RuntimeError):
            piece = type(bs)(pp, order=bs.degree + 1, knots=kk, weights=ww)
            out.append(px._Spline([(p.x, p.y) for p in piece.approximate(16 * len(pp))], plane=plane, style="averaged"))
    return out


def _ocs(ent, s):
    """(to, ccw): to maps the entity's own coordinates (OCS) to the drawing's, scaled, ccw says whether it keeps the
    turning sense; None, with a warning, for an entity in a plane tilted to the drawing"""
    # CAD writes mirrored arcs with extrusion -Z: x mirrored and the turning sense reversed
    z = ent.dxf.extrusion.normalize().z
    if abs(abs(z) - 1.0) > 1e-12:
        warn(f"FromDXF: a {ent.dxftype()} in a plane tilted to the drawing (extrusion {tuple(ent.dxf.extrusion)}) "
             "is skipped", px.PlanexWarning)
        return None
    ocs = ent.ocs()

    def to(x, y):
        p = ocs.to_wcs((x, y, 0))
        return (p.x * s, p.y * s)

    return to, z > 0


def _edges(ent, s, plane):
    kind = ent.dxftype()
    if kind == "LINE":
        return [px.Line((ent.dxf.start.x * s, ent.dxf.start.y * s), (ent.dxf.end.x * s, ent.dxf.end.y * s), plane=plane)]
    if kind in ("ARC", "CIRCLE", "LWPOLYLINE") or (kind == "POLYLINE" and ent.is_2d_polyline):
        frame = _ocs(ent, s)
        if frame is None:
            return []
        to, ccw = frame
    if kind in ("ARC", "CIRCLE"):
        cx, cy, r = ent.dxf.center.x, ent.dxf.center.y, ent.dxf.radius
        c = to(cx, cy)
        if kind == "CIRCLE":
            return [px.Circle(c, r * s, reverse=not ccw, plane=plane)]
        a0 = math.radians(ent.dxf.start_angle)
        a1 = math.radians(ent.dxf.end_angle)
        if a1 <= a0:
            a1 += 2 * math.pi
        start = to(cx + r * math.cos(a0), cy + r * math.sin(a0))
        end = to(cx + r * math.cos(a1), cy + r * math.sin(a1))
        return [px._Arc(start, end, c, ccw, plane=plane)]
    if kind == "LWPOLYLINE":
        return _polyline([(*to(x, y), b if ccw else -b) for x, y, b in ent.get_points("xyb")], ent.closed, plane)
    if kind == "POLYLINE" and ent.is_2d_polyline:
        pts = [(*to(v.dxf.location.x, v.dxf.location.y), v.dxf.bulge if ccw else -v.dxf.bulge) for v in ent.vertices]
        return _polyline(pts, ent.is_closed, plane)
    if kind == "SPLINE":
        return _splines(ent.construction_tool(), s, plane)
    if kind == "ELLIPSE":
        # exact: a rational quadratic b-spline; its axes in world coordinates, the minor one turned by the extrusion
        if abs(abs(ent.dxf.extrusion.normalize().z) - 1.0) > 1e-12:
            warn(f"FromDXF: an ELLIPSE in a plane tilted to the drawing (extrusion {tuple(ent.dxf.extrusion)}) "
                          "is skipped", px.PlanexWarning)
            return []
        e = ent.construction_tool()
        sweep = (e.end_param - e.start_param) % (2 * math.pi)
        if sweep < 1e-9 or sweep > 2 * math.pi - 1e-9:
            sweep = 2 * math.pi
        c, a, b = e.center, e.major_axis, e.minor_axis
        return [px._Ellipse((c.x * s, c.y * s), (a.x * s, a.y * s), (b.x * s, b.y * s), e.start_param,
                            e.start_param + sweep, plane=plane)]
    return []


def _by_parity(loops):
    """the faces inside an odd number of the closed wires"""
    return [f.Copy() for f in px.Shape(loops).faces] if loops else []


def _loops(edges, tol):
    """the closed wires the edges make"""
    return [w for w in px.Wires(edges, tol) if w.IsClosed()]


def _boundary(path, to, ccw, s, plane):
    """the edges of a hatch's boundary path; ezdxf keeps its arcs and elliptic arcs counterclockwise in the OCS"""
    from ezdxf.entities.boundary_paths import BoundaryPathType, EdgeType
    import ezdxf.math

    if path.type == BoundaryPathType.POLYLINE:
        return _polyline([(*to(x, y), b if ccw else -b) for x, y, b in path.vertices], True, plane)
    out = []
    for e in path.edges:
        if e.type == EdgeType.LINE:
            out.append(px.Line(to(*e.start), to(*e.end), plane=plane))
        elif e.type == EdgeType.ARC:
            if math.isclose((e.end_angle - e.start_angle) % 360.0, 0.0, abs_tol=1e-9):
                out.append(px.Circle(to(*e.center), e.radius * s, reverse=not ccw, plane=plane))
            else:
                out.append(px._Arc(to(*e.start_point), to(*e.end_point), to(*e.center), ccw, plane=plane))
        elif e.type == EdgeType.ELLIPSE:
            el = e.construction_tool()
            sweep = (el.end_param - el.start_param) % (2 * math.pi)
            if sweep < 1e-9:
                sweep = 2 * math.pi
            out.append(px._Ellipse(to(el.center.x, el.center.y), to(el.major_axis.x, el.major_axis.y),
                                   to(el.minor_axis.x, el.minor_axis.y), el.start_param, el.start_param + sweep,
                                   plane=plane))
        elif e.control_points:
            out += _splines(ezdxf.math.BSpline([to(p.x, p.y) for p in e.control_points], order=e.degree + 1,
                                               knots=e.knot_values or None, weights=e.weights or None), 1.0, plane)
        else:
            tangents = [to(*t) for t in (e.start_tangent, e.end_tangent)] if e.start_tangent and e.end_tangent else None
            out += _splines(ezdxf.math.fit_points_to_cad_cv([to(p.x, p.y) for p in e.fit_points], tangents=tangents),
                            1.0, plane)
    return out


# the boundary paths a hatch's island style fills by (DXF reference, group codes 75 and 92): normal all of them,
# outer the external (1) and outermost (16) ones, ignore the external ones
_ISLAND_PATHS = {1: 1 | 16, 2: 1}


def _hatched(ent, s, tol, plane):
    """the faces a hatch fills, solid or patterned: its boundary paths by its island style, by parity"""
    frame = _ocs(ent, s)
    if frame is None:
        return []
    to, ccw = frame
    flags = _ISLAND_PATHS.get(ent.dxf.get("hatch_style", 0))
    paths = [p for p in ent.paths if flags is None or p.path_type_flags & flags] or list(ent.paths)
    loops = []
    for p in paths:
        closed = _loops(_boundary(p, to, ccw, s, plane), tol)
        if not closed:
            warn(f"FromDXF: a HATCH on layer {ent.dxf.layer!r} has a boundary that does not close within tol; it is "
                 "left out", px.PlanexWarning)
        loops += closed
    return _by_parity(loops)


def _solid(ent, s, tol, plane):
    """the face of a SOLID or TRACE: three or four corners"""
    frame = _ocs(ent, s)
    if frame is None:
        return []
    to, _ = frame
    # DXF stores the corners crosswise: the outline runs through the first, second, fourth and third
    corners = [to(v.x, v.y) for v in (ent.dxf.vtx0, ent.dxf.vtx1, ent.dxf.get("vtx3", ent.dxf.vtx2), ent.dxf.vtx2)]
    lines = [px.Line(a, b, plane=plane) for a, b in zip(corners, corners[1:] + corners[:1]) if a != b]
    return _by_parity(_loops(lines, tol))


def _width(ent):
    """the width a 2D polyline is drawn with: 0 for none, None where it changes along the polyline"""
    if ent.dxftype() == "LWPOLYLINE":
        if ent.dxf.get("const_width", 0):
            return ent.dxf.const_width
        pairs, closed = [(a, b) for _, _, a, b in ent.get_points("xyse")], ent.closed
    else:
        a, b = ent.dxf.get("default_start_width", 0), ent.dxf.get("default_end_width", 0)
        pairs = [(v.dxf.get("start_width", a), v.dxf.get("end_width", b)) for v in ent.vertices]
        closed = ent.is_closed
    widths = {w for pair in (pairs if closed else pairs[:-1]) for w in pair}
    if not widths:
        return 0
    return widths.pop() if len(widths) == 1 else None


def _band(edges, width, tol):
    """the face a polyline of one width covers: flat ends, sharp joints"""
    return [f.Copy() for f in px.Shape(px.Offset(px.Wires(edges, tol), width / 2, both_sides=True, ends=px.Flat,
                                                 corners=px.Sharp)).faces]


def _is_point(w, tol):
    """True for a wire that lies within tol of one point"""
    (x0, y0), (x1, y1) = w.bounding_box
    return x1 - x0 <= tol and y1 - y0 <= tol


def _place(w):
    """where a wire lies, the key a chain's pieces are sorted by: where a chain touches itself, px.Wires' result
    depends on the order it gets them in"""
    a, b = w.edges[0].start, w.edges[-1].end
    return (*(c for p in w.bounding_box for c in p), a.x, a.y, b.x, b.y)


def _joined(pieces, tol):
    """the pieces (wires) in groups, one per chain: open ends joined where exactly two meet within tol, a piece within
    tol of one point taken along there and not counted; each group's pieces in their order, the groups in the order of
    their first piece, a group of such pieces alone left out"""
    cell = tol if tol > 0 else 1.0
    ends, near, grid = [], [], {}

    def root(i, parent):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i, w in enumerate(pieces):
        for k, at_end in w.OpenEnds():
            p = w.edges[k].end if at_end else w.edges[k].start
            j = len(ends)
            ends.append(i)
            near.append(j)
            cx, cy = math.floor(p.x / cell), math.floor(p.y / cell)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for q, other in grid.get((cx + dx, cy + dy), []):
                        if math.dist((p.x, p.y), q) <= tol:
                            near[root(j, near)] = root(other, near)
            grid.setdefault((cx, cy), []).append(((p.x, p.y), j))
    meeting = {}
    for j in range(len(ends)):
        meeting.setdefault(root(j, near), []).append(ends[j])
    small = [_is_point(w, tol) for w in pieces]
    chain = list(range(len(pieces)))
    for at in meeting.values():
        counted = [i for i in at if not small[i]]
        if len(counted) == 2:
            for i in at:
                chain[root(i, chain)] = root(counted[0], chain)
    groups = {}
    for i in range(len(pieces)):
        groups.setdefault(root(i, chain), []).append(i)
    return sorted(g for g in groups.values() if not all(small[i] for i in g))


# the length in meters of every $INSUNITS code a drawing can count in (DXF reference); 21-24 are US survey units
_METERS = {1: inch, 2: 12 * inch, 3: 63360 * inch, 4: mm, 5: cm, 6: m, 7: 1000 * m, 8: 1e-6 * inch, 9: mil, 10: 36 * inch,
           11: 1e-10, 12: 1e-9, 13: um, 14: 0.1, 15: 10.0, 16: 100.0, 17: 1e9,
           21: 1200 / 3937, 22: 100 / 3937, 23: 3600 / 3937, 24: 6336000 / 3937}


# id= is the id wrapper's (_ids.with_id), which names the result; declared here so that type checkers see it
def FromDXF(path: str | os.PathLike, /, tol: float = 1e-7, layers: list[str] | None = None, unit: float | Auto = Auto,
            scale: float | Auto = Auto, plane: Plane | None = None, *, id: str | None = None) -> list[Face | Wire]:
    """Reads a DXF file's model space as drawn: its lines as wires, its fills as faces.

    Lines, arcs, circles, polylines, splines and ellipses are read as wires. Each entity is one wire, closed where the
    file closes it (a closed polyline, a circle, a closed spline, a full ellipse). Separate entities are joined where
    exactly two ends meet within tol; where three or more meet, the wires end there. Nothing is cut against other
    entities, so wires may cross (a bore and its two centre lines are three wires); a wire that crosses itself is cut
    there. A hatch (solid or patterned), a SOLID and a polyline drawn with a width are faces: a hatch the region its
    boundaries enclose by its island style, a wide polyline the band it covers (flat ends, sharp joints). px.WriteDXF's
    files read back as their faces (the hatches) and their edges as wires.

    Faces from the lines are the next step: px.Face(wires) for an outline with its holes, px.Glue of the closed
    wires' faces for several outlines kept apart, layers= to leave out centre lines and dimensions. An outline the
    file does not trim, as slots drawn across it, bounds no face this way: repair it in a sketch (Diagnose, Trim).

    The coordinates are scaled to meters (or by scale). Blocks (INSERT) are read as their entities; dimensions and
    text are not read. The layer becomes the name, the color the col; an ellipse or part of one becomes the exact
    curve, a rational spline as px.Ellipse makes. A polyline whose width changes along it is read as its line, with a
    warning.

    Args:
        path: the DXF file.
        tol: ends closer than this (after scaling) are joined, a length.
        layers: the names of the layers to read; None reads all.
        unit: the length the file counts in (px.mm, px.inch, ...); px.Auto takes the unit its header ($INSUNITS)
            names, meters with a warning if it names none.
        scale: what the file's numbers are multiplied by; px.Auto converts them from unit into meters, 1 keeps them.
        plane: the plane of the faces and wires; None is the xy plane.

    Returns:
        a list of faces and wires in the order of the file; [] for an empty drawing. px.Glue(result) makes them one
        Shape.

    Raises:
        TypeError: unit is a name; unit or scale is None.
        ValueError: unit is not a positive length, scale not a positive number.

    Examples:
        px.FromDXF("part.dxf", unit=px.mm)
        px.Face(px.FromDXF("part.dxf", layers=["outline"]))
    """
    import ezdxf
    plane = px.AxesOf(plane, "FromDXF")

    doc = ezdxf.readfile(str(path))
    if is_auto(unit, "FromDXF", "unit"):
        code = doc.header.get("$INSUNITS", 0)
        unit = _METERS.get(code)
        if unit is None:
            warn(f"FromDXF: {path} names no length unit ($INSUNITS {code}); read as meters, unit=px.mm or "
                          f"another sets it", px.PlanexWarning)
            unit = m
    else:
        unit = check_length(unit, "FromDXF")
    scale = read_factor(unit, scale, "FromDXF")
    items = []
    for e in doc.modelspace():
        for ent in (e.virtual_entities() if e.dxftype() == "INSERT" else [e]):
            if layers is not None and ent.dxf.layer not in layers:
                continue
            kind, name, col = ent.dxftype(), read_name(ent.dxf.layer), _rgb(ent, doc)
            if kind == "HATCH":
                faces = _hatched(ent, scale, tol, plane)
            elif kind in ("SOLID", "TRACE"):
                faces = _solid(ent, scale, tol, plane)
            else:
                edges = _edges(ent, scale, plane)
                width = _width(ent) if edges and kind in ("LWPOLYLINE", "POLYLINE") else 0
                if width is None:
                    warn(f"FromDXF: a {kind} on layer {ent.dxf.layer!r} whose width changes along it is read as its "
                         "line", px.PlanexWarning)
                if not width:
                    for w in edges:
                        w.name = name
                        if col is not None:
                            w.col = col
                    items += px.Wires(edges, tol) if edges else []
                    continue
                faces = _band(edges, width * scale, tol)
            for f in faces:
                f.name = name
                if col is not None:
                    f.col = col
            items += faces
    pieces = [x for x in items if isinstance(x, px.Wire)]
    chains = {g[0]: g for g in _joined(pieces, tol)}
    out, k = [], 0
    for x in items:
        if not isinstance(x, px.Wire):
            out.append(x)
            continue
        g = chains.get(k)
        if g is not None and len(g) == 1:
            out.append(x)
        elif g is not None:
            for w in px.Wires(sorted((pieces[i] for i in g), key=_place), tol):
                if not _is_point(w, tol):
                    w._RenumberIds()
                    out.append(w)
        k += 1
    return out


def AddEdge(msp, e, at, k, attribs):
    """one edge as a LINE, CIRCLE, ARC or SPLINE entity: at(x, y) the point the file has for a point, k the length it
    has for a length"""
    if isinstance(e, px.LineEdge):
        msp.add_line(at(e.start.x, e.start.y), at(e.end.x, e.end.y), dxfattribs=attribs)
    elif isinstance(e, px.CircleEdge):
        msp.add_circle(at(e.center.x, e.center.y), e.radius * k, dxfattribs=attribs)
    elif isinstance(e, px.ArcEdge):
        a, b = (e.start, e.end) if e.ccw else (e.end, e.start)
        c = e.center
        start_angle = math.degrees(math.atan2(a.y - c.y, a.x - c.x))
        end_angle = math.degrees(math.atan2(b.y - c.y, b.x - c.x))
        msp.add_arc(at(c.x, c.y), e.radius * k, start_angle, end_angle, dxfattribs=attribs)
    else:
        b = e.bspline
        poles = [at(x, y) for x, y in b["poles"]]
        if b["weights"] is None:
            msp.add_open_spline(poles, b["degree"], b["knots"], dxfattribs=attribs)
        else:
            msp.add_rational_spline(poles, b["weights"], b["degree"], b["knots"], dxfattribs=attribs)


# planex's face colour for a face without col
_FACE_RGB = (0xdd, 0xe6, 0xf0)


def _hatch_edge(path, e, at, k):
    """one edge of a hatch boundary, along its own direction (a face's loops come oriented)"""
    if isinstance(e, px.LineEdge):
        path.add_line(at(e.start.x, e.start.y), at(e.end.x, e.end.y))
    elif isinstance(e, px.CircleEdge):
        path.add_arc(at(e.center.x, e.center.y), e.radius * k, 0.0, 360.0, ccw=e.ccw)
    elif isinstance(e, px.ArcEdge):
        c = e.center
        a = math.degrees(math.atan2(e.start.y - c.y, e.start.x - c.x))
        b = math.degrees(math.atan2(e.end.y - c.y, e.end.x - c.x))
        # a clockwise boundary arc names its angles as the counterclockwise arc over the same points
        path.add_arc(at(c.x, c.y), e.radius * k, a if e.ccw else b, b if e.ccw else a, ccw=e.ccw)
    else:
        b = e.bspline
        path.add_spline(control_points=[at(x, y) for x, y in b["poles"]], knot_values=b["knots"], degree=b["degree"],
                        weights=b["weights"])


def _hatch(msp, face, at, k, layer):
    """the face as a solid hatch: its outer loop and its holes, odd parity, in its col"""
    from ezdxf.lldxf import const

    hatch = msp.add_hatch(dxfattribs={"layer": layer})
    rgb = tuple(round(255 * c) for c in face.col[:3]) if face.col is not None else _FACE_RGB
    hatch.set_solid_fill(style=0, rgb=rgb)
    for loop, flags in [(face.outer, const.BOUNDARY_PATH_EXTERNAL)] + [(w, const.BOUNDARY_PATH_DEFAULT) for w in face.inner]:
        path = hatch.paths.add_edge_path(flags=flags)
        for v in loop.edges:
            _hatch_edge(path, v.edge, at, k)


# the DXF $INSUNITS code of each length a file can count in
_INSUNITS = {length: code for code, length in _METERS.items()}


def WriteDXF(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound | list, path: str | os.PathLike, /,
             unit: float = m, scale: float | Auto = Auto) -> None:
    """Writes the faces as solid HATCH entities and the edges as LINE, ARC and SPLINE entities of a DXF file.

    Each face is one hatch filled in its col (else planex's face color) on its name's layer, bounded by its exact loops
    (holes stay holes); each edge is written once on top. A wire is never filled, closed or not, so
    px.WriteDXF(face.wires, path) writes the outlines only (shape.wires for a Shape). An edge's name becomes its layer
    (the characters a layer name cannot have, such as / and :, become _, with a warning), its col the true color.
    $INSUNITS names unit; the coordinates are multiplied by scale, by default converted from meters into unit.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write, or a list of them.
        path: the file to write; without an extension .dxf is added.
        unit: the length the file counts in, one DXF names (px.m, px.cm, px.mm, px.um, px.inch, px.mil, feet, ...).
        scale: what the model's numbers are multiplied by; px.Auto converts them from meters into unit, 1 writes them
            as they are (a model drawn in millimeter numbers: unit=px.mm, scale=1).

    Raises:
        TypeError: unit is a name, not a length; scale is None.
        ValueError: unit is not a positive finite length or one DXF does not name; scale is not a positive number.

    Examples:
        face.WriteDXF("part.dxf", unit=px.mm)
        px.WriteDXF(shape, "part.dxf")
    """
    import ezdxf
    import ezdxf.colors

    unit = check_length(unit, "WriteDXF")
    code = named(unit, _INSUNITS, "WriteDXF", "DXF names lengths of its $INSUNITS list (px.m, px.cm, px.mm, px.um, "
                 "px.inch, px.mil, feet, ...), not this one")
    k = write_factor(unit, scale, "WriteDXF")

    from ezdxf.lldxf.validator import INVALID_LAYER_NAME_CHARACTERS

    doc = ezdxf.new()
    doc.units = code
    msp = doc.modelspace()
    renamed = {}

    def layer_of(name):
        layer = name or "0"
        if INVALID_LAYER_NAME_CHARACTERS.intersection(layer):
            renamed[layer] = layer = "".join("_" if ch in INVALID_LAYER_NAME_CHARACTERS else ch for ch in layer)
        if layer not in doc.layers:
            doc.layers.add(layer)
        return layer

    def at(x, y):
        return (x * k, y * k)

    items = list(obj) if isinstance(obj, (list, tuple)) else [obj]
    for o in items:
        for f in parts(o.Shape() if isinstance(o, sketch_types) else o):
            if isinstance(f, px.Face):
                _hatch(msp, f, at, k, layer_of(f.name))
    for o in items:
        for v in edge_views(o):
            attribs = {"layer": layer_of(v.name)}
            if v.col is not None:
                attribs["true_color"] = ezdxf.colors.rgb2int(tuple(round(255 * c) for c in v.col[:3]))
            AddEdge(msp, v.edge, at, k, attribs)
    if renamed:
        warn("WriteDXF: names a DXF layer cannot have were written as " +
                      ", ".join(f"{a!r} -> {b!r}" for a, b in renamed.items()), px.PlanexWarning)
    doc.saveas(with_extension(path, ".dxf"))
