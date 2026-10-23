# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""inline SVG of planex objects for notebooks"""
from __future__ import annotations

import math
import warnings
from typing import TYPE_CHECKING, Any

from . import _planex as px
from ._sketches import Sketch, sketch_types
from ._compound import Compound
from ._geom import edge_views, oriented, parts, scale

if TYPE_CHECKING:
    from ._planex import Face, QuickSketch, Shape, Wire

SIZE = 300
# a notebook shows the text repr instead of a picture above this many edges
STROKE, FILL = "black", "#dde6f0"


def _fmt(x):
    return format(x, ".10g")


def _pt(v):
    return f"{_fmt(v.x)} {_fmt(0.0 - v.y)}"


def _segment(edge, rev):
    """path commands from the near to the far end of the edge; y is mirrored, so a ccw arc has sweep flag 0"""
    if isinstance(edge, (px.SplineEdge, px.CircleEdge)):
        pieces = edge.approximation
        return " ".join(_segment(p, rev) for p in (reversed(pieces) if rev else pieces))
    far = edge.start if rev else edge.end
    if isinstance(edge, px.LineEdge):
        return "L " + _pt(far)
    r = _fmt(edge.radius)
    arc = f"A {r} {r} 0 0 {1 if edge.ccw == rev else 0} "
    if abs(edge.sweep) > 180:
        return arc + _pt(edge.MidPoint()) + " " + arc + _pt(far)
    return arc + _pt(far)


def _loops(part, eps):
    if not isinstance(part, px.Face):
        return []
    out = []
    for w in part.wires:
        if w.IsClosed() and len(w.edges) > 0:
            chain = oriented([e.edge for e in w.edges], eps)
            first, rev = chain[0]
            start = first.end if rev else first.start
            out.append("M " + _pt(start) + " " + " ".join(_segment(e, r) for e, r in chain) + " Z")
    return out


def _count(part):
    if isinstance(part, sketch_types):
        return sum(len(w.edges) for w in part.Wires())
    return len(part.edges)


def ToSVG(obj: Face | Shape | Wire | Compound | Sketch | QuickSketch | Any, /, size: float = SIZE,
          **options: Any) -> str | None:
    """The object as the text of an SVG picture in its plane coordinates, its faces filled.

    A sketch is drawn in its own look: every element in the colour of its state (green fully constrained, black free,
    pink over-constrained) with its id when hovered, construction dashed, in a frame with numbered ticks on a grid.

    Args:
        obj: a Face, Shape, Wire, Compound, sketch or DeformableShape.
        size: the larger side of the picture in pixels.
        options: for a sketch: show_constraints=True adds a sign per constraint, show_ends=True a blue start and an
            orange end piece on every line and arc, show_direction=True an arrow from start to end; other objects
            ignore them.

    Returns:
        the SVG text; None if the object has no extent.

    Examples:
        face.ToSVG()
        sk.ToSVG(size=500, show_constraints=True)
    """
    if isinstance(obj, sketch_types):
        return SketchSVG(obj, size, **options)
    if isinstance(obj, px.DeformableShape):
        obj = Compound(obj.Wires())
    box = obj.bounding_box
    if box is None:
        return None
    (xmin, ymin), (xmax, ymax) = box
    extent = max(xmax - xmin, ymax - ymin)
    if extent == 0:
        return None
    m = 0.05 * extent
    w, h = xmax - xmin + 2 * m, ymax - ymin + 2 * m
    edges = [v.edge for v in edge_views(obj)]
    eps = 1e-9 * scale(edges)
    # one path per face: its holes stay holes, overlapping parts of a compound are filled
    fill = [" ".join(loops) for loops in (_loops(part, eps) for part in parts(obj)) if loops]
    stroke = ["M " + _pt(e.start) + " " + _segment(e, False) for e in edges]
    body = ""
    for d in fill:
        body += f'<path d="{d}" fill="{FILL}" fill-rule="evenodd" stroke="none"/>'
    body += (f'<path d="{" ".join(stroke)}" fill="none" stroke="{STROKE}" stroke-width="{_fmt(1.5 * max(w, h) / size)}"'
             ' stroke-linecap="round" stroke-linejoin="round"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{_fmt(size * w / max(w, h))}"'
            f' height="{_fmt(size * h / max(w, h))}" viewBox="{_fmt(xmin - m)} {_fmt(0.0 - ymax - m)} {_fmt(w)} {_fmt(h)}">'
            f'{body}</svg>')


