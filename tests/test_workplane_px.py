# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import netgen.occ as occ
import pytest

import planex as px

TOL = 1e-9


def xy(p):
    return (p.x, p.y)


def state(wp):
    return [xy(wp.cur_loc), xy(wp.cur_dir), xy(wp.start_pnt)]


def vertex_points(shape):
    return sorted((round(v.p.x, 8) + 0.0, round(v.p.y, 8) + 0.0, round(v.p.z, 8) + 0.0) for v in shape.vertices)


def names(items):
    return sorted(i.name or "" for i in items)


def same_cursor(theirs, ours):
    for a, b in zip(state(ours), state(theirs)):
        assert a == pytest.approx(b, abs=TOL)


def same(draw, area=None):
    """the same drawing on netgen's WorkPlane and ours: cursor, face area, edges, vertices, names"""
    theirs, ours = draw(occ.WorkPlane()), draw(px.WorkPlane())
    same_cursor(theirs, ours)
    t, o_occ = theirs.Face(), ours.ToOCCFace()
    o = ours.Face()
    assert isinstance(o, px.Face)
    assert o.mass == pytest.approx(t.mass, rel=TOL)
    assert o_occ.mass == pytest.approx(t.mass, rel=TOL)
    if area is not None:
        assert o.mass == pytest.approx(area, rel=TOL)
    assert len(o_occ.edges) == len(t.edges)
    assert len(o_occ.vertices) == len(t.vertices)
    assert vertex_points(o_occ) == vertex_points(t)
    assert names(o_occ.edges) == names(t.edges)
    assert names(o_occ.vertices) == names(t.vertices)
    return t, o


@pytest.mark.parametrize("draw", [
    lambda wp: wp.Rectangle(3, 2),
    lambda wp: wp.MoveTo(1, 2).Rectangle(3, 2, name="r"),
    lambda wp: wp.Rotate(30).Rectangle(2, 1),
    lambda wp: wp.RectangleC(2, 4),
    lambda wp: wp.MoveTo(5, 5).Rotate(20).RectangleC(2, 4, name="c"),
    lambda wp: wp.Line(2).Rotate(90).Line(1).Line(-1, 0.5).LineTo(0, 1.5).Close(),
    lambda wp: wp.Direction(0, 1).LineTo(3, 0, name="a").Line(2).LineTo(0, 2).Close(name="z"),
    lambda wp: wp.Line(l=2).Rotate(120).Line(dx=-1, dy=1.5).Close(),
])
def test_polygons(draw):
    same(draw)


@pytest.mark.parametrize("draw", [
    lambda wp: wp.Arc(1, 180).Arc(1, 180),
    lambda wp: wp.Line(2).Arc(1, 90).Line(1).Arc(0.5, 90).Close(),
    lambda wp: wp.Line(4).Rotate(90).Line(2).Rotate(90).Line(1).Arc(0.5, -180).Rotate(180).Line(2).Close(),
    lambda wp: wp.Line(2).Arc(1, 90, name="q").NameVertex("top").Line(1).Rotate(90).Line(3).Close(),
    lambda wp: wp.Line(3).ArcTo(3, 2, (1, 0)).LineTo(0, 2).Close(),
    lambda wp: wp.Line(4).Rotate(90).Line(2).ArcTo(2, 2, (-1, -1)).LineTo(0, 2).Close(),
])
def test_arcs(draw):
    same(draw)


def test_circle_with_reverse_is_a_hole():
    def draw(wp):
        return wp.Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse()
    t, o = same(draw, area=8 - math.pi / 4)


def test_circle_center_form():
    same(lambda wp: wp.Circle(1, 2, 0.75))


@pytest.mark.parametrize("d", [0.25, -0.25])
def test_offset(d):
    same(lambda wp: wp.Rectangle(2, 1).Offset(d))


def test_rotate_direction_move():
    same(lambda wp: wp.Rotate(45).Move(2).Direction(0, 1).Rectangle(1, 3))


def test_moveto_starts_second_wire():
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    for wp in (theirs, ours):
        wp.Rectangle(2, 1).MoveTo(5, 5).Line(1).Rotate(90).Line(1)
    same_cursor(theirs, ours)
    assert len(ours.Wires()) == 1
    assert len(ours.Wire().edges) == 4
    assert len(theirs.Wires()) == 1


