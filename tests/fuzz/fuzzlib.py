# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Generators, builders and checks of the fuzz tests. A generated case is a plain tuple of numbers (a spec), so a
failure prints as a tuple that can be pinned; the builders turn specs into planex faces."""
import math
import time

import numpy as np
import pytest
from hypothesis import HealthCheck, assume, given, seed, settings
from hypothesis import strategies as st

import planex as px

BATCH = 25
KINDS = ("star", "rounded", "disc", "holed", "spline")
BOOLEAN_KINDS = KINDS + ("holey", "sharp", "slotted", "mixed")
RELATIONS = ("free", "translated", "half turn", "mirrored", "same", "edge shift", "nudged turn", "nudged shift",
             "tangent", "corner on edge", "corner on corner", "same circle", "turned about a corner", "one point moved",
             "offset")
RELATED = RELATIONS[9:]


def run(body, strategy, budget, pinned=()):
    """body on the pinned specs, then on generated ones in seeded batches until this test's share of the budget is
    used; the seeds count up from 0, so a slower machine runs a shorter prefix of the same cases"""
    seconds, report = budget

    def case(spec):
        report["cases"] += 1
        body(spec)

    for spec in pinned:
        case(spec)
    start = time.perf_counter()
    k = 0
    while k == 0 or time.perf_counter() - start < seconds:
        test = settings(max_examples=BATCH, deadline=None, derandomize=False, database=None,
                        suppress_health_check=list(HealthCheck))(given(strategy)(case))
        seed(k)(test)()
        k += 1


def coordinate(lo=-2.0, hi=2.0):
    return st.floats(lo, hi, allow_nan=False, allow_infinity=False)


@st.composite
def stars(draw, n_min=3, n_max=9):
    """a star-shaped polygon around (cx, cy): vertices at increasing angles, so it is simple by construction"""
    n = draw(st.integers(n_min, n_max))
    radii = tuple(draw(st.lists(st.floats(0.3, 1.0), min_size=n, max_size=n)))
    gaps = tuple(draw(st.lists(st.floats(0.2, 1.0), min_size=n, max_size=n)))
    return (draw(coordinate()), draw(coordinate()), draw(st.floats(0.0, 2 * math.pi)), radii, gaps,
            draw(st.floats(0.5, 2.0)))


@st.composite
def shapes(draw, kinds=KINDS):
    kind = draw(st.sampled_from(kinds))
    star = draw(stars())
    extra = draw(st.floats(0.05, 0.6))
    return (kind, star, extra)


@st.composite
def pairs(draw, kinds=BOOLEAN_KINDS):
    a = draw(shapes())
    a = (draw(st.sampled_from(kinds)),) + a[1:]
    relation = draw(st.sampled_from(RELATIONS))
    b = draw(shapes())
    b = (draw(st.sampled_from(kinds)),) + b[1:]
    return (a, relation, b, draw(st.integers(0, 64)))


def star_points(spec):
    cx, cy, rot, radii, gaps, scale = spec
    total = sum(gaps)
    points, a = [], rot
    for r, g in zip(radii, gaps):
        points.append((cx + scale * r * math.cos(a), cy + scale * r * math.sin(a)))
        a += 2 * math.pi * g / total
    return points


def polygon(points):
    s = px.QuickSketch().Move(tuple(points[0]))
    for p in points[1:]:
        s.Line(p)
    return s.Close().Face()


def build(spec):
    """the face (or Shape) of a shape spec"""
    kind, star, extra = spec
    cx, cy, rot, radii, gaps, scale = star
    if kind in MADE:
        return MADE[kind](star, extra)
    if kind == "disc":
        return px.QuickSketch().Circle((cx, cy), scale * (0.3 + extra)).Face()
    if kind == "spline":
        smooth = (cx, cy, rot, tuple(0.75 + 0.25 * r for r in radii), tuple(0.6 + 0.4 * g for g in gaps), scale)
        points = star_points(smooth)
        return px.QuickSketch().Move(tuple(points[0])).Spline(points[1:], periodic=True).Face()
    face = polygon(star_points(star))
    if kind == "rounded":
        return face.Offset(scale * extra * 0.5, inplace=False)
    if kind == "holed":
        inner = (cx, cy, rot, radii, gaps, scale * (0.2 + 0.5 * extra))
        return face - polygon(star_points(inner))
    return face


def vertices(obj):
    return [(e.start.x, e.start.y) for e in obj.edges]


def edges(obj):
    return [((e.start.x, e.start.y), (e.end.x, e.end.y)) for e in obj.lines]


def build_pair(spec):
    """two faces in the relation the spec names: free, or b made from a so they touch or overlap; a generated spline
    that crosses itself is no valid input and is discarded"""
    a, b = made_pair(spec)
    for x in (a, b):
        g = shapely_of(x, 1e-6 * size(x))
        assume(g is not None and g.is_valid)
    return a, b


def made_pair(spec):
    a_spec, relation, b_spec, pick = spec
    a = build(a_spec)
    if relation == "free":
        return a, build(b_spec)
    if relation == "same":
        return a, build(a_spec)
    if relation == "tangent":
        return a, touching(a, b_spec, pick)
    if relation in RELATED:
        return a, related(a, a_spec, relation, b_spec, pick)
    vs = vertices(a)
    p, q = vs[pick % len(vs)], vs[(pick * 7 + 3) % len(vs)]
    tiny = 10.0 ** -(3 + pick % 13)
    if relation == "nudged turn":
        return a, a.Rotated(p if pick % 2 else (0.0, 0.0), math.degrees(tiny))
    if relation == "nudged shift":
        turn = 2 * math.pi * (pick % 8) / 8
        return a, a.Translated((tiny * math.cos(turn), tiny * math.sin(turn)))
    if relation == "translated":
        return a, a.Translated((q[0] - p[0], q[1] - p[1]))
    if relation == "half turn":
        return a, a.Rotated(p, 180)
    straight = edges(a)
    if not straight:
        return a, a.Translated((q[0] - p[0], q[1] - p[1]))
    s, e = straight[pick % len(straight)]
    if relation == "mirrored":
        return a, a.Mirrored(s, (e[0] - s[0], e[1] - s[1]))
    return a, a.Translated((e[0] - s[0], e[1] - s[1]))


def touching(a, b_spec, pick):
    """b's disc, rounded shape or spline touching an edge of a from outside: its point farthest towards a at a point of
    that edge, moved along the edge's normal by 10^-3 ... 10^-16 into the gap or the overlap"""
    p, n = normal_at(a, a.edges[pick % len(a.edges)], 0.2 + 0.06 * ((pick * 7 + 3) % 11))
    shift = (1.0 if pick % 2 else -1.0) * 10.0 ** -(3 + pick % 14)
    target = (p[0] + shift * n[0], p[1] + shift * n[1])
    kind, star, extra = b_spec
    if kind == "spline":
        b = build(b_spec)
        x = farthest(b, n)
        return b.Translated((target[0] - x[0], target[1] - x[1]))
    if kind == "rounded":
        b = build(b_spec)
        # the rounded shape's arcs have one radius, about the polygon's corners: the corner farthest towards a holds
        # the point farthest towards a
        arc = min(b.arcs, key=lambda c: c.center.x * n[0] + c.center.y * n[1])
        c, r = (arc.center.x, arc.center.y), arc.radius
    else:
        c, r = star[:2], star[5] * (0.3 + extra)
        b = px.QuickSketch().Circle(c, r).Face()
    return b.Translated((target[0] - (c[0] - r * n[0]), target[1] - (c[1] - r * n[1])))


def normal_at(a, e, t):
    """the point of a's edge e at the fraction t of its length and the normal there, pointing out of a"""
    p, d = e.PointAt(t), e.TangentAt(t)
    length = math.hypot(d.x, d.y)
    n = (d.y / length, -d.x / length)
    probe = 1e-6 * size(a)
    if a.Contains(np.array([[p.x + probe * n[0], p.y + probe * n[1]]]))[0]:
        n = (-n[0], -n[1])
    return (p.x, p.y), n


def farthest(obj, n):
    """the point of obj's outline farthest along -n: the least x·n of a sample of each edge, then a golden-section
    search beside it"""
    best = None
    for e in obj.edges:
        def along(t):
            p = e.PointAt(t)
            return p.x * n[0] + p.y * n[1]

        ts = np.linspace(0.0, 1.0, 65)
        k = int(np.argmin([along(t) for t in ts]))
        lo, hi = ts[max(k - 1, 0)], ts[min(k + 1, 64)]
        g = (math.sqrt(5.0) - 1.0) / 2.0
        for _ in range(60):
            m1, m2 = hi - g * (hi - lo), lo + g * (hi - lo)
            if along(m1) <= along(m2):
                hi = m2
            else:
                lo = m1
        t = 0.5 * (lo + hi)
        if best is None or along(t) < best[0]:
            p = e.PointAt(t)
            best = (along(t), (p.x, p.y))
    return best[1]


def nudge(pick):
    """how far a relation moves a shape off its exact place: 0 for every third pick, else 10^-3 ... 10^-16 to either
    side"""
    if pick % 3 == 0:
        return 0.0
    return (1.0 if pick % 2 else -1.0) * 10.0 ** -(3 + pick % 14)


def moved_point(spec, pick):
    """the shape spec with one star point moved out by 10^-3 ... 10^-15 of its radius: another outline running within
    that everywhere near the first (a spline changes along its whole length)"""
    kind, (cx, cy, rot, radii, gaps, scale), extra = spec
    i = pick % len(radii)
    radii = radii[:i] + (radii[i] * (1.0 + 10.0 ** -(3 + pick % 13)),) + radii[i + 1:]
    return (kind, (cx, cy, rot, radii, gaps, scale), extra)


