# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-13


def base_and_hub(auto):
    sk = px.Sketch(auto_constraint=auto)
    sk.Move((0, 0)).Line(4, 0, px.Horizontal, id="base")
    sk.Constrain("base.start", px.Fix)
    sk.Constrain("base", px.Distance, 4)
    sk.Circle(6, 3, 1, id="hub")
    sk.Constrain("hub.center", px.Fix)
    sk.Constrain("hub", px.Radius, 1, id="r")
    return sk


def test_the_helpers_place_like_the_turtle_sketch():
    sk = base_and_hub(False)
    sk.Move((0, 3)).Line(tangent="hub", id="belt")
    # the upper touch point on the hub seen from (0, 3): (6 - 1/6, 3 + sqrt(35)/6)
    assert sk._Location("belt.end") == pytest.approx((35 / 6, 3 + math.sqrt(35) / 6), abs=TOL)
    assert sk.pen.direction == pytest.approx((math.sqrt(35) / 6, 1 / 6), abs=TOL)


def test_without_auto_constraint_the_helpers_only_place():
    sk = base_and_hub(False)
    sk.Move((1, 2)).Line("base", perpendicular="base", id="drop")
    assert sk._Location("drop.end") == pytest.approx((1, 0), abs=TOL)
    assert list(sk.constraints) == ["Horizontal", "Fix", "Distance", "Radius"]
    sk.Remove("Fix.1")
    sk.Constrain("base.start", "H", px.Distance, 1)
    assert sk._Location("base.end")[1] == pytest.approx(1, abs=TOL)
    assert sk._Location("drop.end") == pytest.approx((1, 0), abs=TOL)


def test_with_auto_constraint_the_helpers_become_constraints():
    sk = base_and_hub(True)
    sk.Move((1, 2)).Line("base", perpendicular="base", id="drop")
    sk.Move((0, 3)).Line(tangent="hub", id="belt")
    texts = [str(t) for c in sk.constraints.values() for t in c.values()]
    assert '"drop.end", "base", px.Coincident' in texts
    assert '"base", "drop", px.Perpendicular' in texts
    assert '"belt", "hub", px.Tangent' in texts
    assert '"belt.end", "hub", px.Coincident' in texts
    sk["r"] = 2
    (x, y), c = sk._Location("belt.end"), (6, 3)
    (sx, sy) = sk._Location("belt.start")
    assert math.dist((x, y), c) == pytest.approx(2, abs=TOL)
    assert (x - c[0]) * (x - sx) + (y - c[1]) * (y - sy) == pytest.approx(0, abs=1e-12)


def test_auto_constraint_can_be_switched_between_calls():
    sk = base_and_hub(False)
    sk.Move((1, 2)).Direction(0, -1).Line(id="a", until="base")
    sk.auto_constraint = True
    sk.Move((2, 2)).Line(id="b", until="base")
    assert [str(t) for t in sk.constraints["Coincident"].values()] == ['"b.end", "base", px.Coincident']


def test_points_of_elements_and_the_axes_as_targets():
    sk = base_and_hub(True)
    sk.Circle(0.5, center="base.end", id="pin")
    assert sk._Geometry("pin")["center"] == pytest.approx((4, 0), abs=TOL)
    assert '"pin.center", "base.end", px.Coincident' in [str(t) for t in sk.constraints["Coincident"].values()]
    sk.Move((-1, 2)).Line(px.V, parallel=px.H, id="h")
    assert sk._Geometry("h")["end"] == pytest.approx((0, 2), abs=TOL)
    assert '"h.end", px.V, px.Coincident' in [str(t) for t in sk.constraints["Coincident"].values()]


def test_until_ends_on_what_it_meets():
    sk = base_and_hub(True)
    sk.Move((2, 3)).Direction(0, -1)
    sk.Line(until=px.first, id="down")
    assert sk._Location("down.end") == pytest.approx((2, 0), abs=TOL)
    assert '"down.end", "base", px.Coincident' in [str(t) for t in sk.constraints["Coincident"].values()]


def test_flags_and_helpers_together():
    sk = base_and_hub(False)
    sk.Move((1, 2)).Direction(0, -1).Line(id="drop", until="base")
    sk.Move((1, 3)).Line("hub", parallel="base", id="along")
    assert sk._Location("along.end") == pytest.approx((5, 3), abs=TOL)
    assert list(sk.constraints) == ["Horizontal", "Fix", "Distance", "Radius"]
    sk2 = base_and_hub(False)
    sk2.Move((1, 2)).Direction(0, -1).Line(px.Vertical, id="drop", until="base")
    assert '"drop", px.Vertical' in [str(t) for t in sk2.constraints["Vertical"].values()]


