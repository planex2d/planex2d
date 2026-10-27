# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Parametric curves in the sketches (sk.Curve) and standalone (px.Curve): the fit to the formula, the refit when a
dimension changes, h and v in each other, placement, the rigid body under constraints, closed curves, callables,
the reserved ids, copies and what downstream calls do with a curve."""
import math
import random

import numpy as np
import pytest

import planex as px

DEGREE = math.pi / 180.0
INVOLUTE = ("rb*(cos(t) + t*pi/180*sin(t))", "rb*(sin(t) - t*pi/180*cos(t))")


def involute(rb, t):
    return rb * (math.cos(t * DEGREE) + t * DEGREE * math.sin(t * DEGREE)), rb * (math.sin(t * DEGREE) - t * DEGREE * math.cos(t * DEGREE))


def involute_derivative(rb, t):
    """d/dt of the involute, t in degrees"""
    return rb * t * DEGREE * DEGREE * math.cos(t * DEGREE), rb * t * DEGREE * DEGREE * math.sin(t * DEGREE)


def de_boor(knots, degree, poles, u):
    """the point of the B-spline at u"""
    n = len(poles)
    k = degree
    while k < n - 1 and u >= knots[k + 1]:
        k += 1
    d = [np.array(poles[j + k - degree], dtype=float) for j in range(degree + 1)]
    for r in range(1, degree + 1):
        for j in range(degree, r - 1, -1):
            low = knots[j + k - degree]
            span = knots[j + 1 + k - r] - low
            a = 0.0 if span == 0 else (u - low) / span
            d[j] = (1.0 - a) * d[j - 1] + a * d[j]
    return d[degree]


def differentiated(knots, degree, poles):
    """the derivative of a B-spline as a B-spline"""
    q = []
    for i in range(len(poles) - 1):
        span = knots[i + degree + 1] - knots[i + 1]
        q.append(degree * (np.array(poles[i + 1], dtype=float) - np.array(poles[i], dtype=float)) / span if span else np.zeros(2))
    return knots[1:-1], degree - 1, q


def at(b, u, k=0):
    """the k-th derivative of the B-spline dict b (edge.bspline) at u"""
    knots, degree, poles = list(b["knots"]), b["degree"], b["poles"]
    for _ in range(k):
        knots, degree, poles = differentiated(knots, degree, poles)
    return de_boor(knots, degree, poles, u)


def spline_of(wire):
    edges = list(wire.edges)
    assert len(edges) == 1
    return edges[0].edge.bspline


def size_of(points):
    p = np.array(points, dtype=float)
    return float(np.hypot(*(p.max(axis=0) - p.min(axis=0))))


def sketch_with_rb(value=10.0):
    sk = px.Sketch()
    sk.Move((0, -20)).Line(value, 0, construction=True, id="ref")
    sk.Constrain("ref", px.Distance, value, id="rb")
    sk.Move((0, 0))
    return sk


@pytest.mark.parametrize("make", [px.Sketch, px.QuickSketch])
def test_v_of_h_runs_from_its_start_to_its_end_and_the_pen_goes_there(make):
    sk = make()
    sk.Curve(("t", "0.1*t^2"), (0, 10))
    assert sk.elements["Curve"] == ["Curve.1"]
    assert sk.Location("Curve.1.start") == pytest.approx((0, 0), abs=1e-12)
    assert sk.Location("Curve.1.end") == pytest.approx((10, 10), abs=1e-12)
    assert sk.Wire("Curve.1").Distance((5, 2.5)) <= 1e-12
    assert sk.pen.position == pytest.approx((10, 10), abs=1e-12)


def test_the_fit_is_a_quintic_c2_spline_on_the_formula():
    sk = sketch_with_rb()
    sk.Curve(INVOLUTE, (0, 40))
    w = sk.Wire("Curve.1")
    b = spline_of(w)
    assert b["degree"] == 5 and b["weights"] is None
    knots = b["knots"]
    inner = sorted(set(knots[6:-6]))
    assert inner and all(knots.count(u) <= 3 for u in inner)
    points = [involute(10, t) for t in np.linspace(0, 40, 101)]
    size = size_of(points)
    rng = random.Random(184)
    for t in (rng.uniform(0, 40) for _ in range(200)):
        assert w.Distance(involute(10, t)) <= 1e-12 * size
    # the samples are the knots: u = (t - t0) * L / (t1 - t0); there the derivative is the formula's
    length = knots[-1]
    s = length / 40.0
    scale = max(math.hypot(*involute_derivative(10, t)) for t in np.linspace(0, 40, 41))
    for u in sorted(set(knots)):
        t = u / s
        assert at(b, u) == pytest.approx(involute(10, t), abs=1e-12 * size)
        assert at(b, u, 1) * s == pytest.approx(involute_derivative(10, t), abs=1e-9 * scale)


def test_a_new_dimension_refits_the_curve():
    sk = sketch_with_rb()
    sk.Curve(INVOLUTE, (0, 40))
    sk["rb"] = 12
    sk.Update()
    assert sk.Location("Curve.1.end") == pytest.approx(involute(12, 40), abs=1e-12 * 12)
    w = sk.Wire("Curve.1")
    for t in np.linspace(0, 40, 17):
        assert w.Distance(involute(12, t)) <= 1e-12 * 12
    sk.Update({"rb": 10})
    assert sk.Location("Curve.1.end") == pytest.approx(involute(10, 40), abs=1e-12 * 10)


def test_a_driven_dimension_or_a_measure_raises_naming_why():
    sk = sketch_with_rb()
    sk.Move((0, -30)).Line(5, 0, construction=True, id="other")
    sk.Constrain("other", px.Distance, 5, id="d")
    sk.Expression("d = rb/2")
    with pytest.raises(ValueError, match="'d' is driven by Expression.1"):
        sk.Curve(("d*cos(t)", "d*sin(t)"), (0, 90))
    with pytest.raises(ValueError, match="'ref.length' is the length of ref, a measure of the solved geometry"):
        sk.Curve(("ref.length*t", "t"), (0, 1))
    sk.Constrain("other", px.Distance, id="measured", reference=True)
    with pytest.raises(ValueError, match="'measured' is a reference dimension"):
        sk.Curve(("measured*t", "t"), (0, 1))
    assert "Curve" not in sk.elements


def test_a_dimension_a_curve_uses_cannot_go_or_be_driven():
    sk = sketch_with_rb()
    sk.Curve(INVOLUTE, (0, 40))
    with pytest.raises(ValueError, match="Curve.1 uses it in its formula"):
        sk.Remove("rb")
    assert "rb" in sk.dimensions
    sk.Move((0, -30)).Line(5, 0, construction=True, id="other")
    sk.Constrain("other", px.Distance, 5, id="d")
    with pytest.raises(ValueError, match="Curve.1 uses rb in its formula"):
        sk.Expression("rb = 2*d")
    sk.Remove("Curve.1")
    sk.Remove("rb")
    assert "rb" not in sk.dimensions


def test_v_uses_h_and_cycles_raise():
    sk = px.Sketch()
    sk.Curve(("10*(1 - cos(t))/2", "sqrt(h)"), (0, 180))
    w = sk.Wire("Curve.1")
    for t in np.linspace(0, 180, 31):
        h = 10 * (1 - math.cos(t * DEGREE)) / 2
        assert w.Distance((h, math.sqrt(h))) <= 1e-10
    assert sk.Location("Curve.1.end") == pytest.approx((10, math.sqrt(10)), abs=1e-12)
    with pytest.raises(ValueError, match="h uses v and v uses h, a cycle"):
        px.Sketch().Curve(("v + t", "h"), (0, 1))
    with pytest.raises(ValueError, match="h uses h itself"):
        px.Sketch().Curve(("h + t", "t"), (0, 1))
    with pytest.raises(ValueError, match="the range takes numbers and driving dimensions"):
        px.Sketch().Curve(("t", "t"), (0, "2*t"))


def test_a_curve_at_the_pen_joins_its_chain_else_it_stands_alone():
    sk = px.Sketch()
    sk.Line(5, 0)
    sk.Curve(("5 + t", "0.1*t^2"), (0, 5))
    assert len(sk.Wires()) == 1
    assert sk._PointElement("Line.1.end") == sk._PointElement("Curve.1.start")
    alone = px.Sketch()
    alone.Line(5, 0)
    alone.Curve(("6 + t", "0.1*t^2"), (0, 5))
    assert len(alone.Wires()) == 2
    assert alone._PointElement("Line.1.end") != alone._PointElement("Curve.1.start")


def test_a_curve_is_a_rigid_body_fixed_by_default():
    sk = px.Sketch()
    sk.Vertex("anchor", (1, 2))
    sk.Constrain("anchor", px.Fix)
    dof = sk.dof
    sk.Curve(("t", "0.1*t^2"), (0, 10))
    assert sk.dof == dof
    fix = [i for i, c in sk.constraints["Fix"].items() if "Curve.1" in repr(c)]
    assert len(fix) == 1
    sk.Remove(fix[0])
    assert sk.dof == dof + 3
    before = np.array(sk._Geometry("Curve.1")["path"])
    sk.Constrain("Curve.1.start", "anchor", px.Coincident)
    moved = np.array(sk._Geometry("Curve.1")["path"])
    assert moved == pytest.approx(before + (1, 2), abs=1e-12)
    # turned about its start by an angle on its chord, as a whole
    start, end = np.array(sk.Location("Curve.1.start")), np.array(sk.Location("Curve.1.end"))
    chord = end - start
    sk.Move("Curve.1.start", px.Coincident).Line(chord[0] + 1, chord[1] - 1, id="chord", construction=True)
    sk.Constrain("chord.end", "Curve.1.end", px.Coincident)
    turn = math.degrees(math.atan2(chord[1], chord[0])) + 30
    sk.Constrain(px.H, "chord", px.Angle, turn)
    c, s = math.cos(math.radians(30)), math.sin(math.radians(30))
    turned = np.array(sk._Geometry("Curve.1")["path"])
    expected = (moved - start) @ np.array([[c, s], [-s, c]]) + start
    assert turned == pytest.approx(expected, abs=1e-10)
    assert math.dist(sk.Location("Curve.1.start"), sk.Location("Curve.1.end")) == pytest.approx(math.hypot(10, 10), abs=1e-12)
    assert sk.dof == dof


def test_a_closed_curve_is_one_closed_edge():
    sk = px.Sketch()
    sk.Curve(("10*cos(t)", "10*sin(t)"), (0, 360))
    w = sk.Wire("Curve.1")
    assert len(list(w.edges)) == 1
    assert sk.Shape().mass == pytest.approx(100 * math.pi, rel=1e-10)
    b = spline_of(w)
    end = b["knots"][-1]

    def curvature(u):
        d1, d2 = at(b, u, 1), at(b, u, 2)
        return (d1[0] * d2[1] - d1[1] * d2[0]) / math.hypot(*d1) ** 3

    assert curvature(0) == pytest.approx(curvature(end), rel=1e-9)
    assert curvature(0) == pytest.approx(0.1, rel=1e-9)
    drop = px.Sketch()
    drop.Curve(("10*sin(t/2)", "5*sin(t)"), (0, 360))
    b = spline_of(drop.Wire("Curve.1"))
    d0, d1 = at(b, 0, 1), at(b, b["knots"][-1], 1)
    assert abs(d0[0] * d1[1] - d0[1] * d1[0]) / (math.hypot(*d0) * math.hypot(*d1)) == pytest.approx(1, abs=1e-9)
    assert drop.Location("Curve.1.start") == drop.Location("Curve.1.end")


def test_a_callable_is_sampled_once_when_drawn():
    calls = []

    def parabola(t):
        calls.append(t)
        return (t, t * t)

    sk = px.Sketch()
    sk.Curve(parabola, (0, 2))
    assert len(calls) == len(set(calls))
    w = sk.Wire("Curve.1")
    size = math.hypot(2, 4)
    for t in np.linspace(0, 2, 101):
        assert w.Distance((t, t * t)) <= 1e-10 * size
    drawn = len(calls)
    sk.Remove([i for i, c in sk.constraints["Fix"].items() if "Curve.1" in repr(c)][0])
    sk.Constrain("Curve.1.start", px.Fix)
    sk.Constrain(px.H, "Curve.1.end", px.Distance, 3)
    assert len(calls) == drawn
    with pytest.raises(TypeError, match="no point"):
        px.Sketch().Curve(lambda t: "ab", (0, 1))
    with pytest.raises(TypeError, match="no point"):
        px.Sketch().Curve(lambda t: (t, t, t), (0, 1))
    w = px.Sketch().Curve(lambda t: np.array([t, 2 * t]), (0, 1)).Wire("Curve.1")
    assert w.Distance((0.5, 1)) <= 1e-12


@pytest.mark.parametrize("call", [
    lambda sk: sk.Line(1, 0, id="h"),
    lambda sk: sk.Line(1, 0, id="a").Constrain("a", px.Distance, 1, id="t"),
    lambda sk: sk.Line(1, 0).Line(0, 1).Close(id_wire="v"),
    lambda sk: sk.Line(1, 0, id="a").Constrain("a", px.Distance, 1, id="w").Expression("w = 2", id="h"),
    lambda sk: sk.Vertex("v", (1, 1)),
])
def test_h_v_and_t_are_no_ids(call):
    with pytest.raises(ValueError, match="h, v and t are a curve's coordinates and parameter"):
        call(px.Sketch())


def test_a_quick_sketch_takes_numbers_only():
    sk = px.QuickSketch()
    sk.Curve(("2*cos(t) + 1*mm", "sin(t)*pi"), (0, 90))
    assert sk.Location("Curve.1.end") == pytest.approx((px.mm, math.pi), abs=1e-12)
    with pytest.raises(ValueError, match="a QuickSketch has no dimensions"):
        px.QuickSketch().Curve(("rb*t", "t"), (0, 1))


def test_curve_alone_is_a_wire_of_one_spline_edge():
    w = px.Curve(("t", "t^2"), (0, 1))
    assert isinstance(w, px.Wire) and len(list(w.edges)) == 1
    assert isinstance(list(w.edges)[0].edge, px.SplineEdge)
    for p in ((0, 0), (0.5, 0.25), (1, 1)):
        assert w.Distance(p) <= 1e-12
    with pytest.raises(ValueError, match="px.Curve has no dimensions"):
        px.Curve(("rb*t", "t"), (0, 1))


def test_copies_follow_the_formula():
    sk = sketch_with_rb()
    sk.Curve(INVOLUTE, (0, 40))
    sk.Mirrored(["Curve.1"], px.V)
    sk.LinearPattern(["Curve.1"], px.H, 2, 30)
    assert sk.elements["Curve"] == ["Curve.1", "Mirrored.1.Curve.1", "LinearPattern.1.1.Curve.1"]

    def check(rb):
        size = 2 * rb
        mirrored, moved = sk.Wire("Mirrored.1.Curve.1"), sk.Wire("LinearPattern.1.1.Curve.1")
        for t in np.linspace(0, 40, 21):
            x, y = involute(rb, t)
            assert mirrored.Distance((-x, y)) <= 1e-12 * size
            assert moved.Distance((x + 30, y)) <= 1e-12 * size

    check(10)
    sk.Update({"rb": 12})
    check(12)


def test_downstream_calls_take_the_fitted_spline():
    fillet = px.Sketch()
    fillet.Line(5, 0).Curve(("5 + t", "t + 0.1*t^2"), (0, 5))
    fillet.Fillet((5, 0), 0.5)
    assert len(fillet.elements["Arc"]) == 1 and len(fillet.Wires()) == 1
    trim = px.Sketch()
    trim.Curve(("t", "0.1*t^2"), (0, 10))
    trim.Move((5, -1)).Line((5, 12), id="cut")
    trim.Trim(("Curve.1", (8, 6.4)), "cut")
    assert trim.Location("Curve.1.end") == pytest.approx((5, 2.5), abs=1e-9)
    offset = px.Sketch()
    offset.Curve(("t", "0.1*t^2"), (0, 10))
    offset.Offset("Curve.1", 0.5)
    original = offset.Wire("Curve.1")
    sides = [i for i in offset.elements["Spline"]]
    assert len(sides) == 2
    for i in sides:
        # an offset follows the approximation: spline_tol of the size
        for p in offset.Wire(i).Polylines(1e-4)[0]:
            assert original.Distance(tuple(p)) == pytest.approx(0.5, abs=1e-6 * 20)
    profile = px.Sketch()
    profile.Curve(("t", "0.1*t^2"), (0, 10)).Line(0, -10).Close()
    assert profile.Shape().mass == pytest.approx(100 / 3, rel=1e-10)
    assert profile.Face().Contains((8, 2))


def test_a_callable_honors_a_reversed_range_as_a_formula_does():
    formula = px.Curve(("t", "t^2"), (2, 0))
    called = px.Curve(lambda t: (t, t * t), (2, 0))
    for w in (formula, called):
        e = w.edges[0]
        assert (e.start.x, e.start.y) == pytest.approx((2, 4), abs=1e-12)
        assert (e.end.x, e.end.y) == pytest.approx((0, 0), abs=1e-12)
    sk = px.QuickSketch((5, 4)).Line(-3, 0).Curve(lambda t: (t, t * t), (2, 0))
    assert len(sk.Wires()) == 1
