# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""SVG files at true scale: px.WriteSVG and px.FromSVG. The pictures of ToSVG are in
_svg.py."""
from __future__ import annotations

import math
import os
import re
from typing import TYPE_CHECKING
from xml.sax.saxutils import quoteattr

import numpy as np

from . import _planex as px
from ._geom import edge_views, oriented, parts, read_name, scale as size_of
from ._sketches import sketch_types
from ._units import inch, m, mm
from ._auto import Auto, is_auto
from ._fileunits import check_length, read_factor, write_factor
from ._caller import warn
from ._paths import with_extension

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire
    from .types import Plane

# planex's face colour; edges are drawn 0.1 mm wide
FACE_FILL = "#dde6f0"
STROKE = 1e-4


def _n(x):
    text = repr(float(x) + 0.0)
    return text[:-2] if text.endswith(".0") else text


def _colour(col):
    return "#" + "".join(f"{round(255 * min(1.0, max(0.0, c))):02x}" for c in col[:3])


def _bezier_pieces(poles, knots, degree):
    """the Bezier pieces of a clamped b-spline (poles in any dimension), by inserting every inner knot up to degree
    times (Boehm)"""
    P, U, p = [np.asarray(q, float) for q in poles], list(knots), degree
    for u in sorted(set(U[p + 1:len(U) - p - 1])):
        while U.count(u) < p:
            k = max(i for i in range(len(U) - 1) if U[i] <= u < U[i + 1])
            Q = []
            for i in range(len(P) + 1):
                if i <= k - p:
                    Q.append(P[i])
                elif i > k:
                    Q.append(P[i - 1])
                else:
                    a = (u - U[i]) / (U[i + p] - U[i])
                    Q.append((1 - a) * P[i - 1] + a * P[i])
            P, U = Q, U[:k + 1] + [u] + U[k + 1:]
    return [P[j:j + p + 1] for j in range(0, len(P) - 1, p)]


class _Path:
    """the path commands of edges in the file's coordinates: lengths divided by unit, y pointing down"""

    def __init__(self, k):
        self.k = k
        self.approximated = 0

    def pt(self, x, y):
        return f"{_n(x * self.k)} {_n(-y * self.k)}"

    def arc(self, r, ccw, rev, x, y):
        # y mirrored: a counterclockwise arc goes the negative way in the file
        return f"A {_n(r * self.k)} {_n(r * self.k)} 0 0 {1 if ccw == rev else 0} " + self.pt(x, y)

    def edge(self, e, rev):
        """the commands from the near end of the edge to its far end"""
        far = e.start if rev else e.end
        if isinstance(e, px.LineEdge):
            return "L " + self.pt(far.x, far.y)
        # circles and arcs in pieces of at most 90 degrees: a reader finds an arc's center from its ends and radius, which
        # near 180 degrees turns the radius' last digit into an error of 1e-8
        if isinstance(e, (px.CircleEdge, px.ArcEdge)):
            c, near = e.center, e.end if rev else e.start
            sweep = 360.0 if isinstance(e, px.CircleEdge) else abs(e.sweep)
            n = math.ceil(sweep / 90 - 1e-9)
            turn = math.radians((1 if e.ccw != rev else -1) * sweep / n)
            dx, dy = near.x - c.x, near.y - c.y
            steps = [(c.x + dx * math.cos(turn * k) - dy * math.sin(turn * k), c.y + dx * math.sin(turn * k) + dy * math.cos(turn * k))
                     for k in range(1, n)]
            return " ".join(self.arc(e.radius, e.ccw, rev, x, y) for x, y in steps + [(far.x, far.y)])
        return self.spline(e, rev)

    def spline(self, e, rev):
        b = e.bspline
        p, knots, w = b["degree"], list(b["knots"]), b["weights"]
        clamped = knots[:p + 1] == [knots[0]] * (p + 1) and knots[-p - 1:] == [knots[-1]] * (p + 1)
        if clamped and w is None and p <= 3:
            pieces = _bezier_pieces([(x * self.k, -y * self.k) for x, y in b["poles"]], knots, p)
            if rev:
                pieces = [list(reversed(q)) for q in reversed(pieces)]
            letter = {1: "L", 2: "Q", 3: "C"}[p]
            return " ".join(letter + " " + " ".join(f"{_n(q[0])} {_n(q[1])}" for q in piece[1:]) for piece in pieces)
        if clamped and w is not None and p == 2:
            pieces = _bezier_pieces([(x * self.k * wi, -y * self.k * wi, wi) for (x, y), wi in zip(b["poles"], w)], knots, 2)
            if rev:
                pieces = [list(reversed(q)) for q in reversed(pieces)]
            arcs = [self.conic(piece) for piece in pieces]
            if all(arcs):
                return " ".join(arcs)
        self.approximated += 1
        pieces = e.approximation
        return " ".join(self.edge(q, rev) for q in (reversed(pieces) if rev else pieces))

    @staticmethod
    def conic(piece):
        """a rational quadratic piece that is part of an ellipse as an elliptical arc, else None: with the end weights 1
        and w the middle one, the center is P1 + (M - P1) / (1 - w^2), M the middle of the ends; on the circle it is the
        image of, the piece runs over the angles 0 to 2 acos(w)"""
        (x0, y0, w0), (x1, y1, w1), (x2, y2, w2) = piece
        P0, P1, P2 = np.array([x0 / w0, y0 / w0]), np.array([x1 / w1, y1 / w1]), np.array([x2 / w2, y2 / w2])
        w = w1 / math.sqrt(w0 * w2)
        if not 0.0 < w < 1.0:
            return None
        C = P1 + ((P0 + P2) / 2 - P1) / (1 - w * w)
        A = P0 - C
        B = (P1 - C - A) / math.tan(math.acos(w))
        U, s, _ = np.linalg.svd(np.column_stack([A, B]))
        turn = (P1[0] - P0[0]) * (P2[1] - P1[1]) - (P1[1] - P0[1]) * (P2[0] - P1[0])
        return (f"A {_n(s[0])} {_n(s[1])} {_n(math.degrees(math.atan2(U[1, 0], U[0, 0])))} 0 {1 if turn > 0 else 0} "
                f"{_n(P2[0])} {_n(P2[1])}")


