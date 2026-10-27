# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import netgen.occ as occ
import pytest

import planex as px
from helpers import point


def kinds(wire):
    return [type(e.edge).__name__ for e in wire.edges]


def test_lines_stay_lines():
    [w] = px.Project(occ.Segment(occ.Pnt(0, 0, 1), occ.Pnt(2, 3, 5)))
    assert kinds(w) == ["LineEdge"]
    # netgen's Segment stores its end vertex at x = 1.9999999999999998; the projection keeps the vertices exactly
    assert point(w.edges[0].start) + point(w.edges[0].end) == pytest.approx((0, 0, 2, 3))


def test_circles_parallel_to_the_plane_stay_circles():
    [c] = px.Project(occ.Circle(occ.Pnt(1, 2, 3), occ.Z, 2))
    assert kinds(c) == ["CircleEdge"]
    assert all(e.edge.radius == pytest.approx(2) and point(e.edge.center) == (1, 2) for e in c.edges)
    [arc] = px.Project(occ.ArcOfCircle(occ.Pnt(1, 0, 4), occ.Pnt(0, 1, 4), occ.Pnt(-1, 0, 4)))
    a = arc.edges[0]
    assert kinds(arc) == ["ArcEdge"]
    assert point(a.midpoint) == pytest.approx((0, 1))
    b = px.Project(occ.Circle(occ.Pnt(0, 0, 0), -occ.Z, 1))
    assert px.Face(b).mass == pytest.approx(math.pi)


def test_tilted_curves_become_exact_splines():
    tilt = math.radians(60)
    circle = occ.Circle(occ.Pnt(0, 0, 0), occ.Dir(0, math.sin(tilt), math.cos(tilt)), 1)
    [w] = px.Project(circle)
    assert kinds(w) == ["SplineEdge"]
    face = px.Face([w])
    assert face.mass == pytest.approx(math.pi * math.cos(tilt), rel=1e-6)
    assert sum(f.mass for f in face.ToOCCFace().faces) == pytest.approx(math.pi * math.cos(tilt), rel=1e-12)


def test_a_spline_projects_through_the_projected_curve():
    curve = occ.SplineApproximation([occ.Pnt(0, 0, 0), occ.Pnt(1, 2, 1), occ.Pnt(3, 1, 2), occ.Pnt(4, 3, 0)])
    [w] = px.Project(curve)
    assert kinds(w) == ["SplineEdge"]
    flat = w.ToOCCWire(remove_edges_smaller=0, close_gaps_smaller=0).edges[0]
    t0, t1 = curve.parameter_interval
    for k in range(11):
        t = t0 + (t1 - t0) * k / 10
        p, q = curve.Value(t), flat.Value(t)
        assert (q.x, q.y) == pytest.approx((p.x, p.y), abs=1e-12)


def test_curves_seen_edge_on_and_end_on():
    [w] = px.Project(occ.Circle(occ.Pnt(0, 0, 0), occ.X, 1))
    assert kinds(w) == ["LineEdge"]
    assert sorted([point(w.edges[0].start), point(w.edges[0].end)]) == pytest.approx([(0, -1), (0, 1)])
    with pytest.warns(px.PlanexWarning, match="1 point left out"):
        assert px.Project(occ.Segment(occ.Pnt(1, 1, 0), occ.Pnt(1, 1, 4))) == []


def test_names_are_kept_and_projected_onto_any_plane():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3))
    top = box.faces.Max(occ.Z)
    top.edges.name = "top"
    [w] = px.Project(top)
    assert {e.name for e in w.edges} == {"top"}
    assert px.Face([w]).mass == pytest.approx(2)
    # the xz plane sees the top face edge-on: those along x the same segment, the edges along y points at its ends,
    # which add nothing
    side = px.Move((0, 0)).Rectangle(1, 1).Wire().Rotated((0, 0, 0), (1, 0, 0), 90)
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        [s] = side.Project(top)
    ends = [point(p) for e in s.edges for p in (e.start, e.end)]
    assert len(s.edges) == 1 and {round(v, 12) for _, v in ends} == {3}
    assert {round(u, 12) for u, _ in ends} == {0, 2}


def test_sketch_projection_and_points():
    s = px.Move((-1, 0.5)).Project(occ.Segment(occ.Pnt(1, 1, 0), occ.Pnt(1, 1, 4)), name="peg")
    assert [tuple(s._Location(e.local)) for e in s(name="peg")] == [(1, 1)]
    s.Project(occ.Segment(occ.Pnt(2, 2, 0), occ.Pnt(2, 2, 1)), construction=False)
    assert point(s.pen.position) == (-1, 0.5)


