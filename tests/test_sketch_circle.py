# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import point


def circle(sketch, name):
    m = sketch.Measure(name)
    assert m["type"] == "circle"
    return (*m["center"][:2], m["radius"])


def test_circle_center_and_radius_from_references():
    s = px.Move((0, 0)).Vertex("c", (1, 2)).Circle(0.5, center="c", id="ring")
    assert circle(s, "ring") == pytest.approx((1, 2, 0.5))
    s.Move((3, 0)).Arc(center=(0, 0), sweep=90, id="a").Circle(1, concentric="a", id="k")
    assert circle(s, "k") == pytest.approx((0, 0, 1))
    s.Circle(center=(5, 5), coincident="ring", id="copy")
    assert circle(s, "copy") == pytest.approx((5, 5, 0.5))
    s.Circle(center=(0, 0), through=(3, 4), id="rim")
    assert circle(s, "rim") == pytest.approx((0, 0, 5))
    before = point(s.pen.position)
    s.Circle(0.25, center=(9, 9), construction=True)
    assert point(s.pen.position) == before


def test_circle_touching_one_element():
    s = px.Move((2, -1)).Line((5, -1), id="l").Circle(center=(0, 1), tangent="l", id="c")
    assert circle(s, "c") == pytest.approx((0, 1, 2))
    big = px.QuickSketch().Circle((0, 0), 3, id="big")
    assert circle(big.Copy().Circle(center=(1, 0), tangent="big", id="c"), "c")[2] == pytest.approx(2)
    assert circle(big.Copy().Circle(center=(1, 0), tangent="big", reverse=True, id="c"), "c")[2] == pytest.approx(4)
    assert circle(big.Copy().Circle(center=(1, 0), tangent=("big", (-3, 0)), id="c"), "c")[2] == pytest.approx(4)


def test_circle_touching_two_elements():
    s = px.Move((0, 0)).Line((4, 0), id="a").Move((0, 0)).Line((0, 4), id="b").Circle(1, tangent=("a", "b"), id="c")
    assert circle(s, "c") == pytest.approx((1, 1, 1))


def test_circle_errors():
    s = px.Move((0, 0)).Vertex("p", (1, 1))
    with pytest.raises(ValueError, match="exactly one of r, coincident, through and tangent"):
        s.Circle(center=(0, 0))
    with pytest.raises(ValueError, match="center or concentric"):
        s.Circle(1, center=(0, 0), concentric="p")
    with pytest.raises(ValueError, match="needs a radius"):
        s.Move((0, 0)).Line((1, 0), id="a").Move((0, 0)).Line((0, 1), id="b").Circle(tangent=("a", "b"))


def test_circles_touching_two_circles_kinds_pen_and_reverse():
    def bosses():
        return px.QuickSketch().Circle((0, 0), 2, id="a").Circle((5, 0), 2, id="b")

    h = math.sqrt(9 - 2.5**2)
    for pen, y in (((2.5, 3), h), ((2.5, -3), -h)):
        for reverse, sign in ((False, 1), (True, -1)):
            s = bosses().Move(tuple(pen)).Circle(1, tangent=("a", "b"), id="k", reverse=reverse)
            assert s.Measure("k")["center"][:2] == pytest.approx((2.5, sign * y))

    def blend(*hints):
        s = px.QuickSketch().Circle((0, 0), 15, id="b1", construction=True).Circle((80, 0), 10, id="b2", construction=True)
        targets = [(n, p) if p else n for n, p in zip(("b1", "b2"), hints)]
        c = s.Move((40, 60)).Arc("b1", "b2", 40, tangent=tuple(targets), id="blend").Wire("blend").edges[0].center
        return round(math.hypot(c.x, c.y), 9), round(math.hypot(c.x - 80, c.y), 9)

    assert blend(None, None) == (55, 50)
    assert blend((10, 20), (70, 20)) == (55, 50)
    assert blend(None, (95, -5)) == (55, 50)
    assert blend(None, (85, -5)) == (55, 30)


@pytest.mark.parametrize("make", [px.Sketch, px.QuickSketch])
def test_a_circle_takes_its_center_as_one_point(make):
    sk = make()
    sk.Circle((0, 50), 20, id="bore")
    e = sk.Wire("bore").edges[0].edge
    assert (e.center.x, e.center.y, e.radius) == pytest.approx((0, 50, 20), abs=1e-12)
    with pytest.raises(TypeError, match=r"Circle\(\(h, v\), r\)"):
        make().Circle((0, 50), (1, 2))


@pytest.mark.parametrize("make", [px.Sketch, px.QuickSketch])
def test_circle_takes_no_three_numbers(make):
    with pytest.raises(TypeError, match=r"Circle\(h, v, r\): the center is one point now, Circle\(\(h, v\), r\)"):
        make().Circle(0, 50, 20)