STATE = {"full": "#2b8a3e", "under": "black", "over": "#d6338c"}
# projected geometry (Sketch.Project), CATIA's yellow darkened to read on white
PROJECTED = "#d4a017"


def _colour(g):
    """an element's colour: gold where it is projected, else its state's"""
    return PROJECTED if g.get("projected") else STATE[g["state"]]
GRID, SUBGRID, FRAME, LABEL = "#c8c8c8", "#e4e4e4", "#555555", "#333333"
START, END, GLYPH = "#1c7ed6", "#f08c00", "#495057"


class Picture:
    """an SVG picture; a notebook shows it"""

    def __init__(self, svg):
        self.svg = svg

    def _repr_svg_(self):
        return self.svg

    def __str__(self):
        return self.svg


def _clip_axis(g, x0, x1, y0, y1):
    """the part of an infinite line (normal angle, distance) inside the box, or None"""
    t = math.radians(g["normal_angle"])
    n, u = (math.cos(t), math.sin(t)), (-math.sin(t), math.cos(t))
    a = (n[0] * g["d"], n[1] * g["d"])
    lo, hi = -math.inf, math.inf
    for k, (low, high) in enumerate(((x0, x1), (y0, y1))):
        if abs(u[k]) < 1e-15:
            if not low <= a[k] <= high:
                return None
            continue
        s0, s1 = sorted(((low - a[k]) / u[k], (high - a[k]) / u[k]))
        lo, hi = max(lo, s0), min(hi, s1)
    if lo >= hi:
        return None
    return (a[0] + lo * u[0], a[1] + lo * u[1]), (a[0] + hi * u[0], a[1] + hi * u[1])


def _ticks(lo, hi):
    """a round step for about five grid lines over [lo, hi] and its subgrid step"""
    raw = (hi - lo) / 5
    mag = 10 ** math.floor(math.log10(raw))
    mantissa = next(m for m in (1, 2, 5, 10) if raw / mag <= m * 1.5) if raw / mag <= 15 else 10
    step = mantissa * mag
    return step, step / (4 if mantissa == 2 else 5)


def _label(v, step):
    digits = max(0, -math.floor(math.log10(step) + 1e-9))
    text = f"{v:.{digits}f}"
    return "0" if float(text) == 0 else text


def _arc_samples(g, n=48):
    cx, cy = g["center"]
    r = g["r"]
    if "start" not in g:
        return [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n)) for k in range(n + 1)]
    t0 = math.atan2(g["start"][1] - cy, g["start"][0] - cx)
    t1 = math.atan2(g["end"][1] - cy, g["end"][0] - cx)
    sweep = (t1 - t0) % (2 * math.pi) if g["ccw"] else -((t0 - t1) % (2 * math.pi))
    return [(cx + r * math.cos(t0 + sweep * k / n), cy + r * math.sin(t0 + sweep * k / n)) for k in range(n + 1)]


def _unit(x, y):
    n = math.hypot(x, y) or 1.0
    return x / n, y / n


def _along(poly, t):
    """the point at fraction t of the polyline's length and the unit direction there"""
    lengths = [math.dist(a, b) for a, b in zip(poly, poly[1:])]
    goal, run = t * sum(lengths), 0.0
    for (a, b), d in zip(zip(poly, poly[1:]), lengths):
        if run + d >= goal and d > 0:
            f = (goal - run) / d
            return (a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1])), _unit(b[0] - a[0], b[1] - a[1])
        run += d
    return poly[-1], _unit(poly[-1][0] - poly[-2][0], poly[-1][1] - poly[-2][1])


def _piece(poly, length):
    """the first length (pixels) of the polyline"""
    out, run = [poly[0]], 0.0
    for a, b in zip(poly, poly[1:]):
        d = math.dist(a, b)
        if run + d >= length:
            f = (length - run) / d if d > 0 else 0.0
            out.append((a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1])))
            return out
        out.append(b)
        run += d
    return out


def _d(poly, close=False):
    return "M " + " L ".join(f"{_fmt(x)} {_fmt(y)}" for x, y in poly) + (" Z" if close else "")


CURVES = ("Line", "Arc", "Circle", "Spline")


