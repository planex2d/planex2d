# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import point


def last_arc(sketch):
    return sketch.Wire().edges[-1]


def last_line(sketch):
    return sketch.Wire().edges[-1]


def chord_and_circle():
    """a construction circle of radius 2 around the origin and the construction line y = 1; the pen at (2, 0) heads +y"""
    s = px.QuickSketch().Circle((0, 0), 2, id="c", construction=True)
    s.Move((-5, 1)).Line((5, 1), id="chord", construction=True)
    return s.Move((2, 0), dir=(0, 1))


def test_first_and_last_are_not_in_the_star_import():
    assert px.first is not px.last
    assert "first" not in px.__all__ and "last" not in px.__all__


def test_an_arc_through_a_third_point():
    h = px.Move((0, 0)).Arc((2, 0), through=(1, 1))
    assert point(last_arc(h).center) == pytest.approx((1, 0)) and last_arc(h).edge.radius == pytest.approx(1)
    assert not last_arc(h).edge.ccw
    assert point(h.pen.position) == pytest.approx((2, 0))
    below = px.QuickSketch().Arc((0, 0), (2, 0), through=(1, -1))
    assert point(last_arc(below).center) == pytest.approx((1, 0)) and last_arc(below).edge.ccw
    with pytest.raises(ValueError, match=r"Arc\(\(2, 0\), through=\(1, 0\)\): the three points lie on a line"):
        px.Move((0, 0)).Arc((2, 0), through=(1, 0))


def test_two_ends_and_a_radius_go_counterclockwise_the_short_way():
    h = math.sqrt(25 - 1)
    short = px.Move((0, 0)).Arc((2, 0), 5)
    assert point(last_arc(short).center) == pytest.approx((1, h)) and last_arc(short).edge.ccw
    assert last_arc(short).edge.radius == pytest.approx(5)
    assert point(last_arc(px.QuickSketch().Arc((0, 0), (2, 0), 5)).center) == pytest.approx((1, h))
    clockwise = last_arc(px.Move((0, 0)).Arc((2, 0), -5))
    assert point(clockwise.center) == pytest.approx((1, -h)) and not clockwise.edge.ccw
    long = last_arc(px.Move((0, 0)).Arc((2, 0), 5, reverse=True))
    assert point(long.center) == pytest.approx((1, -h)) and long.edge.ccw
    long_clockwise = last_arc(px.Move((0, 0)).Arc((2, 0), -5, reverse=True))
    assert point(long_clockwise.center) == pytest.approx((1, h)) and not long_clockwise.edge.ccw
    with pytest.raises(ValueError, match="the radius is smaller than half the distance between the ends"):
        px.Move((0, 0)).Arc((2, 0), 0.5)


def test_an_arc_with_a_start_direction_or_along_the_pen():
    a = last_arc(px.Move((0, 0)).Arc((2, 2), tangent=(1, 0)))
    assert point(a.center) == pytest.approx((0, 2)) and a.edge.ccw
    b = last_arc(px.QuickSketch().Arc((0, 0), (2, 2), tangent=(0, 1)))
    assert point(b.center) == pytest.approx((2, 0)) and not b.edge.ccw
    c = px.Move((0, 0)).Direction(0, 1).Arc((2, 2))
    assert point(last_arc(c).center) == pytest.approx((2, 0)) and point(c.pen.direction) == pytest.approx((1, 0))
    with pytest.raises(ValueError, match="runs along the chord"):
        px.Move((0, 0)).Arc((2, 0), tangent=(1, 0))


def test_two_ends_alone_do_not_fix_the_arc():
    with pytest.raises(ValueError, match=r"two ends alone do not fix the arc; give a radius, through=, tangent=\(dh, dv\)"):
        px.QuickSketch().Arc((0, 0), (2, 2))
    with pytest.raises(ValueError, match="give one of a radius, through= and tangent=.dh, dv."):
        px.QuickSketch().Arc((0, 0), (2, 0), 5, through=(1, 1))


def test_an_element_as_an_end_needs_the_circle():
    s = chord_and_circle()
    with pytest.raises(ValueError, match=r"Arc\('chord', 5.0\): an element as an end needs the circle"):
        s.Arc("chord", 5)
    with pytest.raises(ValueError, match="an element as an end needs the circle"):
        s.Arc((2, 0), "chord")


def test_an_element_as_to_takes_the_first_or_the_last_intersection_along_the_arc():
    x = math.sqrt(3)
    assert point(chord_and_circle().Arc("chord", center=(0, 0)).pen.position) == pytest.approx((x, 1))
    assert point(chord_and_circle().Arc((2, 0), "chord", center=(0, 0)).pen.position) == pytest.approx((x, 1))
    assert point(chord_and_circle().Arc((2, 0), "chord", center=(0, 0), until=px.first).pen.position) == pytest.approx((x, 1))
    assert point(chord_and_circle().Arc((2, 0), "chord", center=(0, 0), until=px.last).pen.position) == pytest.approx((-x, 1))
    with pytest.raises(ValueError, match="give the end once: a positional to, until= or sweep="):
        chord_and_circle().Arc((2, 0), "chord", center=(0, 0), until="chord")
    with pytest.raises(ValueError, match="give the end once: a positional to, until= or sweep="):
        chord_and_circle().Arc((2, 0), "chord", center=(0, 0), sweep=90)