def test_names():
    def draw(wp):
        return wp.Line(2, name="bottom").NameVertex("corner").Rotate(90).Line(1, name="right").Rotate(90).Line(2).Close(name="top")
    same(draw)


@pytest.mark.parametrize("periodic", [False, True])
def test_spline(periodic):
    def draw(wp):
        wp.Spline([(1, -1), (2, 0), (1, 1)], periodic=periodic, tangents={0: (1, -1)})
        return wp if periodic else wp.LineTo(0, 0)
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    draw(theirs), draw(ours)
    same_cursor(theirs, ours)
    t, o = theirs.Face(), ours.Face()
    assert o.mass == pytest.approx(t.mass, rel=1e-6)


def test_ellipse():
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    for wp in (theirs, ours):
        wp.MoveTo(1, 2).Rotate(30).Ellipse(3, 1.5)
    same_cursor(theirs, ours)
    assert ours.Face().mass == pytest.approx(math.pi * 3 * 1.5, abs=1e-6)
    assert theirs.Face().mass == pytest.approx(math.pi * 3 * 1.5, abs=1e-6)


def test_non_default_axes():
    def axes():
        return occ.Axes((0, 0, 1), n=(1, 0, 0), h=(0, 1, 0))
    theirs, ours = occ.WorkPlane(axes()), px.WorkPlane(axes())
    for wp in (theirs, ours):
        wp.Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse()
    t, o = theirs.Face(), ours.ToOCCFace()
    assert o.mass == pytest.approx(t.mass, rel=TOL)
    assert vertex_points(o) == vertex_points(t)
    assert ours.Face().mass == pytest.approx(8 - math.pi / 4, rel=TOL)


def test_pos():
    pos = occ.gp_Ax2d(occ.gp_Pnt2d(1, 2), occ.gp_Dir2d(0, 1))
    theirs, ours = occ.WorkPlane(pos=pos), px.WorkPlane(pos=pos)
    for wp in (theirs, ours):
        wp.Line(2).Rotate(90).Line(1)
    same_cursor(theirs, ours)
    assert xy(ours.cur_loc) == pytest.approx((0, 4), abs=TOL)


def test_wires_and_last():
    wp = px.WorkPlane()
    assert wp.Wire() is None
    wp.Rectangle(2, 1).MoveTo(5, 5).Rectangle(1, 1)
    assert len(wp.Wires()) == 2
    assert isinstance(wp.Wire(), px.Wire)
    assert wp.Last() is not None


def test_export_functions():
    wp = px.WorkPlane().Rectangle(2, 1, name="s")
    assert wp.ToOCCFace().mass == pytest.approx(2, rel=TOL)
    assert len(wp.ToOCCWire().edges) == 4
    assert len(wp.ToOCCWires().wires) == 1
    assert wp.ToOCCShape().mass == pytest.approx(2, rel=TOL)


HOLE_WARNING = (r"WorkPlane\.Face\(\): wire 2 lies inside wire 1 and is taken as a hole\. planex finds holes by nesting, "
                r"so Reverse\(\) is not needed; the face is the one netgen returns when the hole is reversed\. A face "
                r"cannot lie inside another face: to keep the inner region as a face of its own, use "
                r"px\.Glue\(\[outer, inner\]\)\.")


def test_hole_without_reverse_is_a_hole_with_warning():
    wp = px.WorkPlane().Rectangle(4, 2).MoveTo(2, 1).Circle(0.5)
    with pytest.warns(px.PlanexWarning, match=HOLE_WARNING):
        face = wp.Face()
    assert face.mass == pytest.approx(8 - math.pi / 4, rel=TOL)


def test_hole_with_reverse_does_not_warn():
    wp = px.WorkPlane().Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse()
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert wp.Face().mass == pytest.approx(8 - math.pi / 4, rel=TOL)


def test_reversed_outer_wire_is_the_normal_face_with_warning():
    wp = px.WorkPlane().Rectangle(4, 2).Reverse()
    with pytest.warns(px.PlanexWarning, match="nesting"):
        assert wp.Face().mass == pytest.approx(8, rel=TOL)


