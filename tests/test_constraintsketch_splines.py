# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-12


def geometry(sk, id="Spline.1"):
    return sk._Geometry(id)


def derivative(g, u, k=1):
    """the k-th derivative of a drawn spline at chord length u"""
    tangents = {int(i): t for i, t in g["tangents"].items()}
    return px._planex._InterpolatedAt(g["points"], tangents, g["closed"], u, k)


def flat(points):
    return [v for p in points for v in p]


def unit(v):
    n = math.hypot(*v)
    return v[0] / n, v[1] / n


def cross(a, b):
    a, b = unit(a), unit(b)
    return a[0] * b[1] - a[1] * b[0]


def chord(points):
    return sum(math.dist(a, b) for a, b in zip(points, points[1:]))


def test_a_spline_runs_from_the_pen_through_its_points_which_are_point_elements():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)])
    assert sk.elements["Spline"] == ["Spline.1"]
    assert sk.elements["Point"] == ["origin", "Point.1", "Point.2", "Point.3", "Point.4"]
    g = geometry(sk)
    assert g["points"] == [(0, 0), (1, 0.8), (2.3, 0.5), (3, -0.4)]
    assert (g["closed"], g["tangents"]) == (False, {})
    assert g["path"][0] == (0, 0) and g["path"][-1] == (3, -0.4)
    assert (sk.status, sk.dof) == ("under", 8)
    assert sk.pen.position == pytest.approx((3, -0.4))
    assert sk.pen.direction == pytest.approx(unit(derivative(g, chord(g["points"]))), abs=TOL)


def test_constraints_on_its_points_reshape_it():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)])
    sk.Constrain("Point.4", px.H, px.Coincident)
    sk.Constrain("Point.1", "Point.4", px.Distance, 4)
    sk.Constrain("Point.2", px.Fix)
    sk.Constrain("Point.3", px.Fix)
    assert (sk.status, sk.dof) == ("full", 0)
    g = geometry(sk)
    assert g["points"][-1] == pytest.approx((4, 0), abs=TOL)
    path = g["path"]
    assert path[-1] == pytest.approx((4, 0), abs=TOL)
    assert min(math.dist(p, (2.3, 0.5)) for p in path) == pytest.approx(0, abs=TOL)


def test_a_line_and_a_spline_tangent_at_their_joint():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0.3, px.Fix, id="lead").Spline([(3, 1.2), (4.5, 0.4), (5, 2)], px.Tangent)
    assert sk.constraints["Tangent"]["Tangent.1"].__repr__() == '"lead", "Spline.1", px.Tangent'
    g = geometry(sk)
    assert g["points"][0] == pytest.approx((2, 0.3), abs=TOL)
    assert cross((2, 0.3), derivative(g, 0)) == pytest.approx(0, abs=1e-12)
    # the tangent turned the spline's start, not the fixed line
    assert sk._Location("lead.end") == pytest.approx((2, 0.3), abs=TOL)


def test_a_tangent_turns_the_spline_at_the_joint_and_leaves_its_points():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0, px.Fix, id="lead").Spline([(3, 1.2), (4.5, 0.4), (5, 2)])
    dof = sk.dof
    sk.Constrain("lead", "Spline.1", px.Tangent)
    g = geometry(sk)
    assert flat(g["points"]) == pytest.approx([2, 0, 3, 1.2, 4.5, 0.4, 5, 2], abs=TOL)
    assert g["tangents"] == {0: pytest.approx((1, 0), abs=TOL)}
    assert sk.dof == dof
    sk.Remove("Tangent.1")
    assert geometry(sk)["tangents"] == {} and sk.dof == dof


