# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

pytestmark = pytest.mark.constraints


# a solved system holds every residual below Tol(S) = 1e-14 * S, S its largest coordinate (here at most 10)
TOL = 1e-13


def system():
    return px._planex._ConstraintSystem()


def polar(s, cx, cy, r, degrees):
    return s.Point(cx + r * math.cos(math.radians(degrees)), cy + r * math.sin(math.radians(degrees)))


def everything():
    """every kind of element in general position, and every combination each constraint kind takes"""
    s = system()
    # the largest coordinate, so that small changes of the others leave the size (the residuals' scale) alone
    s.Point(10, 0, fixed=True)
    p0, p1, p2, p3, p4, p5 = (s.Point(*xy) for xy in [(0.3, 0.2), (2.1, 0.7), (1.2, 2.3), (3.0, 1.9), (-0.8, 1.4), (4.2, -1.1)])
    l0, l1, l2 = s.Line(p0, p1), s.Line(p2, p3), s.Line(p5, p1)
    c0 = s.Circle(s.Point(1.5, -1.0), 0.6)
    c1 = s.Circle(s.Point(4.0, 3.0), 0.9)
    a0 = s.Arc(s.Point(-1.0, -1.0), 1.0, polar(s, -1, -1, 1.05, 20), polar(s, -1, -1, 0.97, 130))
    a1 = s.Arc(s.Point(3.0, -2.0), 1.2, polar(s, 3, -2, 1.2, 200), polar(s, 3, -2, 1.1, 340), ccw=False)
    a2_end = s.Point(2.9, -0.2)
    a2 = s.Arc(s.Point(2.6, 0.9), 0.8, p1, a2_end)
    a3 = s.Arc(s.Point(3.6, -0.6), 0.9, a2_end, s.Point(4.4, 0.1))
    x0 = s.Axis(30, 0.4, fixed=False)
    x1 = s.Axis(100, -0.3, fixed=False)
    constraints = [
        ("Coincident", [p0, (l2, "mid")]), ("Coincident", [s.Point(5, 5), s.Point(5.1, 4.9)]), ("Coincident", [p4, l0]), ("Coincident", [x0, p4]), ("Coincident", [p4, c0]),
        ("Coincident", [l0, l1]), ("Coincident", [l0, x0]), ("Coincident", [x0, x1]), ("Coincident", [c0, a0]),
        ("Coincident", [(l0, "mid"), (a0, "mid")]), ("Coincident", [(a1, "start"), l1]), ("Coincident", [(a1, "mid"), c1]),
        ("Horizontal", [l0]), ("Horizontal", [p0, p2]), ("Horizontal", [x0]), ("Vertical", [l1]), ("Vertical", [x1]),
        ("Vertical", [(a0, "end"), (l1, "mid")]),
        ("Parallel", [l0, l1]), ("Parallel", [l0, x0]), ("Perpendicular", [l1, x1]),
        ("Tangent", [l1, c0]), ("Tangent", [c0, x0]), ("Tangent", [l2, a2]), ("Tangent", [c0, c1]), ("Tangent", [a2, a3]),
        ("Equal", [l0, l1]), ("Equal", [c0, a0]),
        ("Symmetric", [p0, p2, x0]), ("Symmetric", [l0, l1, x0]), ("Symmetric", [c0, c1, l2]), ("Symmetric", [a0, a1, x1]),
        ("Symmetric", [(a2, "mid"), (l2, "start"), l0]),
        ("SymmetricCarriers", [l0, l1, x0]), ("SymmetricCarriers", [l0, x1, l2]),
        ("Concentric", [c0, a0]), ("Concentric", [(a1, "center"), p4]),
        ("Fix", [l0]), ("Fix", [x0]), ("Fix", [(a0, "mid")]), ("Fix", [a0]), ("Fix", [p3]),
        ("Distance", [l0], 2), ("Distance", [c0], 1), ("Distance", [p0, p2], 1.5), ("Distance", [p4, l0], 0.5),
        ("Distance", [c0, p4], 0.3), ("Distance", [l0, l1], 1.0), ("Distance", [l1, c0], 0.4), ("Distance", [c0, c1], 0.2),
        ("Distance", [x0, c1], 0.7), ("Distance", [(a2, "mid"), x1], -0.2),
        ("Radius", [a0], 1.1), ("Diameter", [c1], 2.0),
        ("Angle", [l0, l1], 35), ("Angle", [x0, l1], -20),
    ]
    for c in constraints:
        s.Constrain(*c)
    return s, constraints