def _items(obj):
    return list(obj) if isinstance(obj, (list, tuple)) else [obj]


def WriteSVG(obj: Face | Shape | Wire | Compound | Sketch | QuickSketch | list, path: str | os.PathLike, /,
             unit: float = m, scale: float | Auto = Auto) -> None:
    """Writes the faces filled and every edge drawn on top as an SVG file at true scale.

    The width and height are in millimeters, the drawing counts in unit (any length: the width and height give its
    size), y points up as in planex. Each face is one
    closed path (its holes stay holes) in its col, else planex's face color; a wire is never filled, closed or not, so
    px.WriteSVG(face.wires, path) writes the outlines only (shape.wires for a Shape). Each edge is drawn once, in a
    group labeled with its name (an Inkscape layer), in its col. Lines, arcs, circles, ellipses and splines up to
    degree 3 are exact; a rational spline that is no ellipse, or one above degree 3, is written as its arc
    approximation (within px.config.spline_tol), with a warning.

    Args:
        obj: a Face, Shape, Wire, Compound or sketch, or a list of them.
        path: the file to write; without an extension .svg is added.
        unit: the length the drawing counts in (px.m, px.mm, ...).
        scale: what the model's numbers are multiplied by; px.Auto converts them from meters into unit, 1 writes them
            as they are.

    Raises:
        TypeError: unit is a name, not a length; scale is None.
        ValueError: unit is not a positive finite length, scale not a positive number, or there is nothing to draw.

    Examples:
        face.WriteSVG("part.svg", unit=px.mm)
        px.WriteSVG(face.wires, "outline.svg", unit=px.mm)
    """
    unit = check_length(unit, "WriteSVG")
    k = write_factor(unit, scale, "WriteSVG")
    views, faces = [], []
    for o in _items(obj):
        views += edge_views(o)
        faces += [f for f in parts(o.Shape() if isinstance(o, sketch_types) else o) if isinstance(f, px.Face)]
    if not views:
        raise ValueError("WriteSVG: there is nothing to draw")
    boxes = np.array([v.bounding_box for v in views], float).reshape(-1, 4)
    x0, y0, x1, y1 = boxes[:, 0].min(), boxes[:, 1].min(), boxes[:, 2].max(), boxes[:, 3].max()
    path_of = _Path(k)
    eps = 1e-9 * size_of([v.edge for v in views])
    body = []
    for f in faces:
        loops = []
        for w in f.wires:
            if w.IsClosed() and len(w.edges) > 0:
                chain = oriented([e.edge for e in w.edges], eps)
                first, rev = chain[0]
                start = first.end if rev else first.start
                loops.append("M " + path_of.pt(start.x, start.y) + " " + " ".join(path_of.edge(e, r) for e, r in chain) + " Z")
        if loops:
            fill = _colour(f.col) if f.col is not None else FACE_FILL
            body.append(f'<path d="{" ".join(loops)}" fill="{fill}" fill-rule="evenodd" stroke="none"/>')
    groups = {}
    for v in views:
        groups.setdefault(v.name, []).append(v)
    width = _n(STROKE * k)
    for name, members in groups.items():
        lines = []
        for v in members:
            e = v.edge
            colour = _colour(v.col) if v.col is not None else "black"
            lines.append(f'<path d="M {path_of.pt(e.start.x, e.start.y)} {path_of.edge(e, False)}" fill="none" '
                         f'stroke="{colour}" stroke-width="{width}"/>')
        if name is None:
            body += lines
        else:
            body.append(f'<g inkscape:groupmode="layer" inkscape:label={quoteattr(name)}>' + "".join(lines) + "</g>")
    w, h = max(x1 - x0, 0.0), max(y1 - y0, 0.0)
    head = (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
            f'width="{_n(w * k * unit / mm)}mm" height="{_n(h * k * unit / mm)}mm" '
            f'viewBox="{_n(x0 * k)} {_n(-y1 * k)} {_n(w * k)} {_n(h * k)}">\n')
    with open(with_extension(path, ".svg"), "w", encoding="utf-8") as out:
        out.write(head + "\n".join(body) + "\n</svg>\n")
    if path_of.approximated:
        warn(f"WriteSVG: {path_of.approximated} curve(s) have no exact SVG form (rational, or above degree 3) and "
                      "went out as their arc approximation, within px.config.spline_tol", px.PlanexWarning)


