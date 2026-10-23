# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import netgen.occ as occ
import pytest

import planex as px
from helpers import area, ends, is_empty, point, polygon_area, seg, segments

pytest.skip("px.WorkPlane is switched off (later list)", allow_module_level=True)


def test_drawing_moves_the_position():
    wp = px.MoveTo(1, 1)
    assert point(wp.start_pnt) == (1, 1)
    wp.Line(2)
    assert point(wp.cur_loc) == (3, 1)
    wp.Line(0, 2)
    assert point(wp.cur_loc) == (3, 3)
    wp.Line((5, 5))
    assert point(wp.cur_loc) == (5, 5)
    wp.Direction(0, 2)
    assert point(wp.cur_dir) == (0, 1)
    wp.Line(1)
    assert point(wp.cur_loc) == (5, 6)
    wp.Rotate(90)
    assert point(wp.cur_dir) == pytest.approx((-1, 0))
    assert point(wp.start_pnt) == (1, 1)


def test_move_starts_a_new_wire_without_drawing():
    wp = px.MoveTo(0, 0).Line(1).Move(2)
    assert point(wp.cur_loc) == (3, 0)
    assert point(wp.start_pnt) == (3, 0)
    assert len(wp.Wire().lines) == 1


def test_start_position_and_direction():
    wp = px.WorkPlane(pos=occ.gp_Ax2d(occ.gp_Pnt2d(1, 2), occ.gp_Dir2d(0, 1)))
    wp.Line(3)
    assert point(wp.cur_loc) == (1, 5)


def test_arc_turns_left_for_positive_angles():
    wp = px.MoveTo(0, 0).Arc(1, 90)
    assert point(wp.cur_loc) == pytest.approx((1, 1))
    assert point(wp.cur_dir) == pytest.approx((0, 1))
    a = wp.Wire().arcs[0]
    assert point(a.center) == (0, 1)
    assert a.ccw


def test_arc_turns_right_for_negative_angles():
    wp = px.MoveTo(0, 0).Arc(1, -90)
    assert point(wp.cur_loc) == pytest.approx((1, -1))
    a = wp.Wire().arcs[0]
    assert point(a.center) == (0, -1)
    assert not a.ccw


def test_arc_to_with_a_start_tangent():
    wp = px.MoveTo(0, 0).Arc((1, 1), (1, 0))
    assert point(wp.cur_loc) == (1, 1)
    assert point(wp.cur_dir) == pytest.approx((0, 1))
    a = wp.Wire().arcs[0]
    assert point(a.center) == (0, 1)
    assert a.radius == 1


def test_arc_to_with_a_tangent_along_the_chord_fails():
    with pytest.raises(RuntimeError):
        px.MoveTo(0, 0).Arc((1, 0), (1, 0))


def test_zero_length_moves_are_skipped():
    assert is_empty(px.MoveTo(1, 1).Line((1, 1)).Face())


def test_rectangle():
    p = px.MoveTo(1, 2).Rectangle(3, 2).Face()
    assert segments(p) == {seg((1, 2), (4, 2)), seg((4, 2), (4, 4)), seg((4, 4), (1, 4)), seg((1, 4), (1, 2))}
    assert polygon_area(p) == pytest.approx(6)


def test_centered_rectangle_restores_the_position():
    wp = px.MoveTo(5, 5).RectangleC(2, 4)
    assert point(wp.cur_loc) == (5, 5)
    p = wp.Face()
    xs = [x for l in p.lines for x, _ in ends(l)]
    ys = [y for l in p.lines for _, y in ends(l)]
    assert (min(xs), max(xs), min(ys), max(ys)) == (4, 6, 3, 7)


def test_circle():
    p = px.MoveTo(1, 2).Circle(3).Face()
    assert len(p.arcs) == 2
    assert all(point(a.center) == (1, 2) and a.radius == 3 for a in p.arcs)
    assert polygon_area(p) == pytest.approx(9 * math.pi)
    assert polygon_area(px.WorkPlane().Circle(1, 2, 3).Face()) == pytest.approx(9 * math.pi)


def test_close_adds_the_closing_line():
    p = px.MoveTo(0, 0).Line(2).Rotate(90).Line(1).Close().Face()
    assert len(p.lines) == 3
    assert polygon_area(p) == pytest.approx(1)


def test_close_after_returning_to_the_start_adds_nothing():
    p = px.MoveTo(0, 0).Arc(1, 180).Arc(1, 180).Close().Face()
    assert (len(p.lines), len(p.arcs)) == (0, 2)
    assert polygon_area(p) == pytest.approx(math.pi)


def test_finish_closes_a_wire_ending_within_rounding_of_its_start():
    wp = px.MoveTo(1, 0)
    for _ in range(4):
        wp.Arc(1, 90)
    w = wp.Finish().Wire()
    assert (len(w.lines), len(w.arcs)) == (0, 4)
    assert w.arcs[-1].end == w.arcs[0].start
    # the quarters join in the face, up to the one that would close the circle
    p = wp.Face()
    assert (len(p.lines), len(p.arcs)) == (0, 2)
    assert p.mass == pytest.approx(math.pi)