def related(a, a_spec, relation, b_spec, pick):
    """b for the relations that put two shapes' corners, arcs or outlines together: one of b's corners on an edge or a
    corner of a, a disc on one of a's arcs, a turned about one of its corners (several edges through one point), a
    with one point moved, a's offset; exact or nudged by pick"""
    if relation == "one point moved":
        return build(moved_point(a_spec, pick))
    if relation == "offset":
        d = 0.05 * a_spec[1][5] if pick % 3 == 0 else 10.0 ** -(3 + pick % 14)
        return a.Offset(d if pick % 2 else -d, inplace=False)
    if relation == "turned about a corner":
        vs = vertices(a)
        return a.Rotated(vs[pick % len(vs)], 10.0 + 340.0 * ((pick * 37) % 64) / 64)
    if relation == "same circle" and a.arcs:
        arc = a.arcs[pick % len(a.arcs)]
        return px.Circle((arc.center.x + nudge(pick), arc.center.y), arc.radius).Face()
    b = build(b_spec)
    vb = vertices(b)
    s = vb[(pick * 5 + 1) % len(vb)]
    if relation == "corner on edge":
        p, n = normal_at(a, a.edges[pick % len(a.edges)], 0.2 + 0.06 * ((pick * 7 + 3) % 11))
        shift = nudge(pick)
        return b.Translated((p[0] + shift * n[0] - s[0], p[1] + shift * n[1] - s[1]))
    va = vertices(a)
    t = va[pick % len(va)]
    return b.Translated((t[0] + nudge(pick) - s[0], t[1] - s[1]))


def known(spec):
    """the pairs of known limitations, left out of the random runs (their cases are pinned): a spline face against a face
    touching it, a corner on it or a near copy of itself (reshaped, nudged, offset by a hair)"""
    a_spec, relation, b_spec, pick = spec
    hair = relation == "offset" and pick % 3 != 0
    corner = relation in ("corner on edge", "corner on corner", "same circle")
    contact = relation in ("tangent", "one point moved") or relation.startswith("nudged") or corner or hair
    return "spline" in (a_spec[0], b_spec[0]) and contact


def unless_known(check, pairs_of=lambda spec: [spec], also=lambda spec: False):
    """check for the random runs: a spec holding a known pair, or one also names, is left out"""
    def body(spec):
        if not also(spec) and not any(known(p) for p in pairs_of(spec)):
            check(spec)
    return body


def pairs_in_triple(spec):
    return [spec[0], (spec[0][0],) + spec[1:]]


def pair_of_moved(spec):
    return [spec[0]]


def known_moved(spec):
    """a spline's nudged copy, a known limitation"""
    a_spec, relation, b_spec, pick = spec[0]
    return a_spec[0] == "spline" and relation.startswith("nudged")


def area(obj):
    return obj.mass


def size(*objs):
    """the largest coordinate magnitude of the objects' vertices, at least 1: the S of Tol(S)"""
    return max([1.0] + [abs(c) for o in objs for v in vertices(o) for c in v])


def perimeter(obj):
    return sum(e.mass for e in obj.edges)


def area_tol(*objs):
    """how far an area may be off when every vertex is decided within Tol(S): the perimeter times a few Tol(S),
    plus rounding of the area sums"""
    tol = px.config.k * px.config.eps * size(*objs)
    return 8 * tol * sum(perimeter(o) for o in objs) + 1e-12 * sum(area(o) for o in objs)


def check_identities(spec):
    identities(*build_pair(spec))


def identities(a, b):
    A, B = area(a), area(b)
    union, common, a_only, b_only = a + b, a * b, a - b, b - a
    U, I, D, E = area(union), area(common), area(a_only), area(b_only)
    tol = area_tol(a, b)
    assert abs(U - (A + B - I)) <= tol, ("union", U, A + B - I)
    assert abs(U - (D + E + I)) <= tol, ("parts", U, D + E + I)
    assert abs(D - (A - I)) <= tol, ("a - b", D, A - I)
    assert abs(E - (B - I)) <= tol, ("b - a", E, B - I)
    assert abs(area(b + a) - U) <= tol, ("commutative union", area(b + a), U)
    assert abs(area(b * a) - I) <= tol, ("commutative common", area(b * a), I)
    assert area(common - a) <= tol and area(common - b) <= tol, ("common outside an operand",)


def check_self(spec):
    a = build(spec)
    A, tol = area(a), area_tol(a)
    assert abs(area(a + a) - A) <= tol, ("a + a", area(a + a), A)
    assert abs(area(a * a) - A) <= tol, ("a * a", area(a * a), A)
    assert area(a - a) <= tol, ("a - a", area(a - a))


def shapely_of(obj, tol):
    """the faces as shapely polygons, from planex's own loops (Polylines): each face is the even-odd combination of its
    loops; ToShapely is not used, its own loop rebuild joins vertices within 1e-9 x size (work-list 0229). A loop
    narrower than tol samples to a ring crossing itself (work-list 0233): such a ring is repaired by make_valid"""
    import shapely

    faces = getattr(obj, "faces", None)
    if not faces:
        return None
    polygons = []
    for f in faces:
        rings = [shapely.Polygon(r) for r in f.Polylines(tol) if len(r) >= 4]
        rings = [r if r.is_valid else polygonal(shapely.make_valid(r)) for r in rings]
        if not rings:
            continue
        face = rings[0]
        for r in rings[1:]:
            face = shapely.symmetric_difference(face, r)
        polygons.append(face)
    return shapely.union_all(polygons)


def polygonal(geometry):
    """the polygons of a shapely result; an overlay of faces that share an edge also returns that edge as a line"""
    import shapely

    return shapely.union_all([g for g in shapely.get_parts(geometry) if g.geom_type in ("Polygon", "MultiPolygon")])


def check_against_shapely(spec):
    """the results as shapely geometries, against shapely's own booleans of the inputs: the symmetric difference must
    be no larger than the sampling and Tol(S) allow"""
    import shapely

    if spec[1].startswith("nudged") and {spec[0][0], spec[2][0]} & {"rounded", "disc"}:
        return
    a, b = build_pair(spec)
    against_shapely(a, b, spec)


def against_shapely(a, b, spec, straight=False):
    """a + b, a * b and a - b against shapely's booleans of the inputs: the symmetric difference no larger than the
    sampling (none for straight edges) and Tol(S) allow; where they disagree OCC referees"""
    import shapely

    sample = 1e-6 * size(a, b)
    # shapely's overlay is not robust for edges one rounding unit apart (GEOS 3.13.1 returned one operand as the union
    # of a polygon and its mirror copy across a shared edge); snap rounding on a tiny grid is
    grid = 1e-12 * size(a, b)
    tol = area_tol(a, b) + 4 * ((0.0 if straight else sample) + grid) * (perimeter(a) + perimeter(b))
    ops = {"union": shapely.union, "common": shapely.intersection, "difference": shapely.difference}

    def disagreement(name, ours):
        """shapely's symmetric difference to planex's result; a GEOS exception counts as a disagreement"""
        try:
            theirs = polygonal(ops[name](shapely_of(a, sample), shapely_of(b, sample), grid_size=grid))
            mine = shapely_of(ours, sample)
            return theirs.area if mine is None else shapely.symmetric_difference(mine, theirs, grid_size=grid).area
        except shapely.errors.GEOSException as e:
            GEOS_ERRORS.append((name, spec, str(e)))
            return math.inf

    for name, ours in (("union", a + b), ("common", a * b), ("difference", a - b)):
        off = disagreement(name, ours)
        if off > tol and occ_sides_with_planex(name, a, b, ours, tol):
            REFEREED.append((name, spec, off))
            continue
        assert off <= tol, (name, off, tol)


REFEREED = []
GEOS_ERRORS = []


def occ_area(shape):
    return sum(f.mass for f in shape.faces)


def occ_sides_with_planex(name, a, b, ours, tol):
    """where planex and shapely disagree, netgen.occ computes the operation on planex's exact export;
    it sides with planex when its result and planex's differ by no more than tol in area"""
    oa, ob = a.ToOCCShape(), b.ToOCCShape()
    ref = {"union": oa + ob, "common": oa * ob, "difference": oa - ob}[name]
    if not ours.faces:
        return occ_area(ref) <= tol
    mine = ours.ToOCCShape()
    return occ_area(mine - ref) + occ_area(ref - mine) <= tol


def check_points(spec):
    """points well away from both boundaries are inside the common part exactly when inside both operands, inside the
    union exactly when inside either, inside the difference exactly when inside a and not b"""
    import shapely

    a_spec, relation, b_spec, pick = spec
    a, b = build_pair(spec)
    sample = 1e-6 * size(a, b)
    sa, sb = shapely_of(a, sample), shapely_of(b, sample)
    x0, y0, x1, y1 = sa.union(sb).bounds
    rng = np.random.default_rng(pick)
    pts = np.column_stack((rng.uniform(x0, x1, 200), rng.uniform(y0, y1, 200)))
    margin = 1e-6 * size(a, b) + 2 * sample
    boundary = shapely.union(sa.boundary, sb.boundary)
    with np.errstate(divide="ignore", invalid="ignore"):
        far = shapely.distance(boundary, shapely.points(pts)) > margin
    pts = pts[far]
    if len(pts) == 0:
        return
    in_a, in_b = a.Contains(pts), b.Contains(pts)
    for name, result, expected in (("common", a * b, in_a & in_b), ("union", a + b, in_a | in_b),
                                   ("difference", a - b, in_a & ~in_b)):
        got = result.Contains(pts) if result.faces else np.zeros(len(pts), bool)
        wrong = np.flatnonzero(got != expected)
        assert len(wrong) == 0, (name, pts[wrong[:3]].tolist())


STRAIGHT = ("star", "holed")
# the round joins of shapely's buffer: segments per quarter circle; they lie within d * (1 - cos(pi / (4 Q))) of the arc
QUAD_SEGS = 256
# config::solve_tol (not exposed to Python): a sketch is solved when every residual is below it times its size
SOLVE_TOL = 1e-12


@st.composite
def convex_shapes(draw):
    """a convex polygon (the star's vertices on one circle), a disc, or such a polygon rounded"""
    kind = draw(st.sampled_from(("star", "disc", "rounded")))
    cx, cy, rot, radii, gaps, scale = draw(stars())
    return (kind, (cx, cy, rot, (1.0,) * len(radii), gaps, scale), draw(st.floats(0.05, 0.6)))


@st.composite
def offsets(draw, shape_strategy):
    """a shape and the offset distance as a fraction of what its check allows"""
    return (draw(shape_strategy), draw(st.floats(0.05, 0.9)))


def valid(obj):
    """a generated face whose spline crosses itself is no valid input and is discarded"""
    g = shapely_of(obj, 1e-6 * size(obj))
    assume(g is not None and g.is_valid)
    return g


