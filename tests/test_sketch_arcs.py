# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px
from helpers import ends, point


def walls():
    """a construction line x = 5 from y = -5 to 25"""
    return px.Move((5, -5)).Line((5, 25), id="wall", construction=True).Move((0, 0))


def last_arc(sketch):
    return sketch.Wire().edges[-1]


def test_netgen_arc_is_unchanged():
    s = px.Move((0, 0)).Arc(1, 90)
    assert point(s.pen.position) == pytest.approx((1, 1))
    s = px.Move((0, 0)).Arc(1, -90)
    assert point(s.pen.position) == pytest.approx((1, -1))


def test_continuation_until_and_to():
    y = 10 - math.sqrt(75)
    assert point(walls().Arc(10, until="wall").pen.position) == pytest.approx((5, y))
    assert point(walls().Arc(-10, until="wall").pen.position) == pytest.approx((5, -y))
    with pytest.raises(ValueError, match=r"Arc\(3.0, until='wall'\): nothing ahead"):
        walls().Arc(3, until="wall")
    assert point(walls().Arc(10, until=px.first).pen.position) == pytest.approx((5, y))
    assert point(walls().Arc(10, until=px.last).pen.position) == pytest.approx((5, 20 - y))


def test_direction_continues_tangentially():
    s = walls().Arc(10, until="wall")
    c = (0, 10)
    e = s.pen.position
    d = s.pen.direction
    assert d[0] * (e[0] - c[0]) + d[1] * (e[1] - c[1]) == pytest.approx(0, abs=1e-12)


def test_arcs_around_a_center_or_concentric():
    s = px.Move((0, 0)).Line((4, 0), id="base").Vertex("o", (2, 0))
    assert point(s.Copy().Move((4, 0)).Arc(concentric="o", sweep=90).pen.position) == pytest.approx((2, 2))
    assert point(s.Copy().Move((4, 0)).Arc(center=(2, 0), sweep=-90).pen.position) == pytest.approx((2, -2))
    assert point(s.Copy().Move((4, 0)).Arc("base", center="o").pen.position) == pytest.approx((0, 0))
    # the current position projected onto the circle of radius 1
    t = s.Copy().Move((5, 0)).Arc(1, center="o", sweep=180, id="f")
    assert sum(ends_of(t.Wire("f").edges[0]), ()) == pytest.approx((3, 0, 1, 0))
    with pytest.raises(ValueError, match="no element 'q'"):
        s.Copy().Arc(center="q", sweep=10)


def ends_of(edge):
    return [point(edge.start), point(edge.end)]


def test_concentric_with_an_arc():
    s = px.Move((1, 0)).Arc(1, 180, id="half")
    s.Move((3, 0)).Arc(concentric="half", sweep=90, id="q")
    a = s.Wire("q").edges[0]
    assert point(a.center) == pytest.approx(point(s.Wire("half").edges[0].center))
    assert a.edge.radius == pytest.approx(math.sqrt(5))
    with pytest.raises(ValueError, match="concentric needs an arc, a circle or a point"):
        px.Move((0, 0)).Line((1, 0), id="l").Arc(concentric="l", sweep=10)


def test_congruent_from_and_to_pick_the_side():
    s = px.QuickSketch().Circle((0, 0), 2, id="c", construction=True)
    s.Move((-3, 1)).Line((3, 1), id="top", construction=True).Move((-3, -1)).Line((3, -1), id="bottom", construction=True)
    up = s.Copy().Arc(("bottom", (3, 0)), ("top", (3, 0)), coincident="c")
    a = last_arc(up)
    assert a.edge.ccw and a.midpoint.x == pytest.approx(2)
    down = s.Copy().Arc(("top", (3, 0)), ("bottom", (3, 0)), coincident="c")
    assert last_arc(down).midpoint.x == pytest.approx(-2)


def test_fillet_between_two_lines():
    s = px.Move((0, 0)).Line((2, 0), id="a").Line((2, 2), id="b")
    s.Arc("a", "b", 0.5, tangent=("a", "b"), id="f")
    f = s.Wire("f").edges[0]
    assert sum(ends_of(f), ()) == pytest.approx((1.5, 0, 2, 0.5))
    assert point(f.center) == pytest.approx((1.5, 0.5))
    assert f.edge.radius == pytest.approx(0.5)
    # touching the extensions of a and b
    big = px.Move((0, 0)).Line((2, 0), id="a").Line((2, 2), id="b").Arc("a", "b", 5, tangent=("a", "b"), id="f").Wire("f").edges[0]
    assert sum(ends_of(big), ()) == pytest.approx((-3, 0, 2, 5))
    with pytest.raises(ValueError, match="an arc touching two elements goes from the one to the other"):
        s.Arc("a", "b", tangent=("a", "b"))