def test_every_constraint_kind_has_the_derivatives_of_its_residuals():
    s, constraints = everything()
    count = s.Count()
    assert count == len(constraints) + 4  # each arc's ends on its circle
    h = 1e-6
    base = s.values
    for c in range(count):
        r, unknowns, J = s.Evaluate(c)
        for k, u in enumerate(unknowns):
            v = list(base)
            v[u] += h
            s.values = v
            plus = s.Evaluate(c, build=False)[0]
            v[u] -= 2 * h
            s.values = v
            minus = s.Evaluate(c, build=False)[0]
            s.values = base
            for i in range(len(r)):
                fd = (plus[i] - minus[i]) / (2 * h)
                assert J[i][k] == pytest.approx(fd, abs=1e-6), (constraints[c] if c < len(constraints) else "OnCircle", i, u)


def rectangle():
    s = system()
    p = [s.Point(*xy) for xy in [(0, 0), (3.8, 0.2), (4.1, 2.2), (-0.1, 1.9)]]
    lines = [s.Line(p[k], p[(k + 1) % 4]) for k in range(4)]
    s.Constrain("Fix", [p[0]])
    for k, kind in enumerate(["Horizontal", "Vertical", "Horizontal", "Vertical"]):
        s.Constrain(kind, [lines[k]])
    s.Constrain("Distance", [lines[0]], 4)
    s.Constrain("Distance", [lines[1]], 2)
    return s, p, lines


def test_a_rectangle_from_horizontal_vertical_and_two_lengths():
    s, p, _ = rectangle()
    r = s.Solve()
    assert r["solved"]
    assert [v for k in p for v in s.PointOf(k)] == pytest.approx([0, 0, 4, 0, 4, 2, 0, 2], abs=TOL)
    a = s.Analyze()
    assert a["dof"] == 0
    assert not any(a["redundant"])


def test_a_parallel_on_two_horizontal_lines_is_redundant():
    s, _, lines = rectangle()
    c = s.Constrain("Parallel", [lines[0], lines[2]])
    assert s.Solve()["solved"]
    a = s.Analyze()
    assert a["redundant"][c]
    assert a["redundant"][1] and a["redundant"][3]
    assert not a["redundant"][2]


def test_a_line_tangent_to_a_circle_stays_on_the_drawn_side():
    s = system()
    c = s.Circle(s.Point(0, 0, fixed=True), 2)
    s.Constrain("Radius", [c], 2)
    for y in (2.3, -2.3):
        a, b = s.Point(-3, y), s.Point(3, y + 0.1)
        line = s.Line(a, b)
        s.Constrain("Horizontal", [line])
        s.Constrain("Tangent", [line, c])
        assert s.Solve()["solved"]
        assert [s.PointOf(a)[1], s.PointOf(b)[1]] == pytest.approx([math.copysign(2, y)] * 2, abs=TOL)


def test_an_arc_running_on_tangent_from_a_line_end():
    s = system()
    a, b = s.Point(0, 0), s.Point(5, 0)
    line = s.Line(a, b)
    s.Constrain("Fix", [line])
    center = s.Point(5.3, 2.0)
    arc = s.Arc(center, 2.0, b, s.Point(7.1, 2.2))
    s.Constrain("Tangent", [line, arc])
    s.Constrain("Radius", [arc], 2)
    assert s.Solve()["solved"]
    assert s.PointOf(center) == pytest.approx((5, 2), abs=TOL)
    assert s.Analyze()["redundant"] == [False] * 4


def test_symmetric_points_about_an_axis():
    s = system()
    v = s.Axis(0, 0)  # x = 0
    p, q = s.Point(1.0, 0.5), s.Point(-0.9, 0.6)
    s.Constrain("Fix", [p])
    s.Constrain("Symmetric", [p, q, v])
    assert s.Solve()["solved"]
    assert s.PointOf(q) == pytest.approx((-1.0, 0.5), abs=TOL)