def _path(g):
    """a drawn element as points along it"""
    if g["kind"] == "Line":
        return [tuple(g["start"]), tuple(g["end"])]
    if g["kind"] == "Spline":
        return [tuple(p) for p in g["path"]]
    return _arc_samples(g, 96)


def _ends(drawing):
    """the places lines, arcs and splines end at, and the samples of projected splines (these points are not drawn on
    their own)"""
    out = {tuple(g[k]) for g in drawing if g["kind"] in ("Line", "Arc") for k in ("start", "end")}
    out |= {tuple(g["center"]) for g in drawing if g["kind"] in ("Arc", "Circle")}
    out |= {tuple(p) for g in drawing if g["kind"] == "Spline" and g["projected"] for p in g["points"]}
    return out | {tuple(g[k]) for g in drawing if g["kind"] == "Spline" for k in ("start", "end")}


def _esc(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _shown_constraints(sketch, show_constraints):
    """show_constraints for a sketch: always off for a QuickSketch, which has none, with a warning if asked for"""
    if isinstance(sketch, Sketch):
        return bool(show_constraints)
    if show_constraints:
        warnings.warn("a QuickSketch has no constraints, show_constraints is ignored", px.PlanexWarning, stacklevel=3)
    return False


def SketchSVG(sketch, size=SIZE, show_constraints=None, show_ends=False, show_direction=False):
    """a sketch: every element in the colour of its state (green fully constrained, black free, pink over-constrained;
    all free while its constraints are disabled) with its id when hovered, construction dashed, in a frame with
    numbered ticks on a light dashed grid; show_constraints adds a small sign per constraint (hover: what it is; not
    while the constraints are disabled), show_ends a blue start and an orange end piece on every line and arc,
    show_direction an arrow from start to end in their middle"""
    show_constraints = _shown_constraints(sketch, show_constraints)
    everything = {g["id"]: g for g in sketch._Drawing()}
    drawing = [g for i, g in everything.items() if i not in ("H", "V", "origin", "px.origin")]
    ends = _ends(drawing)
    shown = [g for g in drawing if g["kind"] in CURVES or (g["kind"] == "Point" and tuple(g["point"]) not in ends)]
    pts = []
    for g in shown:
        if g["kind"] == "Line":
            pts += [g["start"], g["end"]]
        elif g["kind"] == "Spline":
            pts += g["path"]
        elif g["kind"] == "Point":
            pts.append(g["point"])
        else:
            pts += _arc_samples(g)
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    extent = max(max(xs) - min(xs), max(ys) - min(ys)) or 1.0
    m = 0.06 * extent
    step, sub = _ticks(0, extent)
    x0, x1 = math.floor((min(xs) - m) / sub) * sub, math.ceil((max(xs) + m) / sub) * sub
    y0, y1 = math.floor((min(ys) - m) / sub) * sub, math.ceil((max(ys) + m) / sub) * sub
    k = size / max(x1 - x0, y1 - y0)
    left, bottom, top, right = 44, 24, 8, 16
    groups = _signs(sketch, everything, k) if show_constraints else []
    # the margins grow until every sign's label fits (bold 10 px text, about 6 px a character)
    for g in groups:
        for item in g["items"]:
            if item[0] not in ("text", "circle", "anchor"):
                continue
            px_ = k * (item[1][0] - x0) + item[2][0]
            py_ = k * (y1 - item[1][1]) - item[2][1]
            hw, hh = (3.2 * len(item[3]) if item[0] == "text" else 6.0), 7.0
            left = max(left, hw - px_ + 2)
            right = max(right, px_ + hw - k * (x1 - x0) + 2)
            top = max(top, hh - py_ + 2)
            bottom = max(bottom, py_ + hh - k * (y1 - y0) + 2)
    width, height = left + k * (x1 - x0) + right, top + k * (y1 - y0) + bottom

    def P(p):
        return left + k * (p[0] - x0), top + k * (y1 - p[1])

    def lines(spacing, colour, dash, w):
        out = []
        for i in range(math.ceil(x0 / spacing - 1e-9), math.floor(x1 / spacing + 1e-9) + 1):
            out.append(f"M {_fmt(P((i * spacing, 0))[0])} {_fmt(P((0, y0))[1])} V {_fmt(P((0, y1))[1])}")
        for i in range(math.ceil(y0 / spacing - 1e-9), math.floor(y1 / spacing + 1e-9) + 1):
            out.append(f"M {_fmt(P((x0, 0))[0])} {_fmt(P((0, i * spacing))[1])} H {_fmt(P((x1, 0))[0])}")
        return f'<path d="{" ".join(out)}" stroke="{colour}" stroke-width="{w}" stroke-dasharray="{dash}" fill="none"/>'

    body = f'<rect x="0" y="0" width="{_fmt(width)}" height="{_fmt(height)}" fill="white"/>'
    body += lines(sub, SUBGRID, "2 3", 0.5) + lines(step, GRID, "4 3", 0.7)
    (fx0, fy0), (fx1, fy1) = P((x0, y1)), P((x1, y0))
    body += f'<rect x="{_fmt(fx0)}" y="{_fmt(fy0)}" width="{_fmt(fx1 - fx0)}" height="{_fmt(fy1 - fy0)}" fill="none" stroke="{FRAME}" stroke-width="1"/>'
    labels = ""
    for i in range(math.ceil(x0 / step - 1e-9), math.floor(x1 / step + 1e-9) + 1):
        labels += f'<text x="{_fmt(P((i * step, 0))[0])}" y="{_fmt(fy1 + 15)}" text-anchor="middle">{_label(i * step, step)}</text>'
    for i in range(math.ceil(y0 / step - 1e-9), math.floor(y1 / step + 1e-9) + 1):
        labels += f'<text x="{_fmt(fx0 - 5)}" y="{_fmt(P((0, i * step))[1] + 3.5)}" text-anchor="end">{_label(i * step, step)}</text>'
    body += f'<g font-family="sans-serif" font-size="10" fill="{LABEL}">{labels}</g>'

    # every element as a polyline in pixels
    poly = {}
    for g in shown:
        if g["kind"] in CURVES:
            poly[g["id"]] = [P(p) for p in _path(g)]
    # the projections under the sketch's own lines
    for g in sorted(shown, key=lambda g: not g.get("projected")):
        colour, title = _colour(g), f'<title>{_esc(g["id"])}</title>'
        if g["kind"] == "Point":
            x, y = P(g["point"])
            body += f'<circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="2.5" fill="{colour}" stroke="none">{title}</circle>'
            continue
        style = f'stroke="{colour}" fill="none" stroke-linecap="round" stroke-linejoin="round"'
        style += ' stroke-width="1.2" stroke-dasharray="6 4"' if g["construction"] and not g.get("projected") else ' stroke-width="2"'
        line = poly[g["id"]]
        body += f'<path d="{_d(line, g["kind"] == "Circle" or g.get("closed", False))}" {style}>{title}</path>'
        if g["kind"] == "Circle":
            continue
        total = sum(math.dist(a, b) for a, b in zip(line, line[1:]))
        if show_ends:
            piece = min(14.0, 0.3 * total)
            for points, colour_end, end in ((_piece(line, piece), START, ".start"), (_piece(line[::-1], piece), END, ".end")):
                body += (f'<path d="{_d(points)}" stroke="{colour_end}" stroke-width="2.8" fill="none" stroke-linecap="butt">'
                         f'<title>{_esc(g["id"] + end)}</title></path>')
        if show_direction:
            (mx, my), (tx, ty) = _along(line, 0.5)
            nx, ny = ty, -tx
            tip = (mx + 4 * tx, my + 4 * ty)
            base = [(mx - 4 * tx + 3.5 * nx, my - 4 * ty + 3.5 * ny), (mx - 4 * tx - 3.5 * nx, my - 4 * ty - 3.5 * ny)]
            body += f'<path d="{_d([tip] + base, True)}" fill="{colour}" stroke="none">{title}</path>'
    for g in drawing:
        if g["kind"] != "Axis":
            continue
        seg = _clip_axis(g, x0, x1, y0, y1)
        if seg:
            body += (f'<path d="{_d([P(seg[0]), P(seg[1])])}" stroke="{_colour(g)}" stroke-width="1.2" '
                     f'stroke-dasharray="9 3 2 3" fill="none"><title>{_esc(g["id"])}</title></path>')
    if show_constraints:
        body += _svg_signs(groups, P)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{_fmt(width)}" height="{_fmt(height)}"'
            f' viewBox="0 0 {_fmt(width)} {_fmt(height)}">{body}</svg>')