def test_fillet_between_a_line_and_a_circle():
    s = px.Move((-4, 0)).Line((4, 0), id="floor").Circle((0, 1.2), 1, id="ball")
    s.Arc("floor", "ball", 0.5, tangent=(("floor", (1.5, 0.5)), ("ball", (1.5, 0.5))), id="f")
    f = s.Wire("f").edges[0]
    c = f.center
    assert point(c) == pytest.approx((math.sqrt(1.5 ** 2 - 0.7 ** 2), 0.5))
    assert point(f.start) == pytest.approx((c.x, 0))
    assert math.dist(point(f.end), (0, 1.2)) == pytest.approx(1)


def test_blend_into_a_line_and_into_a_circle():
    wall = px.Line((3, -5), (3, 5))
    s = px.Move((0, 2)).Arc(wall, tangent=wall)
    a = last_arc(s)
    assert point(s.pen.position) == pytest.approx((3, 5))
    assert a.edge.radius == pytest.approx(3)
    assert point(s.pen.direction) == pytest.approx((0, 1))
    s = px.QuickSketch().Circle((5, 0), 1, id="c", construction=True).Move((0, 0))
    s.Arc("c", tangent="c")
    a = last_arc(s)
    t = s.pen.position
    assert math.dist(point(t), (5, 0)) == pytest.approx(1)
    assert math.dist(point(a.center), (5, 0)) == pytest.approx(a.edge.radius + 1) or \
        math.dist(point(a.center), (5, 0)) == pytest.approx(abs(a.edge.radius - 1))
    with pytest.raises(ValueError, match=r"Arc\(1.0, tangent='c'\): tangent='c' gives no end; the arc ending touching it is "
                                         r"Arc\('c', tangent='c'\)"):
        s.Arc(1, tangent="c")
    with pytest.raises(ValueError, match=r"an arc ending touching 'c' leaves the pen along its direction; Arc\('c', "
                                         r"tangent='c'\) takes nothing else"):
        s.Arc("c", 1, tangent="c")


def test_arc_errors_and_construction():
    with pytest.raises(ValueError, match="give a radius, center"):
        walls().Arc(until="wall")
    with pytest.raises(ValueError, match=r"Arc\('wall'\): an element as an end needs the circle: center=, concentric= or "
                                         r"coincident=; Arc\(r, until=<target>\)"):
        walls().Arc("wall")
    with pytest.raises(ValueError, match="coincident takes the radius"):
        px.Move((1, 0)).Arc(1, 90, id="q").Arc(2, coincident="q", sweep=10)
    with pytest.raises(TypeError, match=r"Arc\(ccw=...\): ccw is gone"):
        px.Move((0, 0)).Arc(1, 90, ccw=False)
    with pytest.raises(TypeError, match=r"Arc\(r, sweep, ...\): on a circle given by center=, concentric= or coincident= the "
                                        r"sweep is sweep="):
        px.Move((0, 0)).Arc(1, 90, center=(0, 1))
    s = walls().Arc(10, construction=True, until="wall")
    assert s.Wires() == []
    assert len(s.Construction().edges) == 2


def test_near_picks_a_fillet_among_all_positions():
    """#15: the right edge of a tilt bracket crosses the R62 circle twice at a shallow angle; the default and reverse
    round the corners outside the circle, a point with each target reaches every one, also the small fillet inside the
    circle"""
    def fillet(at=None, **kw):
        s = px.QuickSketch().Circle((0, 0), 62, id="out", construction=True)
        b = (70 * math.cos(math.radians(-20)), 70 * math.sin(math.radians(-20)))
        s.Move(tuple(b)).Line(14, angle=30, id="nB", construction=True)
        t = s.Wire("nB").edges[0].end
        s.Move((t.x, t.y)).Line(200, angle=120, id="rightc", construction=True)
        s.Arc("rightc", "out", 6, tangent=("rightc", "out") if at is None else (("rightc", at), ("out", at)), id="f", **kw)
        c = s.Wire("f").edges[0].center
        return round(c.x, 3), round(c.y, 3), round(math.hypot(c.x, c.y), 9)

    assert fillet() == (67.2, -10.403, 68)
    assert fillet(reverse=True) == (24.591, 63.398, 68)
    assert fillet((24.6, 63.4)) == (24.591, 63.398, 68)
    assert fillet((36, 43)) == (36.846, 42.17, 56)
    assert fillet((46.3, 49.8)) == (46.292, 49.81, 68)
    assert fillet((55, 11))[2] == 56


def full_turn(sketch, r, ang):
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        return sketch.Arc(r, ang)