def same_region(a, b, what):
    """b covers what a covers: equal areas and a symmetric difference within Tol(S) and the sampling per unit of
    boundary length"""
    import shapely

    sample = 1e-6 * size(a, b)
    grid = 1e-12 * size(a, b)
    tol = area_tol(a, b) + 4 * (sample + grid) * (perimeter(a) + perimeter(b))
    assert abs(area(a) - area(b)) <= tol, (what, "area", area(a), area(b), tol)
    off = shapely.symmetric_difference(shapely_of(a, sample), shapely_of(b, sample), grid_size=grid).area
    assert off <= tol, (what, "symmetric difference", off, tol)


def like_shapely(ours, theirs, a, d, what):
    """an offset result against shapely's buffer of the sampled input: the round joins of both are within QUAD_SEGS'
    chord error of the arcs, the input within the sampling"""
    import shapely

    sample = 1e-6 * size(a)
    grid = 1e-12 * size(a)
    chord = d * (1 - math.cos(math.pi / (4 * QUAD_SEGS)))
    tol = area_tol(a) + 8 * (sample + chord + grid) * (perimeter(a) + 2 * math.pi * d)
    mine = shapely_of(ours, sample) if ours is not None else None
    if mine is None:
        assert theirs.area <= tol, (what, "planex empty", theirs.area, tol)
        return
    off = shapely.symmetric_difference(mine, theirs, grid_size=grid).area
    assert off <= tol, (what, off, tol)


def opened(a, d):
    """a.Offset(-d).Offset(d), None where nothing is left after the first"""
    eroded = a.Offset(-d, inplace=False)
    return eroded.Offset(d) if eroded.faces else None


def check_offset_convex(spec):
    """a convex shape grown by d and shrunk by d is itself for every d; shrunk and grown again it is itself when every
    boundary point has a disc of radius d inside (a disc of radius above d, a polygon rounded by more than d), a convex
    polygon loses its corners: there it equals shapely's opening"""
    import shapely

    shape_spec, frac = spec
    kind, star, extra = shape_spec
    scale = star[5]
    a = build(shape_spec)
    valid(a)
    d = frac * scale
    same_region(a, a.Offset(d, inplace=False).Offset(-d), ("closing", d))
    if kind == "star":
        d = frac * 0.3 * scale
        theirs = shapely.buffer(shapely.buffer(shapely_of(a, 1e-6 * size(a)), -d, quad_segs=QUAD_SEGS), d,
                                quad_segs=QUAD_SEGS)
        like_shapely(opened(a, d), theirs, a, d, ("opening of a polygon", d))
        return
    d = frac * (scale * (0.3 + extra) if kind == "disc" else scale * extra * 0.5)
    same_region(a, opened(a, d), ("opening", d))


def check_offset_any(spec):
    """any face: grown and shrunk by d it covers itself (concavities narrower than 2 d fill), shrunk and grown it lies
    inside itself (parts narrower than 2 d and corners go); for straight-edged faces both as shapely's closing and
    opening by d: shapely's buffer of a sampled curve is off by up to ~5e-5 per unit of boundary however fine the
    sampling (a rounded triangle's opening is exact in planex to 2e-16)"""
    import shapely

    shape_spec, frac = spec
    a = build(shape_spec)
    g = valid(a)
    d = frac * 0.3 * shape_spec[1][5]
    tol = area_tol(a) + 4e-6 * size(a) * perimeter(a)
    closing = a.Offset(d, inplace=False).Offset(-d)
    assert area(a - closing) <= tol, ("closing does not cover", d, area(a - closing))
    opening = opened(a, d)
    if opening is not None:
        assert area(opening - a) <= tol, ("opening sticks out", d, area(opening - a))
    if a.arcs or a.splines:
        return
    like_shapely(closing, shapely.buffer(shapely.buffer(g, d, quad_segs=QUAD_SEGS), -d, quad_segs=QUAD_SEGS), a, d,
                 ("closing", d))
    like_shapely(opening, shapely.buffer(shapely.buffer(g, -d, quad_segs=QUAD_SEGS), d, quad_segs=QUAD_SEGS), a, d,
                 ("opening", d))


def same_boundary(a, b, tol, what):
    """as many faces, the areas within tol per unit of boundary length, every boundary point within tol of the other
    boundary"""
    import shapely

    assert len(b.faces) == len(a.faces), (what, "faces", len(a.faces), len(b.faces))
    sample = 1e-6 * size(a, b)
    assert abs(area(a) - area(b)) <= area_tol(a, b) + (tol + 2 * sample) * (perimeter(a) + perimeter(b)), \
        (what, "area", area(a), area(b))
    far = shapely.hausdorff_distance(shapely_of(a, sample).boundary, shapely_of(b, sample).boundary)
    assert far <= tol + 2 * sample, (what, "boundaries apart", far, tol)


def check_occ_round_trip(spec):
    """the OCC export read back: the export cleans within its thresholds (merge, small edges, gaps), a spline goes out
    as its exact curve and comes back approximated again, both approximations within spline_tol x its box diagonal"""
    a = build(spec)
    x0, y0, x1, y1 = valid(a).bounds
    cleanup = max(px.config.merge_collinear_closer, px.config.remove_edges_smaller, px.config.close_gaps_smaller)
    curves = 2 * px.config.spline_tol * math.hypot(x1 - x0, y1 - y0) if spec[0] == "spline" else 0.0
    same_boundary(a, px.FromOCC(a.ToOCCShape()), cleanup + curves + 8 * px.config.k * px.config.eps * size(a), "OCC")


def check_shapely_round_trip(spec):
    """a straight-edged face through ToShapely and FromShapely: the vertices go as they are, ToShapely joins those
    within 1e-9 x the size"""
    a = build(spec)
    valid(a)
    same_boundary(a, px.FromShapely(a.ToShapely()), 1e-9 * size(a), "shapely")


@st.composite
def sketch_specs(draw):
    """a constrained sketch family and random values of its dimensions"""
    length = st.floats(0.2, 10.0)
    family = draw(st.sampled_from(("rectangle", "triangle", "sas", "bend")))
    if family == "rectangle":
        return (family, draw(st.floats(0.05, 20.0)), draw(st.floats(0.05, 20.0)))
    if family == "triangle":
        return (family, draw(length), draw(length), draw(length))
    if family == "sas":
        return (family, draw(length), draw(length), draw(st.floats(15.0, 165.0)))
    return (family, draw(length), draw(st.floats(0.1, 5.0)), draw(st.floats(15.0, 300.0)), draw(length))


def sketch_of(family):
    """the sketch of a family, drawn near some first values, and the ids of its dimensions"""
    sk = px.Sketch()
    if family == "rectangle":
        sk.Move(sk.origin, px.Coincident).Line(4, 0.3, px.Horizontal, id="bottom")
        sk.Line(0.2, 2, px.Perpendicular, id="right").Line(-4, 0.1, px.Perpendicular, id="top")
        sk.Close(px.Perpendicular, id="left")
        sk.Constrain("bottom", px.Distance, 4, id="w")
        sk.Constrain("bottom.start", "top", px.Distance, 2, id="ht")
        return sk, ("w", "ht")
    if family == "bend":
        sk.Move((0, 0), px.Fix).Line(2, 0, px.Horizontal, id="a").Arc(1, 90, px.Tangent, id="b") \
            .Line(1.5, px.Tangent, id="c")
        sk.Constrain("a", px.Distance, 2, id="la")
        sk.Constrain("b", px.Radius, 1, id="r")
        sk.Constrain("b", px.Angle, 90, id="sw")
        sk.Constrain("c", px.Distance, 1.5, id="lc")
        return sk, ("la", "r", "sw", "lc")
    sk.Move(sk.origin, px.Coincident).Line(3, 0, px.Horizontal, id="a").Line(-0.5, 3.8, id="b").Close(id="c")
    sk.Constrain("a", px.Distance, 3, id="A")
    sk.Constrain("b", px.Distance, 4, id="B")
    if family == "triangle":
        sk.Constrain("c", px.Distance, 5, id="C")
        return sk, ("A", "B", "C")
    sk.Constrain("a", "b", px.Angle, 97.5, id="phi")
    return sk, ("A", "B", "phi")


def turn(u, v):
    """the angle from direction u to direction v in degrees, in (-180, 180]"""
    return math.degrees(math.atan2(u[0] * v[1] - u[1] * v[0], u[0] * v[0] + u[1] * v[1]))


def sketch_points(sk):
    return {p: sk.Location(p) for p in sk.elements["Point"]}


def check_sketch(spec):
    """the solved sketch meets every dimension within what the solver's residual allows (SOLVE_TOL x the size, for an
    angle over the shortest line); values without a solution raise and change nothing"""
    family, *values = spec
    sk, ids = sketch_of(family)
    if family == "triangle":
        slack = sum(values) - 2 * max(values)
        assume(abs(slack) > 0.05 * max(values))
        if slack < 0:
            before = (sketch_points(sk), dict(sk.dimensions))
            with pytest.raises(ValueError):
                for name, v in zip(ids, values):
                    sk[name] = v
                sk.Update()
            assert (sketch_points(sk), dict(sk.dimensions)) == before
            return
    for name, v in zip(ids, values):
        sk[name] = v
    sk.Update()
    geo = {e: sk.Geometry(e) for kind in ("Line", "Arc") for e in sk.elements.get(kind, [])}
    points = list(sketch_points(sk).values())
    radii = [g["r"] for g in geo.values() if "r" in g]
    scale = max([1.0] + [abs(c) for p in points for c in p] + radii)
    tol = 10 * SOLVE_TOL * scale
    shortest = min(g["length"] for g in geo.values() if "length" in g)
    turn_tol = math.degrees(tol / min([shortest] + radii))
    value = dict(zip(ids, values))

    def length(e):
        (x0, y0), (x1, y1) = sk.Location(e + ".start"), sk.Location(e + ".end")
        return math.hypot(x1 - x0, y1 - y0)

    def direction(e):
        (x0, y0), (x1, y1) = sk.Location(e + ".start"), sk.Location(e + ".end")
        return (x1 - x0, y1 - y0)

    def near(got, expected, t, what):
        assert abs(got - expected) <= t, (spec, what, got, expected, t)

    if family == "rectangle":
        near(length("bottom"), value["w"], tol, "w")
        (px0, py0), (tx, ty), (dx, dy) = sk.Location("bottom.start"), sk.Location("top.start"), direction("top")
        near(abs(dx * (py0 - ty) - dy * (px0 - tx)) / math.hypot(dx, dy), value["ht"], tol, "ht")
        off = turn((1, 0), direction("bottom")) % 180
        near(min(off, 180 - off), 0, turn_tol, "horizontal")
        for p, q in (("bottom", "right"), ("right", "top"), ("top", "left"), ("left", "bottom")):
            near(abs(turn(direction(p), direction(q))), 90, turn_tol, ("perpendicular", p, q))
    elif family == "bend":
        near(length("a"), value["la"], tol, "la")
        near(length("c"), value["lc"], tol, "lc")
        c, s, e = sk.Location("b.center"), sk.Location("b.start"), sk.Location("b.end")
        for p in (s, e):
            near(math.hypot(p[0] - c[0], p[1] - c[1]), value["r"], tol, "r")
        near(turn((s[0] - c[0], s[1] - c[1]), (e[0] - c[0], e[1] - c[1])) % 360, value["sw"] % 360, turn_tol, "sw")
        for p, q in (("a", "b"), ("b", "c")):
            assert sk.Location(p + ".end") == pytest.approx(sk.Location(q + ".start"), abs=tol), (spec, "joined", p, q)
        near(turn(direction("a"), (-(s[1] - c[1]), s[0] - c[0])), 0, turn_tol, "tangent a b")
        near(turn((-(e[1] - c[1]), e[0] - c[0]), direction("c")), 0, turn_tol, "tangent b c")
    else:
        near(length("a"), value["A"], tol, "A")
        near(length("b"), value["B"], tol, "B")
        if family == "triangle":
            near(length("c"), value["C"], tol, "C")
        else:
            off = (turn(direction("a"), direction("b")) - value["phi"]) % 180
            near(min(off, 180 - off), 0, turn_tol, "phi")