def _signs(sketch, everything, k=None, font=10):
    """the signs of the constraints, in the sketch's coordinates with offsets in pixels (sx, sy; sy up on screen), so
    that every renderer draws them at the same size whatever the zoom: one group per constraint, with its colour,
    hover text and items: ("text", at, (sx, sy), text, angle), ("circle", at, (sx, sy)), ("leader", a, b),
    ("corner", at, u, v), ("anchor", at, (sx, sy)), ("chevron", at, d), ("tangent", at, t): two short lines along the
    tangent t on both sides of the touch point"""
    paths = {}
    for i, g in everything.items():
        if i in ("H", "V", "origin", "px.origin"):
            continue
        if g["kind"] in CURVES:
            paths[i] = _path(g)
    xs = [p[0] for path in paths.values() for p in path] or [0.0]
    ys = [p[1] for path in paths.values() for p in path] or [0.0]
    extent = max(max(xs) - min(xs), max(ys) - min(ys)) or 1.0
    tol = 1e-7 * extent
    # pixels per unit: the place of every sign on screen, to keep them apart
    k = k or 300 / extent
    taken = []

    def free(c, half):
        return all(abs(c[0] - x) >= half[0] + hx or abs(c[1] - y) >= half[1] + hy for (x, y), (hx, hy) in taken)

    def occupy(p, shift, half):
        taken.append(((p[0] * k + shift[0], p[1] * k + shift[1]), half))

    def spot(p, n, half=(6.0, 6.0)):
        """the pixel offset (y up) off p for a sign of half size half: along n, further out, then on the other side,
        then across, the first place no other sign covers"""
        across = (-n[1], n[0])
        for d in (10, 20, 30, 40, 50):
            for v in (n, (-n[0], -n[1]), across, (-across[0], -across[1])):
                shift = (d * v[0], d * v[1])
                if free((p[0] * k + shift[0], p[1] * k + shift[1]), half):
                    occupy(p, shift, half)
                    return shift
        shift = (60 * n[0], 60 * n[1])
        occupy(p, shift, half)
        return shift

    def size(text):
        return 0.31 * font * len(text) + 3.0, 0.55 * font + 2.5

    def label(p, n, text):
        return ("text", p, spot(p, n, size(text)), text, 0)

    def point(ref):
        g = everything.get(ref)
        if g is not None and g["kind"] == "Point":
            return tuple(g["point"])
        return tuple(sketch._Location(ref)) if g is None else None

    def middle(ref):
        """a place on the element and the normal there"""
        if ref in paths:
            g = everything[ref]
            if g["kind"] == "Circle":
                return (g["center"][0], g["center"][1] + g["r"]), (0.0, 1.0)
            p, (tx, ty) = _along(paths[ref], 0.5)
            return p, (-ty, tx)
        return point(ref), (0.0, 1.0)

    def center(ref):
        g = everything.get(ref)
        if g is not None and g["kind"] in ("Arc", "Circle"):
            return tuple(g["center"])
        return point(ref)

    def meet(a, b):
        """where two lines meet within both, and the directions from there to their far ends"""
        if everything[a]["kind"] != "Line" or everything[b]["kind"] != "Line":
            return None
        (p, q), (r, t) = paths[a], paths[b]
        d, e = (q[0] - p[0], q[1] - p[1]), (t[0] - r[0], t[1] - r[1])
        den = d[0] * e[1] - d[1] * e[0]
        if abs(den) <= tol * math.hypot(*d) * math.hypot(*e) / extent:
            return None
        f = ((r[0] - p[0]) * e[1] - (r[1] - p[1]) * e[0]) / den
        x = (p[0] + f * d[0], p[1] + f * d[1])

        def inside(x, a_, b_, d_):
            return min(math.dist(x, a_), math.dist(x, b_)) <= tol or 0 <= (x[0] - a_[0]) * d_[0] + (x[1] - a_[1]) * d_[1] <= d_[0] ** 2 + d_[1] ** 2

        if not (inside(x, p, q, d) and inside(x, r, t, e)):
            return None
        far = lambda a_, b_: a_ if math.dist(a_, x) > math.dist(b_, x) else b_
        u, v = far(p, q), far(r, t)
        return x, _unit(u[0] - x[0], u[1] - x[1]), _unit(v[0] - x[0], v[1] - x[1])

    def joint(a, b):
        if a not in paths or b not in paths or "Circle" in (everything[a]["kind"], everything[b]["kind"]):
            return None
        for p in (paths[a][0], paths[a][-1]):
            for q in (paths[b][0], paths[b][-1]):
                if math.dist(p, q) <= tol:
                    return p
        return None

    def on(ref):
        g = everything[ref]
        return [tuple(q) for q in g["points"]] + [tuple(g["start"]), tuple(g["end"])] if g["kind"] == "Spline" else [paths[ref][0], paths[ref][-1]]

    def touch(a, b):
        """a point both run through (a spline through any of its points)"""
        if a not in paths or b not in paths:
            return None
        return next((p for p in on(a) for q in on(b) if math.dist(p, q) <= tol), None)

    def contact(a, b):
        """where a spline touches the other element away from its points: its point nearest the other"""
        if a not in paths or b not in paths:
            return None
        curve, other = (a, b) if everything[a]["kind"] == "Spline" else (b, a)
        if everything[other]["kind"] == "Spline":
            return min(paths[curve], key=lambda p: min(math.dist(p, q) for q in paths[other]))
        return min(paths[curve], key=lambda p: math.dist(p, foot(p, other)))

    def direction(ref, p):
        g = everything[ref]
        if g["kind"] in ("Arc", "Circle"):
            r = _unit(p[0] - g["center"][0], p[1] - g["center"][1])
            return -r[1], r[0]
        path = paths[ref]
        i = min(range(len(path)), key=lambda j: math.dist(path[j], p))
        a, b = path[max(i - 1, 0)], path[min(i + 1, len(path) - 1)]
        return _unit(b[0] - a[0], b[1] - a[1])

    def foot(p, ref):
        g = everything[ref]
        if g["kind"] == "Spline":
            return min(paths[ref], key=lambda q: math.dist(q, p))
        if g["kind"] in ("Arc", "Circle"):
            c = g["center"]
            u = _unit(p[0] - c[0], p[1] - c[1])
            return c[0] + g["r"] * u[0], c[1] + g["r"] * u[1]
        a, b = paths[ref][0], paths[ref][-1]
        u = _unit(b[0] - a[0], b[1] - a[1])
        f = (p[0] - a[0]) * u[0] + (p[1] - a[1]) * u[1]
        return a[0] + f * u[0], a[1] + f * u[1]

    groups = []
    for c in sketch._Constraints():
        refs = c["refs"]
        if any(everything.get(r, {}).get("kind") in ("Pattern", "Offset") for r in refs):
            continue
        if c["derived"] and c["value"] is not None:
            continue
        drawn = [r for r in refs if r not in ("H", "V")]
        kind, items = c["kind"], []
        if kind == "Coincident":
            places = [p for p in (point(r) for r in refs if r not in paths) if p is not None]
            if places and c["state"] == "over":
                occupy(places[0], (0.0, 0.0), (5.0, 5.0))
                items.append(("circle", places[0], (0.0, 0.0)))
            elif places:
                items.append(("circle", places[0], spot(places[0], (0.7071, 0.7071), (5.0, 5.0))))
            else:
                marks = [middle(r) for r in drawn]
                items += [label(p, n, "≡") for p, n in marks]
                if len(marks) == 2:
                    items.append(("leader", marks[0][0], marks[1][0]))
        elif kind in ("Horizontal", "Vertical"):
            if len(refs) == 2:
                a, b = point(refs[0]), point(refs[1])
                m = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
                items += [("leader", a, b), label(m, (0.0, 1.0), kind[0])]
            elif drawn:
                p, n = middle(drawn[0])
                items.append(label(p, n, kind[0]))
        elif kind == "Perpendicular" and len(drawn) == 2 and meet(*drawn):
            j, u, v = meet(*drawn)
            occupy(j, (5 * (u[0] + v[0]), 5 * (u[1] + v[1])), (5.0, 5.0))
            items.append(("corner", j, u, v))
        elif kind in ("Parallel", "Perpendicular", "Equal"):
            glyph = {"Parallel": "//", "Perpendicular": "⊥", "Equal": "="}[kind]
            marks = [middle(r) for r in drawn]
            items += [label(p, n, glyph) for p, n in marks]
            if len(marks) == 2:
                items.append(("leader", marks[0][0], marks[1][0]))
        elif kind == "Tangent" and len(drawn) == 2 and "Spline" in (everything[drawn[0]]["kind"], everything[drawn[1]]["kind"]):
            j = touch(*drawn) or contact(*drawn)
            if j is not None:
                occupy(j, (0.0, 0.0), (7.0, 7.0))
                items.append(("tangent", j, direction(drawn[0] if everything[drawn[0]]["kind"] == "Spline" else drawn[1], j)))
        elif kind == "Tangent" and len(drawn) == 2:
            a, b = drawn
            round_ref = a if everything[a]["kind"] != "Line" else b
            other = b if round_ref == a else a
            j = joint(a, b) or (foot(tuple(everything[round_ref]["center"]), other) if everything[other]["kind"] == "Line"
                                else foot(tuple(everything[other]["center"]), round_ref))
            c0 = everything[round_ref]["center"]
            r = _unit(j[0] - c0[0], j[1] - c0[1])
            occupy(j, (0.0, 0.0), (7.0, 7.0))
            items.append(("tangent", j, (-r[1], r[0])))
        elif kind in ("Symmetric", "SymmetricCarriers") and len(refs) == 3:
            a, b = center(refs[0]) or middle(refs[0])[0], center(refs[1]) or middle(refs[1])[0]
            occupy(a, (0.0, 0.0), (5.0, 5.0))
            occupy(b, (0.0, 0.0), (5.0, 5.0))
            items += [("chevron", a, _unit(b[0] - a[0], b[1] - a[1])), ("chevron", b, _unit(a[0] - b[0], a[1] - b[1]))]
        elif kind == "Concentric":
            p = center(refs[0])
            occupy(p, (0.0, 0.0), (6.0, 6.0))
            items.append(("text", p, (0.0, 0.0), "◎", 0))
        elif kind == "Fix" and drawn:
            p, n = middle(drawn[0]) if drawn[0] in paths else (center(drawn[0]), (0.0, 1.0))
            items.append(("anchor", p, spot(p, n, (7.0, 8.0))))
        elif c["value"] is not None:
            v = c["value"]
            value = {"Radius": f"R{v:g}", "Diameter": f"⌀{v:g}", "Angle": f"{v:g}°"}.get(kind, f"{v:g}")
            # an automatic id (the kind's, or the Reference.N of a measure in an expression) is not shown
            if not c["id"].startswith((kind + ".", "Reference.")):
                value = f'{c["id"]} = {value}'
            if c.get("reference"):
                value = f"({value})"
            if len(drawn) == 2:
                a, b = drawn
                pa = point(a) if a not in paths else None
                pb = point(b) if b not in paths else None
                if pa is None and pb is None:
                    pb = middle(b)[0] if everything[b]["kind"] == "Line" else center(b)
                if pa is None:
                    pa = foot(pb, a)
                if pb is None:
                    pb = foot(pa, b)
                m = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
                n = _unit(pa[1] - pb[1], pb[0] - pa[0])
                items += [("leader", pa, pb), label(m, n, value)]
            elif drawn:
                p, n = middle(drawn[0])
                items.append(label(p, n, value))
        if items:
            groups.append({"title": c["id"] + ": " + c["text"], "colour": STATE["over"] if c["state"] == "over" else GLYPH,
                           "items": items})
    return groups


