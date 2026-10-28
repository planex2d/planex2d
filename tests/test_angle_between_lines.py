# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""px.Angle of two lines is the angle between the lines: a V drawn down one flank and up the other takes its included
angle, 38 or 180 - 38, and stays a V on the side drawn"""
import math

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def groove(angle):
    sk = px.Sketch()
    sk.Move((-3, 10), px.Fix).Line((0, 0), id="left").Line((3.5, 10), id="right")
    sk.Constrain("left", "right", px.Equal)
    sk.Constrain("left", px.Distance, 10)
    sk.Constrain("left", "right", px.Angle, angle, id="included")
    return sk


@pytest.mark.parametrize("angle", [38, 142])
def test_a_v_takes_its_included_angle_either_way(angle):
    sk = groove(angle)
    apex, end = sk.Location("left.end"), sk.Location("right.end")
    # a V: the right flank goes up from the apex, to the right, 38 degrees from the left one
    assert end[1] > apex[1] and end[0] > apex[0]
    left = math.atan2(sk.Location("left.start")[1] - apex[1], sk.Location("left.start")[0] - apex[0])
    right = math.atan2(end[1] - apex[1], end[0] - apex[0])
    assert math.degrees(abs(left - right)) == pytest.approx(38, abs=1e-9)
    assert sk.dimensions["included"] == pytest.approx(angle)


def test_an_angle_drawn_as_given_keeps_its_geometry():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((10, 0), px.Fix, id="a")
    sk.Move((0, 0)).Line((5, 8), id="b")
    sk.Constrain("a", "b", px.Angle, 60)
    p, q = sk.Location("b.start"), sk.Location("b.end")
    assert math.degrees(math.atan2(q[1] - p[1], q[0] - p[0])) == pytest.approx(60, abs=1e-9)
