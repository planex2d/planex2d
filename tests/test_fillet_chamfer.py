# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def plate():
    """the square (0, 0) to (4, 4); counterclockwise the edge arriving at (4, 4) is the right one"""
    f = px.Move((0, 0)).Rectangle(4, 4).Face()
    for e in f.edges:
        e.name = {(4.0, 0.0): "right", (4.0, 4.0): "top"}.get((round(e.start.x, 9), round(e.start.y, 9)), e.name)
    return f


def arcs(obj):
    return [e for e in obj.edges if isinstance(e.edge, px.ArcEdge)]


def lines_through(obj, p):
    return [e for e in obj.edges if (e.start.x, e.start.y) == pytest.approx(p) or (e.end.x, e.end.y) == pytest.approx(p)]


def test_a_fillet_rounds_one_corner_in_place():
    f = plate()
    assert f.Fillet((4, 4), 1) is f
    assert f.mass == pytest.approx(16 - (1 - math.pi / 4), rel=TOL)
    (a,) = arcs(f)
    assert a.edge.radius == pytest.approx(1, rel=TOL) and (a.edge.center.x, a.edge.center.y) == pytest.approx((3, 3), abs=TOL)
    assert len(f.edges) == 5 and a.id.endswith(".Fillet.1")
    assert a.name == "right"
    assert not lines_through(f, (4, 4))


def test_filleted_leaves_the_original():
    f = plate()
    g = f.Filleted([(4, 4), (0, 0)], 0.5, id="round")
    assert len(f.edges) == 4 and len(arcs(g)) == 2
    assert g.mass == pytest.approx(16 - 2 * 0.25 * (1 - math.pi / 4), rel=TOL)
    assert g.id == "round" and sorted(a.id[len(g.id) + 1:] for a in arcs(g)) == ["Fillet.1", "Fillet.2"]


def test_a_chamfer_cuts_the_corner_symmetric_or_by_two_lengths():
    f = plate().Chamfer((4, 4), 1)
    assert f.mass == pytest.approx(16 - 0.5, rel=TOL) and len(f.edges) == 5
    two = plate().Chamfered((4, 4), 1, d2=2)
    assert two.mass == pytest.approx(16 - 1, rel=TOL)
    # d on the first edge (the right one, arriving counterclockwise), d2 on the top one
    assert lines_through(two, (4, 3)) and lines_through(two, (2, 4))
    back = plate().Chamfered((4, 4), 1, d2=2, reverse=True)
    assert lines_through(back, (4, 2)) and lines_through(back, (3, 4))
    angled = plate().Chamfered((4, 4), 1, angle=45)
    assert angled.mass == pytest.approx(16 - 0.5, rel=TOL)
    with pytest.raises(ValueError, match="Chamfer"):
        plate().Chamfered((4, 4), 1, d2=2, angle=30)


def test_corners_by_vertex_name_view_and_list():
    f = plate()
    for v in f.vertices:
        if (v.h, v.v) in ((4.0, 4.0), (0.0, 4.0)):
            v.name = "upper"
    assert len(arcs(f.Filleted("upper", 1))) == 2
    corner = [v for v in f.vertices if (v.h, v.v) == (0.0, 0.0)][0]
    assert len(arcs(f.Filleted(corner, 1))) == 1
    assert len(arcs(f.Filleted([(0, 0), (4, 0)], 1))) == 2


def test_a_radius_that_does_not_fit_raises_with_the_largest_one():
    with pytest.raises(ValueError, match=r"Fillet") as e:
        plate().Fillet((4, 4), 5)
    assert "4" in str(e.value)
    f = plate()
    with pytest.raises(ValueError):
        f.Fillet((2, 2), 1)
    assert len(f.edges) == 4


def test_the_viable_corners_are_done_and_the_others_warned():
    with pytest.warns(px.PlanexWarning):
        f = plate().Filleted([(4, 4), (2, 2)], 1)
    assert len(arcs(f)) == 1


