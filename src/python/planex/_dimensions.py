# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Drafting dimensions after DIN practice (DIN ISO 129-1) for the constraint sketch's plot and px.Drawing:
what each dimension constraint measures, where its dimension line goes (laid out automatically, or where a drawing
places it), and the strokes that draw it, in the sketch's coordinates with sizes given on paper in mm."""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

import numpy as np

# sizes on paper in mm: line group 0.5 (DIN EN ISO 128-2), lettering 3.5 mm (DIN EN ISO 3098-2)
WIDE, NARROW = 0.5, 0.25
HEIGHT = 3.5
ARROW = 10 * NARROW
OVER = 2.0
FIRST = 8.0
ROW = 7.0
CENTER_OVER = 3.0
CROSS = 6.0
LEADER = 8.0
COMPASS = {"e": 0.0, "ne": 45.0, "n": 90.0, "nw": 135.0, "w": 180.0, "sw": 225.0, "s": 270.0, "se": 315.0}
CURVES = ("Line", "Arc", "Circle", "Spline")


def Fmt(value, sign=","):
    """a number with sign as its decimal sign (DIN's comma) and at most three decimals; a text as it is"""
    if isinstance(value, str):
        return value
    return f"{value:.3f}".rstrip("0").rstrip(".").replace(".", sign)


def _unit(v):
    v = np.asarray(v, float)
    n = float(np.linalg.norm(v))
    return v / n if n > 0 else v


def _upright(u):
    """the angle of text along u, in (-90, 90]: read from below or from the right, a line within 1e-6 degrees of the
    vertical too (a dimension line's ends computed apart are a hair off it)"""
    angle = math.degrees(math.atan2(u[1], u[0]))
    while angle > 90 + 1e-6:
        angle -= 180
    while angle <= -90 + 1e-6:
        angle += 180
    return angle


def Direction(place):
    """a direction as an angle in degrees from h, or a compass string ("n", "ne", ...), as a unit vector"""
    if isinstance(place, str):
        key = place.lower()
        if key not in COMPASS:
            raise ValueError(f"Place: '{place}' is no direction; an angle in degrees or one of "
                             + ", ".join(f"'{k}'" for k in COMPASS))
        angle = COMPASS[key]
    else:
        angle = float(place)
    t = math.radians(angle)
    return np.array([math.cos(t), math.sin(t)])


@dataclass
class Place:
    """where a drawing puts one dimension, see Drawing.Place"""
    offset: float | None = None
    direction: Any = None
    radius: float | None = None
    leader: bool = False
    outside: bool = False
    inside: bool = False
    across: float | None = None
    supplement: bool = False
    count: int | None = None
    text: str | None = None
    suffix: str | None = None


@dataclass
class Dimension:
    """one dimension constraint as a drawing measures it: a linear one from p to q along u (lines: the elements whose
    nearer end an extension line starts from), a radius or diameter of the circle (c, r), an angle at c from direction
    a counterclockwise to b with legs that long; prefix written before the value (⌀ for a turned part's diameter seen
    from the side)"""
    id: str
    kind: str
    value: float
    reference: bool = False
    over: bool = False
    name: str | None = None
    element: str | None = None
    p: Any = None
    q: Any = None
    u: Any = None
    lines: tuple = (None, None)
    c: Any = None
    r: float = 0.0
    a: Any = None
    prefix: str = ""
    b: Any = None
    legs: tuple = ((0.0, 0.0), (0.0, 0.0))
    count: int = 1
    span: float = 0.0
    refs: tuple = ()
    total: float | None = None
    sweep: float = 0.0


@dataclass
class Strokes:
    """what draws one dimension, in the sketch's coordinates: lines (a, b, style) with style "thin", "center" or
    "wide", arcs (polylines), arrows (tip, direction), filled polygons, texts (anchor, angle in degrees, text) with their
    baseline at the anchor, height mm high; entity: the dimension as a CAD dimension entity has it (a DXF file's), with
    "<>" for the measured value in its text"""
    id: str
    over: bool = False
    lines: list = field(default_factory=list)
    arcs: list = field(default_factory=list)
    arrows: list = field(default_factory=list)
    fills: list = field(default_factory=list)
    texts: list = field(default_factory=list)
    height: float = HEIGHT
    entity: dict | None = None


def _counts(everything, constraints):
    """for each circle and arc, how many equal ones it stands for: its pattern copies, its px.Equal partners and its
    mirror images (_mirrors)"""
    parent = {}

    def find(x):
        while parent.get(x, x) != x:
            x = parent[x]
        return x

    def join(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    rounds = [i for i, g in everything.items() if g["kind"] in ("Circle", "Arc")]
    patterns = [i for i, g in everything.items() if g["kind"] == "Pattern"]
    for c in constraints:
        if c["kind"] == "Equal" and len(c["refs"]) == 2 and all(r in rounds for r in c["refs"]):
            join(*c["refs"])
    for i in rounds:
        for j in rounds:
            if j != i and j.endswith("." + i) and any(j.startswith(p + ".") for p in patterns):
                join(i, j)
    for a, b in _mirrors(everything, constraints, rounds):
        join(a, b)
    size = {}
    for i in rounds:
        size[find(i)] = size.get(find(i), 0) + 1
    return {i: size[find(i)] for i in rounds}


def _mirrors(everything, constraints, rounds):
    """the pairs of circles or arcs that are mirror images, one kind and one radius with their centres mirrored, about
    an axis the constraints make the sketch symmetric about"""
    axes = {c["refs"][-1] for c in constraints if c["kind"] == "Symmetric"} | \
           {c["refs"][1] for c in constraints if c["kind"] == "Coincident" and len(c["refs"]) == 2 and str(c["refs"][0]).endswith(".mid")}
    lines = []
    for i in axes:
        g = everything.get(i)
        if g is not None and "start" in g and "end" in g:
            p, q = np.asarray(g["start"], float)[:2], np.asarray(g["end"], float)[:2]
        elif i in ("H", "V"):
            p, q = np.zeros(2), np.array([1.0, 0.0] if i == "H" else [0.0, 1.0])
        else:
            continue
        if np.linalg.norm(q - p) > 0:
            lines.append((p, (q - p) / np.linalg.norm(q - p)))
    places = [(i, everything[i]["kind"], np.asarray(everything[i]["center"], float)[:2], float(everything[i]["r"])) for i in rounds]
    tol = 1e-6 * max([float(np.abs(c).max()) + r for _, _, c, r in places] + [1e-300])
    out = []
    for p, d in lines:
        for k, (i, kind, c, r) in enumerate(places):
            m = p + 2 * ((c - p) @ d) * d - (c - p)
            out += [(i, j) for j, other, e, s in places[k + 1:]
                    if other == kind and abs(s - r) <= tol and np.linalg.norm(m - e) <= tol]
    return out


def _hv(plane, point):
    p = np.asarray(tuple(point), float)
    return np.asarray(plane.ToHV(tuple(p[:3]))) if len(p) == 3 else p[:2]


def _axis_and_line(refs, everything):
    """an angle's axis (H or V) and its line, or None where it is not measured between a line and an axis"""
    axes = [r for r in refs if r in ("H", "V")]
    lines = [r for r in refs if everything.get(r, {}).get("kind") == "Line"]
    return (axes[0], lines[0]) if len(axes) == 1 and len(lines) == 1 else None


def _anchor(g):
    return np.asarray(g["center"] if g["kind"] == "Circle" else g["point"] if g["kind"] == "Point" else g["start"], float)


def _circumcentre(a, b, c):
    d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]))
    if d == 0:
        return None
    sq = lambda p: p @ p
    return np.array([(sq(a) * (b[1] - c[1]) + sq(b) * (c[1] - a[1]) + sq(c) * (a[1] - b[1])) / d,
                     (sq(a) * (c[0] - b[0]) + sq(b) * (a[0] - c[0]) + sq(c) * (b[0] - a[0])) / d])


