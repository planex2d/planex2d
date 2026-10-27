# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import numpy as np
import pytest

import planex as px

TOL = 1e-9


def plate():
    return px.Move((0, 0)).Rectangle(4, 4).Face()


def splines(obj):
    return [e for e in obj.edges if isinstance(e.edge, px.SplineEdge)]


def bezier(poles, u, k=0):
    """the k-th derivative at u in [0, 1] of the Bezier curve with these poles"""
    b = np.asarray(poles, float)
    for _ in range(k):
        b = (len(b) - 1) * np.diff(b, axis=0)
    while len(b) > 1:
        b = (1 - u) * b[:-1] + u * b[1:]
    return b[0]


def bending(d1, d2):
    return (d1[0] * d2[1] - d1[1] * d2[0]) / np.linalg.norm(d1) ** 3


def curvature(poles, u):
    return bending(bezier(poles, u, 1), bezier(poles, u, 2))


def poles(e, degree=5):
    b = e.edge.bspline
    assert b["degree"] == degree and len(b["poles"]) == degree + 1
    return b["poles"]


def de_boor(b, u):
    """a point of the B-spline b (poles, flattened knots, degree) at u"""
    t, p, c = np.asarray(b["knots"], float), b["degree"], np.asarray(b["poles"], float)
    k = min(max(int(np.searchsorted(t, u, side="right")) - 1, p), len(c) - 1)
    d = [c[j + k - p].copy() for j in range(p + 1)]
    for r in range(1, p + 1):
        for j in range(p, r - 1, -1):
            a = (u - t[j + k - p]) / (t[j + 1 + k - r] - t[j + k - p])
            d[j] = (1 - a) * d[j - 1] + a * d[j]
    return d[p]


def derived(b):
    """the derivative of the B-spline b, a B-spline of one degree less"""
    t, p, c = np.asarray(b["knots"], float), b["degree"], np.asarray(b["poles"], float)
    q = [p * (c[i + 1] - c[i]) / (t[i + p + 1] - t[i + 1]) for i in range(len(c) - 1)]
    return {"knots": t[1:-1], "degree": p - 1, "poles": q}


def spline_curvature(b, u):
    """the curvature of the B-spline b at u"""
    d1 = derived(b)
    return bending(de_boor(d1, u), de_boor(derived(d1), u))


def smallest_radius(p):
    return 1 / max(abs(curvature(p, u)) for u in np.linspace(0, 1, 2001))


def corner_gap(p, corner):
    return min(np.linalg.norm(bezier(p, u) - corner) for u in np.linspace(0, 1, 2001))


def drawing(sk):
    return {g["id"]: (g["kind"], bool(g["construction"])) for g in sk._Drawing()}


def square(make=px.QuickSketch):
    """the square (0, 0) to (4, 4), drawn a, b, c, d counterclockwise from the origin"""
    sk = make()
    sk.Move((0, 0)).Line((4, 0), id="a").Line((4, 4), id="b").Line((0, 4), id="c").Close(id="d")
    return sk


def test_a_curvature_fillet_keeps_the_arc_s_touch_points_and_is_g2():
    f = plate()
    assert f.Fillet((4, 4), 1, px.Curvature) is f
    (s,) = splines(f)
    p = poles(s)
    assert {tuple(np.round(p[0], 9)), tuple(np.round(p[-1], 9))} == {(4.0, 3.0), (3.0, 4.0)}
    assert curvature(p, 0) == pytest.approx(0, abs=1e-12) and curvature(p, 1) == pytest.approx(0, abs=1e-12)
    # tighter than the arc inside and closer to the corner
    assert 0.6 < smallest_radius(p) < 0.75
    assert 0.3 < corner_gap(p, np.array([4.0, 4.0])) < math.sqrt(2) - 1
    assert 16 - (1 - math.pi / 4) < f.mass < 16
    assert s.id.endswith(".Fillet.1") and len(f.edges) == 5


def test_filleted_with_curvature_rounds_every_corner_tangent_to_the_lines():
    g = plate().Filleted([(0, 0), (4, 0), (4, 4), (0, 4)], 0.5, px.Curvature)
    assert len(splines(g)) == 4 and len(g.edges) == 8
    for s in splines(g):
        p = np.asarray(poles(s))
        for end, nxt in ((p[0], p[1]), (p[-1], p[-2])):
            d = nxt - end
            # the end tangent along h or v: along the side it leaves
            assert min(abs(d[0]), abs(d[1])) == pytest.approx(0, abs=1e-12)
    assert plate().Filleted((4, 4), 1).mass < plate().Filleted((4, 4), 1, px.Curvature).mass