def circle_of(sketch, id="Circle.1"):
    [edge] = sketch.Wire(id).edges
    assert isinstance(edge.edge, px.CircleEdge)
    return edge


def test_a_full_turn_is_one_circle_and_the_pen_ends_as_it_started():
    s = full_turn(px.QuickSketch(), 1, 360)
    assert point(s.pen.position) == (0, 0) and point(s.pen.direction) == (1, 0)
    [e] = s.Wire().edges
    assert isinstance(e.edge, px.CircleEdge) and e.edge.ccw
    assert point(e.edge.center) == (0, 1) and point(e.start) == (0, 0)
    assert s.Face().mass == pytest.approx(math.pi, rel=1e-14)


def test_a_full_turn_after_a_line_continues_the_chain():
    s = px.QuickSketch().Move((2, 3)).Line(2)
    full_turn(s, 0.5, 360)
    assert point(s.pen.position) == (4, 3) and point(s.pen.direction) == (1, 0)
    s.Line(1)
    c = circle_of(s)
    assert point(c.edge.center) == (4, 3.5) and point(c.start) == (4, 3)
    assert ends(s.Wire("Line.2").edges[0]) == ((4, 3), (5, 3))


def test_a_negative_full_turn_goes_clockwise():
    s = px.QuickSketch().Line(1)
    full_turn(s, 1, -360)
    assert point(s.pen.position) == (1, 0) and point(s.pen.direction) == (1, 0)
    c = circle_of(s)
    assert point(c.edge.center) == (1, -1) and not c.edge.ccw and point(c.start) == (1, 0)


@pytest.mark.parametrize("ang", [720, -1080, math.nextafter(360.0, 0.0)])
def test_every_multiple_of_a_full_turn_within_rounding_is_one_circle(ang):
    s = full_turn(px.QuickSketch(), 1, ang)
    assert len(s.Wire().edges) == 1
    assert s.Face().mass == pytest.approx(math.pi, rel=1e-14)


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_full_turn_around_a_given_center_is_a_circle_through_the_pen(make):
    s = make().Move((3, 0))
    s.Arc(center=(3, 2), sweep=-360, id="c")
    c = circle_of(s, "c")
    assert point(c.edge.center) == pytest.approx((3, 2)) and c.edge.radius == pytest.approx(2)
    assert point(c.start) == pytest.approx((3, 0)) and not c.edge.ccw
    assert point(s.pen.position) == pytest.approx((3, 0))


def test_a_circle_element_has_no_start_end_or_mid():
    s = px.Sketch().Circle((0, 0), 1, id="ring")
    for part in ("start", "end", "mid"):
        with pytest.raises(ValueError, match=r"a circle has no start, end or mid point; its point is 'ring.center'"):
            s.Location("ring." + part)
    assert s.Location("ring.center") == (0, 0)


@pytest.mark.skip(reason="px.WorkPlane is switched off (later list)")
def test_a_full_turn_on_a_workplane():
    w = full_turn(px.WorkPlane(), 2, 360)
    assert isinstance(w, px.WorkPlane)
    assert point(w.cur_loc) == (0, 0) and point(w.cur_dir) == (1, 0)
    assert len(w.Wire().edges) == 2
    assert w.Face().mass == pytest.approx(4 * math.pi, rel=1e-14)


def floor_and_ring(cy, R):
    """the construction line y = 0 and a circle "ring" around (0, cy), the pen far left"""
    s = px.Move((-10, 0)).Line((10, 0), id="floor", construction=True)
    return s.Circle((0, cy), R, id="ring", construction=True).Move((-8, 0))


def fillet_center(s, r, hint, pen=(-8, 0)):
    s.Move(tuple(pen)).Arc("floor", "ring", r, tangent=(("floor", (0, 1)), ("ring", hint)), id="f")
    return point(s.Wire("f").edges[0].center)


def test_a_point_inside_a_larger_circle_gives_the_nested_fillet_and_outside_the_outer_one():
    nested = fillet_center(floor_and_ring(1, 2), 0.5, (1, 1.5))
    assert nested == pytest.approx((math.sqrt(2), 0.5), abs=1e-12)
    assert math.dist(nested, (0, 1)) == pytest.approx(2 - 0.5, abs=1e-12)
    for hint, x in (((3, 3), math.sqrt(6)), ((-3, 3), -math.sqrt(6))):
        outer = fillet_center(floor_and_ring(1, 2), 0.5, hint)
        assert outer == pytest.approx((x, 0.5), abs=1e-12)
        assert math.dist(outer, (0, 1)) == pytest.approx(2 + 0.5, abs=1e-12)


