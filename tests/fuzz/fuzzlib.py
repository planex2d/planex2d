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
RELATIONS = ("free", "translated", "half turn", "mirrored", "same", "edge shift", "nudged turn", "nudged shift")


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
def pairs(draw, kinds=KINDS):
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
    a, b = build_pair(spec)
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
    sample = 1e-6 * size(a, b)
    # shapely's overlay is not robust for edges one rounding unit apart (GEOS 3.13.1 returned one operand as the union
    # of a polygon and its mirror copy across a shared edge); snap rounding on a tiny grid is
    grid = 1e-12 * size(a, b)
    tol = area_tol(a, b) + 4 * (sample + grid) * (perimeter(a) + perimeter(b))
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