LABEL = "{http://www.inkscape.org/namespaces/inkscape}label"
# the length in metres of each unit a width may name (CSS)
_METERS = {"mm": 1e-3, "cm": 1e-2, "q": 2.5e-4, "in": inch, "pt": inch / 72, "pc": inch / 6, "px": inch / 96}


def _width_meters(text):
    """the width as a length in meters, None if it names no unit"""
    match = re.fullmatch(r"\s*([-+0-9.eE]+)\s*([a-zA-Z]*)\s*", text or "")
    if not match or match.group(2).lower() not in _METERS:
        return None
    return float(match.group(1)) * _METERS[match.group(2).lower()]


def _paint(e, attr, colour):
    """whether the shape paints its fill or stroke, and the color: None for a gradient or pattern"""
    if str(e.values.get(attr, "")).strip().startswith("url("):
        return True, None
    if colour is None or colour.value is None or colour.alpha == 0:
        return False, None
    return True, (colour.red / 255, colour.green / 255, colour.blue / 255)


def _edge(seg, at, plane):
    """one segment of a path as a one-edge wire, None for a segment of no length: at maps a point of the file to the
    plane"""
    from svgelements import Arc, Close, CubicBezier, Line, QuadraticBezier

    a, b = at(seg.start), at(seg.end)
    if isinstance(seg, (Line, Close)):
        return px.Line(a, b, plane=plane) if a != b else None
    if isinstance(seg, (CubicBezier, QuadraticBezier)):
        poles = [at(q) for q in ((seg.start, seg.control1, seg.control2, seg.end) if isinstance(seg, CubicBezier)
                                 else (seg.start, seg.control, seg.end))]
        if all(q == poles[0] for q in poles):
            return None
        d = len(poles) - 1
        return px._BSpline(poles, [0.0] * (d + 1) + [1.0] * (d + 1), d, None, plane=plane)
    if isinstance(seg, Arc) and abs(seg.sweep) > 0:
        c, u, v = at(seg.center), at(seg.prx), at(seg.pry)
        A, B = (u[0] - c[0], u[1] - c[1]), (v[0] - c[0], v[1] - c[1])
        t0, sweep = seg.get_start_t(), seg.sweep
        if sweep < 0:
            B, t0, sweep = (-B[0], -B[1]), -t0, -sweep
        r = math.hypot(*A)
        if abs(math.hypot(*B) - r) <= 1e-12 * r and abs(A[0] * B[0] + A[1] * B[1]) <= 1e-12 * r * r and sweep < 2 * math.pi - 1e-12:
            ccw = A[0] * B[1] - A[1] * B[0] > 0
            return px._Arc(a, b, c, ccw, plane=plane)
        return px._Ellipse(c, A, B, t0, t0 + sweep, plane=plane)
    return None