def test_a_fixed_spline_keeps_its_shape_and_the_other_element_turns():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0.5, id="lead").Spline([(3, 1.2), (4.5, 0.4), (5, 2)], px.Fix)
    sk.Constrain("lead", "Spline.1", px.Tangent)
    g = geometry(sk)
    assert g["tangents"] == {}
    (x0, y0), (x1, y1) = sk._Location("lead.start"), sk._Location("lead.end")
    assert (x1, y1) == pytest.approx((2, 0.5), abs=TOL)
    assert cross((x1 - x0, y1 - y0), derivative(g, 0)) == pytest.approx(0, abs=1e-12)


def test_a_spline_the_constraints_determine_keeps_its_shape_and_the_line_turns():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0.5, id="lead").Spline([(3, 1.2), (4.5, 0.4), (5, 2)], id="s")
    for k in range(4):
        sk.Constrain(f"s.p{k}", px.Fix)
    sk.Constrain("lead", "s", px.Tangent)
    g = geometry(sk, "s")
    assert g["tangents"] == {}
    (x0, y0), (x1, y1) = sk._Location("lead.start"), sk._Location("lead.end")
    assert (x1, y1) == pytest.approx((2, 0.5), abs=TOL)
    assert cross((x1 - x0, y1 - y0), derivative(g, 0)) == pytest.approx(0, abs=1e-12)


def test_the_points_of_a_spline_by_index():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)], id="s")
    sk.Constrain("s.p2", px.H, px.Distance, 1, id="height")
    assert sk._Location("s.p2") == pytest.approx((2.3, 1), abs=TOL)
    assert repr(sk.constraints["Distance"]["height"]) == '"s.p2", px.H, px.Distance, 1'
    sk.Move("s.p1", px.Coincident).Line(0, -2, px.Vertical)
    assert sk._Location("Line.1.start") == pytest.approx(sk._Location("s.p1"), abs=TOL)
    with pytest.raises(ValueError, match="the spline has the points p0 to p3"):
        sk.Constrain("s.p4", px.Fix)
    with pytest.raises(ValueError, match=r"\.p0: a point or tangent of a spline"):
        sk.Constrain("Line.1.p0", px.Fix)
    with pytest.raises(ValueError, match=r"id 'a.p1' contains '\.'"):
        sk.Move((5, 5)).Line(1, 0, id="a.p1")


def test_an_angle_at_a_tangent_line_of_a_spline_turns_it_there():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)], id="s")
    dof = sk.dof
    sk.Constrain(px.H, "s.t0", px.Angle, 30, id="lift")
    g = geometry(sk, "s")
    assert g["tangents"][0] == pytest.approx((math.cos(math.pi / 6), math.sin(math.pi / 6)), abs=TOL)
    assert flat(g["points"]) == pytest.approx([0, 0, 1, 0.8, 2.3, 0.5, 3, -0.4], abs=TOL)
    assert sk.dof == dof
    assert repr(sk.constraints["Angle"]["lift"]) == 'px.H, "s.t0", px.Angle, 30'
    sk["lift"] = 10
    assert unit(derivative(geometry(sk, "s"), 0)) == pytest.approx((math.cos(math.pi / 18), math.sin(math.pi / 18)), abs=TOL)
    sk.Remove("lift")
    assert geometry(sk, "s")["tangents"] == {} and sk.dof == dof


def test_a_given_tangent_is_an_angle_unknown_that_constraints_can_turn():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(1, 1), (2, 0)], tangents={0: (0, 1)})
    g = geometry(sk)
    assert g["tangents"] == {0: pytest.approx((0, 1), abs=TOL)}
    assert unit(derivative(g, 0)) == pytest.approx((0, 1), abs=TOL)
    assert sk.dof == 7
    sk.Move((-1, -1)).Line(1, 1, id="in")
    sk.Constrain("in.end", "Spline.1.start", px.Coincident)
    sk.Constrain("in", px.Fix)
    # in arrives where the spline leaves: one runs on the other's way
    sk.Constrain("in", "Spline.1", px.Tangent)
    g = geometry(sk)
    assert unit(derivative(g, 0)) == pytest.approx(unit((1, 1)), abs=1e-12)


