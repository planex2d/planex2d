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
FIRST = 10.0
ROW = 8.0
CENTER_OVER = 3.0
CROSS = 6.0
LEADER = 8.0
COMPASS = {"e": 0.0, "ne": 45.0, "n": 90.0, "nw": 135.0, "w": 180.0, "sw": 225.0, "s": 270.0, "se": 315.0}
CURVES = ("Line", "Arc", "Circle", "Spline")


def Fmt(value):
    """a number with a decimal comma and at most three decimals; a text as it is"""
    if isinstance(value, str):
        return value
    return f"{value:.3f}".rstrip("0").rstrip(".").replace(".", ",")


def _unit(v):
    v = np.asarray(v, float)
    n = float(np.linalg.norm(v))
    return v / n if n > 0 else v


def _upright(u):
    """the angle of text along u, in (-90, 90]: read from below or from the right"""
    angle = math.degrees(math.atan2(u[1], u[0]))
    while angle > 90:
        angle -= 180
    while angle <= -90:
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
    a counterclockwise to b with legs that long"""
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
    b: Any = None
    legs: tuple = ((0.0, 0.0), (0.0, 0.0))
    count: int = 1
    span: float = 0.0
    refs: tuple = ()


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
    """for each circle and arc, how many equal ones it stands for: its pattern copies and its px.Equal partners"""
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
    size = {}
    for i in rounds:
        size[find(i)] = size.get(find(i), 0) + 1
    return {i: size[find(i)] for i in rounds}


def _hv(plane, point):
    p = np.asarray(tuple(point), float)
    return np.asarray(plane.ToHV(tuple(p[:3]))) if len(p) == 3 else p[:2]


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
        if any(everything.get(r, {}).get("kind") in ("Pattern", "Offset") for r in refs):
            continue
        name = None if c["id"].startswith((kind + ".", "Reference.")) else c["id"]
        d = Dimension(c["id"], kind, value, bool(c.get("reference")), c["state"] == "over", name, refs=tuple(refs))
        g0 = everything.get(refs[0]) if refs else None
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
            else:
                continue
            if np.linalg.norm(d.q - d.p) == 0:
                continue
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
        else:
            continue
        out.append(d)
    return out


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


def Linear(pen, p, q, n, level, text, u):
    """p to q measured along u, the dimension line where n . x = level"""
    P, Q = p + (level - n @ p) * n, q + (level - n @ q) * n
    pen.extension(p, P)
    pen.extension(q, Q)
    span = float(np.linalg.norm(Q - P))
    along = (Q - P) / span if span > 0 else u
    if span > 2.5 * pen.mm(ARROW) + pen.width(text):
        pen.line(P, Q)
        pen.arrow(P, -along)
        pen.arrow(Q, along)
    else:
        out = 2 * pen.mm(ARROW)
        pen.line(P - along * out, Q + along * out)
        pen.arrow(P, along)
        pen.arrow(Q, -along)
    pen.along(P, Q, text)


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
        pen.along(start + (end - start) * 0.35, end, text)


def Fits(pen, r, text):
    """whether a diameter's value and arrows fit inside the circle"""
    return 2 * r >= pen.width(text) + pen.mm(2 * ARROW + 2)


def Diameter(pen, c, r, d, text):
    """the value along a dimension line through the center, inside the circle"""
    pen.line(c - d * r, c + d * r)
    pen.arrow(c + d * r, d)
    pen.arrow(c - d * r, -d)
    middle = c + d * r * 0.55
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

    def side(p, q, r):
        return (q[..., 0] - p[..., 0]) * (r[..., 1] - p[..., 1]) - (q[..., 1] - p[..., 1]) * (r[..., 0] - p[..., 0])

    return int(np.count_nonzero((side(P, Q, a) * side(P, Q, b) <= 0) & (side(a, b, P) * side(a, b, Q) <= 0)))


def _room(pen, text):
    """the length of dimension line a value needs between two arrows"""
    return pen.width(text) + pen.mm(2 * ARROW + 2)