def test_sketch_projection_as_construction_or_drawn_edges():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3))
    s = px.Move((0, 0)).Project(box.faces.Max(occ.Z), name="outline")
    assert len(s.Construction().edges) == 4 and s.Wires() == []
    s.Project(box.faces.Max(occ.Z), construction=False)
    assert s.Face().mass == pytest.approx(2)


def test_occ_shapes_as_targets_are_projected_once():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3))
    top = box.faces.Max(occ.Z)
    s = px.Move((-1, 0.5))
    assert point(s.Line(until=top).pen.position) == (0, 0.5)
    assert point(s.Move((-1, 0.25)).Line(top, angle=0, reverse=True).pen.position) == pytest.approx((2, 0.25))
    assert len(s.Construction().edges) == 0
    s = px.QuickSketch().Circle((5, 0), 1, name="c", construction=True)
    s.Move((0, 0)).Line(tangent=occ.Circle(occ.Pnt(5, 0, 2), occ.Z, 1))
    assert point(s.pen.position) == pytest.approx((4.8, math.sqrt(24) / 5))


def test_edges_falling_together_stay_once_with_the_front_name():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3))
    box.faces.Max(occ.Z).edges.name = "top"
    box.faces.Min(occ.Z).edges.name = "bottom"
    # the side edges seen end-on lie on the corners: no points, no warnings
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        [w] = px.Project(box)
        below = px.Line((0, 0), (1, 0)).Translated((0, 0, -4))
        assert {e.name for x in below.Project(box) for e in x.edges} == {"top"}
        s = px.Move((-1, 0.5)).Project(box, construction=False)
    assert len(w.edges) == 4 and {e.name for e in w.edges} == {"top"}
    assert px.Face([w]).mass == pytest.approx(2)
    assert len(s.Wires()[0].edges) == 4 and s.points == {}
    crossing = occ.Compound([occ.Segment(occ.Pnt(0, 1, 0), occ.Pnt(2, 1, 0)), occ.Segment(occ.Pnt(1, 0, 5), occ.Pnt(1, 2, 5))])
    assert sum(len(w.edges) for w in px.Project(crossing)) == 4


def test_a_list_of_shapes_projects_as_their_compound():
    a = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3)).faces.Max(occ.Z)
    b = occ.Box(occ.Pnt(3, 0, 0), occ.Pnt(4, 2, 1)).faces.Max(occ.Z)
    expected = px.Project(occ.Compound([a, b]))
    for given in ([a, b], (a, b)):
        got = px.Project(given)
        assert len(got) == len(expected) == 2
        assert sorted(w.mass for w in got) == pytest.approx(sorted(w.mass for w in expected))
    square = px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Line((0, 1)).Close().Wire()
    for plane in (square, px.Face([square]), px.Unify([square])):
        assert sorted(w.mass for w in plane.Project([a, b])) == pytest.approx([6, 6])
    for make in (px.QuickSketch, px.Sketch):
        sk = make()
        sk.Project([a, b], construction=False)
        assert sorted(f.mass for f in sk.Shape().faces) == pytest.approx([2, 2])


def test_a_list_with_something_else_names_the_call():
    a = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3)).faces.Max(occ.Z)
    with pytest.raises(TypeError, match=r"^Project: 3 is no OCC shape; give a shape or a list of shapes"):
        px.Project([a, 3])
    with pytest.raises(TypeError, match=r"^Project: 'face' is no OCC shape"):
        px.Sketch().Project("face")


def test_intersect3d_takes_a_list_as_one_compound():
    from netgen import occ

    a, b = occ.Box((0, 0, 0), (1, 1, 1)), occ.Box((2, 0, 0), (3, 1, 1))
    cut = px.Intersect3d(px.Plane(px.XY, 0.5), [a, b])
    assert len(cut.faces) == 2 and cut.mass == pytest.approx(2, rel=1e-12)
    sk = px.Sketch(px.Plane(px.XY, 0.5)).Intersect3d((a, b))
    assert len(sk.elements["Line"]) == 8
    with pytest.raises(TypeError, match="Intersect3d: 3 is no OCC shape"):
        px.Intersect3d(px.XY, [a, 3])