def test_a_fixed_spline_holds_its_points_and_tangents():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(1, 1), (2, 0), (3, 1)], px.Fix, tangents={3: (1, 0)})
    assert (sk.status, sk.dof) == ("full", 0)
    assert repr(sk.constraints["Fix"]["Fix.1"]) == '"Spline.1", px.Fix'


def test_two_splines_tangent_where_one_ends_and_the_other_starts():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(1, 1), (2, 0.2)], id="a").Spline([(3, 1.5), (4, 0)], px.Tangent, id="b")
    a, b = geometry(sk, "a"), geometry(sk, "b")
    assert a["points"][-1] == pytest.approx(b["points"][0], abs=TOL)
    assert cross(derivative(a, chord(a["points"])), derivative(b, 0)) == pytest.approx(0, abs=1e-12)


def test_a_closed_spline_is_a_face_through_its_points():
    sk = px.Sketch()
    pts = [(2, 0), (1.5, 1.5), (0, 1), (-0.5, 0.2)]
    sk.Move((0.3, -1)).Spline(pts, periodic=True, name="blob")
    g = geometry(sk)
    assert g["closed"] and len(g["points"]) == 5
    assert g["path"][0] == pytest.approx(g["path"][-1])
    seam = chord(g["points"] + [g["points"][0]])
    for k in (1, 2):
        assert derivative(g, 0, k) == pytest.approx(derivative(g, seam, k), abs=1e-9)
    face = sk.Face()
    assert face.mass > 2.0
    wire = sk.Wire()
    assert all(e.name == "blob" for e in wire.edges)
    # a closed edge is two, like a circle
    edges = wire.edges
    assert flat([(edges[0].start.x, edges[0].start.y), (edges[-1].end.x, edges[-1].end.y)]) == pytest.approx([0.3, -1, 0.3, -1], abs=TOL)


def test_a_spline_closing_a_chain_of_lines_makes_a_face():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0).Line(0, 1).Spline([(2, 2.5), (0, 0)])
    assert sk.wires == {"Chain.1": ["Line.1", "Line.2", "Spline.1"]}
    assert geometry(sk)["points"][-1] == (0, 0)
    assert sk.Face().mass == pytest.approx(sk.Face().mass)
    assert sk.Face().mass > 4


def test_start_from_localpos_false_starts_at_the_first_point():
    sk = px.Sketch()
    sk.Move((5, 5)).Spline([(0, 0), (1, 1), (2, 0)], start_from_localpos=False)
    assert geometry(sk)["points"] == [(0, 0), (1, 1), (2, 0)]


def test_the_exported_spline_is_our_interpolation():
    pts = [(0, 0), (1, 0.8), (2.3, 0.5), (3, -0.4)]
    sk = px.Sketch()
    sk.Move((0, 0)).Spline(pts[1:], tangents={1: (1, 0)})
    wire = sk.Wire()
    # cut at its points for the export, the pieces of one spline join again in a wire
    assert len(wire.edges) == 1
    b = wire.splines[0].bspline
    f = px._planex._Interpolate(pts, {1: (1, 0)})
    assert b["degree"] == f["degree"] and b["weights"] is None
    assert b["knots"] == pytest.approx(f["knots"], abs=TOL)
    assert flat(b["poles"]) == pytest.approx(flat((p.x, p.y) for p in f["poles"]), abs=TOL)


def test_mirrored_splines_follow_their_original():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)])
    sk.Mirrored("Chain.1", px.V)
    assert sk.elements["Spline"] == ["Spline.1", "Mirrored.1.Spline.1"]
    sk.Constrain("Point.4", px.Fix)
    sk.Constrain("Point.2", px.Fix)
    sk.Constrain("Point.3", px.Fix)
    a, b = geometry(sk, "Spline.1"), geometry(sk, "Mirrored.1.Spline.1")
    assert flat(b["points"]) == pytest.approx(flat((-x, y) for x, y in a["points"]), abs=TOL)
    assert (sk.status, sk.dof) == ("full", 0)