def test_a_helper_that_fails_draws_nothing():
    sk = base_and_hub(True)
    with pytest.raises(ValueError, match="does not meet"):
        sk.Move((1, 5)).Line("hub", parallel="base")
    assert sk.elements["Line"] == ["base"]
    with pytest.raises(TypeError, match="unexpected keyword argument 'tangnet'"):
        sk.Line(tangnet="hub")


def test_a_line_tangent_to_two_circles_gets_both_tangents():
    for tangent in (("a", "b"), ["a", "b"]):
        sk = px.Sketch(auto_constraint=True)
        sk.Circle(0, 0, 1, id="a")
        sk.Circle(5, 0, 1.5, id="b")
        for c, r in (("a", 1), ("b", 1.5)):
            sk.Constrain(c + ".center", px.Fix)
            sk.Constrain(c, px.Radius, r, id="r" + c)
        sk.Move((0, 1)).Line(tangent=tangent, id="belt")
        assert sorted(repr(c) for c in sk.constraints["Tangent"].values()) == ['"belt", "a", px.Tangent', '"belt", "b", px.Tangent']
        sk.Update({"ra": 1.4, "rb": 0.8})
        (x0, y0), (x1, y1) = sk.Location("belt.start"), sk.Location("belt.end")
        length = math.hypot(x1 - x0, y1 - y0)
        for (cx, cy), r in (((0, 0), 1.4), ((5, 0), 0.8)):
            assert abs((x1 - x0) * (cy - y0) - (y1 - y0) * (cx - x0)) / length == pytest.approx(r, abs=1e-12)


def test_a_target_with_a_hint_is_one_tangent():
    sk = base_and_hub(True)
    sk.Move((0, 3)).Line(tangent=("hub", (6, 4)), id="belt")
    assert [repr(c) for c in sk.constraints["Tangent"].values()] == ['"belt", "hub", px.Tangent']


def test_a_helper_constraint_that_follows_from_the_others_is_left_out():
    sk = px.Sketch(auto_constraint=True)
    sk.Move((0, 0)).Line(3, 0, px.Horizontal, id="base")
    with pytest.warns(px.PlanexWarning, match=r'auto_constraint: left out "base", "top", px.Parallel, it follows from '
                                              r'Horizontal.1, Horizontal.2'):
        sk.Move((0, 1)).Line(2, px.Horizontal, parallel="base", id="top")
    assert "Parallel" not in sk.constraints and sk.redundant == []
    assert sk.status == "under"


def test_a_helper_constraint_that_still_takes_freedom_is_kept():
    """the arc's start lies where the line touches the circle: on the circle twice over, yet the arc's centre and
    radius come only from congruent="c\""""
    sk = px.Sketch(auto_constraint=True)
    sk.Circle(0, 0, 1, px.Fix, id="c")
    sk.Move((-1, -1)).Line(1, 0, px.Horizontal, id="tangent")
    sk.Constrain("tangent", "c", px.Tangent)
    sk.Constrain("tangent.end", "c", px.Coincident)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Arc("tangent", sweep=90, congruent="c", id="a")
    assert '"a", "c", px.Coincident' in [repr(t) for t in sk.constraints["Coincident"].values()]
    g = sk.Geometry("a")
    assert g["center"] == pytest.approx((0, 0), abs=TOL) and g["r"] == pytest.approx(1, abs=TOL)


def boss_and_tangent(sk):
    """a fixed boss of radius 1 and a horizontal line tangent to it from below, ending at the touch point"""
    sk.Circle(0, 0, 1, px.Fix, id="boss")
    sk.Move((-3, -1)).Line(3, 0, px.Horizontal, id="lower")
    sk.Constrain("lower", "boss", px.Tangent)
    sk.Constrain("lower.end", "boss", px.Coincident)
    sk.Constrain("lower.start", px.Fix)
    return sk


def test_a_point_given_as_from_shares_the_vertex():
    sk = boss_and_tangent(px.Sketch(auto_constraint=True))
    sk.Move((5, 5))
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Arc("lower.end", sweep=90, congruent="boss", id="a")
    texts = [repr(t) for t in sk.constraints["Coincident"].values()]
    assert '"a.start", "lower.end", px.Coincident' in texts
    assert sk.Location("a.start") == sk.Location("lower.end")


def test_an_end_landing_on_the_end_of_its_target_warns():
    sk = boss_and_tangent(px.Sketch(auto_constraint=True))
    sk.Move((5, 5))
    with pytest.warns(px.PlanexWarning, match=r'Arc\("lower"\): its start lands on lower.end but only lies on lower; '
                                              r'"lower.end" as from makes them one point'):
        sk.Arc("lower", sweep=90, congruent="boss", id="a")