def chord_params(points, closed):
    """the points' parameters of an interpolating spline: the summed chords, back to the start when closed"""
    loop = list(points) + list(points[:1]) if closed else list(points)
    t = [0.0]
    for a, b in zip(loop, loop[1:]):
        t.append(t[-1] + math.dist(a, b))
    return t


def quintic(points, closed, tangents, tensions, radii):
    """the exact quintic through the points: at(u, k), its k-th derivative at the chord parameter u"""
    def at(u, k=0):
        return np.array(px._planex._InterpolatedAt([tuple(p) for p in points], tangents, closed, u, k, style="quintic",
                                                    tensions=tensions, curvature_radii=radii))

    return at


@st.composite
def spline_specs(draw):
    """a quintic spline's points, open or closed; at some of them a tangent direction, alone, with a tension or with a
    signed curvature; and a turn, scale and shift of the whole"""
    closed = draw(st.booleans())
    n = draw(st.integers(3 if closed else 2, 7))
    points = tuple((draw(coordinate()), draw(coordinate())) for _ in range(n))
    setting = st.tuples(st.floats(-math.pi, math.pi), st.sampled_from(("tangent", "tension", "curvature")),
                        st.floats(0.3, 3.0), st.floats(-3.0, 3.0))
    settings = tuple(draw(st.one_of(st.none(), setting)) for _ in range(n))
    moved = (draw(st.floats(-math.pi, math.pi)), draw(st.floats(0.1, 10.0)), draw(coordinate(-5.0, 5.0)),
             draw(coordinate(-5.0, 5.0)))
    return (closed, points, settings, moved)


def spline_settings(settings):
    """the tangents, tensions and curvature radii of a spline spec's settings"""
    tangents, tensions, radii = {}, {}, {}
    for i, s in enumerate(settings):
        if s is None:
            continue
        angle, kind, tension, curvature = s
        tangents[i] = (math.cos(angle), math.sin(angle))
        if kind == "tension":
            tensions[i] = tension
        if kind == "curvature" and abs(curvature) >= 0.05:
            radii[i] = 1 / curvature
    return tangents, tensions, radii


def check_spline(spec):
    """the quintic through random points with random settings: through every point; the given tangent direction and
    signed curvature where set, a lone tangent as long as the plain spline's times its tension; smooth to the fourth
    derivative at a free point, the third at a tangent, the second at a curvature; an open end without a setting
    natural (third and fourth derivative zero); the reversed points the same curve backwards; the points turned,
    scaled and moved the curve turned, scaled and moved; px.Spline through every point with the curvature set"""
    closed, points, settings, (turn_by, scale, dx, dy) = spec
    n = len(points)
    t = chord_params(points, closed)
    chords = [b - a for a, b in zip(t, t[1:])]
    spread = max(math.dist(p, q) for p in points for q in points)
    apart = min(math.dist(p, q) for k, p in enumerate(points) for q in points[:k])
    assume(apart >= 0.05 and apart >= 0.1 * spread)
    tangents, tensions, radii = spline_settings(settings)
    at = quintic(points, closed, tangents, tensions, radii)
    knots, length, shortest = t[:n], t[-1], min(chords)
    size = max([1.0] + [abs(c) for p in points for c in p])
    for i, (u, p) in enumerate(zip(knots, points)):
        assert math.dist(at(u), p) <= 1e-10 * size, (spec, "through", i)
    for i, d in tangents.items():
        x, y = at(knots[i], 1)
        speed = math.hypot(x, y)
        assert abs(d[0] * y - d[1] * x) <= 1e-9 * speed and d[0] * x + d[1] * y > 0, (spec, "tangent", i)
        if i in radii:
            ax, ay = at(knots[i], 2)
            assert (x * ay - y * ax) / speed ** 3 == pytest.approx(1 / radii[i], rel=1e-8), (spec, "curvature", i)
    if len(tangents) == 1 and not radii:
        (i, _), = tangents.items()
        plain = float(np.linalg.norm(quintic(points, closed, {}, {}, {})(knots[i], 1)))
        assert float(np.linalg.norm(at(knots[i], 1))) == pytest.approx(tensions.get(i, 1.0) * plain, rel=1e-9), \
            (spec, "tangent length", i)
    h = 1e-9 * shortest
    # the jump of a k-th derivative over 2 h, as a fraction of the shortest chord's own scale of it: about 1e-7 where it
    # is continuous, 10 and more where it jumps
    for i in (range(n) if closed else range(1, n - 1)):
        smooth = 2 if i in radii else 3 if i in tangents else 4
        for k in range(1, smooth + 1):
            before, after = (at(length - h, k), at(h, k)) if i == 0 else (at(knots[i] - h, k), at(knots[i] + h, k))
            assert float(np.linalg.norm(after - before)) * shortest ** (k - 1) <= 1e-4, (spec, "smooth", i, k)
    if not closed:
        for i in (0, n - 1):
            if i not in tangents:
                for k in (3, 4):
                    end = float(np.linalg.norm(at(knots[i], k))) * shortest ** (k - 1)
                    assert end <= 1e-8, (spec, "natural end", i, k)
    order = [(n - j) % n for j in range(n)] if closed else list(range(n - 1, -1, -1))
    back = quintic([points[j] for j in order], closed,
                   {k: (-tangents[j][0], -tangents[j][1]) for k, j in enumerate(order) if j in tangents},
                   {k: tensions[j] for k, j in enumerate(order) if j in tensions},
                   {k: -radii[j] for k, j in enumerate(order) if j in radii})
    c, s = math.cos(turn_by), math.sin(turn_by)

    def moved(p):
        return np.array((scale * (c * p[0] - s * p[1]) + dx, scale * (s * p[0] + c * p[1]) + dy))

    there = quintic([tuple(moved(p)) for p in points], closed,
                    {i: (c * d[0] - s * d[1], s * d[0] + c * d[1]) for i, d in tangents.items()}, tensions,
                    {i: scale * r for i, r in radii.items()})
    far = scale * size + abs(dx) + abs(dy)
    for u in np.linspace(0.0, length, 23):
        assert math.dist(back(length - u), at(u)) <= 1e-9 * size, (spec, "reversed", u)
        assert math.dist(there(scale * u), moved(at(u))) <= 1e-9 * far, (spec, "moved", u)
    try:
        w = px.Spline([tuple(p) for p in points], periodic=closed, tangents=tangents or None, tensions=tensions or None,
                      curvature_radii=radii or None, style="quintic")
    except ValueError as e:
        import shapely

        path = shapely.set_precision(shapely.LineString([tuple(at(u)) for u in np.linspace(0.0, length, 2000)]),
                                     1e-9 * size)
        assert not path.is_simple, (spec, "px.Spline refused a curve that does not cross or touch itself", e)
        return
    for i, p in enumerate(points):
        m = w.Measure(at=p)
        assert m["distance"] <= 1e-9 * size, (spec, "px.Spline through", i)
        if i in radii:
            assert m["curvature"] == pytest.approx(1 / radii[i], rel=1e-7), (spec, "px.Spline curvature", i)


@st.composite
def sketch_spline_specs(draw):
    """a quintic spline tangent to the bend family's arc, its points drawn at random ahead of the arc's end, a curvature
    radius at its first drawn point; and new values of the line, the arc and the curvature"""
    steps = tuple((draw(st.floats(-1.5, 1.5)), draw(st.floats(0.3, 1.5))) for _ in range(draw(st.integers(2, 4))))
    curvature = st.one_of(st.floats(-2.0, -0.05), st.floats(0.05, 2.0))
    return (steps, draw(curvature), draw(st.floats(0.5, 4.0)), draw(st.floats(0.3, 2.0)), draw(st.floats(30.0, 150.0)),
            draw(curvature))


