# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import point


def references():
    """a construction wall at x = 5 and a construction circle around (5, 0) with radius 1"""
    s = px.Move((5, -1)).Line((5, 5), id="wall", construction=True)
    return s.Circle(5, 0, 1, id="c", construction=True).Move((0, 0))


def two_circles():
    s = px.QuickSketch().Circle(0, 0, 1, id="c1", construction=True).Circle(5, 0, 1, id="c2", construction=True)
    return s.Move((8, -3)).Line((8, 3), id="far", construction=True).Move((2, 3))


def ends(sketch):
    e = sketch.Wire().edges[-1]
    return point(e.start) + point(e.end)


def test_reverse_turns_a_forward_line_around():
    assert point(px.Move((0, 0)).Line(3, reverse=True).pen.position) == (-3, 0)
    assert point(px.Move((0, 0)).Line(3, parallel=px.Line((0, 0), (0, 1)), reverse=True).pen.position) == (0, -3)


def test_reverse_takes_the_second_solution_or_warns():
    assert point(references().Line(tangent="c").pen.position)[1] > 0
    assert point(references().Line(tangent="c", reverse=True).pen.position)[1] < 0
    with pytest.warns(px.PlanexWarning, match="reverse found only one solution"):
        references().Line("wall", angle=0, reverse=True)
    with pytest.warns(px.PlanexWarning, match="reverse has nothing to switch"):
        references().Line(("c", (6, 0)), ("wall", (5, 0)), angle=0, reverse=True)


def test_a_point_fixes_the_choice_of_its_target():
    assert ends(references().Line(("c", (6, 0)), "wall", angle=0)) == (6, 0, 5, 0)
    assert ends(references().Line(("c", (10, 0)), ("c", (4, 0)), angle=0)) == (6, 0, 4, 0)
    s = references().Line(tangent=("c", (5, -1)))
    assert point(s.pen.position) == pytest.approx((4.8, -math.sqrt(24) / 5))


def test_line_touching_two_circles():
    s = two_circles().Line(tangent=("c1", "c2"))
    assert ends(s) == pytest.approx((0, 1, 5, 1))
    assert ends(two_circles().Line(tangent=("c1", "c2"), reverse=True)) == pytest.approx((0, -1, 5, -1))
    # c1 fixed on top, c2 switched: the crossing line
    h = math.sqrt(1 - 0.4 ** 2)
    crossing = two_circles().Line(tangent=(("c1", (0, 1)), "c2"), reverse=True)
    assert ends(crossing) == pytest.approx((0.4, h, 4.6, -h))
    assert ends(two_circles().Line("c2", "c1", tangent=("c1", "c2"))) == pytest.approx((5, 1, 0, 1))
    # from the touch point on c1 (where a line touching two circles starts) along the line until the last thing ahead
    s = two_circles().Line("c1", tangent=("c1", "c2"), until=px.last)
    assert ends(s) == pytest.approx((0, 1, 8, 1))


def test_solutions_on_the_edge_come_before_the_carrier():
    s = px.Move((1, 0)).Arc(center=(0, 0), sweep=90, id="quarter").Move((3, 0))
    # tangent points of the full circle from (3, 0); only the upper one lies on the quarter arc
    assert point(s.Copy().Line(tangent="quarter").pen.position) == pytest.approx((1 / 3, math.sqrt(8) / 3))
    assert point(s.Copy().Line(tangent="quarter", reverse=True).pen.position) == pytest.approx((1 / 3, -math.sqrt(8) / 3))


def test_fillet_sides_reverse_and_points():
    def corner():
        return px.Move((0, 0)).Line((2, 0), id="a").Line((2, 2), id="b")
    f = corner().Arc("a", "b", 0.5, tangent=("a", "b"), id="f").Edge("f").edges[0]
    assert point(f.center) == pytest.approx((1.5, 0.5))
    f = corner().Arc("a", "b", 0.5, tangent=("a", "b"), reverse=True, id="f").Edge("f").edges[0]
    assert point(f.center) == pytest.approx((2.5, -0.5))
    f = corner().Arc("a", "b", 0.5, tangent=(("a", (1, 1)), "b"), reverse=True, id="f").Edge("f").edges[0]
    assert point(f.center) == pytest.approx((2.5, 0.5))
    assert point(f.start) == pytest.approx((2.5, 0))


def test_arc_reverse():
    s = px.Move((5, -5)).Line((5, 25), id="wall", construction=True).Move((0, 0))
    y = 10 - math.sqrt(75)
    assert point(s.Copy().Arc(10, until=px.first).pen.position) == pytest.approx((5, y))
    # the same circle, clockwise: over the top first
    assert point(s.Copy().Arc(10, until=px.first, reverse=True).pen.position) == pytest.approx((5, 20 - y))
    assert point(s.Copy().Arc(10, 90, reverse=True).pen.position) == pytest.approx((-10, 10))


def test_construction_leaves_the_drawing_pen_alone():
    s = px.Move((0, 0)).Line((1, 0)).Line(5, id="ref", construction=True).Line((1, 1))
    assert len(s.Wire().edges) == 2
    assert point(s.Edge("ref").edges[0].end) == (6, 0)
    s = px.Move((0, 0)).Line((1, 0)).Circle(3, 3, 1, construction=True).Rectangle(1, 1, construction=True).Line((1, 1))
    assert len(s.Wire().edges) == 2
    assert len(s.Construction().edges) == 5
