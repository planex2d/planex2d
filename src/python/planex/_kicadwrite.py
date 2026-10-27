# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""px.WriteKiCad: planex shapes as a KiCad 9 board (.kicad_pcb)"""

from __future__ import annotations

import bisect
import math
import os
import re
import uuid
from importlib import metadata
from typing import TYPE_CHECKING

from . import _planex as px
from ._geom import oriented, xy
from ._compound import Compound
from ._units import m
from ._caller import warn

if TYPE_CHECKING:
    from ._planex import Face, Shape, Wire

# KiCad 9.0's file version
_VERSION = 20241229
# the board outline's stroke in mm
_EDGE_WIDTH = 0.05
# KiCad 9's layers after the copper, in the order of its layer table: (number, name, user name)
_TECHNICAL = ((9, "F.Adhes", "F.Adhesive"), (11, "B.Adhes", "B.Adhesive"), (13, "F.Paste", None),
              (15, "B.Paste", None), (5, "F.SilkS", "F.Silkscreen"), (7, "B.SilkS", "B.Silkscreen"),
              (1, "F.Mask", None), (3, "B.Mask", None), (17, "Dwgs.User", "User.Drawings"),
              (19, "Cmts.User", "User.Comments"), (21, "Eco1.User", "User.Eco1"), (23, "Eco2.User", "User.Eco2"),
              (25, "Edge.Cuts", None), (27, "Margin", None), (31, "F.CrtYd", "F.Courtyard"),
              (29, "B.CrtYd", "B.Courtyard"), (35, "F.Fab", None), (33, "B.Fab", None))
_COPPER = re.compile(r"(?:F|B|In([1-9]|[12][0-9]|30))\.Cu$")
_NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://planex2d.org/WriteKiCad")