def test_a_point_inside_a_smaller_circle_gives_the_fillet_around_it_and_outside_the_outer_one():
    around = fillet_center(floor_and_ring(2.5, 2), 3, (1, 2.5))
    assert around == pytest.approx((-math.sqrt(3) / 2, 3), abs=1e-12)
    assert math.dist(around, (0, 2.5)) == pytest.approx(3 - 2, abs=1e-12)
    outer = fillet_center(floor_and_ring(2.5, 2), 3, (4, 4))
    assert outer == pytest.approx((math.sqrt(24.75), 3), abs=1e-12)
    assert math.dist(outer, (0, 2.5)) == pytest.approx(3 + 2, abs=1e-12)


def test_the_touch_point_nearest_the_point_picks_the_mirror_whatever_the_pen():
    for pen in ((-8, 0), (8, 0)):
        assert fillet_center(floor_and_ring(1, 2), 0.5, (1, 1.5), pen) == pytest.approx((math.sqrt(2), 0.5), abs=1e-12)
        assert fillet_center(floor_and_ring(1, 2), 0.5, (-1, 1.5), pen) == pytest.approx((-math.sqrt(2), 0.5), abs=1e-12)


@pytest.mark.parametrize("auto", [False, True])
@pytest.mark.constraints
def test_a_constraint_sketch_fillet_takes_its_kind_from_the_point(auto):
    sk = px.Sketch(auto_constraint=auto)
    sk.Move((-10, 0)).Line(20, 0, id="floor")
    sk.Circle((0, 1), 2, id="ring")
    sk.Arc("floor", "ring", 0.5, tangent=(("floor", (0, 1)), ("ring", (1, 1.5))), id="nested")
    sk.Arc("floor", "ring", 0.5, tangent=(("floor", (0, 1)), ("ring", (-3, 3))), id="outer")
    assert sk._Geometry("nested")["center"] == pytest.approx((math.sqrt(2), 0.5), abs=1e-12)
    assert sk._Geometry("outer")["center"] == pytest.approx((-math.sqrt(6), 0.5), abs=1e-12)


def test_a_point_on_the_circle_raises_and_just_inside_or_outside_picks_the_kind():
    on = r": the point \(2, 1\) lies on the circle 'ring', no unambiguous solution: put it inside or outside the circle$"
    with pytest.raises(ValueError, match=r"^Arc\(.*" + on):
        fillet_center(floor_and_ring(1, 2), 0.5, (2, 1))
    with pytest.raises(ValueError, match=r"^Circle\(.*" + on):
        floor_and_ring(1, 2).Circle(0.5, tangent=(("floor", (0, 1)), ("ring", (2, 1))))
    inside = fillet_center(floor_and_ring(1, 2), 0.5, (2 - 1e-9, 1))
    assert inside == pytest.approx((math.sqrt(2), 0.5), abs=1e-12)
    outside = fillet_center(floor_and_ring(1, 2), 0.5, (2 + 1e-9, 1))
    assert outside == pytest.approx((math.sqrt(6), 0.5), abs=1e-12)


def test_a_point_that_picks_a_kind_without_a_fillet_raises():
    with pytest.raises(ValueError, match="no arc of that radius touches both on those sides"):
        fillet_center(floor_and_ring(1, 2), 2, (0.5, 1))
    with pytest.raises(ValueError, match="no arc of that radius touches both on those sides"):
        fillet_center(floor_and_ring(5, 2), 0.5, (0, 5))
    with pytest.raises(ValueError, match="no circle of that radius touches both on those sides"):
        floor_and_ring(1, 2).Circle(2, tangent=(("floor", (0, 1)), ("ring", (0.5, 1))))


@pytest.mark.skip(reason="px.WorkPlane is switched off (later list)")
@pytest.mark.parametrize("ang, center, ccw", [(360, (0, -1), False), (math.nextafter(360.0, 0.0), (0, -1), False),
                                              (-360, (0, 1), True), (720, (0, -1), False)])
def test_a_negative_radius_full_turn_on_a_workplane_leaves_along_the_pen(ang, center, ccw):
    w = px.WorkPlane()
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        w.Arc(-1, ang)
    assert [str(c.message) for c in caught] == ["Arc(-1, %g): " % ang + FULL]
    assert point(w.cur_loc) == (0, 0) and point(w.cur_dir) == (1, 0)
    arcs = w.Wire().edges
    assert [point(e.edge.center) for e in arcs] == [center, center] and all(e.edge.ccw == ccw for e in arcs)
    tangent = arcs[0].TangentAt(0)
    assert (tangent.x, tangent.y) == pytest.approx((1, 0), abs=1e-15)
    assert w.Face().mass == pytest.approx(math.pi, rel=1e-14)