def _subpaths(path, at, plane):
    """the subpaths of a path, each its edges in order with its first and last point and whether the file closes it
    (z)"""
    from svgelements import Close, Move

    out = []
    for seg in path.segments():
        if isinstance(seg, Move):
            out.append({"edges": [], "start": at(seg.end), "end": at(seg.end), "closed": False})
            continue
        if not out:
            out.append({"edges": [], "start": at(seg.start), "end": at(seg.start), "closed": False})
        edge = _edge(seg, at, plane)
        if edge is not None:
            out[-1]["edges"].append(edge)
        out[-1]["end"] = at(seg.end)
        if isinstance(seg, Close):
            out[-1]["closed"] = True
    return [s for s in out if s["edges"]]


def _polygon(edges, step):
    """the points along edges (wires or edge views) in their order and direction, each chord within step of them"""
    return np.vstack([x.Sample(step) for e in edges for x in (e.edges if isinstance(e, px.Wire) else [e])])


def _winding(p, polygons):
    """how often the closed polygons wind around p, counterclockwise counted positive"""
    total = 0
    for P in polygons:
        Q = np.roll(P, -1, axis=0)
        side = (Q[:, 0] - P[:, 0]) * (p[1] - P[:, 1]) - (p[0] - P[:, 0]) * (Q[:, 1] - P[:, 1])
        up = (P[:, 1] <= p[1]) & (Q[:, 1] > p[1]) & (side > 0)
        down = (P[:, 1] > p[1]) & (Q[:, 1] <= p[1]) & (side < 0)
        total += int(up.sum()) - int(down.sum())
    return total


# heights across a face its inside point is looked for at: irrational fractions, so that no vertex lies on them
_SCANS = (math.sqrt(2) - 1, (math.sqrt(5) - 1) / 2, math.pi - 3, math.e - 2, 1 / math.sqrt(3))


def _inside(face, step):
    """a point inside the face, the middle of the widest stretch inside it along a few horizontal lines"""
    polygons = [_polygon(w.edges, step) for w in face.wires]
    (x0, y0), (x1, y1) = face.bounding_box
    best, point = -1.0, None
    for f in _SCANS:
        y = y0 + f * (y1 - y0)
        xs = []
        for P in polygons:
            Q = np.roll(P, -1, axis=0)
            hit = (P[:, 1] <= y) != (Q[:, 1] <= y)
            t = (y - P[hit, 1]) / (Q[hit, 1] - P[hit, 1])
            xs += list(P[hit, 0] + t * (Q[hit, 0] - P[hit, 0]))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            if b - a > best:
                best, point = b - a, ((a + b) / 2, y)
    return point