def _pattern_step(c, everything, location):
    """a pattern's spacing or angle as one dimension between its first two instances, with the number of steps
    ("6× 12"); None for the step of a full circle, which its count implies"""
    pattern, linear, second = c["refs"][0], c["kind"] == "Distance", c["id"].endswith(".spacing2")
    copies = {}
    for i in everything:
        if not i.startswith(pattern + "."):
            continue
        parts = i[len(pattern) + 1:].split(".")
        grid = linear and len(parts) > 2 and parts[0].isdigit() and parts[1].isdigit()
        if grid and int(parts[0 if second else 1]) == 0:
            copies.setdefault(".".join(parts[2:]), {})[int(parts[1 if second else 0])] = i
        elif not grid and not second and len(parts) > 1 and parts[0].isdigit():
            copies.setdefault(".".join(parts[1:]), {})[int(parts[0])] = i
    origin = next((o for o in copies if o in everything and everything[o]["kind"] in ("Circle", "Point", "Line", "Arc")), None)
    if origin is None or 1 not in copies[origin]:
        return None
    steps, value = max(copies[origin]), abs(float(c["value"]))
    d = Dimension(c["id"], c["kind"], value, bool(c.get("reference")), c["state"] != "ok", refs=(pattern,))
    d.count, d.total = steps, steps * value
    p0, p1 = _anchor(everything[origin]), _anchor(everything[copies[origin][1]])
    if linear:
        d.kind, d.p, d.q = "linear", p0, p1
        d.span = float(np.linalg.norm(p1 - p0))
        d.u = _unit(p1 - p0)
        return d if d.span > 0 else None
    if abs((steps + 1) * value - 360.0) < 1e-6 or 2 not in copies[origin]:
        return None
    centre = _circumcentre(p0, p1, _anchor(everything[copies[origin][2]]))
    if centre is None:
        return None
    d.kind, d.c = "angle", centre
    d.a, d.b, d.r = _unit(p0 - centre), _unit(p1 - centre), float(np.linalg.norm(p0 - centre))
    if d.a[0] * d.b[1] - d.a[1] * d.b[0] < 0:
        d.a, d.b = d.b, d.a
    return d