def test_separate_regions_and_an_island_are_a_shape_with_netgen_s_area():
    def two(wp):
        return wp.Rectangle(2, 2).MoveTo(3, 0).Rectangle(2, 2).Face()

    def island(wp):
        return wp.Rectangle(6, 4).MoveTo(1, 1).Rectangle(4, 2).Reverse().MoveTo(2, 1.5).Rectangle(2, 1).Face()

    for build in (two, island):
        theirs, ours = build(occ.WorkPlane()), build(px.WorkPlane())
        assert isinstance(ours, px.Shape) and len(ours.faces) == 2
        assert ours.mass == pytest.approx(theirs.mass, rel=1e-12)
    assert sorted(f.mass for f in island(px.WorkPlane()).faces) == pytest.approx([2, 16], rel=1e-12)
    assert isinstance(px.WorkPlane().Rectangle(2, 2).Face(), px.Face)


def test_wire_grouping_joins_at_a_vertex():
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    for wp in (theirs, ours):
        wp.LineTo(1, 0).MoveTo(2, 0).LineTo(1, 0).Finish()
    assert len(theirs.Wires()) == 1
    wires = ours.Wires()
    assert len(wires) == 1 and isinstance(wires[0], px.Wire) and len(list(wires[0].edges)) == 2
    o = ours.ToOCCWires().wires
    assert len(o) == 1 and len(o[0].edges) == len(theirs.Wires()[0].edges) == 2


def test_gap_and_circle_while_open_raise():
    wp = px.WorkPlane().LineTo(1, 0).MoveTo(3, 0)
    with pytest.raises(ValueError, match=r"Finish\(\)"):
        wp.LineTo(4, 0)
    wp = px.WorkPlane().LineTo(1, 0).MoveTo(2, 0)
    with pytest.raises(ValueError, match=r"Finish\(\)"):
        wp.Circle(0.5)
    with pytest.raises(ValueError, match=r"Finish\(\)"):
        wp.Ellipse(2, 1)


def test_branched_wire():
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    for wp in (theirs, ours):
        wp.LineTo(1, 0).LineTo(2, 0).MoveTo(1, 0).LineTo(1, 1).Finish()
    assert len(theirs.Wires()) == 1 and len(theirs.Wires()[0].edges) == 3
    o = ours.ToOCCWires().wires
    assert len(o) == 1 and len(o[0].edges) == 3
    assert sorted(len(list(w.edges)) for w in ours.Wires()) == [1, 2]
    with pytest.raises(ValueError, match=r"Wires\(\)"):
        ours.Wire()
    assert len(ours.ToOCCWire().edges) == 3


def test_offset_of_reversed_wires():
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    for wp in (theirs, ours):
        wp.Rectangle(4, 2).Reverse().Offset(0.5)
    assert abs(theirs.Face().mass) == pytest.approx(3, rel=TOL)
    with pytest.warns(px.PlanexWarning):
        assert ours.Face().mass == pytest.approx(3, rel=TOL)

    def hole(wp):
        return wp.Rectangle(4, 2).MoveTo(1.5, 0.5).Rectangle(1, 1).Reverse().Offset(0.1)
    t, o = same(hole, area=7.36)

    circle = px.WorkPlane().Circle(0.5).Reverse().Offset(0.1)
    assert circle.Wire().edges[0].edge.radius == pytest.approx(0.4, rel=TOL)
    ring = px.WorkPlane().Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse().Offset(0.1)
    assert ring.Face().mass == pytest.approx(8 - math.pi * 0.16, rel=TOL)


def test_spline_tol():
    theirs, ours = occ.WorkPlane(), px.WorkPlane()
    for wp in (theirs, ours):
        wp.MoveTo(1, 1)
    with pytest.raises(Exception):
        theirs.Spline([(1, 1.05), (2, 0)], tol=0.1)
    with pytest.raises(ValueError, match="tol"):
        ours.Spline([(1, 1.05), (2, 0)], tol=0.1)
    ours.Spline([(1, 1.05), (2, 0)], tol=1e-3)