def test_open_wire_warns_when_converted_to_a_polygon():
    with pytest.warns(px.PlanexWarning, match="open wire"):
        px.MoveTo(0, 0).Line((1, 0)).Line((1, 1)).Face()


def test_wires_and_wire():
    wp = px.MoveTo(0, 0).Rectangle(1, 1).MoveTo(5, 5).Rectangle(2, 2)
    assert len(wp.OCCWires().wires) == 2
    assert area(occ.Face(wp.OCCWire())) == pytest.approx(4)


def test_wire_needs_a_finished_wire():
    with pytest.raises(ValueError, match=r"OCCWire\(\): nothing is drawn"):
        px.WorkPlane().OCCWire()


def test_face_of_separate_wires_is_a_compound():
    face = px.MoveTo(0, 0).Rectangle(1, 1).MoveTo(5, 5).Rectangle(2, 2).OCCFace()
    assert len(face.faces) == 2
    assert area(face) == pytest.approx(5)


def test_overlapping_wires_bound_by_parity():
    # all drawn edges bound the region together: the overlap of two squares is outside, a circle inside is a hole
    face = px.MoveTo(0, 0).Rectangle(2, 2).MoveTo(1, 1).Rectangle(2, 2).OCCFace()
    assert area(face) == pytest.approx(6)
    holed = px.MoveTo(0, 0).Rectangle(4, 4).Circle(2, 2, 1).Face()
    assert holed.mass == pytest.approx(16 - math.pi) and len(holed.inner) == 1


def test_cut_in_is_removed():
    wp = px.MoveTo(0, 0).Line((4, 0)).Line((4, 4)).Line((2, 4)).Line((2, 2)).Line((2, 4)).Line((0, 4)).Close()
    p = wp.Face()
    assert segments(p) == {seg((0, 0), (4, 0)), seg((4, 0), (4, 4)), seg((4, 4), (0, 4)), seg((0, 4), (0, 0))}
    assert polygon_area(p) == pytest.approx(16)


def test_every_overlap_of_a_wire_is_removed():
    # the bottom drawn three times goes completely: an open U bounds no region (pairs cancelling would leave a square)
    wp = px.MoveTo(0, 0).Line((4, 0)).Line((0, 0)).Line((4, 0)).Line((4, 4)).Line((0, 4)).Close()
    with pytest.warns(px.PlanexWarning, match="open wire with 2 open ends"):
        assert is_empty(wp.Face())


def test_self_crossing_wire_is_split_at_the_crossing():
    p = px.MoveTo(0, 0).Line((2, 2)).Line((2, 0)).Line((0, 2)).Close().Shape()
    assert len(p.faces) == 2
    assert segments(p) == {
        seg((0, 0), (1, 1)), seg((1, 1), (2, 2)), seg((2, 2), (2, 0)),
        seg((2, 0), (1, 1)), seg((1, 1), (0, 2)), seg((0, 2), (0, 0)),
    }


def test_workplane_rotates_its_drawing_in_place():
    wp = px.MoveTo(0, 0).Rectangle(2, 1).MoveTo(1, 0)
    rotated = wp.Rotated((0, 0), 180)
    assert point(wp.cur_loc) == (1, 0)
    assert point(rotated.cur_loc) == pytest.approx((-1, 0))
    wp.Rotate((0, 0), 180)
    p = wp.Face()
    corners = {(round(l.start.x, 12), round(l.start.y, 12)) for l in p.lines}
    assert corners == {(0, 0), (-2, 0), (-2, -1), (0, -1)}
    assert polygon_area(p) == pytest.approx(2)


def test_axes_place_the_face_in_3d():
    wp = px.WorkPlane(axes=occ.Axes((0, 0, 1), occ.Z, occ.X)).Rectangle(2, 1)
    face = wp.OCCFace()
    assert face.center.z == pytest.approx(1)
    assert area(face) == pytest.approx(2)


@pytest.mark.parametrize("r, ang", [(1, 90), (-1, 90), (-1, 45), (-1, 180), (-1, 270), (1, -90), (-1, -90), (-1, -45)])
def test_arc_with_either_sign_draws_what_netgen_draws(r, ang):
    ours = px.WorkPlane().Line(2).Arc(r, ang).Line(0.5)
    theirs = occ.WorkPlane().Line(2).Arc(r, ang).Line(0.5).Close().Face()
    assert point(ours.cur_loc) == pytest.approx(tuple(theirs.edges[2].end)[:2], abs=1e-14)
    arc = ours.Wire().edges[1]
    assert arc.mass == pytest.approx(theirs.edges[1].mass, rel=1e-14)
    assert point(arc.edge.end) == pytest.approx(tuple(theirs.edges[1].end)[:2], abs=1e-14)