def Dimensions(sketch, everything=None):
    """the sketch's dimension constraints as drawings measure them, in the sketch's plane coordinates"""
    import planex as px

    everything = everything if everything is not None else {g["id"]: g for g in sketch._Drawing()}
    constraints = sketch._Constraints()
    counts = _counts(everything, constraints)
    plane = px.Plane(sketch)
    out = []

    def location(ref):
        g = everything.get(ref)
        if g is not None and g["kind"] == "Point":
            return np.asarray(g["point"], float)
        if g is not None and g["kind"] == "Line":
            return np.asarray(g["start"], float)
        return np.asarray(sketch._Location(ref), float)

    for c in constraints:
        refs, kind, value = c["refs"], c["kind"], c["value"]
        if value is None or c["derived"]:
            continue
        sides = _slot_sides(refs[0], everything) if kind == "Diameter" and len(refs) == 1 else None
        if kind in ("Distance", "Angle") and len(refs) == 1 and everything.get(refs[0], {}).get("kind") == "Pattern":
            step = _pattern_step(c, everything, location)
            if step is not None:
                out.append(step)
            continue
        if not sides and any(everything.get(r, {}).get("kind") in ("Pattern", "Offset") for r in refs):
            continue
        name = None if c["id"].startswith((kind + ".", "Reference.")) else c["id"]
        d = Dimension(c["id"], kind, value, bool(c.get("reference")), c["state"] != "ok", name, refs=tuple(refs))
        g0 = everything.get(refs[0]) if refs else None
        across = None
        if kind == "Distance":
            if len(refs) == 1 and g0 is not None and g0["kind"] == "Line":
                d.kind, d.p, d.q = "linear", np.asarray(g0["start"], float), np.asarray(g0["end"], float)
            elif len(refs) == 2 and refs[1] in ("H", "V"):
                p = location(refs[0])
                q = np.array([p[0], 0.0]) if refs[1] == "H" else np.array([0.0, p[1]])
                d.kind, d.p, d.q = "linear", p, q
            elif len(refs) == 2:
                ga, gb = everything.get(refs[0]), everything.get(refs[1])
                kinds = [None if g is None else g["kind"] for g in (ga, gb)]
                if "Line" in kinds and not set(kinds) & {"Arc", "Circle", "Spline"} and kinds != ["Line", "Line"]:
                    # a point and a line: its distance from the line drawn out past the line's end, square to it
                    k = kinds.index("Line")
                    line, point = (ga, gb)[k], location(refs[1 - k])
                    s0, e0 = np.asarray(line["start"], float), np.asarray(line["end"], float)
                    along = _unit(e0 - s0)
                    foot = s0 + along * (along @ (point - s0))
                    p, q = (foot, point) if k == 0 else (point, foot)
                else:
                    p, q = (_hv(plane, pt) for pt in sketch.Measure(refs[0], to=refs[1])["points"])
                if kinds == ["Line", "Line"]:
                    along = _unit(np.subtract(ga["end"], ga["start"]))
                    if abs(along @ _unit(np.subtract(gb["end"], gb["start"]))) > 1 - 1e-9:
                        # parallel lines: their distance square to them, wherever their ends lie
                        q = p + (q - p) - along * (along @ (q - p))
                d.kind, d.p, d.q = "linear", p, q
                d.lines = tuple((np.asarray(g["start"], float), np.asarray(g["end"], float))
                                if g is not None and g["kind"] == "Line" else None for g in (ga, gb))
            elif len(refs) == 3:
                g2 = everything.get(refs[2])
                along = (np.array([1.0, 0.0]) if refs[2] == "H" else np.array([0.0, 1.0]) if refs[2] == "V"
                         else _unit(np.subtract(g2["end"], g2["start"])))
                d.kind, d.p, d.q = "linear", location(refs[0]), location(refs[1])
                across = np.array([-along[1], along[0]])
                if across @ (d.q - d.p) < 0:
                    across = -across
            else:
                continue
            offset = d.q - d.p
            span = float(across @ offset) if across is not None else float(np.linalg.norm(offset))
            if span == 0:
                continue
            d.u = across if across is not None else offset / span
            d.span = span
        elif sides:
            d.kind, d.p, d.q, d.lines = "linear", *sides
            d.u = _unit(d.q - d.p)
            d.span = float(np.linalg.norm(d.q - d.p))
        elif kind in ("Radius", "Diameter") and g0 is not None and g0["kind"] in ("Circle", "Arc"):
            d.kind, d.element = kind.lower(), refs[0]
            d.c, d.r = np.asarray(g0["center"], float), float(g0["r"])
            d.count = counts.get(refs[0], 1)
            if g0["kind"] == "Arc":
                a, b = np.subtract(g0["start"], g0["center"]), np.subtract(g0["end"], g0["center"])
                if not g0["ccw"]:
                    a, b = b, a
                t0, t1 = math.atan2(a[1], a[0]), math.atan2(b[1], b[0])
                while t1 <= t0:
                    t1 += 2 * math.pi
                d.a = np.array([math.cos((t0 + t1) / 2), math.sin((t0 + t1) / 2)])
                d.sweep = t1 - t0
        elif kind == "Radius" and g0 is not None and g0["kind"] == "Spline" and "fillet" in g0:
            # a curvature fillet's r, on the blend: from its hidden arc's centre to the blend's middle
            path = np.asarray(g0["path"], float)
            mid = path[len(path) // 2]
            d.kind, d.element = "radius", refs[0]
            d.c = np.asarray(g0["fillet"]["center"], float)
            d.r = float(np.linalg.norm(mid - d.c))
            d.a = _unit(mid - d.c)
        elif kind == "Angle" and len(refs) == 1 and g0 is not None and g0["kind"] == "Arc":
            d.kind, d.c = "angle", np.asarray(g0["center"], float)
            a, b = np.subtract(g0["start"], g0["center"]), np.subtract(g0["end"], g0["center"])
            if value < 0:
                a, b = b, a
            d.a, d.b, d.r = _unit(a), _unit(b), float(np.linalg.norm(a))
            d.legs = ((0.0, d.r), (0.0, d.r))
        elif kind == "Angle" and len(refs) == 2 and all(everything.get(r, {}).get("kind") == "Line" for r in refs):
            (p0, p1), (q0, q1) = ((np.asarray(everything[r]["start"], float), np.asarray(everything[r]["end"], float))
                                  for r in refs)
            u, w = p1 - p0, q1 - q0
            den = u[0] * -w[1] - u[1] * -w[0]
            if abs(den) < 1e-12 * np.linalg.norm(u) * np.linalg.norm(w):
                continue
            s = ((q0 - p0)[0] * -w[1] - (q0 - p0)[1] * -w[0]) / den
            d.kind, d.c = "angle", p0 + s * u
            sides = []
            for e0, e1 in ((p0, p1), (q0, q1)):
                near, far = sorted((e0, e1), key=lambda e: np.linalg.norm(e - d.c))
                sides.append((_unit(far - d.c), (float(np.linalg.norm(near - d.c)), float(np.linalg.norm(far - d.c)))))
            (d.a, la), (d.b, lb) = sides
            d.legs = (la, lb)
        elif kind == "Angle" and len(refs) == 2 and (pair := _axis_and_line(refs, everything)):
            # the axis from the line's start is a side without an edge: its extension line is the auxiliary line
            axis, line = pair
            p0, p1 = (np.asarray(everything[line][k], float) for k in ("start", "end"))
            d.kind, d.c = "angle", p0
            d.a, d.b = np.array([1.0, 0.0] if axis == "H" else [0.0, 1.0]), _unit(p1 - p0)
            if abs(d.a[0] * d.b[1] - d.a[1] * d.b[0]) < 1e-9:
                continue
            d.legs = ((0.0, 0.0), (0.0, float(np.linalg.norm(p1 - p0))))
        else:
            continue
        out.append(d)
    return out


def _slot_sides(ref, everything):
    """a slot (an offset with a side left and right, lines or arcs): the middle of its left side, the point across on its
    right side, and the sides as lines for extension lines (None for an arc); None for any other offset"""
    left, right = everything.get(ref + ".left"), everything.get(ref + ".right")
    if everything.get(ref, {}).get("kind") != "Offset" or left is None or right is None or left["kind"] != right["kind"]:
        return None
    if left["kind"] == "Line":
        a, b, c, e = (np.asarray(g[k], float) for g in (left, right) for k in ("start", "end"))
        p, u = (a + b) / 2, _unit(e - c)
        return p, c + u * (u @ (p - c)), ((a, b), (c, e))
    if left["kind"] != "Arc":
        return None
    o = np.asarray(left["center"], float)
    s, t = np.subtract(left["start"], o), np.subtract(left["end"], o)
    if not left["ccw"]:
        s, t = t, s
    t0, t1 = math.atan2(s[1], s[0]), math.atan2(t[1], t[0])
    while t1 <= t0:
        t1 += 2 * math.pi
    w = np.array([math.cos((t0 + t1) / 2), math.sin((t0 + t1) / 2)])
    return o + left["r"] * w, o + right["r"] * w, (None, None)


def _ccw(a, b):
    return math.degrees(math.atan2(a[0] * b[1] - a[1] * b[0], a @ b)) % 360


class Pen:
    """strokes in the sketch's coordinates with paper sizes in mm: mm is the sketch's units per mm on paper; outline
    the part's polylines, to tell where an extension line leaves it; width(text) the width of a value in mm"""

    def __init__(self, mm, outline, width):
        self.per_mm, self.width_mm = mm, width
        self.segments = [(np.asarray(a, float), np.asarray(b, float)) for pts in outline for a, b in zip(pts[:-1], pts[1:])]
        self.out = None

    def mm(self, mm):
        return mm * self.per_mm

    def width(self, text):
        return self.mm(self.width_mm(text))

    def line(self, a, b, style="thin"):
        self.out.lines.append((np.asarray(a, float), np.asarray(b, float), style))

    def arrow(self, tip, direction):
        self.out.arrows.append((np.asarray(tip, float), _unit(direction)))

    def along(self, a, b, text):
        """the value above the line a-b, along it, at its middle"""
        a, b = np.asarray(a, float), np.asarray(b, float)
        angle = _upright(_unit(b - a))
        up = np.array([-math.sin(math.radians(angle)), math.cos(math.radians(angle))])
        self.out.texts.append(((a + b) / 2 + up * self.mm(1.0), angle, text))

    def exit(self, a, b):
        """the share of a to b after which the segment is outside the part: its last crossing of the outline"""
        r, last = b - a, 0.0
        for p, q in self.segments:
            s = q - p
            den = r[0] * s[1] - r[1] * s[0]
            if abs(den) < 1e-12:
                continue
            t = ((p[0] - a[0]) * s[1] - (p[1] - a[1]) * s[0]) / den
            v = ((p[0] - a[0]) * r[1] - (p[1] - a[1]) * r[0]) / den
            if 1e-9 < t < 1 and -1e-9 <= v <= 1 + 1e-9:
                last = max(last, t)
        return last

    def on_outline(self, p):
        tol = self.mm(0.1)
        for a, b in self.segments:
            s = b - a
            t = np.clip((p - a) @ s / max(s @ s, 1e-300), 0, 1)
            if np.linalg.norm(a + t * s - p) < tol:
                return True
        return False

    def extension(self, start, foot):
        """from start to the dimension line at foot and OVER past it: from a center dash-dot while it runs inside the
        part, narrow continuous outside; from an edge point narrow continuous all the way"""
        start, foot = np.asarray(start, float), np.asarray(foot, float)
        if np.linalg.norm(foot - start) < 1e-12:
            return
        out = start + (foot - start) * (0.0 if self.on_outline(start) else self.exit(start, foot))
        if np.linalg.norm(out - start) > 1e-12:
            self.line(start, out, "center")
        self.line(out, foot + _unit(foot - start) * self.mm(OVER))


def Beyond(pen, span, width):
    """whether a linear dimension's value, width long, is wider than the span between its extension lines"""
    return span < width + pen.mm(1)


def Linear(pen, p, q, n, level, text, u):
    """p to q measured along u, the dimension line where n . x = level: the value above its middle, the arrows from
    outside where they leave it no room; a value wider than the space between the extension lines beyond the arrow on
    its reading side (right, up)"""
    P, Q = p + (level - n @ p) * n, q + (level - n @ q) * n
    pen.extension(p, P)
    pen.extension(q, Q)
    span = float(np.linalg.norm(Q - P))
    along = (Q - P) / span if span > 0 else u
    width = pen.width(text)
    if span > 2.5 * pen.mm(ARROW) + width:
        pen.line(P, Q)
        pen.arrow(P, -along)
        pen.arrow(Q, along)
        pen.along(P, Q, text)
    elif not Beyond(pen, span, width):
        out = 2 * pen.mm(ARROW)
        pen.line(P - along * out, Q + along * out)
        pen.arrow(P, along)
        pen.arrow(Q, -along)
        pen.along(P, Q, text)
    else:
        t = math.radians(_upright(along))
        r = np.array([math.cos(t), math.sin(t)])
        near, far = (P, Q) if r @ (Q - P) >= 0 else (Q, P)
        start = far + r * pen.mm(ARROW + 1)
        pen.line(near - r * 2 * pen.mm(ARROW), start + r * width)
        pen.arrow(near, r)
        pen.arrow(far, -r)
        pen.along(start, start + r * width, text)


def Radius(pen, c, r, d, text, outside=False, inside=False, from_center=False):
    """R and the value along a dimension line in direction d from the center c, one arrow at the arc"""
    tip = c + d * r
    room = pen.width(text) + pen.mm(ARROW + 3)
    if outside or (not inside and r < room):
        end = tip + d * room
        pen.line(tip, end)
        pen.arrow(tip, -d)
        pen.along(tip + d * pen.mm(ARROW), end, text)
    else:
        start = c if from_center and r >= room else tip - d * room
        pen.line(start, tip)
        pen.arrow(tip, d)
        end = tip - d * pen.mm(ARROW)
        start = start + (end - start) * 0.35
        # a value long for its stretch ends a millimeter before the arrow
        if (start + end) @ d / 2 + pen.width(text) / 2 > end @ d - pen.mm(1):
            end = end - d * pen.mm(1)
            start = end - d * pen.width(text)
        pen.along(start, end, text)


def Fits(pen, r, text):
    """whether a diameter's value and arrows fit inside the circle"""
    return 2 * r >= pen.width(text) + pen.mm(2 * ARROW + 2)


def Diameter(pen, c, r, d, text, share=0.55):
    """the value along a dimension line through the center, inside the circle, its middle share of the radius out"""
    pen.line(c - d * r, c + d * r)
    pen.arrow(c + d * r, d)
    pen.arrow(c - d * r, -d)
    # a value long for the circle ends a millimeter before the arrow
    middle = c + d * min(r * share, r - pen.mm(ARROW + 1) - pen.width(text) / 2)
    half = d * pen.width(text) / 2
    pen.along(middle - half, middle + half, text)


def _leader(pen, c, r, d, text, beyond):
    """a leader's tip on the circle, its bend beyond mm out from the circle toward d, the end of its reference line, and
    the far end of a diameter's line past the circle's other side"""
    tip = c + d * r
    bend = tip + d * pen.mm(beyond)
    end = bend + np.array([math.copysign(pen.width(text) + pen.mm(2), d[0] if abs(d[0]) > 1e-9 else 1.0), 0.0])
    return tip, bend, end, c - d * (r + pen.mm(2 * ARROW))


def Leader(pen, c, r, d, text, beyond=LEADER, through=False):
    """a value on a leader out toward d, bent to a short horizontal reference line with the value above it: through, a
    small diameter's dimension line through the center with both arrows on the circle from outside (DIN); else a
    radius's one arrow"""
    tip, bend, end, back = _leader(pen, c, r, d, text, beyond)
    pen.line(back if through else tip, bend)
    pen.line(bend, end)
    pen.arrow(tip, -d)
    if through:
        pen.arrow(c - d * r, d)
    pen.along(*sorted((bend, end), key=lambda p: p[0]), text)


def Head(tip, direction, length):
    """a closed arrowhead at tip pointing along direction: length long, 15 degrees wide"""
    d = _unit(direction)
    base, half = np.asarray(tip, float) - d * length, np.array([-d[1], d[0]]) * length * math.tan(math.radians(7.5))
    return np.array([np.asarray(tip, float), base + half, base - half])


def _off_segment(p, a, b):
    """the distance of the point p from the segment a b"""
    ab = b - a
    t = 0.0 if not ab @ ab > 0 else min(max((p - a) @ ab / (ab @ ab), 0.0), 1.0)
    return float(np.linalg.norm(p - a - t * ab))


def _box(anchor, angle, width, height):
    """a text's box: its edges and diagonals as segments, the baseline centered at anchor and turned by angle"""
    t = math.radians(angle)
    u, up = np.array([math.cos(t), math.sin(t)]) * width, np.array([-math.sin(t), math.cos(t)]) * height
    a = np.asarray(anchor, float) - u / 2
    k = (a, a + u, a + u + up, a + up)
    return [(k[0], k[1]), (k[1], k[2]), (k[2], k[3]), (k[3], k[0]), (k[0], k[2]), (k[1], k[3])]


def _ends(segments):
    if not segments:
        return np.zeros((0, 2)), np.zeros((0, 2))
    return np.array([s[0] for s in segments], float), np.array([s[1] for s in segments], float)


def _hits(a, b, P, Q):
    """how many of the segments P-Q the segment a-b crosses or touches"""
    if len(P) == 0:
        return 0
    return int(np.count_nonzero(_meets(a, b, P, Q)))


def _meets(a, b, P, Q):
    """whether the segment a-b crosses or touches each of the segments P-Q, two on one line only where they overlap;
    a and b may be the ends of n segments, shaped (n, 1, 2) against P and Q shaped (1, m, 2)"""

    def side(p, q, r):
        return (q[..., 0] - p[..., 0]) * (r[..., 1] - p[..., 1]) - (q[..., 1] - p[..., 1]) * (r[..., 0] - p[..., 0])

    def dot(u, v):
        return u[..., 0] * v[..., 0] + u[..., 1] * v[..., 1]

    s, t = side(P, Q, a), side(P, Q, b)
    out = (s * t <= 0) & (side(a, b, P) * side(a, b, Q) <= 0)
    ab, pq = b - a, Q - P
    # on one line: both ends of a-b off the line P-Q by at most 1e-9 of a-b's length
    hair = 1e-9 * np.sqrt(dot(ab, ab) * dot(pq, pq))
    on = (np.abs(s) <= hair) & (np.abs(t) <= hair) & (hair > 0)
    if np.any(on):
        n = np.maximum(dot(ab, ab), 1e-300)
        u, v = dot(P - a, ab) / n, dot(Q - a, ab) / n
        out = np.where(on, np.maximum(np.minimum(u, v), 0.0) <= np.minimum(np.maximum(u, v), 1.0), out)
    return out


def _crossed(segments, ends):
    """how often the segments cross the segments ends = (P, Q)"""
    P, Q = ends
    if not len(segments) or not len(P):
        return 0
    A, B = _ends(segments)
    return _hits(A[:, None], B[:, None], P[None], Q[None])


def _struck(segments, ends, of):
    """how many things the segments cross: of numbers the thing each of the segments ends = (P, Q) belongs to"""
    P, Q = ends
    if not len(segments) or not len(P):
        return 0
    A, B = _ends(segments)
    return len(np.unique(of[_meets(A[:, None], B[:, None], P[None], Q[None]).any(axis=0)]))


@dataclass
class _Drawn:
    """what a layout has drawn so far, as the ends of segments: the lines (arcs as their segments) but the center lines
    with the number of the strokes each belongs to, the center lines (which get a gap at a value), the lines not through
    a centre, the values' boxes with the number of their value"""
    lines: tuple
    line_of: Any
    centre: tuple
    free: tuple
    boxes: tuple
    box_of: Any


def _gaps(a, b, boxes):
    """the stretches of the segment a-b inside the boxes, as shares of it; boxes (anchors, n, h): box i holds the
    points x with n[i, j] . (x - anchors[i]) <= h[i, j] for every j"""
    anchors, n, h = boxes
    num, den = h - np.einsum("ijk,ik->ij", n, a - anchors), n @ (b - a)
    with np.errstate(divide="ignore", invalid="ignore"):
        t = num / den
    t0 = np.max(np.where(den < 0, t, 0.0), axis=1)
    t1 = np.min(np.where(den > 0, t, 1.0), axis=1)
    inside = (t0 < t1) & ~np.any((den == 0) & (num < 0), axis=1)
    return sorted(zip(t0[inside], t1[inside]))


def _kept(gaps):
    """the stretches between the gaps, as shares of a segment"""
    out, start = [], 0.0
    for t0, t1 in gaps:
        if t0 > start:
            out.append((start, t0))
        start = max(start, t1)
    if start < 1.0:
        out.append((start, 1.0))
    return out


def _cut(points, boxes):
    """a polyline without its stretches inside the boxes: the polylines left"""
    out, current = [], [points[0]]
    for a, b in zip(points[:-1], points[1:]):
        kept = _kept(_gaps(a, b, boxes))
        for t0, t1 in kept:
            if t0 > 0 or not current:
                if len(current) > 1:
                    out.append(np.array(current))
                current = [a + (b - a) * t0]
            current.append(a + (b - a) * t1)
        if not kept or kept[-1][1] < 1.0:
            if len(current) > 1:
                out.append(np.array(current))
            current = []
    if len(current) > 1:
        out.append(np.array(current))
    return out


def _room(pen, text):
    """the length of dimension line a value needs between two arrows"""
    return pen.width(text) + pen.mm(2 * ARROW + 2)


def Angular(pen, c, a, b, legs, radius, text, beyond=0):
    """the angle at c from direction a counterclockwise to b: an arc of radius around c with an arrow at each end, the
    value along the arc at its middle, or beyond the second arrow with both arrows from outside where the arc is too
    short for it; beyond 1 or -1: the value beyond the second or the first arrow, the arrows from outside; a value on the
    arc's inner side as far in as the arc bends away under its ends; legs (near, far): the stretch of each side's edge
    from c, an extension line from its nearer end where the arc misses it"""
    t0, t1 = math.atan2(a[1], a[0]), math.atan2(b[1], b[0])
    while t1 <= t0:
        t1 += 2 * math.pi
    for t, (near, far) in ((t0, legs[0]), (t1, legs[1])):
        d = np.array([math.cos(t), math.sin(t)])
        if radius > far:
            pen.line(c + d * far, c + d * (radius + pen.mm(OVER)))
        elif radius < near:
            pen.line(c + d * near, c + d * (radius - pen.mm(OVER)))
    width = pen.width(text)
    if beyond == 0 and radius * (t1 - t0) >= _room(pen, text):
        start, end, middle = t0, t1, (t0 + t1) / 2
        inward = -1.0
    else:
        reach, short = (pen.mm(ARROW + 2) + width) / radius, pen.mm(2 * ARROW) / radius
        shift = (pen.mm(ARROW + 1) + width / 2) / radius
        if beyond < 0:
            start, end, middle = t0 - reach, t1 + short, t0 - shift
        else:
            start, end, middle = t0 - short, t1 + reach, t1 + shift
        inward = 1.0
    t = np.linspace(start, end, 97)
    pen.out.arcs.append(c + radius * np.c_[np.cos(t), np.sin(t)])
    for s, sign in ((t0, inward), (t1, -inward)):
        pen.arrow(c + radius * np.array([math.cos(s), math.sin(s)]), sign * np.array([-math.sin(s), math.cos(s)]))
    out = np.array([math.cos(middle), math.sin(middle)])
    at = c + radius * out
    half = np.array([-out[1], out[0]]) * width / 2
    up = math.radians(_upright(half))
    if out @ np.array([-math.sin(up), math.cos(up)]) < 0:
        at = at - out * (radius - math.sqrt(max(radius * radius - width * width / 4, 0.0)))
    pen.along(at - half, at + half, text)


def Text(d, place, names, measured=None, fmt=Fmt):
    """what a dimension writes: its value as fmt writes it (a decimal comma), R or a diameter sign (a thread's size such
    as "M5" in place of the sign), a chamfer's leg × 45°, n x for equal features, a suffix, a reference dimension in
    parentheses, the id in front where names is True and the id is not automatic; measured: written in place of the
    value with no R or sign (a CAD dimension entity's "<>", whose entity adds them)"""
    p = place or Place()
    sign = d.kind in ("radius", "diameter")
    if p.text is not None:
        value, sign = p.text, sign and p.text[:1] != "M"
    elif measured is not None:
        value, sign = measured, False
    elif d.kind == "angle":
        value = fmt(180 - abs(d.value) if p.supplement else abs(d.value)) + "°"
    elif d.kind == "chamfer":
        value = fmt(d.value) + (f" × {fmt(d.r)}" if d.r else " × 45°")
    else:
        value = fmt(d.value)
    if sign:
        value = ("⌀" if d.kind == "diameter" else "R") + value
    elif p.text is None:
        value = d.prefix + value
    count = p.count if p.count is not None else d.count
    text = (f"{count}× " if count > 1 else "") + value + (f" {p.suffix}" if p.suffix else "")
    if names and d.name:
        text = f"{d.name} = {text}"
    return f"({text})" if d.reference else text


def Ordered(found):
    """the dimensions in the order a layout draws them: the linear ones first, shorter ones first (the rows fill from the
    part outward), then the angles, smaller ones first (the arcs at one vertex stack outward), then the radii and
    diameters, which have the most places to go"""
    return sorted(found, key=lambda d: (d.kind != "linear", d.kind != "angle",
                                        d.span if d.kind == "linear" else abs(d.value) if d.kind == "angle" else 0.0))


class Layout:
    """the dimension lines' places: given ones from a drawing, the others in rows outside the part (horizontal ones
    below or above, vertical ones left or right, shorter nearer the part, no two on one stretch of a row), inclined
    ones off their measured points away from the part, radii and diameters away from the part's middle, the angles at
    one vertex on arcs a row apart; where there is a choice, the place whose value nothing crosses and whose lines cross
    no value drawn before"""

    def __init__(self, outline, mm, width, places=None, names=False, fmt=Fmt, row=ROW):
        pts = [np.asarray(p, float) for line in outline for p in line]
        self.pen = Pen(mm, outline, width)
        self.edges = _ends(self.pen.segments)
        self.places, self.names, self.fmt = places or {}, names, fmt
        # rows this far apart on the paper (mm): further where the lettering is drawn taller than HEIGHT
        self.row = row
        if pts:
            pts = np.array(pts)
            self.lo, self.hi = pts.min(axis=0), pts.max(axis=0)
        else:
            self.lo = self.hi = np.zeros(2)
        self.middle = (self.lo + self.hi) / 2
        self.rows = {}
        self.drawn = []
        # the radius and diameter lines drawn at each centre: (centre, direction)
        self.spokes = []
        # the angles' arcs drawn: (vertex, radius)
        self.arcs = []

    def _row(self, side, lo, hi):
        """the first row of side whose stretches leave [lo, hi] free"""
        rows = self.rows.setdefault(side, [])
        for k, taken in enumerate(rows):
            if all(hi <= a or lo >= b for a, b in taken):
                taken.append((lo, hi))
                return k
        rows.append([(lo, hi)])
        return len(rows) - 1

    def _level(self, d, place):
        """the normal n of the dimension line, its level n . x and, for an inclined one, the sign of n away from the
        part (0 for the others)"""
        pen, u = self.pen, d.u
        if abs(u[1]) < 1e-9:
            n = np.array([0.0, 1.0])
        elif abs(u[0]) < 1e-9:
            n = np.array([1.0, 0.0])
        else:
            n = np.array([-u[1], u[0]])
            if n[0] < 0:
                n = -n
        levels = (n @ d.p, n @ d.q)
        if place is not None and place.offset is not None:
            return n, self._offset(n, levels, place.offset), 0
        if abs(u[1]) < 1e-9 or abs(u[0]) < 1e-9:
            axis = 1 if abs(u[1]) < 1e-9 else 0
            low = min(levels) - self.lo[axis] <= self.hi[axis] - max(levels)
            side = ("below" if low else "above") if axis == 1 else ("left" if low else "right")
            along = 1 - axis
            text = pen.width(Text(d, place, self.names, fmt=self.fmt))
            a, b = sorted((d.p[along], d.q[along]))
            m = (a + b) / 2
            lo, hi = min(a, m - text / 2) - pen.mm(ARROW), max(b, m + text / 2) + pen.mm(ARROW)
            if Beyond(pen, b - a, text):
                # the value beyond the upper end's arrow: right or up, as Linear reads it
                lo, hi = a - pen.mm(3 * ARROW), b + pen.mm(2 * ARROW + 1) + text
            k = self._row(side, lo, hi)
            reach = pen.mm(FIRST + self.row * k)
            return n, (self.lo[axis] - reach) if low else (self.hi[axis] + reach), 0
        away = n if n @ ((d.p + d.q) / 2 - self.middle) >= 0 else -n
        sign = 1 if away @ n > 0 else -1
        return n, (max(away @ d.p, away @ d.q) + pen.mm(FIRST)) * sign, sign

    def _offset(self, n, levels, offset):
        """the level of a dimension line offset from the measured points' levels along n: + away from the part's middle,
        - towards it; measured points on both sides of the middle along the drawing's axes (+ up, + right)"""
        sides = [level - n @ self.middle for level in levels]
        away = 1.0 if min(sides) >= 0 and max(sides) > 0 else -1.0 if max(sides) <= 0 and min(sides) < 0 else 0.0
        if away == 0.0:
            return (max(levels) if offset >= 0 else min(levels)) + offset
        return max(levels) + offset if away > 0 else min(levels) - offset

    def Draw(self, d):
        """the strokes of one dimension"""
        pen = self.pen
        place = self.places.get(d.id)
        pen.out = Strokes(d.id, d.over)
        text = Text(d, place, self.names, fmt=self.fmt)
        entity = dict(kind=d.kind, text=Text(d, place, self.names, "<>", self.fmt))
        if d.kind == "linear":
            n, level, away = self._level(d, place)
            if away:
                level = self._inclined(d, n, level, away, text)
            p, q = self._starts(d, n, level)
            Linear(pen, p, q, n, level, text, d.u)
            entity.update(p1=p, p2=q, base=p + (level - n @ p) * n, angle=_upright((-n[1], n[0])))
        elif d.kind in ("radius", "diameter"):
            given = place.direction if place is not None else None
            u = Direction(given) if given is not None else self._direction(d)
            if place is not None and place.across is not None and d.kind == "diameter":
                p, q = d.c - u * d.r, d.c + u * d.r
                n = np.array([-u[1], u[0]])
                if n[0] < 0 or (n[0] == 0 and n[1] < 0):
                    n = -n
                level = self._offset(n, (n @ p, n @ q), place.across)
                Linear(pen, p, q, n, level, text, u)
                entity.update(kind="linear", text=Text(d, place, self.names, "⌀<>", self.fmt), p1=p, p2=q,
                              base=p + (level - n @ p) * n, angle=_upright((-n[1], n[0])))
            elif (place is not None and place.leader) or (d.kind == "diameter" and not Fits(pen, d.r, text)):
                beyond, through = LEADER, d.kind == "diameter"
                if given is None:
                    u, beyond, _ = self._leader(d, text, u, through)
                Leader(pen, d.c, d.r, u, text, beyond, through)
                entity.update(c=d.c, r=d.r, angle=math.degrees(math.atan2(u[1], u[0])), location=pen.out.texts[-1][0])
            else:
                def draw(u, inside=False, outside=False, share=0.55):
                    if d.kind == "diameter":
                        Diameter(pen, d.c, d.r, u, text, share)
                    else:
                        Radius(pen, d.c, d.r, u, text, outside or (place is not None and place.outside),
                               inside or (place is not None and place.inside))

                inside = outside = False
                share, beyond = 0.55, None
                if given is None and d.a is None:
                    u, share, clash = self._free(d, draw, u)
                    if clash[:2] != (0, 0):
                        # every place in the circle crosses a value: a leader where that crosses fewer
                        v, out, crossed = self._leader(d, text, u, d.kind == "diameter")
                        if crossed < clash:
                            u, beyond = v, out
                elif given is None and d.kind == "radius" and place is None:
                    u, inside, beyond = self._arc_radius(d, text, u)
                    outside = not inside
                if beyond is None:
                    draw(u, inside, outside, share)
                    entity.update(c=d.c, r=d.r, angle=math.degrees(math.atan2(u[1], u[0])), location=None)
                else:
                    Leader(pen, d.c, d.r, u, text, beyond, d.kind == "diameter")
                    entity.update(c=d.c, r=d.r, angle=math.degrees(math.atan2(u[1], u[0])), location=pen.out.texts[-1][0])
            self.spokes.append((d.c, u))
        elif d.kind == "chamfer":
            # a 45 degree chamfer's callout on a leader from its middle, out of the part
            if place is not None and place.direction is not None:
                u, beyond = Direction(place.direction), LEADER
            else:
                start = math.degrees(math.atan2(d.a[1], d.a[0]))
                u, beyond, _ = self._leader(d, text, d.a, False, [start + k for k in (0, -15, 15, -30, 30, -45, 45)])
            Leader(pen, d.c, 0.0, u, text, beyond)
            entity = None
        else:
            a, b, legs = d.a, d.b, d.legs
            shown = abs(d.value)
            if place is not None and place.supplement:
                shown = 180 - shown
            # an extension beyond the vertex has no edge: its stretch (0, 0) gets an extension line from the vertex
            none = (0.0, 0.0)
            for a_, b_, legs_ in ((a, b, legs), (b, a, legs[::-1]), (a, -b, (legs[0], none)), (-b, a, (none, legs[0])),
                                  (-a, b, (none, legs[1])), (b, -a, (legs[1], none))):
                if abs(_ccw(a_, b_) - shown) < 0.5:
                    a, b, legs = a_, b_, legs_
                    break
            beyond = 0
            if place is not None and place.radius is not None:
                radius = place.radius
            elif d.total is not None:
                radius, beyond = self._angle(d, a, b, legs, text, d.r + pen.mm(self.row), free=True)
            else:
                # the arc long enough for its value, but never further out than just past the shorter leg's end, nor,
                # where an edge starts at the vertex, than three rows on the paper (a vertex off the part, a taper's,
                # keeps its arc at the edges): past the longer one it reaches across the rest of the part, and a small
                # angle, a long value or legs short on the paper get their value beyond the arrows there; a row further
                # out than an angle drawn before at the same vertex
                fars = [far for _, far in d.legs if far > 0]
                base = max(pen.mm(FIRST * 1.5), 0.5 * min(fars) if fars else 0.0)
                radius = max(base, _room(pen, text) / math.radians(_ccw(a, b)))
                # an edge from the vertex: its nearer end within 0.1 mm of it on the paper
                if min(near for near, _ in d.legs) <= pen.mm(0.1):
                    radius = min(radius, pen.mm(3 * self.row))
                if fars:
                    radius = min(radius, min(fars) + pen.mm(OVER))
                for c, r in self.arcs:
                    # the same vertex on the paper: 0.1 mm
                    if np.linalg.norm(c - d.c) <= pen.mm(0.1):
                        radius = max(radius, r + pen.mm(self.row))
                radius, beyond = self._angle(d, a, b, legs, text, radius)
            self.arcs.append((d.c, radius))
            Angular(pen, d.c, a, b, legs, radius, text, beyond)
            middle = math.atan2(a[1], a[0]) + math.radians(_ccw(a, b)) / 2
            entity.update(c=d.c, a=a, b=b, legs=legs, radius=radius,
                          base=d.c + radius * np.array([math.cos(middle), math.sin(middle)]))
        pen.out.entity = entity
        self.drawn.append(pen.out)
        return pen.out

    def _direction(self, d):
        """a radius's or diameter's line: an arc's middle, else the diagonal away from the part's middle"""
        if d.a is not None:
            return d.a
        v = d.c - self.middle
        angle = math.atan2(v[1], v[0]) if np.linalg.norm(v) > self.pen.mm(1) else math.radians(45)
        return Direction(45 + 90 * math.floor(math.degrees(angle) / 90))

    def _obstacles(self, centre=None):
        """what is drawn so far (_Drawn); with centre, its free lines leave out the lines through it (its own and a
        concentric circle's center lines, which every radius or diameter line there crosses)"""
        pen, lines, line_of, centres, free, boxes, box_of = self.pen, [], [], [], [], [], []
        for k, s in enumerate(self.drawn):
            pieces = [(a, b) for a, b, _ in s.lines]
            free += [(a, b) for a, b in pieces if centre is None or _off_segment(centre, a, b) > pen.mm(0.1)]
            arcs = [(a, b) for arc in s.arcs for a, b in zip(arc[:-1], arc[1:])]
            free += arcs
            if s.id == "center lines":
                centres += pieces + arcs
            else:
                lines += pieces + arcs
                line_of += [k] * (len(pieces) + len(arcs))
            for anchor, angle, text in s.texts:
                box = _box(anchor, angle, pen.width(text), pen.mm(s.height))
                box_of += [len(box_of) // 6] * 6
                boxes += box
        return _Drawn(_ends(lines), np.array(line_of, int), _ends(centres), _ends(free), _ends(boxes),
                      np.array(box_of, int))

    def _trial(self, d, draw, *args):
        """the strokes draw(*args) makes, drawn aside"""
        kept, self.pen.out = self.pen.out, Strokes(d.id)
        draw(*args)
        trial, self.pen.out = self.pen.out, kept
        return trial

    def _pieces(self, trial):
        """a trial's lines and arcs, and its values' boxes, as segments"""
        pen = self.pen
        lines = [(a, b) for a, b, _ in trial.lines] + [(a, b) for arc in trial.arcs for a, b in zip(arc[:-1], arc[1:])]
        boxes = [seg for anchor, a, t in trial.texts for seg in _box(anchor, a, pen.width(t), pen.mm(trial.height))]
        return lines, boxes

    def _clash(self, lines, boxes, drawn):
        """what a dimension's lines and value boxes run into, the worst first: the values drawn before its values lie
        on, then the things crossing its values (the strokes drawn before, the part's edges) and the values drawn before
        its lines cross, then how often center lines cross its values (Interrupt gives them a gap) and its lines cross
        the lines drawn before (drawn.free), then how often its lines cross the part's edges"""
        crossed = _struck(boxes, drawn.lines, drawn.line_of) + (_crossed(boxes, self.edges) > 0)
        return (_struck(boxes, drawn.boxes, drawn.box_of), crossed + _struck(lines, drawn.boxes, drawn.box_of),
                _crossed(boxes, drawn.centre) + _crossed(lines, drawn.free), _crossed(lines, self.edges))

    def _free(self, d, draw, first):
        """a circle's radius or diameter line, a diameter's value's place along it (Diameter's share) and what it runs
        into (_clash): the first direction, else the next diagonal, 30 or 60 degrees off, not along a concentric
        circle's line drawn before, the value at the middle of the radius, further out, further in: the first that runs
        into nothing, else the one that runs into the least"""
        pen = self.pen
        drawn = self._obstacles(d.c)
        # a concentric circle's line already drawn: not along it, its value would lie on the other's
        taken = [t for c, t in self.spokes if np.linalg.norm(c - d.c) <= pen.mm(0.1)]
        if not any(len(ends[0]) for ends in (drawn.lines, drawn.centre, drawn.boxes, self.edges)) and not taken:
            return first, 0.55, (0, 0, 0, 0)
        start, best = math.degrees(math.atan2(first[1], first[0])), None
        for angle in (start, start + 90, start - 90, start + 180, start + 30, start - 30, start + 60, start - 60):
            u = Direction(angle)
            if any(abs(u[0] * t[1] - u[1] * t[0]) < 0.17 for t in taken):
                continue
            for share in (0.55, 0.75, 0.35) if d.kind == "diameter" else (0.55,):
                score = self._clash(*self._pieces(self._trial(d, draw, u, False, False, share)), drawn)
                if not any(score):
                    return u, share, score
                if best is None or score < best[0]:
                    best = (score, u, share)
        return (first, 0.55, (0, 0, 0, 0)) if best is None else (best[1], best[2], best[0])

    def _arc_radius(self, d, text, middle):
        """an arc's radius line, the side of its value and None, or a leader's direction and how far out it bends: through
        its middle, else 30, 60, ... 150 degrees from it while on the arc, not along a concentric one's line drawn
        before; inside where it fits between the center and the arc, else outside; of these the one that runs into the
        least (_clash), then the nearest the middle; where every one crosses a value, a leader from the arc (_leader)
        if that crosses fewer"""
        pen = self.pen
        drawn = self._obstacles(d.c)
        taken = [t for c, t in self.spokes if np.linalg.norm(c - d.c) <= pen.mm(0.1)]
        fits = d.r >= pen.width(text) + pen.mm(ARROW + 3)
        start, best, on = math.degrees(math.atan2(middle[1], middle[0])), None, []
        for turn in (0, 15, -15, 30, -30, 45, -45, 60, -60, 75, -75, 90, -90, 105, -105, 120, -120, 135, -135, 150, -150):
            if turn and math.radians(abs(turn) + 5) > d.sweep / 2:
                continue
            u = Direction(start + turn)
            if any(abs(u[0] * t[1] - u[1] * t[0]) < 0.17 for t in taken):
                continue
            on.append(start + turn)
            if turn % 30:
                continue
            for inside in (True, False) if fits else (False, True):
                trial = self._trial(d, Radius, pen, d.c, d.r, u, text, not inside, inside)
                score = (*self._clash(*self._pieces(trial), drawn), abs(turn))
                if best is None or score < best[0]:
                    best = (score, u, inside)
        if best is None:
            return middle, fits, None
        if best[0][:2] != (0, 0) and on:
            u, beyond, clash = self._leader(d, text, middle, False, on)
            if clash < best[0][:4]:
                return u, False, beyond
        return best[1], best[2], None

    def _leader(self, d, text, first, through, angles=None):
        """a leader's direction, how far out from the circle it bends and what it runs into (_clash): the one that runs
        into the least, then the shortest; the diagonal away from the part's middle first, the other diagonals, then
        30 and 60 degrees from h; angles: the directions to try instead, in degrees"""
        pen = self.pen
        if angles is None:
            start = math.degrees(math.atan2(first[1], first[0]))
            angles = [start + k for k in (0, 90, -90, 180)]
            angles += [a + s for a in angles for s in (-15, 15)]
        drawn = self._obstacles(d.c)
        best = None
        for k in range(1 + max((len(rows) for rows in self.rows.values()), default=0) + 1):
            beyond = LEADER + self.row * k
            for rank, angle in enumerate(angles):
                u = Direction(angle)
                tip, bend, end, back = _leader(pen, d.c, d.r, u, text, beyond)
                left, right = sorted((bend, end), key=lambda p: p[0])
                box = _box((left + right) / 2 + np.array([0.0, pen.mm(1)]), 0.0, pen.width(text), pen.mm(HEIGHT))
                # the pieces outside the circle; the line inside it meets the circle's own centre lines anyway
                pieces = [(tip + u * pen.mm(0.3), bend), (bend, end)]
                if through:
                    pieces.append((d.c - u * (d.r + pen.mm(0.3)), back))
                clash = self._clash(pieces, box, drawn)
                if not any(clash):
                    return u, beyond, clash
                if best is None or (*clash, k, rank) < best[0]:
                    best = ((*clash, k, rank), u, beyond)
        return best[1], best[2], best[0][:4]

    def _inclined(self, d, n, level, away, text):
        """an inclined dimension line's level: the first of level and a row and two rows further away (sign away along
        n) where no value is crossed (_clash), else the one crossing the fewest"""
        drawn, best = self._obstacles(), None
        for k in range(3):
            at = level + away * self.pen.mm(self.row * k)
            p, q = self._starts(d, n, at)
            crossed = self._clash(*self._pieces(self._trial(d, Linear, self.pen, p, q, n, at, text, d.u)), drawn)[:2]
            if crossed == (0, 0):
                return at
            if best is None or crossed < best[0]:
                best = (crossed, at)
        return best[1]

    def _angle(self, d, a, b, legs, text, radius, free=False):
        """an angle's arc radius and where its value goes (Angular's beyond): at radius the value where Angular puts it,
        beyond the second arrow, beyond the first, then the same a row and two rows further out; the first where no
        value is crossed (_clash), else the one crossing the fewest; free (an arc tied to no edge, a pattern's step):
        three rows further out too, the one that runs into the least, its lines along the part's edges too"""
        drawn, best = self._obstacles(), None
        for k in range(4 if free else 3):
            for beyond in (0, 1, -1):
                r = radius + self.pen.mm(self.row * k)
                trial = self._trial(d, Angular, self.pen, d.c, a, b, legs, r, text, beyond)
                clash = self._clash(*self._pieces(trial), drawn)
                crossed = clash if free else clash[:2]
                if not any(crossed):
                    return r, beyond
                if best is None or crossed < best[0]:
                    best = (crossed, r, beyond)
        return best[1], best[2]

    def Interrupt(self, centres):
        """a gap in the center lines (the strokes centres) where a value drawn lies on one, half a millimeter clear of
        the value"""
        pen, anchors, normals, reach = self.pen, [], [], []
        gap = pen.mm(0.5)
        for s in self.drawn:
            if s is centres:
                continue
            for anchor, angle, text in s.texts:
                t = math.radians(angle)
                u, up = np.array([math.cos(t), math.sin(t)]), np.array([-math.sin(t), math.cos(t)])
                half = pen.width(text) / 2 + gap
                anchors.append(np.asarray(anchor, float))
                normals.append((u, -u, up, -up))
                reach.append((half, half, pen.mm(s.height) + gap, gap))
        if not anchors:
            return
        boxes = (np.array(anchors), np.array(normals), np.array(reach))
        lines = []
        for a, b, style in centres.lines:
            lines += [(a + (b - a) * t0, a + (b - a) * t1, style) for t0, t1 in _kept(_gaps(a, b, boxes))]
        centres.lines = lines
        centres.arcs = [piece for arc in centres.arcs for piece in _cut(np.asarray(arc, float), boxes)]

    def _starts(self, d, n, level):
        """where the extension lines start: a line's end nearer the dimension line (its corner), the measured point
        otherwise; a dimension line crossing a measured line ends on it"""
        ends = []
        for line, point in zip(d.lines, (d.p, d.q)):
            if line is None:
                ends.append(point)
                continue
            s, e = line
            if (n @ s - level) * (n @ e - level) <= 0 and abs(n @ (e - s)) > 1e-12:
                point = s + (e - s) * (level - n @ s) / (n @ (e - s))
            else:
                point = min((s, e), key=lambda x: abs(n @ x - level))
            ends.append(point)
        return ends


def CenterLines(everything, dims, mm, outline, centers=None, axes=(), carried=()):
    """the center lines: a cross through every circle drawn (not construction) and every construction circle centers
    names, a short cross at the center of an arc whose radius or diameter is dimensioned, the sketch's axes (not H and
    V) across the part, a circular pattern's pitch circle;
    centers: {id: dict(arms=..., cross=..., off=...)} changes; axes: more (a, b) or (a, b, through); carried: the
    construction elements a drawn dimension measures, drawn as center lines"""
    from ._svg import _path

    centers = centers or {}
    out = Strokes("center lines")
    pts = [np.asarray(p, float) for line in outline for p in line]
    lo, hi = (np.min(pts, axis=0), np.max(pts, axis=0)) if pts else (np.zeros(2), np.zeros(2))
    dimensioned = {d.element for d in dims if d.kind in ("radius", "diameter")}

    def cross(c, reach, arms="hv"):
        for arm, d in (("h", (1.0, 0.0)), ("v", (0.0, 1.0))):
            if arm in arms:
                out.lines.append((c - np.array(d) * reach, c + np.array(d) * reach, "center"))

    for i, g in everything.items():
        how = centers.get(i, {})
        if how.get("off"):
            continue
        # a dimension never stands alone: what it measures is there, a construction line or arc as a centre line
        if i in carried and g["construction"] and g["kind"] in ("Line", "Arc", "Spline"):
            out.arcs.append(np.asarray(_path(g), float))
        if g["kind"] == "Circle" and not g["construction"]:
            c, r = np.asarray(g["center"], float), float(g["r"])
            cross(c, mm * CROSS if how.get("cross") else r + mm * CENTER_OVER, how.get("arms", "hv"))
        elif g["kind"] == "Arc" and (i in dimensioned or i in centers):
            cross(np.asarray(g["center"], float), mm * CROSS, how.get("arms", "hv"))
        elif g["kind"] == "Circle" and g["construction"]:
            if how.get("pitch") or i in carried:
                t = np.linspace(0, 2 * math.pi, 241)
                out.arcs.append(np.c_[g["center"][0] + g["r"] * np.cos(t), g["center"][1] + g["r"] * np.sin(t)])
            # its center marked only when asked for, as a drawn circle's: lines across it or a cross
            if i in centers and not how.get("pitch"):
                c, r = np.asarray(g["center"], float), float(g["r"])
                cross(c, mm * CROSS if how.get("cross") else r + mm * CENTER_OVER, how.get("arms", "hv"))
        elif g["kind"] == "Axis" and i not in ("H", "V"):
            t = math.radians(g["normal_angle"])
            n, u = np.array([math.cos(t), math.sin(t)]), np.array([-math.sin(t), math.cos(t)])
            mid = (lo + hi) / 2
            foot = mid - (n @ mid - g["d"]) * n
            corners = [lo, hi, np.array([lo[0], hi[1]]), np.array([hi[0], lo[1]])]
            ts = [u @ (c_ - foot) for c_ in corners]
            out.lines.append((foot + u * (min(ts) - mm * CENTER_OVER), foot + u * (max(ts) + mm * CENTER_OVER), "center"))
    for axis in axes:
        if len(axis) > 3:
            out.arcs.append(np.asarray(axis, float))
            continue
        a, b = np.asarray(axis[0], float), np.asarray(axis[1], float)
        through = np.asarray(axis[2], float) if len(axis) > 2 else (a + b) / 2
        out.lines.append((through, a, "center"))
        out.lines.append((through, b, "center"))
    return out