def test_an_open_end_and_a_shared_vertex_are_no_corners():
    w = px.Move((0, 0)).Line((4, 0)).Line((4, 4)).Wire()
    with pytest.raises(ValueError):
        w.Fillet((0, 0), 1)
    assert len(arcs(w.Filleted((4, 0), 1))) == 1
    two = px.Glue([px.Move((0, 0)).Rectangle(2, 2).Face(), px.Move((2, 0)).Rectangle(2, 2).Face()])
    with pytest.raises(ValueError):
        two.Fillet((2, 2), 0.5)


def test_a_fillet_between_a_line_and_an_arc_touches_both():
    edges = [px.Line((0, 0), (4, 0)), px.Line((4, 0), (4, 4)), px.Arc((4, 4), (0, 4), (2, 2)), px.Line((0, 4), (0, 0))]
    f = px.Wires(edges)[0].Face()
    before = f.mass
    f.Fillet((4, 4), 0.5)
    new = [a for a in arcs(f) if a.edge.radius == pytest.approx(0.5)]
    assert len(new) == 1
    c = new[0].edge.center
    assert c.x == pytest.approx(3.5, abs=TOL)
    assert math.hypot(c.x - 2, c.y - 2) == pytest.approx(math.sqrt(8) - 0.5, abs=TOL)
    assert f.mass < before


def test_a_fillet_at_a_spline_is_tangent_to_it():
    edges = [px.Line((0, 0), (4, 0)), px.Line((4, 0), (4, 4)), px.Spline([(4, 4), (2, 5), (0, 4)]),
             px.Line((0, 4), (0, 0))]
    f = px.Wires(edges)[0].Face()
    f.Fillet((4, 4), 0.3)
    (a,) = [a for a in arcs(f) if a.edge.radius == pytest.approx(0.3)]
    c = a.edge.center
    spline = [e for e in f.edges if isinstance(e.edge, px.SplineEdge)][0]
    at_start = any((spline.start.x, spline.start.y) == pytest.approx((p.x, p.y), abs=1e-9) for p in (a.start, a.end))
    joint = spline.start if at_start else spline.end
    t = spline.TangentAt(0.0 if at_start else 1.0)
    # TangentAt follows the spline's arc approximation, within config.spline_tol of the curve
    assert abs(t.x * (joint.x - c.x) + t.y * (joint.y - c.y)) == pytest.approx(0, abs=1e-4)
    assert math.hypot(joint.x - c.x, joint.y - c.y) == pytest.approx(0.3, abs=1e-9)


def test_fillets_may_use_an_edge_up():
    circle = plate().Fillet([(0, 0), (4, 0), (4, 4), (0, 4)], 2)
    assert len(circle.edges) == 4 and len(arcs(circle)) == 4
    assert circle.mass == pytest.approx(math.pi * 4, rel=TOL)
    slot = px.Move((0, 0)).Rectangle(6, 2).Face().Fillet([(0, 0), (6, 0), (6, 2), (0, 2)], 1)
    assert len(slot.edges) == 6 and slot.mass == pytest.approx(4 * 2 + math.pi, rel=TOL)
    quarter = plate().Fillet((4, 4), 4)
    (a,) = arcs(quarter)
    assert (a.edge.center.x, a.edge.center.y) == pytest.approx((0, 0), abs=TOL) and len(quarter.edges) == 3
    assert quarter.mass == pytest.approx(4 * math.pi, rel=TOL)
    cut = plate().Chamfer((4, 4), 4)
    assert len(cut.edges) == 3 and cut.mass == pytest.approx(8, rel=TOL)


def test_the_largest_size_is_named_as_it_fits():
    with pytest.raises(ValueError, match=r"the largest that fits is radius 4\b"):
        plate().Fillet((4, 4), 5)
    with pytest.warns(px.PlanexWarning, match=r"the largest that fits is radius 1\b"):
        plate().Fillet([(4, 0), (4, 4)], 3)