def test_the_tangents_of_a_mirrored_spline_follow_the_original():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(2, 0.3, id="base").Spline([(3, 1.5), (1.5, 2.5)], px.Tangent, id="s")
    sk.Constrain(px.H, "base", px.Angle, 10, id="tilt")
    sk.Mirrored("Chain.1", px.V)
    sk["tilt"] = 20
    a, b = geometry(sk, "s"), geometry(sk, "Mirrored.1.s")
    t = math.radians(20)
    assert a["tangents"][0] == pytest.approx((math.cos(t), math.sin(t)), abs=TOL)
    assert b["tangents"][0] == pytest.approx((-math.cos(t), math.sin(t)), abs=TOL)
    assert flat(b["points"]) == pytest.approx(flat((-x, y) for x, y in a["points"]), abs=TOL)


def test_a_circular_pattern_of_a_spline():
    sk = px.Sketch()
    sk.Move((1, 0)).Spline([(1.5, 0.3), (2, 0)], px.Fix, id="petal", tangents={0: (1, 1)})
    sk.CircularPattern("petal", sk.origin, 4)
    assert sk.elements["Spline"] == ["petal"] + [f"CircularPattern.1.{k}.petal" for k in (1, 2, 3)]
    assert flat(geometry(sk, "CircularPattern.1.1.petal")["points"]) == pytest.approx([0, 1, -0.3, 1.5, 0, 2], abs=TOL)
    sk["CircularPattern.1.angle"] = 45
    p = geometry(sk, "CircularPattern.1.1.petal")["points"][1]
    q = (1.5 * math.cos(math.pi / 4) - 0.3 * math.sin(math.pi / 4), 1.5 * math.sin(math.pi / 4) + 0.3 * math.cos(math.pi / 4))
    assert p == pytest.approx(q, abs=TOL)
    t = math.radians(45 + 45)
    assert geometry(sk, "CircularPattern.1.1.petal")["tangents"][0] == pytest.approx((math.cos(t), math.sin(t)), abs=TOL)


def test_removing_a_spline_takes_its_points_but_not_a_point_it_runs_through():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Spline([(2, 1), (3, 0)])
    with pytest.raises(ValueError, match="Spline.1 runs through it, remove it first"):
        sk.Remove("Point.3")
    sk.Remove("Spline.1")
    assert "Spline" not in sk.elements
    assert sk.elements["Point"] == ["origin", "Point.1", "Point.2"]


def test_the_errors_of_a_spline():
    sk = px.Sketch()
    with pytest.raises(ValueError, match="two points at least"):
        sk.Move((0, 0)).Spline([])
    with pytest.raises(ValueError, match="two following points at one place"):
        sk.Move((0, 0)).Spline([(1, 1), (1, 1), (2, 0)])
    with pytest.raises(ValueError, match="tangent 5 at a point it does not have"):
        sk.Move((0, 0)).Spline([(1, 1), (2, 0)], tangents={5: (1, 0)})
    with pytest.raises(ValueError, match="a closed spline needs three points"):
        sk.Move((0, 0)).Spline([(1, 1)], periodic=True)
    with pytest.raises(TypeError, match="unexpected keyword argument 'closed'"):
        sk.Spline([(1, 1)], closed=True)
    assert "Spline" not in sk.elements
    sk.Move((0, 0)).Spline([(1, 1), (2, 0)]).Move((1, -1)).Line(0, 2)
    sk.Move((0, 3)).Spline([(1, 4.1), (2, 3), (1, 2)], periodic=True).Move((1.1, 3)).Line(0, 2, id="stub")
    with pytest.raises(ValueError, match="a closed spline needs two cuts"):
        sk.Split("Spline.2", "stub")