def test_the_mid_of_lines_and_arcs():
    s = system()
    line = s.Line(s.Point(1, 1), s.Point(3, 2))
    assert s.PointOf((line, "mid")) == pytest.approx((2, 1.5), abs=1e-15)
    o = s.Point(0, 0)
    r = math.sqrt(0.5)
    quarter = s.Arc(o, 1, s.Point(1, 0), s.Point(0, 1))
    assert s.PointOf((quarter, "mid")) == pytest.approx((r, r), abs=1e-15)
    three_quarters = s.Arc(o, 1, s.Point(1, 0), s.Point(0, -1))
    assert s.PointOf((three_quarters, "mid")) == pytest.approx((-r, r), abs=1e-15)
    clockwise = s.Arc(o, 1, s.Point(1, 0), s.Point(0, 1), ccw=False)
    assert s.PointOf((clockwise, "mid")) == pytest.approx((-r, -r), abs=1e-15)
    p = s.Point(0.4, 0.1)
    s.Constrain("Coincident", [p, (line, "mid")])
    s.Constrain("Fix", [line])
    assert s.Solve()["solved"]
    assert s.PointOf(p) == pytest.approx((2, 1.5), abs=TOL)


@pytest.mark.parametrize("degrees", [30, -30, 135])
def test_a_signed_angle(degrees):
    s = system()
    base = s.Line(s.Point(0, 0), s.Point(1, 0))
    s.Constrain("Fix", [base])
    t = math.radians(degrees * 0.8)
    start = s.Point(2, 0)
    end = s.Point(2 + math.cos(t), math.sin(t))
    turned = s.Line(start, end)
    s.Constrain("Fix", [start])
    s.Constrain("Distance", [turned], 1)
    s.Constrain("Angle", [base, turned], degrees)
    assert s.Solve()["solved"]
    x, y = s.PointOf(end)
    assert math.degrees(math.atan2(y, x - 2)) == pytest.approx(degrees, abs=1e-10)


def test_a_negative_distance_switches_sides_once():
    s = system()
    line = s.Line(s.Point(0, 0), s.Point(1, 0))
    s.Constrain("Fix", [line])
    p = s.Point(0.5, 0.7)
    c = s.Constrain("Distance", [p, line], 1)
    assert s.Solve()["solved"]
    assert s.PointOf(p) == pytest.approx((0.5, 1), abs=TOL)
    s.SetValue(c, -1)
    for _ in range(2):
        assert s.Solve()["solved"]
        assert s.PointOf(p) == pytest.approx((0.5, -1), abs=TOL)


def test_the_distance_inside_a_circle():
    s = system()
    big = s.Circle(s.Point(0, 0), 5)
    small_center = s.Point(1, 0)
    small = s.Circle(small_center, 1)
    s.Constrain("Fix", [big])
    s.Constrain("Radius", [small], 1)
    s.Constrain("Distance", [big, small], 0.5)
    assert s.Solve()["solved"]
    assert s.PointOf(small_center) == pytest.approx((3.5, 0), abs=TOL)


def test_a_symmetry_line_of_two_crossing_lines_is_their_bisector():
    s = system()
    a = s.Line(s.Point(0, 0), s.Point(2, 0))
    b = s.Line(s.Point(0, 0), s.Point(1, 1))
    s.Constrain("Fix", [a])
    s.Constrain("Fix", [b])
    axis = s.Axis(100, 0.1, fixed=False)
    s.Constrain("SymmetricCarriers", [a, b, axis])
    assert s.Solve()["solved"]
    t, d = s.values[-2:]
    assert math.degrees(t) % 180 == pytest.approx(112.5, abs=1e-10)
    assert d == pytest.approx(0, abs=TOL)
    assert s.Analyze()["dof"] == 0


def test_combinations_a_kind_does_not_relate_raise():
    s = system()
    p, q = s.Point(0, 0), s.Point(1, 0)
    c = s.Circle(s.Point(3, 0), 1)
    with pytest.raises(ValueError, match="Tangent: cannot relate"):
        s.Constrain("Tangent", [p, q])
    with pytest.raises(ValueError, match="must be positive"):
        s.Constrain("Distance", [p, q], -1)
    with pytest.raises(ValueError, match="no such point"):
        s.Constrain("Coincident", [(c, "start"), p])
    with pytest.raises(ValueError, match="no constraint kind"):
        s.Constrain("Congruent", [p, q])
    assert s.Count() == 0