def test_the_blend_takes_up_the_curvature_of_an_arc():
    edges = [px.Line((0, 0), (4, 0)), px.Line((4, 0), (4, 4)), px.Arc((4, 4), (0, 4), (2, 2)), px.Line((0, 4), (0, 0))]
    f = px.Wires(edges)[0].Face()
    tangent = px.Wires(edges)[0].Face().Fillet((4, 4), 0.5)
    f.Fillet((4, 4), 0.5, px.Curvature)
    (s,) = splines(f)
    p = poles(s)
    ends = [np.asarray(p[0]), np.asarray(p[-1])]
    on_arc = [abs(np.linalg.norm(q - (2, 2)) - math.sqrt(8)) < 1e-9 for q in ends]
    assert sorted(on_arc) == [False, True]
    k_arc, k_line = (curvature(p, 1), curvature(p, 0)) if on_arc[1] else (curvature(p, 0), curvature(p, 1))
    assert abs(k_arc) == pytest.approx(1 / math.sqrt(8), rel=1e-12) and k_line == pytest.approx(0, abs=1e-12)
    assert k_arc * curvature(p, 0.5) > 0
    (a,) = [e for e in tangent.edges if isinstance(e.edge, px.ArcEdge) and e.edge.radius == pytest.approx(0.5)]
    assert {tuple(np.round(q, 9)) for q in ends} == {(round(a.start.x, 9), round(a.start.y, 9)), (round(a.end.x, 9), round(a.end.y, 9))}


def test_the_blend_takes_up_the_curvature_of_a_spline():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Curve(("t", "0.1*t^2"), (0, 10)).Line((20, 10))
    sk.Fillet((10, 10), 2, px.Curvature)
    # the blend: the spline that ends on the line
    (s,) = [e for e in sk.Wire().edges if isinstance(e.edge, px.SplineEdge) and 10 - 1e-9 < max(e.start.y, e.end.y)]
    p = np.asarray(poles(s))
    first = p[0][1] < 9.999
    start = p[0] if first else p[-1]
    # the parabola v = 0.1 h^2 bends by 0.2 / (1 + (0.2 h)^2)^1.5
    expected = 0.2 / (1 + (0.2 * start[0]) ** 2) ** 1.5
    k = curvature(p, 0) if first else -curvature(p, 1)
    assert start[1] == pytest.approx(0.1 * start[0] ** 2, abs=1e-9)
    assert k == pytest.approx(expected, rel=1e-8)


def test_a_curvature_fillet_that_does_not_fit_raises_as_the_arc_does():
    with pytest.raises(ValueError, match="the largest that fits is radius 4"):
        plate().Fillet((4, 4), 5, px.Curvature)
    with pytest.raises(ValueError, match="continuity is px.Tangent or px.Curvature"):
        plate().Fillet((4, 4), 1, px.Distance)


def test_a_quick_sketch_fillet_with_curvature_is_one_spline():
    sk = square().Fillet("a.end", 1, px.Curvature)
    d = drawing(sk)
    assert d["Fillet.1"] == ("Spline", False)
    assert not any(i.startswith("Fillet.1.") for i in d)
    assert sk.Location("a.end") == pytest.approx((3, 0), abs=TOL) and sk.Location("b.start") == pytest.approx((4, 1), abs=TOL)
    assert 16 - (1 - math.pi / 4) < sk.Face().mass < 16
    assert sk.elements["Spline"] == ["Fillet.1"]


@pytest.mark.constraints
def test_a_sketch_curvature_fillet_hides_its_arc_and_follows_r():
    sk = square(px.Sketch).Fillet("a.end", 1, px.Curvature)
    sk.Constrain("a", px.Horizontal).Constrain("b", px.Vertical).Constrain("a.start", px.Fix).Constrain("b.end", px.Fix)
    d = drawing(sk)
    assert d["Fillet.1"] == ("Spline", False)
    assert not any(i.startswith("Fillet.1.") for i in d)
    assert "Arc" not in sk.elements and sk.elements["Spline"] == ["Fillet.1"]
    assert sk["Fillet.1.r"] == pytest.approx(1)
    assert all(".arc" not in r for c in sk._Constraints() for r in c["refs"])
    sk["Fillet.1.r"] = 1.5
    sk.Update()
    assert sk.Location("a.end") == pytest.approx((2.5, 0), abs=1e-9) and sk.Location("b.start") == pytest.approx((4, 1.5), abs=1e-9)
    (s,) = [e for e in sk.Face().edges if isinstance(e.edge, px.SplineEdge)]
    p = poles(s)
    assert curvature(p, 0) == pytest.approx(0, abs=1e-9) and curvature(p, 1) == pytest.approx(0, abs=1e-9)
    assert 0.6 * 1.5 < smallest_radius(p) < 0.75 * 1.5
    assert "<path" in sk._repr_svg_()