def Angular(pen, c, a, b, legs, radius, text):
    """the angle at c from direction a counterclockwise to b: an arc of radius around c with an arrow at each end, the
    value along the arc at its middle, or beyond the second arrow with both arrows from outside where the arc is too
    short for it; legs (near, far): the stretch of each side's edge from c, an extension line from its nearer end where
    the arc misses it"""
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
    if radius * (t1 - t0) >= _room(pen, text):
        start, end, middle = t0, t1, (t0 + t1) / 2
        inward = -1.0
    else:
        start, end = t0 - pen.mm(2 * ARROW) / radius, t1 + (pen.mm(ARROW + 2) + width) / radius
        middle = t1 + (pen.mm(ARROW + 1) + width / 2) / radius
        inward = 1.0
    t = np.linspace(start, end, 97)
    pen.out.arcs.append(c + radius * np.c_[np.cos(t), np.sin(t)])
    for s, sign in ((t0, inward), (t1, -inward)):
        pen.arrow(c + radius * np.array([math.cos(s), math.sin(s)]), sign * np.array([-math.sin(s), math.cos(s)]))
    at = c + radius * np.array([math.cos(middle), math.sin(middle)])
    half = np.array([-math.sin(middle), math.cos(middle)]) * width / 2
    pen.along(at - half, at + half, text)


def Text(d, place, names, measured=None, fmt=Fmt):
    """what a dimension writes: its value as fmt writes it (a decimal comma), R or a diameter sign (a thread's size such
    as "M5" in place of the sign), n x for equal features, a suffix, a reference dimension in parentheses, the id in
    front where names is True and the id is not automatic; measured: written in place of the value with no R or sign
    (a CAD dimension entity's "<>", whose entity adds them)"""
    p = place or Place()
    sign = d.kind in ("radius", "diameter")
    if p.text is not None:
        value, sign = p.text, sign and p.text[:1] != "M"
    elif measured is not None:
        value, sign = measured, False
    elif d.kind == "angle":
        value = fmt(180 - abs(d.value) if p.supplement else abs(d.value)) + "°"
    else:
        value = fmt(d.value)
    if sign:
        value = ("⌀" if d.kind == "diameter" else "R") + value
    count = p.count if p.count is not None else d.count
    text = (f"{count}× " if count > 1 else "") + value + (f" {p.suffix}" if p.suffix else "")
    if names and d.name:
        text = f"{d.name} = {text}"
    return f"({text})" if d.reference else text