def _mm(v):
    s = ("%.6f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


def _quoted(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def _netname(name):
    """the name as KiCad stores a net's name: / as {slash}, line breaks dropped"""
    return name.replace("/", "{slash}").replace("\n", "").replace("\r", "")


def _sweep(a, b, c, ccw):
    """the angle from a to b around c in the arc's direction, in (0, 2 pi]"""
    t = math.atan2(b[1] - c[1], b[0] - c[0]) - math.atan2(a[1] - c[1], a[0] - c[0])
    t = t % (2 * math.pi) if ccw else (-t) % (2 * math.pi)
    return t if t > 0 else 2 * math.pi


def _at(c, r, t):
    return (c[0] + r * math.cos(t), c[1] + r * math.sin(t))


def _arc(a, b, c, r, ccw, sweep):
    """an arc segment from a to b, its mid point on the circle halfway"""
    t = math.atan2(a[1] - c[1], a[0] - c[0]) + (sweep / 2 if ccw else -sweep / 2)
    return ("A", a, b, _at(c, r, t), c, r, ccw, sweep)


def _on_arc(seg, t, eps):
    """the direction t from the center lies on the arc seg (within the angle eps of its ends)"""
    _, a, _, _, c, _, ccw, sweep = seg
    d = t - math.atan2(a[1] - c[1], a[0] - c[0])
    d = d % (2 * math.pi) if ccw else (-d) % (2 * math.pi)
    return d <= sweep + eps or d >= 2 * math.pi - eps


def _split(seg, p):
    """seg in two at its point p"""
    if seg[0] != "A":
        return (seg[0], seg[1], p), (seg[0], p, seg[2])
    _, a, b, _, c, r, ccw, sweep = seg
    first = _sweep(a, p, c, ccw)
    return _arc(a, p, c, r, ccw, first), _arc(p, b, c, r, ccw, max(sweep - first, 0.0))


def _y_range(seg):
    if seg[0] != "A":
        return min(seg[1][1], seg[2][1]), max(seg[1][1], seg[2][1])
    lo, hi = min(seg[1][1], seg[2][1]), max(seg[1][1], seg[2][1])
    c, r = seg[4], seg[5]
    if _on_arc(seg, math.pi / 2, 0.0):
        hi = c[1] + r
    if _on_arc(seg, -math.pi / 2, 0.0):
        lo = c[1] - r
    return lo, hi


def _hit(seg, p, eps, grid):
    """the point of the segment nearest to p on the ray from p to the left (-x), else None. An end within grid (a
    nanometre, which the file rounds to) of the ray's line counts as on it, and so does a crossing that near an end, so
    no bridge runs along an edge or ends a nanometre from a vertex"""
    x0, y0 = p
    ends = [q for q in (seg[1], seg[2]) if abs(q[1] - y0) <= grid]
    crossings = []
    if seg[0] != "A":
        (ax, ay), (bx, by) = seg[1], seg[2]
        if (ay - y0) * (by - y0) < 0:
            crossings.append((ax + (y0 - ay) / (by - ay) * (bx - ax), y0))
    else:
        c, r = seg[4], seg[5]
        dy = y0 - c[1]
        if abs(dy) <= r + grid:
            dx = math.sqrt(max(r * r - dy * dy, 0.0))
            for x in (c[0] - dx, c[0] + dx):
                t = math.atan2(max(min(dy, r), -r), x - c[0])
                if _on_arc(seg, t, eps / r):
                    crossings.append(_at(c, r, t))
    points = ends + [q for q in crossings if all(math.dist(q, e) > 2 * grid for e in ends)]
    left = [q for q in points if q[0] < x0]
    return max(left) if left else None


def _area(loop):
    """the signed area a loop of segments bounds, counterclockwise positive"""
    area = 0.0
    for s in loop:
        (ax, ay), (bx, by) = s[1], s[2]
        area += 0.5 * (ax * by - bx * ay)
        if s[0] == "A":
            area += (0.5 if s[6] else -0.5) * s[5] * s[5] * (s[7] - math.sin(s[7]))
    return area


def _reversed(loop):
    return [(s[0], s[2], s[1]) if s[0] != "A" else ("A", s[2], s[1], s[3], s[4], s[5], not s[6], s[7])
            for s in reversed(loop)]


def _leftmost(loop):
    """the loop's point of least x (then y) and the index of the segment it starts or lies on"""
    best = None
    for i, s in enumerate(loop):
        points = [s[1]]
        if s[0] == "A" and _on_arc(s, math.pi, 0.0):
            points.append((s[4][0] - s[5], s[4][1]))
        for q in points:
            if best is None or q < best[0]:
                best = (q, i)
    return best


def _starting_at(loop, p, i, eps):
    """the loop from its point p on segment i round to p"""
    s = loop[i]
    if math.dist(s[1], p) <= eps:
        return loop[i:] + loop[:i]
    if math.dist(s[2], p) <= eps:
        return loop[i + 1:] + loop[:i + 1]
    first, second = _split(s, p)
    return [second] + loop[i + 1:] + loop[:i] + [first]


class _Node:
    __slots__ = ("seg", "prev", "next")

    def __init__(self, seg):
        self.seg = seg
        self.prev = self.next = self


def _keyholed(outer, holes, eps, grid):
    """one loop of the outer loop and its holes, as KiCad fractures a polygon: the holes from the left, each joined at
    its leftmost point by a bridge straight to the left, to the first edge it meets (split there); the bridge runs
    there and back, so it crosses no edge"""
    if not holes:
        return list(outer)
    ys = [q[1] for loop in [outer] + holes for s in loop for q in (s[1], s[2])]
    y0, height = min(ys), max(ys) - min(ys)
    count = max(1, sum(len(loop) for loop in [outer] + holes) // 4)
    step = height / count if height > 0 else 1.0
    buckets = {}

    def register(node):
        lo, hi = _y_range(node.seg)
        for k in range(math.floor((lo - grid - y0) / step), math.floor((hi + grid - y0) / step) + 1):
            buckets.setdefault(k, []).append(node)

    def after(node, segs):
        last = node
        for s in segs:
            n = _Node(s)
            n.prev, n.next = last, last.next
            last.next.prev = n
            last.next = n
            if s[0] != "B":
                register(n)
            last = n
        return last

    head = _Node(outer[0])
    register(head)
    after(head, outer[1:])
    order = sorted(range(len(holes)), key=lambda j: _leftmost(holes[j])[0])
    for j in order:
        p, i = _leftmost(holes[j])
        loop = _starting_at(holes[j], p, i, eps)
        best = None
        for node in buckets.get(math.floor((p[1] - y0) / step), ()):
            if node.seg[0] == "B":
                continue
            h = _hit(node.seg, p, eps, grid)
            if h is not None and (best is None or h[0] > best[0][0]):
                best = (h, node)
        if best is None:
            raise ValueError("WriteKiCad: a hole of a face lies outside its outer loop")
        h, node = best
        s = node.seg
        if math.dist(s[1], h) <= eps:
            node, h = node.prev, s[1]
        elif math.dist(s[2], h) <= eps:
            h = s[2]
        else:
            first, second = _split(s, h)
            node.seg = first
            after(node, [second])
        bridge = [("B", h, p)] if math.dist(h, p) > eps else []
        back = [("B", p, h)] if bridge else []
        after(node, bridge + loop + back)
    out, node = [], head
    while True:
        out.append(node.seg)
        node = node.next
        if node is head:
            return out


def _segments(wire, tol, eps):
    """a loop or chain of edges as segments ("L", a, b) and ("A", a, b, mid, center, radius, ccw, sweep) in its order,
    splines as lines within tol"""
    out = []
    edges = [v.edge for v in wire.edges]
    for e, back in oriented([a for e in edges for a in (e.approximation if isinstance(e, px.CircleEdge) else [e])], eps):
        if isinstance(e, px.LineEdge):
            a, b = xy(e.start), xy(e.end)
            out.append(("L", b, a) if back else ("L", a, b))
        elif isinstance(e, px.ArcEdge):
            a, b, c, ccw = xy(e.start), xy(e.end), xy(e.center), e.ccw
            if back:
                a, b, ccw = b, a, not ccw
            sweep = math.radians(abs(e.sweep))
            if sweep > math.pi * 1.5:
                halfway = _arc(a, b, c, e.radius, ccw, sweep)[3]
                out.append(_arc(a, halfway, c, e.radius, ccw, sweep / 2))
                out.append(_arc(halfway, b, c, e.radius, ccw, sweep / 2))
            else:
                out.append(("A", a, b, xy(e.MidPoint()), c, e.radius, ccw, sweep))
        else:
            pts = [(x, y) for x, y in e.Sample(tol).tolist()]
            if back:
                pts.reverse()
            out.extend(("L", a, b) for a, b in zip(pts, pts[1:]))
    return out


def _circle(loop, eps):
    """(center, radius) if the loop is one whole circle of arcs"""
    if not loop or any(s[0] != "A" for s in loop):
        return None
    c, r = loop[0][4], loop[0][5]
    if any(math.dist(s[4], c) > eps or abs(s[5] - r) > eps for s in loop):
        return None
    return (c, r) if abs(sum(s[7] for s in loop) - 2 * math.pi) < 1e-9 else None


def _parts(obj, layer):
    """(faces, chains of edges on no face) of a layer's Shape, Face, Wire or Compound"""
    if isinstance(obj, Compound):
        faces, chains = [], []
        for p in obj.parts:
            f, c = _parts(p, layer)
            faces += f
            chains += c
        return faces, chains
    if isinstance(obj, px.Shape):
        return [px.Face(f) for f in obj.faces], list(obj._FreeWires())
    if isinstance(obj, px.Face):
        return [obj], []
    if isinstance(obj, px.Wire):
        return [], list(obj.wires)
    raise TypeError("WriteKiCad: %s holds a %s, not a planex Shape, Face, Wire or Compound" %
                    (layer, type(obj).__name__))


def _plane(obj, layer):
    """the plane of every part of obj, (origin, u, v) in 3d"""
    if isinstance(obj, Compound):
        return [q for p in obj.parts for q in _plane(p, layer)]
    if not isinstance(obj, (px.Shape, px.Face, px.Wire)):
        raise TypeError("WriteKiCad: %s holds a %s, not a planex Shape, Face, Wire or Compound" %
                        (layer, type(obj).__name__))
    o, u, v = obj.ToXYZ((0, 0)), obj.ToXYZ((1, 0)), obj.ToXYZ((0, 1))
    return [(o, tuple(a - b for a, b in zip(u, o)), tuple(a - b for a, b in zip(v, o)))]


def _same_plane(p, q):
    scale = max(1.0, max(abs(x) for x in p[0] + q[0]))
    return all(abs(a - b) <= 1e-9 * scale for a, b in zip(p[0], q[0])) and \
        all(abs(a - b) <= 1e-12 for a, b in zip(p[1] + p[2], q[1] + q[2]))


class _Board:
    def __init__(self, unit, tol, line_width):
        self.k = unit / 1e-3
        self.tol = tol
        self.line_width = line_width * self.k
        self.eps = 1e-9 / self.k
        self.grid = 1e-6 / self.k
        self.quantum = 1e-3 / self.k
        self.footprints, self.items, self.vias = [], [], []
        self.seen = {}
        self.cache = {}

    def loops(self, face):
        """the face's loops, the outer first, as [(wire, segments, hole key, (center, radius) if round)], worked out
        once per face"""
        got = self.cache.get(id(face))
        if got is None or got[0] is not face:
            loops = []
            for w in [face.outer] + list(face.inner):
                segs = _segments(w, self.tol, self.eps)
                loops.append((w, segs) + _hole_key(segs, self.eps, self.quantum))
            got = self.cache[id(face)] = (face, loops)
        return got[1]

    def p(self, q):
        return "%s %s" % (_mm(q[0] * self.k), _mm(-q[1] * self.k))

    def nm(self, q):
        return (round(q[0] * self.k * 1e6), round(-q[1] * self.k * 1e6))

    def uid(self, key):
        """a uuid5 of an element's text and of how many identical ones came before it"""
        n = self.seen.get(key, 0)
        self.seen[key] = n + 1
        return uuid.uuid5(_NAMESPACE, "%s#%d" % (key, n))

    def item(self, head, body, into=None):
        uid = self.uid("(%s %s" % (head, " ".join(body)))
        (self.items if into is None else into).append(
            "\n".join(["\t(%s" % head] + ["\t\t" + b for b in body] + ['\t\t(uuid "%s")' % uid, "\t)"]))

    def via(self, c, pad, drill, net):
        """a through via: its pad's diameter rounded down to the nanometre, so it stays within the copper"""
        size = math.floor(2 * pad * self.k * 1e6 + 1e-6) / 1e6
        self.item("via", ["(at %s)" % self.p(c), "(size %s)" % _mm(size), "(drill %s)" % _mm(2 * drill * self.k),
                          '(layers "F.Cu" "B.Cu")', "(net %d)" % net], self.vias)

    def mounting_hole(self, c, r):
        """an unplated hole: a footprint with one np_thru_hole pad as wide as the hole, as KiCad's MountingHole
        footprints have it"""
        d = _mm(2 * r * self.k)
        pad = '(pad "" np_thru_hole circle (at 0 0) (size %s %s) (drill %s) (layers "*.Cu" "*.Mask")' % (d, d, d)
        pad += ' (uuid "%s"))' % self.uid(pad + self.p(c))
        self.item('footprint "MountingHole"', ['(layer "F.Cu")', "(at %s)" % self.p(c), pad], self.footprints)

    def straight(self, s):
        """the arc s drawn as a line: its ends or its mid point on one nanometre grid line"""
        a, mid, b = self.nm(s[1]), self.nm(s[3]), self.nm(s[2])
        cross = (mid[0] - a[0]) * (b[1] - a[1]) - (mid[1] - a[1]) * (b[0] - a[0])
        return a == b or abs(cross) <= math.dist(a, b)

    def pts(self, loop):
        """a loop as KiCad's points: (xy) for a line's start, (arc (start) (mid) (end)) for an arc, an arc's end not
        repeated; starting with a line where there is one (KiCad fills a polygon with a hole wrongly when it starts with
        an arc and closes with a line back to that arc's start)"""
        k = next((i for i, s in enumerate(loop) if s[0] != "A" or self.straight(s)), 0)
        loop = loop[k:] + loop[:k]
        out, after_arc = [], False
        for s in loop:
            if s[0] == "A" and not self.straight(s):
                out.append("(arc (start %s) (mid %s) (end %s))" % (self.p(s[1]), self.p(s[3]), self.p(s[2])))
                after_arc = True
            else:
                if not after_arc:
                    out.append("(xy %s)" % self.p(s[1]))
                after_arc = False
        return out

    def poly(self, loop, layer, net):
        pts = self.pts(loop)
        if len(pts) < 2:
            return
        body = ["(pts"] + ["\t" + " ".join(pts[k:k + 4]) for k in range(0, len(pts), 4)] + [")"]
        body += ["(stroke (width 0) (type solid))", "(fill yes)", "(layer %s)" % _quoted(layer)]
        if net is not None:
            body.append("(net %d)" % net)
        self.item("gr_poly", body)

    def stroke(self, s, layer, width):
        """a segment as a gr_line or gr_arc"""
        tail = ["(stroke (width %s) (type solid))" % _mm(width), "(layer %s)" % _quoted(layer)]
        if s[0] == "A" and not self.straight(s):
            self.item("gr_arc", ["(start %s)" % self.p(s[1]), "(mid %s)" % self.p(s[3]), "(end %s)" % self.p(s[2])]
                      + tail)
        elif self.nm(s[1]) != self.nm(s[2]):
            self.item("gr_line", ["(start %s)" % self.p(s[1]), "(end %s)" % self.p(s[2])] + tail)

    def circle(self, c, r, layer, width):
        self.item("gr_circle", ["(center %s)" % self.p(c), "(end %s)" % self.p((c[0] + r, c[1])),
                                "(stroke (width %s) (type solid))" % _mm(width), "(fill no)",
                                "(layer %s)" % _quoted(layer)])

    def faces(self, faces, layer, nets):
        for f, net in zip(faces, nets):
            loops = self.loops(f)
            outer = loops[0][1]
            if _area(outer) < 0:
                outer = _reversed(outer)
            holes = [h if _area(h) < 0 else _reversed(h) for h in (lp[1] for lp in loops[1:]) if h]
            self.poly(_keyholed(outer, holes, self.eps, self.grid), layer, net)

    def outline(self, faces, chains, drilled):
        """every loop of the board outline on Edge.Cuts but the round holes drilled (keys in drilled), an edge two
        faces share once; a whole circle as gr_circle"""
        loops = [lp for f in faces for lp in self.loops(f)]
        loops += [(w, segs) + _hole_key(segs, self.eps, self.quantum)
                  for w, segs in ((w, _segments(w, self.tol, self.eps)) for w in chains)]
        written = set()
        for _, segs, hole, _ in loops:
            if hole in drilled:
                continue
            key = tuple(sorted(tuple(sorted((self.p(s[1]), self.p(s[2])))) for s in segs))
            if key in written:
                continue
            written.add(key)
            circle = _circle(segs, self.eps)
            if circle is not None:
                self.circle(circle[0], circle[1], "Edge.Cuts", _EDGE_WIDTH)
                continue
            for s in segs:
                self.stroke(s, "Edge.Cuts", _EDGE_WIDTH)


def _quantum(v, q):
    """v counted in q, rounded at boundaries 0.3765 nm off KiCad's nanometre grid, so a board's coordinates and their
    copies a few ulps off round alike"""
    return math.floor(v / q + 0.5003765)


def _hole_key(loop, eps, q):
    """a key the same for the same hole drawn anywhere (q a micrometer), and (center, radius) if the hole is round"""
    circle = _circle(loop, eps)
    if circle is not None:
        (cx, cy), r = circle
        return ("o", _quantum(cx, q), _quantum(cy, q), _quantum(r, q)), circle
    return ("p", len(loop)) + tuple(sorted({(_quantum(s[1][0], q), _quantum(s[1][1], q)) for s in loop})), None


def _named_hole(wire):
    """"pth" or "npth" if every edge of the loop has that name, as FromKiCad names its holes"""
    names = {v.name for v in wire.edges}
    name = names.pop() if len(names) == 1 else None
    return name if name in ("pth", "npth") else None


class _Drills:
    """the board's holes, each once: [kind, (center, radius) if round, key, face to cut out]"""

    def __init__(self, board):
        self.board = board
        self.holes = []
        self.index = {}

    def add(self, kind, wire, face, key=None, circle=None):
        if key is None:
            key, circle = _hole_key(_segments(wire, self.board.tol, self.board.eps), self.board.eps,
                                    self.board.quantum)
        if key not in self.index:
            self.index[key] = len(self.holes)
            self.holes.append([kind, circle, key, face if face is not None else wire.Face()])

    def through(self, faces):
        """{hole: the face it is a hole of} and every face's loops [(wire, hole or None)]"""
        found, loops = {}, []
        for i, f in enumerate(faces):
            own = []
            for n, (w, _, key, _) in enumerate(self.board.loops(f)):
                k = self.index.get(key) if n else None
                own.append((w, k))
                if k is not None:
                    found.setdefault(k, i)
            loops.append(own)
        return found, loops

    def cut(self, faces, layer):
        """the faces with every hole cut out that they do not have yet"""
        if not faces or not self.holes:
            return faces
        found, _ = self.through(faces)
        missing = [h[3] for k, h in enumerate(self.holes) if k not in found]
        if not missing:
            return faces
        shape = px.Glue(faces) if len(faces) > 1 else faces[0]
        return _parts(shape - (px.Unify(missing) if len(missing) > 1 else missing[0]), layer)[0]


class _Rings:
    """the copper around a hole: the distance from its center to the face's other loops, the widest disc there within
    the copper; the holes sorted by the middle of their box in x, so a search looks at the near ones only"""

    def __init__(self, loops):
        self.outer = loops[0][0]
        boxes = []
        for w, k in loops[1:]:
            (x0, y0), (x1, y1) = w.bounding_box
            boxes.append(((x0 + x1) / 2, x0, y0, x1, y1, w, k))
        boxes.sort(key=lambda b: b[0])
        self.boxes = boxes
        self.middles = [b[0] for b in boxes]
        self.half = max((b[3] - b[1] for b in boxes), default=0.0) / 2

    def __call__(self, hole, c):
        best = self.outer.Distance((c[0], c[1]))
        start = bisect.bisect_left(self.middles, c[0])
        for step in (1, -1):
            j = start if step == 1 else start - 1
            while 0 <= j < len(self.boxes):
                middle, x0, y0, x1, y1, w, k = self.boxes[j]
                if abs(middle - c[0]) - self.half >= best:
                    break
                if k != hole and math.hypot(max(x0 - c[0], 0.0, c[0] - x1), max(y0 - c[1], 0.0, c[1] - y1)) < best:
                    best = min(best, w.Distance((c[0], c[1])))
                j += step
        return best


def _nets(copper, faces, through, drills):
    """the net of every copper face (layer index, face index): faces on one layer with one name are one net, faces a
    plated hole goes through are one net; a net is named after the first named face, from the top layer and in the
    order of the faces, else net1, net2, ... in that order, skipping names faces have; a net with faces of two names
    warns"""
    parent = {}

    def root(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def join(a, b):
        ra, rb = root(a), root(b)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)

    names = {(li, i): f.name for li, layer in enumerate(copper) for i, f in enumerate(faces[layer])}
    for li, layer in enumerate(copper):
        first = {}
        for i, f in enumerate(faces[layer]):
            if f.name:
                join((li, i), first.setdefault(f.name, (li, i)))
    clash = {}
    for k, (kind, circle, key, face) in enumerate(drills.holes):
        if kind != "pth":
            continue
        nodes = [(li, through[layer][k]) for li, layer in enumerate(copper) if k in through[layer]]
        for node in nodes[1:]:
            join(nodes[0], node)
        here = list(dict.fromkeys(names[n] for n in nodes if names[n]))
        if len(here) > 1 and root(nodes[0]) not in clash:
            (x0, y0), (x1, y1) = face.bounding_box
            clash[root(nodes[0])] = (here[0], here[1], circle[0] if circle else ((x0 + x1) / 2, (y0 + y1) / 2))
    groups = {}
    for node in sorted(names):
        groups.setdefault(root(node), []).append(node)
    used = {n for n in names.values() if n}
    out, count = {}, 0
    for r, nodes in sorted(groups.items()):
        named = list(dict.fromkeys(names[n] for n in nodes if names[n]))
        if named:
            net = named[0]
            if len(named) > 1:
                a, b, at = clash.get(r, (named[0], named[1], None))
                where = " at (%.6g, %.6g)" % at if at is not None else ""
                warn("WriteKiCad: faces %r and %r are connected through a plated hole%s; their net is %r" %
                              (a, b, where, net), px.PlanexWarning)
        else:
            count += 1
            while "net%d" % count in used:
                count += 1
            net = "net%d" % count
        for n in nodes:
            out[n] = net
    return out


def WriteKiCad(path: str | os.PathLike,
               layers: dict[str, Shape | Face | Wire | Compound] | list[Shape | Face | Compound], /,
               outline: Shape | Face | Wire | Compound | None = None, unit: float = m,
               pth: Shape | Face | Wire | Compound | None = None, npth: Shape | Face | Wire | Compound | None = None,
               tol: float = 1e-6, thickness: float = 1.6e-3, line_width: float = 1e-4) -> None:
    """Writes the layers as a KiCad 9 board (.kicad_pcb, millimeters, y down).

    The board has as many copper layers as the highest one given needs, those not given empty. Everything lies in one
    plane, the xy plane or the plane of the first shape. Every face becomes one filled polygon (gr_poly): lines and
    arcs exactly, splines as lines within tol, a face with holes as one outline with every hole joined to it by a
    bridge there and back, as KiCad fractures a zone. Edges on no face become lines of line_width (gr_line, gr_arc),
    except on copper, which needs an area: they are left out with a warning. The outline's loops become lines, arcs and
    circles on Edge.Cuts.

    Drill holes are the faces of pth and npth (or of layers["pth"] and layers["npth"]) and the holes of the layers'
    faces whose edges are all named "pth" or "npth" (as FromKiCad names them). Every hole is cut out of every copper
    layer and the outline. A round plated hole becomes a via through all copper layers, its pad as wide as the copper
    around the hole on the layer with the least (it adds no copper). A round unplated hole becomes a footprint
    "MountingHole" with one unplated pad. Other holes become cutouts of the outline.

    Nets: faces on one layer with one name, and faces a plated hole goes through, are one net, named after the first
    named face from the top layer down (faces with two names warn), else net1, net2, ... from the top. Numbers are
    written in millimeters with 6 decimals; the uuids are made from each element's text, so the same layers give the
    same file.

    Args:
        path: the file to write.
        layers: a dict {KiCad layer name: Shape, Face, Wire or Compound} (copper "F.Cu", "In1.Cu", ..., "B.Cu", the
            board outline "Edge.Cuts", the other KiCad layers such as "F.SilkS" or "F.Mask", and "pth", "npth"), or a
            list of the copper layers from the top.
        outline: the board outline, instead of "Edge.Cuts" in the dict.
        unit: the length the geometry counts in (px.m, px.mm, ...); tol, thickness and line_width count in it too.
        pth: the plated holes, as faces or closed wires.
        npth: the unplated holes, as faces or closed wires.
        tol: the largest distance of the lines from a spline.
        thickness: the board's thickness.
        line_width: the width of the lines of edges on no face.

    Raises:
        ValueError: unit, tol, thickness or line_width is not a positive length; the outline or a kind of holes is
            given twice; a name is no KiCad layer; more than 32 copper layers; the layers lie in different planes; an
            open wire as a hole; a plated hole without copper around it on a copper layer.

    Examples:
        px.WriteKiCad("board.kicad_pcb", {"F.Cu": top, "B.Cu": bottom}, outline=board, unit=px.mm)
        px.WriteKiCad("board.kicad_pcb", [top, bottom], pth=vias)
    """
    if isinstance(unit, str) or not (unit > 0 and math.isfinite(unit)):
        raise ValueError("WriteKiCad(unit=%r): a unit is a positive length, e.g. px.mm" % (unit,))
    for name, value in (("tol", tol), ("thickness", thickness), ("line_width", line_width)):
        if not (value > 0 and math.isfinite(value)):
            raise ValueError("WriteKiCad(%s=%r): a positive length" % (name, value))
    if isinstance(layers, dict):
        given = dict(layers)
    else:
        given = {name: obj for name, obj in zip(_copper_names(len(layers)), layers)}
    if outline is not None:
        if "Edge.Cuts" in given:
            raise ValueError('WriteKiCad: the outline is given twice, as outline= and as layers["Edge.Cuts"]')
        given["Edge.Cuts"] = outline
    drill = {"pth": pth, "npth": npth}
    for kind in ("pth", "npth"):
        if given.get(kind) is not None:
            if drill[kind] is not None:
                raise ValueError('WriteKiCad: the %s holes are given twice, as %s= and as layers["%s"]' %
                                 (kind, kind, kind))
            drill[kind] = given[kind]
        given.pop(kind, None)
    technical = [name for _, name, _ in _TECHNICAL]
    for name in given:
        if not isinstance(name, str) or not (_COPPER.match(name) or name in technical):
            raise ValueError("WriteKiCad: %r is no KiCad layer; the layers are F.Cu, In1.Cu, ..., In30.Cu, B.Cu, %s" %
                             (name, ", ".join(technical)))
    given = {name: obj for name, obj in given.items() if obj is not None}
    inner = max([int(name[2:-3]) for name in given if name.startswith("In")], default=0)
    copper = _copper_names(inner + 2)
    planes = [(name, q) for name, obj in given.items() for q in _plane(obj, "layer %r" % name)]
    planes += [(kind + "=", q) for kind, obj in drill.items() if obj is not None for q in _plane(obj, kind + "=")]
    for name, q in planes[1:]:
        if not _same_plane(q, planes[0][1]):
            raise ValueError("WriteKiCad: layer %r lies in another plane than layer %r: everything must lie in one "
                             "plane" % (name, planes[0][0]))
    board = _Board(unit, tol, line_width)
    parts = {name: _parts(obj, "layer %r" % name) for name, obj in given.items()}
    drills = _Drills(board)
    for kind, obj in drill.items():
        if obj is None:
            continue
        holes, chains = _parts(obj, kind + "=")
        for f in holes:
            drills.add(kind, f.outer, f)
        for w in chains:
            if not w.IsClosed():
                raise ValueError("WriteKiCad(%s=): an open wire is no hole" % kind)
            drills.add(kind, w, None)
    for name in copper + ["Edge.Cuts"]:
        for f in parts.get(name, ([], []))[0]:
            for w, _, key, circle in board.loops(f)[1:]:
                kind = _named_hole(w)
                if kind is not None:
                    drills.add(kind, w, None, key, circle)
    faces = {name: drills.cut(parts[name][0] if name in parts else [], "layer %r" % name) for name in copper}
    if "Edge.Cuts" in parts:
        parts["Edge.Cuts"] = (drills.cut(parts["Edge.Cuts"][0], "layer 'Edge.Cuts'"), parts["Edge.Cuts"][1])
    through, loops = {}, {}
    for name in copper:
        through[name], loops[name] = drills.through(faces[name])
    nets = _nets(copper, faces, through, drills)
    codes = {name: k for k, name in enumerate(sorted(set(nets.values())), 1)}
    free = {}
    for li, name in enumerate(copper):
        board.faces(faces[name], name, [codes[nets[(li, k)]] for k in range(len(faces[name]))])
        edges = sum(len(w.edges) for w in parts.get(name, ([], []))[1])
        if edges:
            free[name] = edges
    rings = {}
    for k, (kind, circle, key, face) in enumerate(drills.holes):
        if circle is None:
            continue
        if kind == "npth":
            board.mounting_hole(*circle)
            continue
        pad = math.inf
        for name in copper:
            i = through[name].get(k)
            if i is not None and (name, i) not in rings:
                rings[(name, i)] = _Rings(loops[name][i])
            ring = rings[(name, i)](k, circle[0]) if i is not None else 0.0
            if not ring > circle[1]:
                raise ValueError("WriteKiCad: the plated hole at (%.6g, %.6g) has no copper around it on %s; a via "
                                 "needs copper on every layer (blind and buried vias are not written)" %
                                 (circle[0][0], circle[0][1], name))
            pad = min(pad, ring)
        board.via(circle[0], pad, circle[1], codes[nets[(0, through[copper[0]][k])]])
    for _, name, _ in _TECHNICAL:
        if name not in parts:
            continue
        f, chains = parts[name]
        if name == "Edge.Cuts":
            board.outline(f, chains, {h[2] for h in drills.holes if h[1] is not None})
            continue
        board.faces(f, name, [None] * len(f))
        for w in chains:
            for s in _segments(w, tol, board.eps):
                board.stroke(s, name, board.line_width)
    if free:
        n = sum(free.values())
        warn("WriteKiCad: %d edge%s on no face left out on %s (copper needs an area)" %
                      (n, "s" if n > 1 else "", ", ".join(free)), px.PlanexWarning)
    text = _header(copper, thickness * board.k, codes) + board.footprints + board.items + board.vias + [")"]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(text) + "\n")


def _copper_names(n):
    if n > 32:
        raise ValueError("WriteKiCad: %d copper layers; KiCad has at most 32" % n)
    return ["F.Cu"] + ["In%d.Cu" % k for k in range(1, n - 1)] + (["B.Cu"] if n > 1 else [])


def _header(copper, thickness, codes):
    try:
        generator = metadata.version("planex2d")
    except metadata.PackageNotFoundError:
        generator = "unknown"
    table = ["\t\t(%d %s signal)" % (0 if name == "F.Cu" else 2 if name == "B.Cu" else 2 * int(name[2:-3]) + 2,
                                     _quoted(name)) for name in copper]
    table += ["\t\t(%d %s user%s)" % (k, _quoted(name), " " + _quoted(user) if user else "")
              for k, name, user in _TECHNICAL]
    nets = ['\t(net 0 "")'] + ["\t(net %d %s)" % (k, _quoted(_netname(name))) for name, k in sorted(codes.items(),
                                                                                                    key=lambda x: x[1])]
    return (["(kicad_pcb", "\t(version %d)" % _VERSION, '\t(generator "planex")',
             "\t(generator_version %s)" % _quoted(generator), "\t(general", "\t\t(thickness %s)" % _mm(thickness),
             "\t\t(legacy_teardrops no)", "\t)", "\t(layers"] + table +
            ["\t)", "\t(setup", "\t\t(pad_to_mask_clearance 0)", "\t\t(allow_soldermask_bridges_in_footprints no)",
             "\t\t(tenting front back)", "\t)"] + nets)
