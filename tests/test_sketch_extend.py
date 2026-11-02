# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-12


def with_wall(make=px.Sketch):
    sk = make()
    sk.Move((0, 0)).Line((5, 0), id="l")
    sk.Move((8, -2)).Line((8, 2), id="wall")
    return sk


@pytest.mark.parametrize("make", [px.Sketch, px.QuickSketch])
def test_a_line_extends_to_a_target_from_its_nearer_end_or_by_a_length(make):
    sk = with_wall(make)
    sk.Extend("l", "wall")
    assert sk.Location("l.end") == pytest.approx((8, 0), abs=TOL) and sk.Location("l.start") == pytest.approx((0, 0), abs=TOL)
    sk.Extend("l.start", 2)
    assert sk.Location("l.start") == pytest.approx((-2, 0), abs=TOL)


def test_a_line_extends_to_the_first_place_it_meets_a_circle():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((5, 0), id="l")
    sk.Circle((10, 0), 2, id="c")
    sk.Extend("l.end", "c")
    assert sk.Location("l.end") == pytest.approx((8, 0), abs=TOL)


def test_an_arc_extends_along_its_circle():
    sk = px.Sketch()
    sk.Move((5, 0), dir=(0, 1)).Arc(5, 90, id="a")
    sk.Extend("a.end", 5 * math.pi / 4)
    assert sk.Location("a.end") == pytest.approx((5 * math.cos(math.radians(135)), 5 * math.sin(math.radians(135))), abs=1e-9)
    # from 135 degrees on round the circle, x = -4 comes first at (-4, 3)
    sk.Move((-4, -6)).Line((-4, 6), id="wall")
    sk.Extend("a", "wall")
    assert sk.Location("a.end") == pytest.approx((-4, 3), abs=1e-9)
    # the gap round to the start is 216.87 degrees, 18.9 along the circle
    with pytest.raises(ValueError, match="the arc would close into a circle"):
        sk.Extend("a.start", 20)


def test_with_auto_constraint_the_new_end_stays_on_its_target():
    sk = with_wall(lambda: px.Sketch(auto_constraint=True))
    sk.Extend("l", "wall")
    # the coincidence names the end's point
    (on,) = [repr(c).split('"')[1] for kind in sk.constraints.values() for c in kind.values() if '"wall"' in repr(c)]
    assert sk.Location(on) == pytest.approx((8, 0), abs=TOL) and sk.Location("l.end") == pytest.approx((8, 0), abs=TOL)


def test_what_cannot_be_extended_says_why():
    sk = with_wall()
    with pytest.raises(ValueError, match=r"a length moves one end; give 'l.start' or 'l.end'"):
        sk.Extend("l", 3)
    with pytest.raises(ValueError, match="the length is above 0"):
        sk.Extend("l.end", -1)
    with pytest.raises(ValueError, match="it does not meet l along its line"):
        sk.Extend("wall", "l")
    sk.Move("l.end", px.Coincident).Line((5, 3), id="up")
    with pytest.raises(ValueError, match="up ends there too"):
        sk.Extend("l.end", 1)
    held = px.Sketch()
    held.Move((0, 0)).Line((5, 0), id="l")
    held.Constrain("l", px.Distance, 5, id="len")
    with pytest.raises(ValueError, match="len holds its end where it is; nothing changed"):
        held.Extend("l.end", 1)
    assert held.Location("l.end") == pytest.approx((5, 0), abs=TOL)