class Layout:
    """the dimension lines' places: given ones from a drawing, the others in rows outside the part (horizontal ones
    below or above, vertical ones left or right, shorter nearer the part, no two on one stretch of a row), inclined
    ones off their measured points away from the part, radii and diameters away from the part's middle"""

    def __init__(self, outline, mm, width, places=None, names=False, fmt=Fmt):
        pts = [np.asarray(p, float) for line in outline for p in line]
        self.pen = Pen(mm, outline, width)
        self.places, self.names, self.fmt = places or {}, names, fmt
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
        """the normal n of the dimension line and its level n . x"""
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
            return n, self._offset(n, levels, place.offset)
        if abs(u[1]) < 1e-9 or abs(u[0]) < 1e-9:
            axis = 1 if abs(u[1]) < 1e-9 else 0
            low = min(levels) - self.lo[axis] <= self.hi[axis] - max(levels)
            side = ("below" if low else "above") if axis == 1 else ("left" if low else "right")
            along = 1 - axis
            text = pen.width(Text(d, place, self.names, fmt=self.fmt))
            a, b = sorted((d.p[along], d.q[along]))
            m = (a + b) / 2
            lo, hi = min(a, m - text / 2) - pen.mm(ARROW), max(b, m + text / 2) + pen.mm(ARROW)
            k = self._row(side, lo, hi)
            reach = pen.mm(FIRST + ROW * k)
            return n, (self.lo[axis] - reach) if low else (self.hi[axis] + reach)
        away = n if n @ ((d.p + d.q) / 2 - self.middle) >= 0 else -n
        return n, (max(away @ d.p, away @ d.q) + pen.mm(FIRST)) * (1 if away @ n > 0 else -1)

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
            n, level = self._level(d, place)
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
                    u, beyond = self._leader(d, text, u, through)
                Leader(pen, d.c, d.r, u, text, beyond, through)
                entity.update(c=d.c, r=d.r, angle=math.degrees(math.atan2(u[1], u[0])), location=pen.out.texts[-1][0])
            else:
                def draw(u, inside=False):
                    if d.kind == "diameter":
                        Diameter(pen, d.c, d.r, u, text)
                    else:
                        Radius(pen, d.c, d.r, u, text, place is not None and place.outside,
                               inside or (place is not None and place.inside))

                inside = False
                if given is None and d.a is None:
                    u = self._free(d, draw, u)
                elif given is None and d.kind == "radius" and place is None:
                    u, inside = self._arc_radius(d, text, u)
                draw(u, inside)
                entity.update(c=d.c, r=d.r, angle=math.degrees(math.atan2(u[1], u[0])), location=None)
            self.spokes.append((d.c, u))
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
            if place is not None and place.radius is not None:
                radius = place.radius
            else:
                # the arc long enough for its value; where that is more than twice as far out as the part's size would
                # put it (a small angle), past the edges' far ends with the value beyond its arrows
                fars = [far for _, far in d.legs if far > 0]
                base = max(pen.mm(FIRST * 1.5), 0.5 * min(fars) if fars else 0.0)
                radius = max(base, _room(pen, text) / math.radians(_ccw(a, b)))
                if radius > 2 * base:
                    # just past the shorter leg's end: past the longer one it reaches across the rest of the part
                    radius = max(base, min(fars, default=0.0) + pen.mm(FIRST))
            Angular(pen, d.c, a, b, legs, radius, text)
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
        """the segments drawn so far: lines, arcs and the values' boxes; with center, not the lines through it (its own
        and a concentric circle's center lines, which every radius or diameter line there crosses)"""
        pen, out = self.pen, []
        for s in self.drawn:
            out += [(a, b) for a, b, _ in s.lines if centre is None or _off_segment(centre, a, b) > pen.mm(0.1)]
            out += [(a, b) for arc in s.arcs for a, b in zip(arc[:-1], arc[1:])]
            out += [seg for anchor, angle, text in s.texts for seg in _box(anchor, angle, pen.width(text), pen.mm(s.height))]
        return out

    def _free(self, d, draw, first):
        """a circle's radius or diameter line: the first direction, else the next diagonal whose line and value cross
        nothing drawn before (a concentric circle's) and none of the part's edges, else the one that crosses the fewest
        dimensions, then the fewest edges"""
        pen = self.pen
        P, Q = _ends(self._obstacles(d.c))
        O, R = _ends(pen.segments)
        # a concentric circle's line already drawn: not along it, its value would lie on the other's
        taken = [t for c, t in self.spokes if np.linalg.norm(c - d.c) <= pen.mm(0.1)]
        if len(P) == 0 and len(O) == 0 and not taken:
            return first
        start, best, kept = math.degrees(math.atan2(first[1], first[0])), None, pen.out
        for angle in (start, start + 90, start - 90, start + 180, start + 30, start - 30, start + 60, start - 60):
            u = Direction(angle)
            if any(abs(u[0] * t[1] - u[1] * t[0]) < 0.17 for t in taken):
                continue
            pen.out = Strokes(d.id)
            draw(u)
            trial, pen.out = pen.out, kept
            crossed = (self._crossings(trial, P, Q) if len(P) else 0, self._crossings(trial, O, R) if len(O) else 0)
            if crossed == (0, 0):
                return u
            if best is None or crossed < best[0]:
                best = (crossed, u)
        return first if best is None else best[1]

    def _crossings(self, trial, P, Q):
        """how often the lines and value boxes of the trial strokes cross the segments P[i] - Q[i]"""
        pen = self.pen
        pieces = [(a, b) for a, b, _ in trial.lines]
        pieces += [seg for anchor, a, t in trial.texts for seg in _box(anchor, a, pen.width(t), pen.mm(HEIGHT))]
        return sum(_hits(a, b, P, Q) for a, b in pieces)

    def _arc_radius(self, d, text, middle):
        """an arc's radius line through its middle and the side of its value: outside when it does not fit between the
        center and the arc, unless outside crosses more of the part (a concave arc's outside is the part), then more of
        the dimensions drawn before"""
        pen = self.pen
        if d.r >= pen.width(text) + pen.mm(ARROW + 3):
            return middle, True
        P, Q = _ends(self._obstacles(d.c))
        O, R = _ends(pen.segments)
        best = None
        for inside in (False, True):
            kept, pen.out = pen.out, Strokes(d.id)
            Radius(pen, d.c, d.r, middle, text, inside=inside)
            trial, pen.out = pen.out, kept
            crossed = (self._crossings(trial, O, R) if len(O) else 0, self._crossings(trial, P, Q) if len(P) else 0)
            if best is None or crossed < best[0]:
                best = (crossed, inside)
        return middle, best[1]

    def _leader(self, d, text, first, through):
        """a leader's direction and how far out from the circle it bends: the one that crosses the fewest dimensions
        drawn before, then the fewest of the part's edges, then the shortest; the diagonal away from the part's middle
        first, the other diagonals, then 30 and 60 degrees from h"""
        pen = self.pen
        start = math.degrees(math.atan2(first[1], first[0]))
        angles = [start + k for k in (0, 90, -90, 180)]
        angles += [a + s for a in angles for s in (-15, 15)]
        P, Q = _ends(self._obstacles(d.c))
        O, R = _ends(pen.segments)
        best = None
        for k in range(1 + max((len(rows) for rows in self.rows.values()), default=0) + 1):
            beyond = LEADER + ROW * k
            for rank, angle in enumerate(angles):
                u = Direction(angle)
                tip, bend, end, back = _leader(pen, d.c, d.r, u, text, beyond)
                left, right = sorted((bend, end), key=lambda p: p[0])
                box = _box((left + right) / 2 + np.array([0.0, pen.mm(1)]), 0.0, pen.width(text), pen.mm(HEIGHT))
                # the pieces outside the circle; the line inside it meets the circle's own centre lines anyway
                pieces = [(tip + u * pen.mm(0.3), bend), (bend, end)] + box
                if through:
                    pieces.append((d.c - u * (d.r + pen.mm(0.3)), back))
                score = (sum(_hits(a, b, P, Q) for a, b in pieces), sum(_hits(a, b, O, R) for a, b in pieces), k, rank)
                if best is None or score < best[0]:
                    best = (score, u, beyond)
                if score[:2] == (0, 0):
                    return u, beyond
        return best[1], best[2]

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
    """the center lines: a cross through every circle drawn (not construction), a short cross at the center of an arc
    whose radius is dimensioned, the sketch's axes (not H and V) across the part, a circular pattern's pitch circle;
    centers: {id: dict(arms=..., cross=..., off=...)} changes; axes: more (a, b) or (a, b, through); carried: the
    construction elements a drawn dimension measures, drawn as center lines"""
    from ._svg import _path

    centers = centers or {}
    out = Strokes("center lines")
    pts = [np.asarray(p, float) for line in outline for p in line]
    lo, hi = (np.min(pts, axis=0), np.max(pts, axis=0)) if pts else (np.zeros(2), np.zeros(2))
    dimensioned = {d.element for d in dims if d.kind == "radius"}

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
        elif g["kind"] == "Circle" and g["construction"] and (how.get("pitch") or i in carried):
            t = np.linspace(0, 2 * math.pi, 241)
            out.arcs.append(np.c_[g["center"][0] + g["r"] * np.cos(t), g["center"][1] + g["r"] * np.sin(t)])
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