def sketch_spline_of(steps, curvature):
    """a fixed line, a tangent arc and a quintic spline tangent to it, drawn at the first values (2, 1, 90 degrees)"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(2, 0, px.Horizontal, id="a").Arc(1, 90, px.Tangent, id="b")
    points, y = [], 1.0
    for side, ahead in steps:
        y += ahead
        points.append((3 + side, y))
    sk.Spline(points, px.Tangent, id="s", style="quintic")
    sk.Constrain("a", px.Distance, 2, id="la")
    sk.Constrain("b", px.Radius, 1, id="r")
    sk.Constrain("b", px.Angle, 90, id="sw")
    sk.Constrain("s.p1", px.CurvatureRadius, 1 / curvature, id="kr")
    return sk, len(points)


def check_sketch_spline(spec):
    """the solved sketch meets its line, arc and curvature radius within the solver's residual, the spline runs on from
    the arc's end along its tangent and through every point it was given"""
    steps, curvature, la, r, sw, new_curvature = spec
    sk, count = sketch_spline_of(steps, curvature)
    sk.Update({"la": la, "r": r, "sw": sw, "kr": 1 / new_curvature})
    points = [sk.Location(f"s.p{k}") for k in range(count + 1)]
    scale = max([1.0, r] + [abs(v) for p in points for v in p])
    tol = 10 * SOLVE_TOL * scale
    turn_tol = math.degrees(tol / min(la, r))
    a0, a1 = sk.Location("a.start"), sk.Location("a.end")
    c, s, e = sk.Location("b.center"), sk.Location("b.start"), sk.Location("b.end")
    assert abs(math.dist(a0, a1) - la) <= tol, (spec, "la")
    for p in (s, e):
        assert abs(math.dist(p, c) - r) <= tol, (spec, "r")
    assert abs((turn((s[0] - c[0], s[1] - c[1]), (e[0] - c[0], e[1] - c[1])) - sw + 180) % 360 - 180) <= turn_tol, \
        (spec, "sw")
    assert math.dist(points[0], e) <= tol, (spec, "joined")
    start = sk.Measure("s", at=e)
    assert abs(turn((-(e[1] - c[1]), e[0] - c[0]), start["tangent_hv"])) <= turn_tol, (spec, "tangent")
    for k, p in enumerate(points):
        assert sk.Measure("s", at=p)["distance"] <= tol, (spec, "through", k)
    assert sk.Measure("s", at=points[1])["curvature"] == pytest.approx(new_curvature, rel=1e-7), (spec, "curvature")


def least_rectangle(hull):
    """the least area of a rectangle around a convex polygon (shapely's), one side along an edge (rotating calipers)"""
    pts = np.asarray(hull.exterior.coords)[:-1]
    best = math.inf
    for a, b in zip(pts, np.roll(pts, -1, axis=0)):
        u = (b - a) / np.linalg.norm(b - a)
        along, across = pts @ u, pts @ np.array([-u[1], u[0]])
        best = min(best, float((along.max() - along.min()) * (across.max() - across.min())))
    return best


def check_hull(spec):
    """ConvexHull covers the face, EnclosingCircle and EnclosingRectangle cover the hull; against shapely's convex hull
    and minimum bounding circle and the least rectangle around shapely's hull, within the sampling (a spline's hull
    also within its approximation)"""
    import shapely

    a = build(spec)
    g = valid(a)
    sample = 1e-6 * size(a)
    x0, y0, x1, y1 = g.bounds
    near = 4 * sample + (2 * px.config.spline_tol * math.hypot(x1 - x0, y1 - y0) if a.splines else 0.0)
    hull = a.Measure(px.measurements.ConvexHull())
    circle = a.Measure(px.measurements.EnclosingCircle())
    rect = a.Measure(px.measurements.EnclosingRectangle())
    curved = near * perimeter(hull) if a.splines else 0.0
    assert area(a - hull) <= area_tol(a, hull) + curved, (spec, "hull leaves out", area(a - hull))
    for name, f in (("circle", circle["face"]), ("rectangle", rect["face"])):
        assert area(hull - f) <= area_tol(hull, f) + curved, (spec, name + " leaves out", area(hull - f))
    theirs = shapely.convex_hull(g)
    assert abs(area(hull) - theirs.area) <= near * perimeter(hull) + area_tol(hull), \
        (spec, "hull area", area(hull), theirs.area)
    assert abs(circle["radius"] - shapely.minimum_bounding_radius(g)) <= near + 1e-12 * size(a), \
        (spec, "circle radius", circle["radius"], shapely.minimum_bounding_radius(g))
    length, width = rect["length"], rect["width"]
    assert length >= width and abs(area(rect["face"]) - length * width) <= area_tol(rect["face"]), (spec, "rectangle")
    assert abs(length * width - least_rectangle(theirs)) <= near * 2 * (length + width) + area_tol(hull), \
        (spec, "rectangle area", length * width, least_rectangle(theirs))


def transposed(matrix):
    return "".join(matrix[3 * j + i] for i in range(3) for j in range(3))


# the named relations as DE-9IM patterns of two faces (Crosses has none between areas)
PATTERNS = {"Equals": ["T*F**FFF*"], "IsDisjoint": ["FF*FF****"], "Contains": ["T*****FF*"], "IsWithin": ["T*F**F***"],
            "Covers": ["T*****FF*", "*T****FF*", "***T**FF*", "****T*FF*"],
            "IsCoveredBy": ["T*F**F***", "*TF**F***", "**FT*F***", "**F*TF***"],
            "Touches": ["FT*******", "F**T*****", "F***T****"], "Overlaps": ["T*T***T**"]}


def matches(matrix, pattern):
    return all(p == "*" or (p == "T" and m != "F") or p == m for m, p in zip(matrix, pattern))


def check_relate(spec):
    """the DE-9IM matrix of a pair: the other way round its transpose, every named relation as its pattern says, a face
    with itself equal; for straight-edged faces apart or the same, shapely's matrix of the same vertices"""
    import shapely

    a, b = build_pair(spec)
    m = a.Relate(b)
    assert b.Relate(a) == transposed(m), (spec, "transposed", m, b.Relate(a))
    for name, patterns in PATTERNS.items():
        assert getattr(a, name)(b) == any(matches(m, p) for p in patterns), (spec, name, m)
    assert a.Intersects(b) == (not a.IsDisjoint(b)), (spec, "Intersects", m)
    if spec[1] == "same":
        assert m == "2FFF1FFF2", (spec, "same", m)
    if spec[1] in ("free", "same") and spec[0][0] in STRAIGHT and spec[2][0] in STRAIGHT:
        ga, gb = shapely_of(a, 1e-6 * size(a)), shapely_of(b, 1e-6 * size(b))
        # a free pair can still have a vertex within rounding of the other's boundary (two stars of one outline):
        # there shapely's exact arithmetic and planex's tolerance differ (shapely's distance may even round to 0), so
        # only pairs apart or meeting at the very same vertices count
        tol = 1e-9 * size(a, b)

        def near(x, y, g):
            corners = set(vertices(y))
            return any(p not in corners and d <= tol
                       for p, d in zip(vertices(x), shapely.distance(shapely.points(vertices(x)), g.boundary)))

        if not (near(a, b, gb) or near(b, a, ga)):
            theirs = shapely.relate(ga, gb)
            assert m == theirs, (spec, "shapely", m, theirs)


@st.composite
def band_specs(draw):
    """an open polyline running to the right (so it never crosses itself) and an offset distance"""
    n = draw(st.integers(2, 6))
    x, points = draw(coordinate()), []
    for _ in range(n):
        points.append((x, draw(coordinate())))
        x += draw(st.floats(0.2, 1.0))
    return (tuple(points), draw(st.floats(0.02, 0.3)))


def check_band_thickness(spec):
    """a polyline offset by d to both sides, with round corners and ends, is the union of discs of radius d around it:
    its thinnest wall is 2 d, wherever the band runs straight or round its ends"""
    points, d = spec
    s = px.QuickSketch().Move(points[0])
    for p in points[1:]:
        s.Line(p)
    band = px.Shape([s.Wire().Offset(d, True, px.Round, px.Round)])
    got = band.Measure(px.measurements.MinThickness())
    assert abs(got - 2 * d) <= 1e-9 * size(band), (spec, "thinnest", got, 2 * d)


def tol_of(*objs):
    """Tol(S) of the objects together"""
    return px.config.k * px.config.eps * size(*objs)


def two_within(pts, tol):
    """two different points of pts within tol and within the Tol(S) of their own coordinates of each other (their
    distance and the points), None if there are none: by cells of size tol"""
    cells = {}
    for k, (x, y) in enumerate(pts):
        cells.setdefault((math.floor(x / tol), math.floor(y / tol)), []).append(k)
    for (i, j), ks in cells.items():
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for m in cells.get((i + di, j + dj), ()):
                    for k in ks:
                        d = math.dist(pts[k], pts[m])
                        if 0.0 < d <= tol and d <= local_tol(pts[k], pts[m]):
                            return d, tuple(pts[k]), tuple(pts[m])
    return None


def ends(obj):
    return np.array([(v.x, v.y) for e in obj.edges for v in (e.start, e.end)]).reshape(-1, 2)


def local_tol(*points):
    """Tol(S) of the points' own coordinates, as planex takes it for an edge"""
    return px.config.k * px.config.eps * max([1.0] + [abs(c) for p in points for c in p])


def clean(obj, what):
    """a result as the basic rule has it, each edge at the Tol(S) of its own coordinates: no edge shorter than that
    (by its chord, as planex measures an edge it would drop; a closed one by its length),
    the ends of its edges the same point or more than tol apart, every face with area, and glued again (its edges cut
    against each other) the same edges and faces"""
    es = list(obj.edges)
    if not es:
        return
    tol = tol_of(obj)
    short = [(math.dist((e.start.x, e.start.y), (e.end.x, e.end.y)) or e.mass,
              local_tol((e.start.x, e.start.y), (e.end.x, e.end.y))) for e in es]
    worst = min(short, key=lambda x: x[0] / x[1])
    assert worst[0] > worst[1], (what, "an edge under its tol", worst)
    lengths = np.array([e.mass for e in es])
    close = two_within(ends(obj), tol)
    assert close is None or close[0] > local_tol(close[1], close[2]), (what, "two ends within tol", close)
    assert all(f.mass > 0.0 for f in obj.faces), (what, "a face without area")
    recut = px.Glue([obj])
    assert len(recut.edges) == len(es) and len(recut.faces) == len(obj.faces), \
        (what, "glued again", len(es), len(recut.edges), len(obj.faces), len(recut.faces))
    assert abs(sum(e.mass for e in recut.edges) - lengths.sum()) <= tol * len(es), (what, "glued again, longer")


OPS = (("a + b", lambda x, y: x + y), ("a * b", lambda x, y: x * y), ("a - b", lambda x, y: x - y),
       ("b - a", lambda x, y: y - x))


def check_clean(spec):
    a, b = build_pair(spec)
    for name, op in OPS:
        clean(op(a, b), name)


@st.composite
def triples(draw):
    """a pair and a third shape in a relation of its own to the pair's first"""
    return (draw(pairs()), draw(st.sampled_from(RELATIONS)), draw(shapes()), draw(st.integers(0, 64)))


