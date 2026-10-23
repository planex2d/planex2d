# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def starts(sk, scope="Rectangle.1"):
    return [tuple(round(c, 9) + 0.0 for c in sk.Location("%s.Line.%d.start" % (scope, k))) for k in range(1, 5)]


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_two_opposite_corners_give_an_axis_parallel_rectangle(make):
    sk = make().Move((7, 7), dir=(0, 1)).Rectangle((4, 3), (1, 1))
    assert starts(sk) == [(1, 1), (4, 1), (4, 3), (1, 3)]
    assert sk.Face().mass == pytest.approx(6, rel=TOL)
    assert tuple(sk.pen.position) == pytest.approx((7, 7)) and tuple(sk.pen.direction) == pytest.approx((0, 1))


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_three_points_give_a_rotated_rectangle_from_p0_counterclockwise(make):
    left = make().Rectangle((0, 0), (3, 4), (-4, 3))
    assert starts(left) == [(0, 0), (3, 4), (-1, 7), (-4, 3)]
    assert left.Face().mass == pytest.approx(25, rel=TOL)
    right = make().Rectangle((0, 0), (3, 4), (5, 2))
    s = starts(right)
    assert s[0] == (0, 0) and s[3] == (3, 4)
    assert right.Location("Rectangle.1.Line.4.end") == pytest.approx((0, 0), abs=TOL)
    side = math.hypot(3, 4)
    width = abs((5 - 3) * (-4 / side) + (2 - 4) * (3 / side))
    assert right.Face().mass == pytest.approx(side * width, rel=TOL)


def test_corner_points_that_are_no_rectangle_raise():
    with pytest.raises(ValueError, match="Rectangle"):
        px.QuickSketch().Rectangle((0, 0), (0, 0), (1, 1))
    with pytest.raises(ValueError, match="Rectangle"):
        px.QuickSketch().Rectangle((0, 0), (4, 0))
    with pytest.raises(TypeError, match="four|4 arguments"):
        px.QuickSketch().Rectangle((0, 0), (1, 0), (1, 1), (0, 1))
    with pytest.raises(TypeError, match="not an id string"):
        px.QuickSketch().Rectangle("A", (1, 1))


@pytest.mark.constraints
def test_selected_corners_tie_the_rectangle():
    sk = px.Sketch().Move((0, 0)).Line((2, 0), id="a").Line((2, 1), id="b")
    sk.Rectangle(sk("a.start"), sk("b.end"))
    sk.Constrain("a", px.Distance, 2, id="la")
    sk["la"] = 3
    sk.Update()
    assert sk.Location("Rectangle.1.Line.3.start") == pytest.approx(sk.Location("b.end"), abs=TOL)
    assert sk._Geometry("Rectangle.1.Line.1")["angle"] == pytest.approx(0, abs=1e-9)