def test_splines_in_the_plots():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0.3, id="lead").Spline([(3, 1.2), (4.5, 0.4), (5, 2)], px.Tangent)
    svg = sk.ToSVG(show_constraints=True, show_ends=True)
    assert "<title>Spline.1</title>" in svg and "<title>Spline.1.end</title>" in svg
    assert "Tangent.1" in svg
    fig = sk.Plot(show_constraints=True, show_ends=True, show_direction=True, backend="plotly")
    assert any("Spline.1" in str(t) for t in fig.data)
    sk.Constrain("Spline.1.p2", px.H, px.Distance, 0.5, id="low")
    sk.Constrain(px.H, "Spline.1.t3", px.Angle, 60, id="steep")
    sk.Circle(4.3, 2.0, 0.3, id="ball")
    sk.Constrain("ball", "Spline.1", px.Tangent)
    svg = sk.ToSVG(show_constraints=True)
    assert "low: " in svg and "steep: " in svg and "Tangent.2" in svg
    sk.Plot(show_constraints=True, backend="plotly")


def test_the_derivatives_of_a_tangent_with_a_spline():
    """also through the chord lengths, which change with the points"""
    s = px._planex._ConstraintSystem()
    s.Point(10, 0, fixed=True)
    p = [s.Point(*xy) for xy in [(0.3, 0.2), (2.1, 0.7), (3.2, 1.9), (4.0, 0.6), (-1.0, -0.4)]]
    line = s.Line(p[4], p[0])
    a = s.Spline(p[:3], {2: 40.0})
    b = s.Spline([p[2], p[3]])
    s.Constrain("Tangent", [line, a])
    s.Constrain("Tangent", [a, b])
    s.Constrain("Fix", [a])
    s.Constrain("Coincident", [s.Point(3.1, 1.2), (b, "mid")])
    # a spline its points determine gets no tangent: the line's direction meets the curve's own
    c = [s.Point(*xy) for xy in [(5, 1), (6, 2.5), (7, 1.5)]]
    curve = s.Spline(c)
    for point in c:
        s.Constrain("Fix", [point])
    s.Constrain("Tangent", [s.Line(s.Point(4.2, 0.1), c[0]), curve])
    assert s.Element(curve)["tangents"] == []
    # away from the points: positions along the splines as unknowns
    s.Constrain("Coincident", [s.Point(5.8, 2.6), curve])
    s.Constrain("Distance", [s.Point(6.5, 0.9), curve], 0.3)
    s.Constrain("Tangent", [s.Line(s.Point(4.6, 2.7), s.Point(7.2, 2.4)), curve])
    s.Constrain("Tangent", [s.Circle(s.Point(1.5, -0.6), 0.4), a])
    s.Constrain("Tangent", [a, curve])
    h = 1e-6
    base = s.values
    for c in range(s.Count()):
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
                assert J[i][k] == pytest.approx((plus[i] - minus[i]) / (2 * h), abs=1e-6), (c, i, u)
    # the given tangent and the one the first Tangent made
    assert s.Element(a)["tangent_at"] == [2, 0] and s.Element(a)["tangents"][0] == pytest.approx(40.0)


def curve_at(e, t, k=0):
    """the k-th derivative of a system spline (its Element dict) at chord length t"""
    tangents = {i: (math.cos(math.radians(a)), math.sin(math.radians(a))) for i, a in zip(e["tangent_at"], e["tangents"])}
    return px._planex._InterpolatedAt(e["points"], tangents, e["closed"], t, k)


def fixed_spline(s, points):
    return s.Spline([s.Point(*xy, fixed=True) for xy in points])


WAVE = [(0, 0), (1, 0.8), (2.3, 0.5), (3, -0.4)]


def test_a_point_on_a_spline_has_its_place_along_it_as_an_unknown():
    s = px._planex._ConstraintSystem()
    curve = fixed_spline(s, WAVE)
    q = s.Point(1.6, 1.4)
    s.Constrain("Coincident", [q, curve])
    assert s.Solve()["solved"]
    t = s.values[-1]
    assert curve_at(s.Element(curve), t) == pytest.approx(s.Element(q)["point"], abs=TOL)