def _filled(subpaths, rule, tol, plane):
    """the faces a path fills: the regions its subpaths, each closed by a straight line, cut the plane into, kept where
    they wind around them: nonzero, or an odd number of times for evenodd"""
    edges = []
    for s in subpaths:
        loop = list(s["edges"])
        if s["end"] != s["start"]:
            loop.append(px.Line(s["end"], s["start"], plane=plane))
        edges += loop
    boxes = np.array([e.bounding_box for e in edges], float).reshape(-1, 4)
    step = 1e-5 * max(boxes[:, 2].max() - boxes[:, 0].min(), boxes[:, 3].max() - boxes[:, 1].min())
    if not step > 0:
        return []
    polygons = [_polygon(s["edges"], step) for s in subpaths]
    loops = [px.Face([w]) for w in px.Wires(edges, tol) if w.IsClosed()]
    if not loops:
        return []
    kept = []
    for f in px.Glue(loops).faces:
        p = _inside(f, step)
        n = 0 if p is None else _winding(p, polygons)
        if (n % 2 == 1) if rule == "evenodd" else n != 0:
            kept.append(f.Copy())
    return [f.Copy() for f in px.Unify(kept).faces] if kept else []


def _chains(subpaths, tol):
    """each subpath as the wires it draws: one chain, closed where it comes back to its start, cut where it crosses
    itself"""
    return [w for s in subpaths for w in px.Wires(s["edges"], tol)]


# the caps and joins of a stroke as px.Offset makes them; the miter limit is not kept: a miter is always sharp
_ENDS = {"butt": px.Flat, "round": px.Round, "square": px.Square}
_CORNERS = {"miter": px.Sharp, "miter-clip": px.Sharp, "arcs": px.Sharp, "round": px.Round, "bevel": px.Chamfer}


def _band(e, chains, width):
    """the region the stroke of chains covers, as px.Offset draws it with the shape's caps and joins"""
    ends = _ENDS.get(str(e.values.get("stroke-linecap", "butt")).strip(), px.Flat)
    corners = _CORNERS.get(str(e.values.get("stroke-linejoin", "miter")).strip(), px.Sharp)
    return px.Shape(px.Offset(chains, width / 2, both_sides=True, ends=ends, corners=corners))


def _overlap(a, b):
    (ax0, ay0), (ax1, ay1) = a
    (bx0, by0), (bx1, by1) = b
    return ax0 <= bx1 and bx0 <= ax1 and ay0 <= by1 and by0 <= ay1


def _painted(paints):
    """the faces the paints leave visible: each paint covers what earlier paints of another color or layer show there;
    the paints of one color and layer united, in the order they first appear"""
    regions = []
    for key, region in paints:
        if not region.mass > 0:
            continue
        box = region.bounding_box
        for r in regions:
            if r[0] != key and r[1].mass > 0 and _overlap(r[1].bounding_box, box):
                r[1] = r[1] - region
        regions.append([key, region])
    out = []
    for key in dict.fromkeys(k for k, _ in regions):
        col, name = key
        shown = [s for k, s in regions if k == key and s.mass > 0]
        for f in (px.Unify(shown).faces if shown else []):
            face = f.Copy()
            face.name = name
            if col is not None:
                face.col = col
            out.append(face)
    return out