def _svg_signs(groups, P):
    """the signs as SVG, P the sketch-to-pixel map (pixel y down); a sign set off its place shows a thin line to it
    while hovered"""
    out = "<style>.sg .ld{visibility:hidden}.sg:hover .ld{visibility:visible}</style>"
    for g in groups:
        colour, body = g["colour"], ""
        for item in g["items"]:
            kind = item[0]
            if kind in ("text", "circle", "anchor"):
                px_, py_ = P(item[1])
                x, y = px_ + item[2][0], py_ - item[2][1]
                reach = math.hypot(*item[2])
                if reach > 8:
                    f = (reach - 6) / reach
                    body += (f'<path class="ld" d="M {_fmt(px_)} {_fmt(py_)} L {_fmt(px_ + f * item[2][0])} {_fmt(py_ - f * item[2][1])}" '
                             f'stroke="{colour}" stroke-width="0.6" fill="none"/>')
            if kind == "text":
                body += (f'<text x="{_fmt(x)}" y="{_fmt(y)}" font-family="sans-serif" font-size="10" font-weight="bold" '
                         f'text-anchor="middle" dominant-baseline="central" fill="{colour}">{_esc(item[3])}</text>')
            elif kind == "circle":
                body += f'<circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="4" fill="none" stroke="{colour}" stroke-width="1.2"/>'
            elif kind == "leader":
                a, b = P(item[1]), P(item[2])
                body += f'<path d="M {_fmt(a[0])} {_fmt(a[1])} L {_fmt(b[0])} {_fmt(b[1])}" stroke="{colour}" stroke-width="0.6" stroke-dasharray="2 2" fill="none"/>'
            elif kind == "corner":
                j, u, v = P(item[1]), (item[2][0], -item[2][1]), (item[3][0], -item[3][1])
                corner = [(j[0] + 7 * u[0], j[1] + 7 * u[1]), (j[0] + 7 * (u[0] + v[0]), j[1] + 7 * (u[1] + v[1])), (j[0] + 7 * v[0], j[1] + 7 * v[1])]
                body += f'<path d="{_d(corner)}" stroke="{colour}" stroke-width="0.9" fill="none"/>'
            elif kind == "anchor":
                anchor = (f"M {_fmt(x)} {_fmt(y - 3.2)} V {_fmt(y + 5.5)} M {_fmt(x - 3)} {_fmt(y - 1.5)} H {_fmt(x + 3)} "
                          f"M {_fmt(x - 5)} {_fmt(y + 1.5)} Q {_fmt(x)} {_fmt(y + 8.5)} {_fmt(x + 5)} {_fmt(y + 1.5)} "
                          f"M {_fmt(x - 6.2)} {_fmt(y + 3)} L {_fmt(x - 5)} {_fmt(y + 1.5)} L {_fmt(x - 3.4)} {_fmt(y + 2.4)} "
                          f"M {_fmt(x + 6.2)} {_fmt(y + 3)} L {_fmt(x + 5)} {_fmt(y + 1.5)} L {_fmt(x + 3.4)} {_fmt(y + 2.4)}")
                body += (f'<circle cx="{_fmt(x)}" cy="{_fmt(y - 4.8)}" r="1.6" fill="none" stroke="{colour}" stroke-width="1"/>'
                         f'<path d="{anchor}" stroke="{colour}" stroke-width="1.1" fill="none" stroke-linecap="round" stroke-linejoin="round"/>')
            elif kind == "tangent":
                j, t = P(item[1]), (item[2][0], -item[2][1])
                n = (-t[1], t[0])
                for side in (3.5, -3.5):
                    c = (j[0] + side * n[0], j[1] + side * n[1])
                    body += f'<path d="{_d([(c[0] - 6 * t[0], c[1] - 6 * t[1]), (c[0] + 6 * t[0], c[1] + 6 * t[1])])}" stroke="{colour}" stroke-width="1.2" fill="none"/>'
            elif kind == "chevron":
                p, d = P(item[1]), (item[2][0], -item[2][1])
                n = (d[1], -d[0])
                tip = (p[0] + 5 * d[0], p[1] + 5 * d[1])
                body += f'<path d="{_d([(p[0] + 4 * n[0], p[1] + 4 * n[1]), tip, (p[0] - 4 * n[0], p[1] - 4 * n[1])])}" stroke="{colour}" stroke-width="1.2" fill="none"/>'
        out += f'<g class="sg"><title>{_esc(g["title"])}</title>{body}</g>'
    return out


def SketchPicture(sketch, size=SIZE, show_constraints=None, show_ends=False, show_direction=False):
    """the sketch as an SVG picture with the options of SketchSVG"""
    return Picture(SketchSVG(sketch, size, show_constraints, show_ends, show_direction))


def _repr_svg(obj):
    """_repr_svg_ for notebooks: the picture, or None (the text repr) above px.config.max_preview_edges edges"""
    if isinstance(obj, sketch_types):
        return SketchSVG(obj) if len(obj._Drawing()) <= px.config.max_preview_edges else None
    if isinstance(obj, px.DeformableShape):
        obj = Compound(obj.Wires())
    if sum(_count(part) for part in parts(obj)) > px.config.max_preview_edges:
        return None
    return ToSVG(obj)