@pytest.mark.constraints
def test_a_distance_to_the_blend_acts_through_r():
    sk = square(px.Sketch).Fillet("a.end", 1, px.Curvature)
    sk.Constrain("a.trimmed.1.end", px.Fix)
    sk.Remove("Fillet.1.r")
    sk.Constrain("a.trimmed.1.end", "Fillet.1", px.Distance, 0.2)
    m = sk.Measure("a.trimmed.1.end", to="Fillet.1")
    assert m["distance"] == pytest.approx(0.2, abs=1e-8)


@pytest.mark.constraints
def test_the_elements_a_blend_joins_stay_while_it_does():
    sk = square(px.Sketch).Fillet("a.end", 1, px.Curvature)
    with pytest.raises(ValueError, match="'Fillet.1' is computed from it"):
        sk.Remove("b")
    sk.Remove("Fillet.1")
    assert "Fillet.1" not in drawing(sk) and "Radius" not in sk.constraints
    assert sk.Location("a.end") == pytest.approx((3, 0), abs=TOL)


@pytest.mark.constraints
def test_a_second_fillet_on_the_same_line_keeps_the_first_blend_on_it():
    sk = square(px.Sketch).Fillet(["a.end", "b.end"], 0.5, px.Curvature)
    sk.Constrain("a", px.Horizontal).Constrain("b", px.Vertical).Constrain("c", px.Horizontal).Constrain("a.start", px.Fix)
    assert sk.elements["Spline"] == ["Fillet.1", "Fillet.2"]
    sk["Fillet.1.r"] = 1
    sk.Update()
    f = sk.Face()
    assert len(splines(f)) == 2
    for s in splines(f):
        p = poles(s)
        assert curvature(p, 0) == pytest.approx(0, abs=1e-9) and curvature(p, 1) == pytest.approx(0, abs=1e-9)


@pytest.mark.constraints
def test_a_mirrored_blend_joins_the_mirrored_lines():
    sk = px.Sketch()
    sk.Move((1, 0)).Line((4, 0), id="a").Line((4, 4), id="b")
    sk.Fillet("a.end", 1, px.Curvature)
    sk.Constrain("a", px.Horizontal).Constrain("b", px.Vertical).Constrain("a.start", px.Fix).Constrain("b.end", px.Fix)
    sk.Mirrored(["a", "b", "Fillet.1"], px.V)
    assert len(sk.elements["Spline"]) == 2 and "Arc" not in sk.elements
    ends = []
    for s in [e for w in sk.Wires() for e in w.edges if isinstance(e.edge, px.SplineEdge)]:
        p = poles(s)
        assert curvature(p, 0) == pytest.approx(0, abs=1e-9) and curvature(p, 1) == pytest.approx(0, abs=1e-9)
        ends.append(sorted((round(q[0], 9), round(q[1], 9)) for q in (p[0], p[-1])))
    assert sorted(ends) == [[(-4.0, 1.0), (-3.0, 0.0)], [(3.0, 0.0), (4.0, 1.0)]]
    sk["Fillet.1.r"] = 0.5
    sk.Update()
    assert sk.Location("a.end") == pytest.approx((3.5, 0), abs=1e-9)


def two_lines(make=px.QuickSketch):
    sk = make()
    sk.Move((0, 0)).Line((4, 0), id="a")
    sk.Move((6, 1)).Line((10, 1), id="b")
    return sk


def connected(sk):
    (s,) = [e for e in sk.Wires() for e in e.edges if e.id.endswith("Connect.1")] or [None] * 1
    return s


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_connect_joins_the_nearest_ends_tangent_by_default(make):
    sk = two_lines(make).Connect("a", "b")
    d = drawing(sk)
    assert d["Connect.1"] == ("Spline", False)
    g = sk._Geometry("Connect.1")
    assert g["start"] == pytest.approx((4, 0)) and g["end"] == pytest.approx((6, 1))


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_connect_with_curvature_or_a_point(make):
    sk = two_lines(make).Connect("a", "b", px.Curvature)
    assert sk._Geometry("Connect.1")["start"] == pytest.approx((4, 0))
    sk = two_lines(make).Connect("a", "b", px.Coincident)
    assert drawing(sk)["Connect.1"] == ("Line", False)