def test_reversed_spline_is_exact():
    scipy_bspline = pytest.importorskip("scipy.interpolate").BSpline
    wp = px.WorkPlane().Spline([(1, 1), (2, 0), (1, -1)], tangents={0: (1, 0)}).Finish()
    before = wp.Wire().edges[0].edge
    wp.Reverse()
    after = wp.Wire().edges[0].edge
    assert xy(after.start) == pytest.approx(xy(before.end), abs=1e-12)
    assert xy(after.end) == pytest.approx(xy(before.start), abs=1e-12)
    b, a = before.bspline, after.bspline
    orig = scipy_bspline(b["knots"], b["poles"], b["degree"])
    rev = scipy_bspline(a["knots"], a["poles"], a["degree"])
    lo, hi = b["knots"][0], b["knots"][-1]
    for t in [lo + (hi - lo) * i / 40 for i in range(41)]:
        assert rev(lo + hi - t) == pytest.approx(orig(t), abs=1e-12)


def test_face_without_wire():
    theirs = occ.WorkPlane().Face()
    assert theirs is not None
    with pytest.raises(ValueError, match="no finished wire"):
        px.WorkPlane().Face()


def test_face_forgets_wires():
    wp = px.WorkPlane().Rectangle(2, 1)
    wp.Face()
    assert wp.Wires() == []


def test_reverse_reverses_the_wire():
    wp = px.WorkPlane().Line(2, name="a").Arc(1, 90, name="b").Spline([(3, 3), (1, 3)], name="c").Close(name="d")
    before = wp.Wire()
    wp.Reverse()
    after = wp.Wire()
    assert after.IsClosed()
    assert [e.name for e in after.edges] == ["d", "c", "b", "a"]
    assert xy(after.edges[0].start) == pytest.approx(xy(before.edges[-1].end), abs=TOL)
    assert xy(after.edges[-1].end) == pytest.approx(xy(before.edges[0].start), abs=TOL)
    assert px.Face([after]).mass == pytest.approx(px.Face([before]).mass, rel=1e-7)
    wp.Reverse()
    assert xy(wp.Wire().edges[0].start) == pytest.approx(xy(before.edges[0].start), abs=TOL)


def test_reverse_without_wire_raises():
    with pytest.raises(ValueError, match="Reverse"):
        px.WorkPlane().Reverse()


def test_a_point_as_first_argument_starts_the_cursor_on_the_xy_plane():
    wp = px.WorkPlane((1, 2))
    assert (wp.cur_loc.h, wp.cur_loc.v) == (1, 2)
    assert wp.Line(1).Rotate(90).Line(1).Rotate(90).Line(1).Close().Face().mass == pytest.approx(1, abs=1e-12)


def test_copy_takes_the_geometry_as_finished_wires():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(4, 2, 3))
    top = box.faces.Max(occ.Z)
    wp = px.WorkPlane(top, copy=True)
    assert wp.Face().mass == pytest.approx(8, abs=1e-12)
    hole = px.WorkPlane(top, copy=True).MoveTo(2, 1).Circle(0.5).Reverse()
    assert hole.Face().mass == pytest.approx(8 - math.pi / 4, rel=1e-9)
    assert px.WorkPlane(top).Wires() == []


def test_copy_without_geometry_warns():
    with pytest.warns(px.PlanexWarning, match="nothing to copy"):
        px.WorkPlane(copy=True)


def test_overlapping_wires_follow_the_even_odd_rule_of_the_sketches():
    with pytest.warns(px.PlanexWarning, match="wires 1, 2 cross"):
        shape = px.WorkPlane().Rectangle(4, 4).MoveTo(2, 2).Rectangle(4, 4).Face()
    assert isinstance(shape, px.Shape) and sorted(f.mass for f in shape.faces) == pytest.approx([12, 12], rel=1e-12)
    sketch = px.QuickSketch()
    sketch.Move((0, 0)).Rectangle(4, 4)
    sketch.Move((2, 2)).Rectangle(4, 4)
    assert sorted(f.mass for f in sketch.Shape().faces) == pytest.approx(sorted(f.mass for f in shape.faces), rel=1e-12)