def test_a_point_as_to_with_until_points_at_through():
    with pytest.raises(ValueError, match="until= picks among the intersections with an element as the end, a point has none; "
                                         "through= gives a point the arc passes"):
        chord_and_circle().Arc((2, 0), (0, 2), center=(0, 0), until=px.first)


def test_a_single_positional_with_until_or_ang_is_the_start():
    s = px.QuickSketch().Arc((2, 0), sweep=90, center=(0, 0))
    a = last_arc(s)
    assert point(a.start) == pytest.approx((2, 0)) and point(a.end) == pytest.approx((0, 2))
    a = last_arc(px.QuickSketch().Arc((2, 0), sweep=-90, center=(0, 0)))
    assert point(a.end) == pytest.approx((0, -2))
    wall = px.Move((5, -5)).Line((5, 5), id="wall", construction=True).Move((0, 0))
    ln = last_line(wall.Copy().Line((1, 1), until="wall"))
    assert (point(ln.start), point(ln.end)) == ((1, 1), (5, 1))
    to = last_line(wall.Copy().Line((1, 1)))
    assert (point(to.start), point(to.end)) == ((0, 0), (1, 1))


def test_reverse_on_a_given_circle_takes_the_other_direction():
    ccw = last_arc(px.QuickSketch().Move((2, 0), dir=(0, 1)).Arc(center=(0, 0), sweep=90))
    assert point(ccw.end) == pytest.approx((0, 2)) and ccw.edge.ccw
    cw = last_arc(px.QuickSketch().Move((2, 0), dir=(0, 1)).Arc(center=(0, 0), sweep=90, reverse=True))
    assert point(cw.end) == pytest.approx((0, -2)) and not cw.edge.ccw
    quarter = last_arc(px.QuickSketch().Arc((2, 0), (0, 2), center=(0, 0)))
    assert quarter.edge.ccw and math.radians(abs(quarter.edge.sweep)) == pytest.approx(math.pi / 2)
    three_quarters = last_arc(px.QuickSketch().Arc((2, 0), (0, 2), center=(0, 0), reverse=True))
    assert not three_quarters.edge.ccw and abs(math.radians(abs(three_quarters.edge.sweep))) == pytest.approx(3 * math.pi / 2)


def test_until_is_first_last_or_a_target_for_arcs_too():
    s = px.Move((5, -5)).Line((5, 25), id="wall", construction=True).Move((0, 0))
    with pytest.raises(ValueError, match=r"Arc\(10.0, until='last'\): until='last' names no element; until=px.last"):
        s.Arc(10, until="last")
    with pytest.raises(TypeError, match="until is px.first, px.last or a target"):
        s.Arc(10, until=3)


def test_the_old_keyword_forms_raise_naming_the_positional_ones():
    s = px.Move((0, 0))
    with pytest.raises(TypeError, match=r"Arc\(from_=...\): the ends are positional: Arc\(start, end, ...\), or Arc\(to, ...\)"):
        s.Arc(from_=(0, 0), to=(1, 1))
    with pytest.raises(TypeError, match=r"Arc\(to=...\): the ends are positional"):
        s.Arc(to=(1, 1))
    with pytest.raises(TypeError, match=r"Line\(from_=...\): the ends are positional: Line\(start, end, ...\), or Line\(to, ...\)"):
        s.Line(from_=(0, 0))
    with pytest.raises(TypeError, match=r"Arc\(r=...\): the radius is positional: Arc\(r, sweep\)"):
        s.Arc(r=2, sweep=45)
    with pytest.raises(TypeError, match=r"Arc\(ccw=...\): ccw is gone"):
        s.Arc((1, 0), 2, ccw=False)
    with pytest.raises(TypeError, match=r"Arc\(r, sweep=...\): along the pen the sweep is positional, Arc\(r, sweep\)"):
        s.Arc(1, sweep=90)


def test_the_pen_is_a_point_a_direction_or_a_reference_where_one_is_asked():
    s = px.Move((1, 2))
    assert s.pen.position == (1, 2) and s.pen.direction == (1, 0)
    before = s.pen
    s.Line(3)
    assert before.position == (1, 2) and s.pen.position == (4, 2)
    s.Line((4, 5)).Line(before)
    assert point(last_line(s).end) == (1, 2)
    t = px.Move((0, 0)).Direction(0, 1)
    a = last_arc(t.Arc((2, 2), tangent=t.pen))
    assert point(a.center) == pytest.approx((2, 0))
    u = px.Move((3, 0)).Direction(0, 1)
    assert point(u.Copy().Line(2, parallel=u.pen).pen.position) == pytest.approx((3, 2))
    perpendicular = point(px.Move((0, 0)).Direction(1, 0).Line(2, perpendicular=px.Move((0, 0)).pen).pen.position)
    assert perpendicular[0] == pytest.approx(0, abs=1e-12) and abs(perpendicular[1]) == pytest.approx(2)


def test_chain_start_is_where_the_chain_began():
    s = px.Move((1, 1)).Line((3, 1))
    assert s.chain_start == (1, 1)
    assert s.Move((5, 5)).chain_start == (5, 5)