def test_connect_reverse_and_named_ends():
    sk = two_lines().Connect("a", "b", reverse=True)
    g = sk._Geometry("Connect.1")
    assert g["start"] == pytest.approx((0, 0)) and g["end"] == pytest.approx((10, 1))
    sk = two_lines().Connect("a.start", "b.start", px.Curvature)
    g = sk._Geometry("Connect.1")
    assert g["start"] == pytest.approx((0, 0)) and g["end"] == pytest.approx((6, 1))


def test_connect_raises_for_what_it_cannot_join():
    with pytest.raises(ValueError, match="both ends are on one element"):
        two_lines().Connect("a", "a.end")
    with pytest.raises(ValueError, match="reverse=True"):
        two_lines().Connect("a.end", "b.start", reverse=True)
    with pytest.raises(ValueError, match="continuity is px.Coincident, px.Tangent or px.Curvature"):
        two_lines().Connect("a", "b", px.Distance)
    sk = two_lines()
    sk.Move((4, 0)).Line((4, -3), id="c")
    with pytest.raises(ValueError, match="no gap"):
        sk.Connect("a.end", "c.start")


def test_connect_with_curvature_is_a_quintic_flat_at_lines():
    sk = two_lines().Connect("a", "b", px.Curvature)
    (s,) = [e for w in sk.Wires() for e in w.edges if isinstance(e.edge, px.SplineEdge)]
    p = poles(s)
    assert curvature(p, 0) == pytest.approx(0, abs=1e-12) and curvature(p, 1) == pytest.approx(0, abs=1e-12)
    # leaving a along +h, arriving at b along +h
    assert p[1][1] == pytest.approx(p[0][1], abs=1e-12) and p[-2][1] == pytest.approx(p[-1][1], abs=1e-12)
    cubic = two_lines().Connect("a", "b")
    (c,) = [e for w in cubic.Wires() for e in w.edges if isinstance(e.edge, px.SplineEdge)]
    assert c.edge.bspline["degree"] == 3


@pytest.mark.constraints
def test_a_connection_follows_the_lines_it_joins():
    sk = two_lines(px.Sketch).Connect("a", "b", px.Curvature)
    sk.Constrain("a.end", "b.start", px.Distance, 4)
    (s,) = [e for w in sk.Wires() for e in w.edges if isinstance(e.edge, px.SplineEdge)]
    p = np.asarray(poles(s))
    assert np.linalg.norm(p[-1] - p[0]) == pytest.approx(4, rel=1e-9)
    assert curvature(p, 0) == pytest.approx(0, abs=1e-9) and curvature(p, 1) == pytest.approx(0, abs=1e-9)
    with pytest.raises(ValueError, match="'Connect.1' is computed from it"):
        sk.Remove("a")


@pytest.mark.constraints
def test_curvature_as_a_constraint_between_an_arc_and_a_spline():
    sk = px.Sketch()
    sk.Move((0, 0)).Arc((4, 4), center=(4, 0), id="arc").Spline([(6, 7), (9, 6)], id="s")
    sk.Constrain("arc", "s", px.Curvature)
    r = sk._Geometry("arc")["r"]
    joint = np.asarray(sk.Location("arc.end"))
    (e,) = [e for w in sk.Wires() for e in w.edges if isinstance(e.edge, px.SplineEdge)
            and min(np.linalg.norm(np.subtract((q.x, q.y), joint)) for q in (e.start, e.end)) < 1e-9]
    b = e.edge.bspline
    first = np.linalg.norm(np.subtract(b["poles"][0], joint)) < 1e-9
    k = spline_curvature(b, b["knots"][0]) if first else -spline_curvature(b, b["knots"][-1])
    assert abs(k) == pytest.approx(1 / r, rel=1e-5)
    assert "<path" in sk._repr_svg_()


@pytest.mark.constraints
def test_a_line_and_an_arc_cannot_be_curvature_continuous():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((4, 0), id="l").Arc((8, 4), center=(4, 4), id="arc")
    sk.Constrain("l", "arc", px.Tangent)
    with pytest.warns(px.PlanexWarning, match="no solution"):
        sk.Constrain("l", "arc", px.Curvature)