def FromSVG(path: str | os.PathLike, /, tol: float = 1e-7, layers: list[str] | None = None, unit: float | Auto = Auto,
            scale: float | Auto = Auto, plane: Plane | None = None, painted: bool = True) -> list[Face | Wire]:
    """Reads an SVG file as it looks (painted=True) or its paths as drawn (painted=False).

    painted=True: the faces of what the file paints. A fill is the region its path encloses by the path's fill rule
    (nonzero or evenodd), a stroke the band px.Offset draws along the path with its width, caps and joins; later paint
    covers earlier, and the paint of one color and layer is united into faces. painted=False: every path as drawn,
    nothing joined across paths: a filled path's faces, a stroked path's wires, one per subpath (closed where it comes
    back to its start, cut where it crosses itself). px.WriteSVG's files read back exactly with painted=False (it draws
    every edge on top, 0.1 mm wide).

    Lines and circular arcs stay lines and arcs; Bezier curves become exact splines, elliptical arcs exact rational
    splines (as px.Ellipse makes them); the file's transforms are applied, and y is flipped so that v points up. The
    coordinates are scaled to meters from the width and height (mm, cm, in, pt, pc), or with unit= the length of one of
    the drawing's units, or multiplied by scale. A group's label (an Inkscape layer) becomes the name, the paint's color
    the col. Not read: text that is not converted to paths (with a warning), images, a miter's limit (miters are sharp),
    dashes (strokes are solid), transparency (paint covers fully). Needs svgelements (pip install svgelements).

    Args:
        path: the SVG file.
        tol: ends closer than this (after scaling) are joined, a length.
        layers: the labels of the groups to read; None reads all.
        unit: the length one unit of the drawing (its viewBox) stands for (px.mm, ...); px.Auto takes the width and
            height, and CSS pixels (1/96 inch) with a warning if they name no unit.
        scale: what the drawing's numbers are multiplied by; px.Auto converts them from unit into meters, 1 keeps
            them.
        plane: the plane of the faces and wires; None is the xy plane.
        painted: True: the faces of what the file paints; False: the paths as drawn, faces and wires.

    Returns:
        a list of faces and wires in the order of the file; [] for an empty drawing. px.Glue(result) makes them one
        Shape.

    Raises:
        ImportError: svgelements is not installed.
        TypeError: unit is a name; unit or scale is None.
        ValueError: unit is not a positive length, scale not a positive number.

    Examples:
        px.Glue(px.FromSVG("logo.svg"))
        px.FromSVG("part.svg", layers=["outline"], painted=False)
    """
    try:
        from svgelements import SVG, Path, Shape, Text
    except ImportError as e:
        raise ImportError("FromSVG reads SVG files with svgelements: pip install svgelements") from e
    plane = px.AxesOf(plane, "FromSVG")
    svg = SVG.parse(str(path), reify=True)
    # svgelements gives CSS pixels through its own (rounded) factors: the width it gives in pixels divides them out
    width = str(svg.values.get("width", "")).strip()
    box = svg.viewbox
    # the drawing's units per CSS pixel of svgelements
    units = box.width / svg.width if box is not None and svg.width else 1.0
    given = not is_auto(unit, "FromSVG", "unit")
    if given:
        unit = check_length(unit, "FromSVG")
    if not is_auto(scale, "FromSVG", "scale"):
        s = read_factor(1.0, scale, "FromSVG") * units
    elif given:
        s = unit * units
    else:
        meters = _width_meters(width)
        if meters is None or not svg.width:
            warn(f"FromSVG: {path} names no length unit (width {width or 'missing'!r}); read as CSS pixels "
                          "(1/96 inch), unit=px.mm or another sets the length of one unit", px.PlanexWarning)
            s = inch / 96
        else:
            s = meters / svg.width

    def at(p):
        return (p[0] * s, -p[1] * s)

    out, paints, texts = [], [], 0
    for e in svg.elements():
        if isinstance(e, Text):
            texts += 1
        if not isinstance(e, Shape) or str(e.values.get("visibility", "")).strip() in ("hidden", "collapse"):
            continue
        label = e.values.get(LABEL)
        if layers is not None and label not in layers:
            continue
        name = read_name(label) if label is not None else None
        fills, fill_col = _paint(e, "fill", e.fill)
        strokes, stroke_col = _paint(e, "stroke", e.stroke)
        strokes = strokes and e.stroke_width > 0
        subpaths = _subpaths(Path(e), at, plane)
        if not subpaths or not (fills or strokes):
            continue
        faces = _filled(subpaths, str(e.values.get("fill-rule", "nonzero")).strip(), tol, plane) if fills else []
        chains = _chains(subpaths, tol) if strokes else []
        if painted:
            if faces:
                paints.append(((fill_col, name), px.Unify(faces)))
            if chains:
                paints.append(((stroke_col, name), _band(e, chains, e.stroke_width * s)))
            continue
        for item, col in [(f, fill_col) for f in faces] + [(w, stroke_col) for w in chains]:
            if name is not None:
                item.name = name
            if col is not None:
                item.col = col
            out.append(item)
    if texts:
        warn(f"FromSVG: {path} holds {texts} text element(s) whose letters are no paths in the file; they are left "
             "out (in Inkscape, Path > Object to Path converts them)", px.PlanexWarning)
    return _painted(paints) if painted else out