def check_chains(spec):
    """three shapes and results as inputs: the union associative, the common part distributive over it, a difference by
    a union two differences, a shape split by another put back together, a repeated operation the same"""
    pair = spec[0]
    a, b = build_pair(pair)
    _, c = build_pair((pair[0],) + spec[1:])
    tol = 4 * area_tol(a, b, c)

    def same(x, y, what):
        assert abs(area(x) - area(y)) <= tol, (what, area(x), area(y), tol)

    ab = a + b
    abc = ab + c
    same(abc, a + (b + c), "(a + b) + c")
    same(a * (b + c), a * b + a * c, "a * (b + c)")
    same(a - (b + c), (a - b) - c, "a - (b + c)")
    back = (a - b) + a * b
    assert area(back - a) <= tol and area(a - back) <= tol, ("(a - b) + a * b", area(back - a), area(a - back), tol)
    same(ab + b, ab, "(a + b) + b")
    same((a * b) * b, a * b, "(a * b) * b")
    clean(abc, "(a + b) + c")
    clean(back, "(a - b) + a * b")


@st.composite
def moved_pairs(draw):
    """a pair and a similarity: scaled by 10^-3 ... 10^3, turned, maybe mirrored, moved 10^0 ... 10^6 from the
    origin"""
    return (draw(pairs()), draw(st.integers(-3, 3)), draw(st.floats(0.0, 2 * math.pi)), draw(st.booleans()),
            draw(st.integers(0, 6)), draw(st.floats(0.0, 2 * math.pi)))


def similar(obj, scale, turn, mirror, far, heading):
    o = obj.Scaled((0.0, 0.0), 10.0 ** scale).Rotated((0.0, 0.0), math.degrees(turn))
    if mirror:
        o = o.Mirrored((0.0, 0.0), (math.cos(turn), math.sin(turn)))
    return o.Translated((10.0 ** far * math.cos(heading), 10.0 ** far * math.sin(heading)))


def check_moved(spec):
    """T(a) op T(b) is T(a op b) for a similarity T: as much area, scaled, and no farther apart than the tolerances of
    both places allow"""
    pair, *t = spec
    a, b = build_pair(pair)
    ta, tb = similar(a, *t), similar(b, *t)
    squared = 10.0 ** (2 * t[0])
    tol = area_tol(ta, tb) + squared * area_tol(a, b)
    for name, op in OPS:
        here, there = op(a, b), op(ta, tb)
        assert abs(area(there) - squared * area(here)) <= tol, (name, area(there), squared * area(here), tol)
        if here.faces and there.faces:
            moved = similar(here, *t)
            off = area(moved - there) + area(there - moved)
            assert off <= 2 * tol, (name, "apart", off, tol)
        clean(there, name)


@st.composite
def crowds(draw):
    """5 ... 50 discs or rounded squares on a turned grid whose spacing makes neighbours touch, overlap or stand apart
    by 10^-3 ... 10^-16, or 5 ... 30 random shapes on top of each other"""
    if draw(st.booleans()):
        return ("grid", draw(st.integers(5, 50)), draw(st.sampled_from(("disc", "rounded"))), draw(st.integers(0, 64)),
                draw(st.floats(0.0, 2 * math.pi)))
    return ("heap", tuple(draw(st.lists(shapes(), min_size=5, max_size=30))))