def test_the_distance_of_a_point_from_a_spline_is_along_its_normal():
    s = px._planex._ConstraintSystem()
    curve = fixed_spline(s, WAVE)
    q = s.Point(1.6, 1.4)
    s.Constrain("Distance", [q, curve], 0.5)
    assert s.Solve()["solved"]
    e, t, p = s.Element(curve), s.values[-1], s.Element(q)["point"]
    c, d = curve_at(e, t), curve_at(e, t, 1)
    assert math.dist(p, c) == pytest.approx(0.5, abs=TOL)
    assert (p[0] - c[0]) * d[0] + (p[1] - c[1]) * d[1] == pytest.approx(0, abs=TOL)
    # on the side it was drawn
    assert cross(d, (p[0] - c[0], p[1] - c[1])) > 0


def test_a_line_and_a_circle_touch_a_spline_away_from_its_points():
    s = px._planex._ConstraintSystem()
    curve = fixed_spline(s, WAVE)
    line = s.Line(s.Point(0.2, 1.1), s.Point(2.2, 1.0))
    s.Constrain("Tangent", [line, curve])
    at_line = len(s.values) - 1
    circle = s.Circle(s.Point(2.6, -1.2, fixed=True), 0.6)
    s.Constrain("Tangent", [circle, curve])
    assert s.Solve()["solved"]
    e = s.Element(curve)
    tl, tc = s.values[at_line], s.values[-1]
    a, b = s.Element(line)["start"], s.Element(line)["end"]
    p, d = curve_at(e, tl), curve_at(e, tl, 1)
    assert cross((b[0] - a[0], b[1] - a[1]), (p[0] - a[0], p[1] - a[1])) == pytest.approx(0, abs=TOL)
    assert cross((b[0] - a[0], b[1] - a[1]), d) == pytest.approx(0, abs=TOL)
    g = s.Element(circle)
    p, d = curve_at(e, tc), curve_at(e, tc, 1)
    assert math.dist(p, g["center"]) == pytest.approx(g["r"], abs=TOL)
    assert (p[0] - g["center"][0]) * d[0] + (p[1] - g["center"][1]) * d[1] == pytest.approx(0, abs=1e-12)


def test_two_splines_touch_away_from_their_points():
    s = px._planex._ConstraintSystem()
    a = fixed_spline(s, WAVE)
    b = s.Spline([s.Point(*xy) for xy in [(0.5, 1.6), (1.4, 1.0), (2.5, 1.7)]])
    s.Constrain("Tangent", [a, b])
    assert s.Solve()["solved"]
    ta, tb = s.values[-2], s.values[-1]
    ea, eb = s.Element(a), s.Element(b)
    assert curve_at(ea, ta) == pytest.approx(curve_at(eb, tb), abs=TOL)
    assert cross(curve_at(ea, ta, 1), curve_at(eb, tb, 1)) == pytest.approx(0, abs=1e-12)


def test_a_line_ending_on_a_spline_follows_it():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)], id="s")
    sk.Constrain("s.p3", px.Fix)
    sk.Constrain("s.p2", px.H, px.Distance, 0.5, id="bump")
    sk.Move((1.6, 2)).Line(0, -1.3, px.Vertical, id="post")
    dof = sk.dof
    sk.Constrain("post.end", "s", px.Coincident)
    assert sk.dof == dof - 1
    assert repr(sk.constraints["Coincident"]["Coincident.1"]) == '"post.end", "s", px.Coincident'
    sk["bump"] = 1.2
    g = geometry(sk, "s")
    tangents = {int(i): t for i, t in g["tangents"].items()}
    assert nearest_on(g["points"], sk._Location("post.end"), tangents, g["closed"]) == pytest.approx(0, abs=1e-9)
    sk.Remove("Coincident.1")
    assert sk.dof == dof


def nearest_on(points, p, tangents=None, closed=False):
    """the distance of p from an interpolated spline"""
    return param_on(points, p, tangents, closed)[0]


def param_on(points, p, tangents=None, closed=False):
    """the distance of p from an interpolated spline and the position there: the best of samples, then Newton steps"""
    f = px._planex._Interpolate(points, tangents or {}, closed)
    end = f["params"][-1]

    def at(u, k=0):
        return px._planex._InterpolatedAt(points, tangents or {}, closed, u, k)

    u = min((end * k / 400 for k in range(401)), key=lambda t: math.dist(at(t), p))
    for _ in range(30):
        c, d1, d2 = at(u), at(u, 1), at(u, 2)
        g = (c[0] - p[0]) * d1[0] + (c[1] - p[1]) * d1[1]
        h = d1[0] ** 2 + d1[1] ** 2 + (c[0] - p[0]) * d2[0] + (c[1] - p[1]) * d2[1]
        u = min(max(u - g / h, 0.0), end)
    return math.dist(at(u), p), u


def construction(sk):
    return {g["id"]: g["construction"] for g in sk._Drawing() if g["kind"] == "Spline"}


def test_a_trimmed_spline_keeps_its_shape_and_the_cut_follows_the_cutter():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)], id="s")
    for k in (1, 2, 3):
        sk.Constrain(f"s.p{k}", px.Fix)
    sk.Move((1.6, -1)).Line(0, 3, px.Vertical, id="cut")
    points = geometry(sk, "s")["points"]
    sk.Trim(("s", (2.5, 0.3)), "cut")
    assert construction(sk) == {"s": False, "s.trimmed.1": True}
    a, b = geometry(sk, "s"), geometry(sk, "s.trimmed.1")
    assert flat(a["points"]) == pytest.approx(flat(points), abs=TOL)
    assert a["start"] == pytest.approx((0, 0), abs=TOL) and a["end"] == pytest.approx(b["start"], abs=TOL)
    assert a["end"][0] == pytest.approx(1.6, abs=TOL) and b["end"] == pytest.approx((3, -0.4), abs=TOL)
    assert nearest_on(points, a["end"]) == pytest.approx(0, abs=1e-12)
    sk.Constrain("cut", px.V, px.Distance, 1.2)
    end = geometry(sk, "s")["end"]
    assert end[0] == pytest.approx(1.2, abs=TOL)
    assert nearest_on(points, end) == pytest.approx(0, abs=1e-12)
    ends = [(v.x, v.y) for w in sk.Wires() for e in w.splines for v in (e.start, e.end)]
    assert flat([ends[0], ends[-1]]) == pytest.approx([0, 0, *end], abs=TOL)


def test_a_split_spline_is_two_drawn_pieces_and_its_end_constraints_follow():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4)], id="s")
    sk.Constrain("s.end", px.H, px.Distance, 0.4, id="drop")
    sk.Move((1.6, -1)).Line(0, 3, px.Fix, id="cut")
    sk.Split("s", "cut")
    assert construction(sk) == {"s.1": False, "s.2": False} and sk.wires["s"] == ["s.1", "s.2"]
    assert repr(sk.constraints["Distance"]["drop"]) == '"s.end", px.H, px.Distance, 0.4'
    sk["drop"] = 0.7
    assert geometry(sk, "s.2")["end"][1] == pytest.approx(-0.7, abs=TOL)
    assert len(sk.Wires()) == 2