def crowd(spec):
    if spec[0] == "heap":
        parts = [build(s) for s in spec[1]]
        for p in parts:
            valid(p)
        return parts
    _, n, kind, pick, turn = spec
    side = math.ceil(math.sqrt(n))
    spacing = 0.75 if pick % 5 == 4 else 1.0 + nudge(pick)
    parts = []
    for i in range(n):
        x, y = (i % side) * spacing, (i // side) * spacing
        if kind == "disc":
            f = px.Circle((x, y), 0.5).Face()
        else:
            f = px.Move((x - 0.4, y - 0.4)).Rectangle(0.8, 0.8).Face().Offset(0.1, inplace=False)
        parts.append(f.Rotated((0.0, 0.0), math.degrees(turn)))
    return parts


def check_crowd(spec):
    """px.Unify of many shapes: within a time limit, as the operations one by one and as shapely's union, the same
    multithreaded; px.Glue's faces cover it once; clean"""
    import shapely

    parts = crowd(spec)
    tol = area_tol(*parts) * len(parts)
    start = time.perf_counter()
    u = px.Unify(parts, multithreaded=False)
    took = time.perf_counter() - start
    assert took <= 2.0 + 0.2 * len(parts), ("slow", len(parts), took)
    folded = parts[0]
    for p in parts[1:]:
        folded = folded + p
    assert abs(area(u) - area(folded)) <= tol, ("one by one", area(u), area(folded), tol)
    threaded = px.Unify(parts, multithreaded=True)
    assert abs(area(threaded) - area(u)) <= tol, ("multithreaded", area(threaded), area(u), tol)
    assert len(threaded.edges) == len(u.edges), ("multithreaded edges", len(threaded.edges), len(u.edges))
    glued = px.Glue(parts)
    assert abs(sum(f.mass for f in glued.faces) - area(u)) <= tol, ("glue", sum(f.mass for f in glued.faces), area(u))
    sample = 1e-6 * size(*parts)
    theirs = shapely.union_all([shapely_of(p, sample) for p in parts])
    off = shapely.symmetric_difference(shapely_of(u, sample), theirs).area
    assert off <= tol + 4 * sample * sum(perimeter(p) for p in parts), ("shapely", off)
    clean(u, "unify")


def outline(obj):
    return sorted((e.start.x, e.start.y, e.end.x, e.end.y, e.mass) for e in obj.edges)


def off_outline(points, obj):
    """how far each point lies from obj's outline"""
    return np.min([e.Distance(points) for e in obj.edges], axis=0)


def check_same_answer(spec, by_order=True):
    """an operation done twice gives the same edges to the last bit; b + a and b * a as many edges and faces as a + b
    and a * b, each vertex on the other's outline within four tol (a crossing at a small angle may lie elsewhere along
    the edges, by its rounding over the angle's sine)"""
    a, b = build_pair(spec)
    tol = 4 * tol_of(a, b)
    for name, op in OPS:
        assert outline(op(a, b)) == outline(op(a, b)), (name, "twice")
    if not by_order:
        return
    for name, x, y in (("a + b", a + b, b + a), ("a * b", a * b, b * a)):
        assert (len(x.edges), len(x.faces)) == (len(y.edges), len(y.faces)), \
            (name, "edges and faces", len(x.edges), len(y.edges), len(x.faces), len(y.faces))
        if x.edges:
            off = max(off_outline(ends(y), x).max(), off_outline(ends(x), y).max())
            assert off <= tol, (name, "a vertex off the other's outline", off, tol)


@st.composite
def offset_inputs(draw):
    """a shape and an offset distance: 10^-3 ... 10^-16 or 5 % of its size, outward or inward"""
    return (draw(shapes()), draw(st.integers(2, 16)), draw(st.booleans()))


def check_offset_inputs(spec):
    """a shape against its offset: outward the offset holds it, inward it holds the offset; the area changes by no more
    than d along the outline and round its corners, and a spline's fitted offset by up to spline_tol along it
"""
    shape, k, outward = spec
    a = build(shape)
    valid(a)
    d = 0.05 * shape[1][5] if k == 2 else 10.0 ** -k
    o = a.Offset(d if outward else -d, inplace=False)
    tol = area_tol(a, o) if o.edges else area_tol(a)
    inner, outer = (a, o) if outward else (o, a)
    assert area(inner - outer) <= tol, ("inner outside the outer", area(inner - outer), tol)
    assert abs(area(inner + outer) - area(outer)) <= tol, ("union", area(inner + outer), area(outer))
    assert abs(area(inner * outer) - area(inner)) <= tol, ("common", area(inner * outer), area(inner))
    grown = abs(area(o) - area(a))
    fitted = 2 * px.config.spline_tol * size(a) * perimeter(a) if any(isinstance(e.edge, px.SplineEdge) for e in a.edges) else 0.0
    assert grown <= abs(d) * (perimeter(a) + math.pi * abs(d) * len(a.edges)) + fitted + tol, ("the area changes by more than d along the outline", grown)


CURVES = ("line", "arc", "circle", "spline")
LINKS = ("free", "end on", "tangent", "small angle")


@st.composite
def curve_sets(draw):
    """3 ... 12 curves in a box: lines, arcs, circles and open splines, each free or drawn from the one before it:
    an end on it, tangent to it, or crossing it at an angle of 10^-3 ... 10^-15, exact or nudged"""
    n = draw(st.integers(3, 12))
    return tuple((draw(st.sampled_from(CURVES)), draw(st.sampled_from(LINKS)),
                  tuple(draw(st.lists(coordinate(), min_size=8, max_size=8))), draw(st.integers(0, 64)))
                 for _ in range(n))


def free_curve(kind, xs):
    p, q, m = (xs[0], xs[1]), (xs[2], xs[3]), (xs[4], xs[5])
    if kind == "line":
        assume(math.dist(p, q) > 0.05)
        return px.Line(p, q)
    r = 0.1 + 0.4 * abs(xs[6])
    if kind == "circle":
        return px.Circle(p, r)
    if kind == "arc":
        a0, a1 = xs[6], xs[6] + 0.5 + abs(xs[7])
        return px.Arc((m[0] + r * math.cos(a0), m[1] + r * math.sin(a0)), (m[0] + r * math.cos(a1), m[1] + r * math.sin(a1)), m)
    points = [p, m, q, (xs[6], xs[7])]
    assume(min(math.dist(u, v) for u, v in zip(points, points[1:])) > 0.1)
    try:
        return px.Spline(points)
    except ValueError:
        assume(False)


def linked_curve(prev, kind, link, xs, pick):
    """a line drawn from the curve before: from a point of it (nudged off by pick), tangent to it there, or through it
    at a small angle; a link to a line that would lie along it becomes a free curve"""
    e = prev.edges[0]
    t = 0.2 + 0.06 * ((pick * 7 + 3) % 11)
    p, d = e.PointAt(t), e.TangentAt(t)
    length = math.hypot(d.x, d.y)
    u = (d.x / length, d.y / length)
    n = (-u[1], u[0])
    shift = nudge(pick)
    at = (p.x + shift * n[0], p.y + shift * n[1])
    if link == "end on":
        far = (xs[0], xs[1])
        assume(math.dist(at, far) > 0.05)
        return px.Line(at, far)
    straight = type(e.edge).__name__ == "LineEdge"
    if link == "tangent" and straight:
        return free_curve(kind, xs)
    angle = 0.0 if link == "tangent" else 10.0 ** -(3 + pick % 13)
    v = (u[0] * math.cos(angle) - u[1] * math.sin(angle), u[0] * math.sin(angle) + u[1] * math.cos(angle))
    reach = 0.3 + abs(xs[2])
    start = at if link == "tangent" and pick % 2 else (at[0] - reach * v[0], at[1] - reach * v[1])
    return px.Line(start, (at[0] + reach * v[0], at[1] + reach * v[1]))


def loose_curves(spec):
    curves = []
    for kind, link, xs, pick in spec:
        if curves and link != "free":
            curves.append(linked_curve(curves[-1], kind, link, xs, pick))
        else:
            curves.append(free_curve(kind, xs))
    return curves


def check_loose_curves(spec, again=True):
    """px.Wires of loose curves: no edge shorter than tol, every piece on one of the curves, every curve covered by the
    pieces, and the pieces cut again the same pieces (every crossing is already a vertex). A spline's PointAt and
    midpoint lie on its approximation, within spline_tol x its box diagonal of the curve"""
    curves = loose_curves(spec)
    pieces = [e for w in px.Wires(curves) for e in w.edges]
    tol = tol_of(*curves)
    splines = any(kind == "spline" for kind, *_ in spec)
    near = 1e3 * tol + (3 * px.config.spline_tol * size(*curves) if splines else 0.0)
    lengths = np.array([e.mass for e in pieces])
    assert lengths.min() > tol, ("an edge under tol", lengths.min(), tol)
    mids = np.array([(e.midpoint.x, e.midpoint.y) for e in pieces])
    off = np.min([c.Distance(mids) for c in curves], axis=0)
    assert off.max() <= near, ("a piece off every curve", off.max())
    for c in curves:
        e = c.edges[0]
        samples = np.array([(q.x, q.y) for q in (e.PointAt(t) for t in np.linspace(0.0, 1.0, 21))])
        gap = np.min([pc.Distance(samples) for pc in pieces], axis=0)
        assert gap.max() <= near, ("a curve not covered", gap.max())
    if again:
        recut = [e for w in px.Wires(pieces) for e in w.edges]
        assert len(recut) == len(pieces), ("cut again", len(pieces), len(recut))


def regular(c, r, n, turn):
    return [(c[0] + r * math.cos(turn + 2 * math.pi * k / n), c[1] + r * math.sin(turn + 2 * math.pi * k / n))
            for k in range(n)]


def square(c, h):
    return px.Move((c[0] - h / 2, c[1] - h / 2)).Rectangle(h, h).Face()


def holey(star, extra):
    """a regular polygon with one to three holes: apart, two squares sharing a side or a corner, a disc touching the
    outline from inside, or a hole with an island in it"""
    cx, cy, rot, radii, gaps, scale = star
    n = len(radii)
    face = polygon(regular((cx, cy), scale, n, rot))
    h = 0.12 * scale
    mode = int(extra * 1000) % 5
    holes = 1 + int(extra * 10) % 3
    spots = [(cx + 0.25 * scale * math.cos(rot + gaps[0] + 2 * math.pi * j / holes),
              cy + 0.25 * scale * math.sin(rot + gaps[0] + 2 * math.pi * j / holes)) for j in range(holes)]
    if mode == 0:
        for p in spots:
            face = face - (square(p, h) if radii[0] > 0.6 else px.Circle(p, h / 2).Face())
    elif mode in (1, 2):
        p = spots[0]
        face = face - square(p, h) - square((p[0] + h, p[1] + (h if mode == 2 else 0.0)), h)
    elif mode == 3:
        inner = scale * math.cos(math.pi / n)
        normal = rot + math.pi / n
        r = h / 2
        face = face - px.Circle((cx + (inner - r) * math.cos(normal), cy + (inner - r) * math.sin(normal)), r).Face()
    else:
        face = face - px.Circle((cx, cy), h).Face() + px.Circle((cx, cy), h / 2).Face()
    return face


def sharp(star, extra):
    """a star polygon with a spike at its first point, its base 10^-3 ... 10^-12 of its size wide, or with a corner of at
    most 90 degrees cut off by an edge of eight Tol(S), its ends more than five Tol(S) off the neighbouring sides"""
    cx, cy, rot, radii, gaps, scale = star
    points = star_points(star)
    p = points[0]
    d = math.hypot(p[0] - cx, p[1] - cy)
    u = ((p[0] - cx) / d, (p[1] - cy) / d)
    t = (-u[1], u[0])
    k = int(extra * 1e4)
    if k % 2:
        n = len(points)
        for i in range(n):
            q, r, o = points[i - 1], points[i], points[(i + 1) % n]
            u1 = (q[0] - r[0], q[1] - r[1])
            u2 = (o[0] - r[0], o[1] - r[1])
            l1, l2 = math.hypot(*u1), math.hypot(*u2)
            angle = math.acos(max(-1.0, min(1.0, (u1[0] * u2[0] + u1[1] * u2[1]) / (l1 * l2))))
            if angle <= math.pi / 2:
                tol = px.config.k * px.config.eps * max([1.0] + [abs(c) for x in points for c in x])
                e = 8 * tol / (2 * math.sin(angle / 2))
                cut = [(r[0] + e * u1[0] / l1, r[1] + e * u1[1] / l1), (r[0] + e * u2[0] / l2, r[1] + e * u2[1] / l2)]
                return polygon(points[:i] + cut + points[i + 1:])
    w = scale * 10.0 ** -(3 + k % 10)
    tip = (p[0] + 0.6 * d * u[0], p[1] + 0.6 * d * u[1])
    base = [(p[0] - w / 2 * t[0], p[1] - w / 2 * t[1]), tip, (p[0] + w / 2 * t[0], p[1] + w / 2 * t[1])]
    return polygon(base + points[1:])


def slotted(star, extra):
    """a star polygon with a slot from its centre outwards, 10^-3 ... 10^-12 of its size wide"""
    cx, cy, rot, radii, gaps, scale = star
    w = scale * 10.0 ** -(3 + int(extra * 1e4) % 10)
    slot = px.Move((cx, cy - w / 2)).Rectangle(scale * (0.3 + extra), w).Face().Rotated((cx, cy), math.degrees(rot))
    return polygon(star_points(star)) - slot


def mixed(star, extra):
    """a star polygon 1000 times larger, a small star cut out at one corner and a small disc added at another"""
    cx, cy, rot, radii, gaps, scale = star
    big = (1000 * cx, 1000 * cy, rot, radii, gaps, 1000 * scale)
    points = star_points(big)
    small = polygon(star_points((points[0][0], points[0][1], rot, radii, gaps, scale)))
    return polygon(points) - small + px.Circle(points[1 % len(points)], scale * (0.3 + extra)).Face()


MADE = {"holey": holey, "sharp": sharp, "slotted": slotted, "mixed": mixed}


OPERATIONS = ("+", "-", "*", "offset", "move", "mirror", "scale")


def known_program(spec):
    """the sharp kind, whose 8 tol chamfer a program scales below what a clean input keeps"""
    return "sharp" in {s[0] for s in spec[0]}


@st.composite
def programs(draw):
    """a pool of two to four shapes and 5 ... 30 operations on it, each result added to the pool"""
    pool = tuple(draw(st.lists(shapes(BOOLEAN_KINDS), min_size=2, max_size=4)))
    n = draw(st.integers(5, 30))
    return (pool, tuple((draw(st.sampled_from(OPERATIONS)), draw(st.integers(0, 63)), draw(st.integers(0, 63)),
                         draw(st.integers(0, 64))) for _ in range(n)))


def check_program(spec):
    """a program of operations, every result an input of later ones: after each step a boolean as shapely's, an offset
    holding or held by its input, a move, mirror or scaling keeping the area (scaled); booleans and offsets clean"""
    shapes_, steps = spec
    pool = [build(s) for s in shapes_]
    for p in pool:
        valid(p)
    for step, (op, i, j, pick) in enumerate(steps):
        x, y = pool[i % len(pool)], pool[j % len(pool)]
        what = (step, op)
        vx, vy = vertices(x), vertices(y)
        if op in ("+", "-", "*"):
            r = x + y if op == "+" else x - y if op == "-" else x * y
            against_shapely(x, y, (spec, what))
        elif op == "offset":
            d = size(x) * (0.05 if pick % 3 == 0 else 10.0 ** -(3 + pick % 3))
            r = x.Offset(d if pick % 2 else -d, inplace=False)
            inner, outer = (x, r) if pick % 2 else (r, x)
            assert area(inner - outer) <= area_tol(x), (what, "offset not nested", area(inner - outer))
        elif op == "move":
            p, q = vx[pick % len(vx)], vy[(pick * 7 + 3) % len(vy)]
            r = x.Translated((q[0] - p[0], q[1] - p[1]))
            assert abs(area(r) - area(x)) <= area_tol(x, r), (what, area(r), area(x))
        elif op == "mirror":
            p, q = vx[pick % len(vx)], vx[(pick + 1) % len(vx)]
            if p == q:
                continue
            r = x.Mirrored(p, (q[0] - p[0], q[1] - p[1]))
            assert abs(area(r) - area(x)) <= area_tol(x, r), (what, area(r), area(x))
        else:
            f = (0.5, 2.0, 1.0 / 3.0)[pick % 3]
            r = x.Scaled(vx[pick % len(vx)], f)
            assert abs(area(r) - f * f * area(x)) <= area_tol(x, r), (what, area(r), f * f * area(x))
        if op in ("+", "-", "*", "offset"):
            clean(r, what)
        if r.faces:
            pool.append(r)


@st.composite
def grid_pairs(draw):
    """two unions of one to four rectangles with corners on a grid of step 1, 0.25, 0.1 or 1/3, the second turned by
    0, 90, 45 or 30 degrees about a grid point"""
    def rects():
        return tuple(draw(st.tuples(st.integers(0, 6), st.integers(0, 6), st.integers(1, 4), st.integers(1, 4)))
                     for _ in range(draw(st.integers(1, 4))))
    return (draw(st.sampled_from((1.0, 0.25, 0.1, 1.0 / 3.0))), rects(), rects(), draw(st.sampled_from((0, 90, 45, 30))),
            draw(st.integers(0, 6)), draw(st.integers(0, 6)))


def grid_shape(step, rects):
    faces = [px.Move((x * step, y * step)).Rectangle(w * step, h * step).Face() for x, y, w, h in rects]
    shape = faces[0]
    for f in faces[1:]:
        shape = shape + f
    return shape


def check_grid(spec):
    """corners on a grid: corners on corners, edges along edges, corners on edges; the identities, shapely exact for
    straight edges, clean results"""
    step, ra, rb, turn, gx, gy = spec
    a = grid_shape(step, ra)
    b = grid_shape(step, rb).Rotated((gx * step, gy * step), turn)
    identities(a, b)
    against_shapely(a, b, spec, straight=True)
    for name, op in OPS:
        clean(op(a, b), name)


@st.composite
def circle_polygons(draw):
    """a disc and a regular polygon of 3 ... 1000 corners on its circle or around it, maybe nudged out or in by
    10^-3 ... 10^-16 of the radius"""
    return (draw(st.floats(0.1, 10.0)), draw(st.sampled_from((3, 4, 5, 6, 8, 12, 24, 64, 100, 360, 1000))),
            draw(st.booleans()), draw(st.floats(0.0, 2 * math.pi)),
            draw(coordinate()), draw(coordinate()), draw(st.integers(0, 64)))


def check_circle_polygon(spec):
    """a disc against its inscribed polygon (every corner on the circle) or its circumscribed one (every side touching
    it): exact areas, or shapely's when nudged; clean results"""
    r, n, inscribed, phase, cx, cy, pick = spec
    nudged = nudge(pick)
    radius = (r if inscribed else r / math.cos(math.pi / n)) * (1.0 + nudged)
    a = px.Circle((cx, cy), r).Face()
    b = polygon(regular((cx, cy), radius, n, phase))
    if nudged == 0.0:
        disc, poly = math.pi * r * r, area(b)
        inner = poly if inscribed else disc
        same_areas(a, b, inner, area(b) - inner)
    else:
        identities(a, b)
        against_shapely(a, b, spec)
    for name, op in OPS:
        clean(op(a, b), name)


def same_areas(a, b, common, b_only):
    tol = area_tol(a, b)
    A = area(a)
    for name, got, want in (("a + b", a + b, A + b_only), ("a * b", a * b, common), ("a - b", a - b, A - common),
                            ("b - a", b - a, b_only)):
        assert abs(area(got) - want) <= tol, (name, area(got), want, tol)


@st.composite
def cut_faces(draw):
    """a face and a polyline of one to four segments across its box, or two polylines"""
    return (draw(shapes(("star", "rounded", "disc", "holed", "holey"))),
            tuple(draw(st.tuples(st.floats(-0.2, 1.2), st.floats(-0.2, 1.2))) for _ in range(draw(st.integers(2, 5)))),
            tuple(draw(st.tuples(st.floats(-0.2, 1.2), st.floats(-0.2, 1.2))) for _ in range(draw(st.integers(2, 5)))))


def across(face, points):
    """the polyline through the points given as fractions of the face's box, rounded to thousandths: two polylines
    then lie on each other exactly or apart, never within rounding (where shapely's exact union differs)"""
    (x0, y0), (x1, y1) = face.bounding_box
    pts = [(x0 + round(u, 3) * (x1 - x0), y0 + round(v, 3) * (y1 - y0)) for u, v in points]
    assume(all(math.dist(p, q) > 1e-3 * (x1 - x0 + y1 - y0) for p, q in zip(pts, pts[1:])))
    s = px.QuickSketch().Move(pts[0])
    for p in pts[1:]:
        s.Line(p)
    try:
        return s.Wire(), pts
    except ValueError:
        assume(False)


def check_wire_cuts(spec):
    """a face and a polyline: px.Glue and Split keep the area and give as many faces, as many as shapely's split where
    the polyline's ends lie off the outline (GEOS may not split at a line ending on it); two polylines united as long
    as shapely's union of the lines (a polyline that runs back over itself is one line, as the sketch cuts it)"""
    import shapely
    from shapely.ops import split as cut_by

    shape, first, second = spec
    f = build(shape)
    g = valid(f)
    w, pts = across(f, first)
    glued = px.Glue([f, w])
    tol = area_tol(f) + 4 * 1e-6 * size(f) * perimeter(f)
    assert abs(sum(x.mass for x in glued.faces) - area(f)) <= tol, ("glue area", sum(x.mass for x in glued.faces), area(f))
    pieces = f.Split(w)
    assert abs(sum(p.mass for p in pieces) - area(f)) <= tol, ("split area", sum(p.mass for p in pieces), area(f))
    assert sum(len(p.faces) for p in pieces) == len(glued.faces), \
        ("split and glue", [len(p.faces) for p in pieces], len(glued.faces))
    if min(g.boundary.distance(shapely.Point(p)) for p in (pts[0], pts[-1])) > 1e-9 * size(f):
        theirs = sorted(p.area for part in shapely.get_parts(g)
                        for p in shapely.get_parts(cut_by(part, shapely.LineString(pts))))
        ours = sorted(x.mass for x in glued.faces)
        big = [a for a in theirs if a > 1e3 * tol]
        assert len([a for a in ours if a > 1e3 * tol]) == len(big), ("glue pieces", ours, theirs)
    v, qts = across(f, second)
    both = w + v
    length = shapely.union_all([shapely.LineString(pts), shapely.LineString(qts)]).length
    assert abs(sum(e.mass for e in both.edges) - length) <= 1e-9 * size(f), ("united wires", sum(e.mass for e in both.edges), length)


def collapsing(shape, tol):
    """the length of the spike or slot of a shape spec no wider than a few tol, which may collapse at that tol (a needle
    thinner than tol collapses, the rest stays), else 0"""
    kind, star, extra = shape
    k = int(extra * 1e4)
    if kind not in ("sharp", "slotted") or (kind == "sharp" and k % 2):
        return 0.0
    cx, cy, scale = star[0], star[1], star[5]
    if scale * 10.0 ** -(3 + k % 10) > 4 * tol:
        return 0.0
    if kind == "slotted":
        return scale * (0.3 + extra)
    p = star_points(star)[0]
    return 0.6 * math.hypot(p[0] - cx, p[1] - cy)


def check_measures(spec):
    """the booleans and the measurements agree: away from the boundaries, a + b's signed distance is the least of a's
    and b's outside both, a * b's the largest inside both, a - b's max(d_a, -d_b) inside a and outside b (but for a spike
    or slot thinner than the pair's tol, which may collapse); Contains as the sign says; the relations as the areas say"""
    a, b = build_pair(spec)
    tol = tol_of(a, b)
    rng = np.random.default_rng(spec[3])
    (x0, y0), (x1, y1) = (a + b).bounding_box
    pts = np.column_stack((rng.uniform(x0, x1, 60), rng.uniform(y0, y1, 60)))
    da, db = a.Distance(pts, signed=True), b.Distance(pts, signed=True)
    margin = 1e-6 * size(a, b)
    near = 1e3 * tol + max(collapsing(spec[0], tol), collapsing(spec[2], tol))
    for name, r, where, want in (("a + b", a + b, (da > margin) & (db > margin), np.minimum(da, db)),
                                 ("a * b", a * b, (da < -margin) & (db < -margin), np.maximum(da, db)),
                                 ("a - b", a - b, (da < -margin) & (db > margin), np.maximum(da, -db))):
        if not where.any() or not r.faces:
            continue
        got = r.Distance(pts[where], signed=True)
        assert np.abs(got - want[where]).max() <= near, (name, "signed distance", np.abs(got - want[where]).max())
        sure = np.abs(got) > margin
        assert (r.Contains(pts[where][sure]) == (got[sure] < 0)).all(), (name, "Contains against the sign")
    big = 1e3 * area_tol(a, b)
    if area(a * b) > big:
        assert a.Intersects(b) and not a.IsDisjoint(b), ("intersects", area(a * b))
    if a.IsDisjoint(b):
        assert area(a * b) <= area_tol(a, b), ("disjoint", area(a * b))
    if a.Covers(b):
        assert area(b - a) <= area_tol(a, b), ("covers", area(b - a))
    if area(b - a) > big:
        assert not a.Covers(b), ("not covering", area(b - a))


SKETCHED = ("free", "corner on corner", "corner on edge", "turned about a corner", "half turn", "nudged shift")


@st.composite
def sketch_pairs(draw):
    """two solved sketches (rectangle, triangle, side-angle-side) and how the second is placed against the first"""
    def closed():
        spec = draw(sketch_specs())
        assume(spec[0] != "bend")
        return spec
    return (closed(), closed(), draw(st.sampled_from(SKETCHED)), draw(st.integers(0, 64)))


def sketched(spec):
    family, *values = spec
    sk, ids = sketch_of(family)
    if family == "triangle":
        assume(sum(values) - 2 * max(values) > 0.05 * max(values))
    for name, v in zip(ids, values):
        sk[name] = v
    sk.Update()
    shape = sk.Shape()
    assume(len(shape.faces) > 0)
    return shape


def placed(a, b, relation, pick):
    """b placed against a as the relation says (the objects' own corners and edges)"""
    va, vb = vertices(a), vertices(b)
    t, s = va[pick % len(va)], vb[(pick * 5 + 1) % len(vb)]
    if relation == "free":
        return b
    if relation == "corner on corner":
        return b.Translated((t[0] + nudge(pick) - s[0], t[1] - s[1]))
    if relation == "corner on edge":
        p, n = normal_at(a, a.edges[pick % len(a.edges)], 0.2 + 0.06 * ((pick * 7 + 3) % 11))
        return b.Translated((p[0] + nudge(pick) * n[0] - s[0], p[1] + nudge(pick) * n[1] - s[1]))
    if relation == "turned about a corner":
        return a.Rotated(t, 10.0 + 340.0 * ((pick * 37) % 64) / 64)
    if relation == "half turn":
        return a.Rotated(t, 180)
    tiny = 10.0 ** -(3 + pick % 13)
    return a.Translated((tiny, tiny))


def check_sketch_pair(spec):
    """shapes from solved sketches, the second placed against the first: the identities, as shapely's, clean"""
    a = sketched(spec[0])
    b = placed(a, sketched(spec[1]), spec[2], spec[3])
    identities(a, b)
    against_shapely(a, b, spec, straight=True)
    for name, op in OPS:
        clean(op(a, b), name)