def test_a_closed_spline_cut_by_a_chord_gives_a_face():
    sk = px.Sketch()
    pts = [(0.3, -1), (2, 0), (1.5, 1.5), (0, 1), (-0.8, 0.2)]
    sk.Move(tuple(pts[0])).Spline(pts[1:], periodic=True, id="blob")
    sk.Move((-2, 0.4)).Line(5, 0, px.Horizontal, id="chord")
    sk.Constrain("chord.start", px.Fix)
    sk.Trim(("blob", (0.3, -1)), "chord")
    assert construction(sk) == {"blob": False, "blob.trimmed.1": True}
    top = "blob"
    g = geometry(sk, top)
    assert not g["closed"] and g["start"][1] == pytest.approx(0.4, abs=TOL) and g["end"][1] == pytest.approx(0.4, abs=TOL)
    assert all(nearest_on(g["points"], g[k], closed=True) == pytest.approx(0, abs=1e-12) for k in ("start", "end"))
    sk.Trim(("chord", (-1.9, 0.4)))
    sk.Trim(("chord", (2.9, 0.4)))
    g = geometry(sk, top)
    # the area under the piece and the chord back: the curve finely, from where it starts to where it ends
    q = g["points"]
    end = px._planex._Interpolate(q, {}, True)["params"][-1]
    u0, u1 = param_on(q, g["start"], closed=True)[1], param_on(q, g["end"], closed=True)[1]
    if u1 < u0:
        u1 += end
    path = [px._planex._InterpolatedAt(q, {}, True, (u0 + (u1 - u0) * k / 4000) % end, 0) for k in range(4001)]
    area = 0.5 * abs(sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(path, path[1:] + path[:1])))
    assert sk.Face().mass == pytest.approx(area, rel=1e-6)


def trimmed():
    """a spline held by its points, trimmed by a vertical line that can still move sideways"""
    sk = px.Sketch()
    sk.Move((0.5, 0), px.Fix).Spline([(1.5, 1.2), (2.5, 0.4)], id="s")
    sk.Constrain("s.p1", px.Fix)
    sk.Constrain("s.p2", px.Fix)
    sk.Move((2.0, -0.5)).Line(0, 2, px.Vertical, id="cut")
    sk.Constrain("cut.start", px.H, px.Distance, 0.5)
    sk.Trim(("s", (2.4, 0.5)), "cut")
    return sk


def test_a_mirrored_trimmed_spline_follows_its_cut():
    import warnings

    sk = trimmed()
    dof = sk.dof
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Mirrored(["s", "s.trimmed.1"], px.V)
    assert sk.elements["Spline"] == ["s", "s.trimmed.1", "Mirrored.1.s", "Mirrored.1.s.trimmed.1"]
    assert construction(sk) == {"s": False, "s.trimmed.1": True, "Mirrored.1.s": False, "Mirrored.1.s.trimmed.1": True}
    assert (sk.status, sk.dof) == ("under", dof)
    sk.Constrain("cut", px.V, px.Distance, 1.6)
    a, b = geometry(sk, "s"), geometry(sk, "Mirrored.1.s")
    assert a["end"][0] == pytest.approx(1.6, abs=TOL)
    assert b["end"] == pytest.approx((-a["end"][0], a["end"][1]), abs=TOL)
    assert b["start"] == pytest.approx((-0.5, 0), abs=TOL)


def test_a_pattern_of_a_trimmed_spline_turns_its_cut_along():
    sk = trimmed()
    sk.CircularPattern(["s", "s.trimmed.1"], sk.origin, 3)
    sk.Constrain("cut", px.V, px.Distance, 1.6)
    end = geometry(sk, "s")["end"]
    t = 2 * math.pi / 3
    turned = (end[0] * math.cos(t) - end[1] * math.sin(t), end[0] * math.sin(t) + end[1] * math.cos(t))
    assert geometry(sk, "CircularPattern.1.1.s")["end"] == pytest.approx(turned, abs=TOL)


def test_a_free_copy_of_a_trimmed_spline_ends_anywhere_on_its_copy():
    sk = trimmed()
    dof = sk.dof
    sk.Mirrored(["s"], px.V, auto_constraint=False)
    copy = geometry(sk, "Mirrored.1.s")
    assert nearest_on(copy["points"], copy["end"]) == pytest.approx(0, abs=1e-12)
    # its points (3 of them) are free, and where along it it ends
    assert sk.dof == dof + 6 + 1
