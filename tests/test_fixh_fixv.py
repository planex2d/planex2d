# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-9
NO_CONSTRAINTS = r"a QuickSketch has no constraints; px\.Sketch\(\.\.\.\) has them"


def test_fixh_holds_a_points_h_and_lets_its_v_follow():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((4, 3), id="a")
    sk.Constrain("a.end", px.FixH)
    assert sk.dof == 1
    sk.Constrain("a", px.Distance, 10)
    assert sk.status == "full"
    assert sk.Location("a.end") == pytest.approx((4, math.sqrt(84)), abs=TOL)


def test_fixv_holds_a_points_v_and_lets_its_h_follow():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((3, 4), id="a")
    sk.Constrain("a.end", px.FixV)
    sk.Constrain("a", px.Distance, 10)
    assert sk.status == "full"
    assert sk.Location("a.end") == pytest.approx((math.sqrt(84), 4), abs=TOL)


def test_a_line_drawn_with_fixh_keeps_its_h_and_slides_and_stretches_vertically():
    sk = px.Sketch()
    sk.Move((2, 0)).Line(0, 5, px.FixH, id="post")
    assert list(sk.constraints["FixH"]) == ["FixH.1"] and sk.dof == 2
    sk.Constrain("post", px.Distance, 8)
    sk.Constrain("post.start", px.H, px.Distance, 1)
    assert sk.status == "full"
    start, end = sk.Location("post.start"), sk.Location("post.end")
    assert (start[0], end[0]) == pytest.approx((2, 2), abs=TOL)
    assert abs(end[1] - start[1]) == pytest.approx(8, abs=TOL) and abs(start[1]) == pytest.approx(1, abs=TOL)


def test_a_circle_with_fixv_keeps_its_centres_v_not_its_radius():
    sk = px.Sketch()
    sk.Circle((3, 2), 1, px.FixV, id="c")
    sk.Constrain("c", px.Radius, 1.5)
    sk.Constrain("c.center", px.V, px.Distance, 5)
    assert sk.Location("c.center") == pytest.approx((5, 2), abs=TOL)
    assert sk._Geometry("c")["r"] == pytest.approx(1.5, abs=TOL)


def test_move_takes_fixh_and_fixv_for_the_start():
    sk = px.Sketch()
    sk.Move((1, 3), px.FixH).Line((5, 2), id="a")
    sk.Constrain("a.end", px.Fix)
    sk.Constrain("a", px.Distance, 5)
    start = sk.Location("a.start")
    assert start[0] == pytest.approx(1, abs=TOL) and math.dist(start, (5, 2)) == pytest.approx(5, abs=TOL)
    other = px.Sketch((1, 3), px.FixV)
    assert list(other.constraints) == ["FixV"] and other.dof == 1


def test_a_curve_given_fixh_holds_its_h_and_turn_instead_of_the_default_fix():
    sk = px.Sketch()
    sk.Curve(("t", "0.1*t^2"), (0, 10), px.FixH)
    assert "Fix" not in sk.constraints and sk.dof == 1
    sk.Constrain("Curve.1.start", px.H, px.Distance, 2)
    assert sk.status == "full"
    start = sk.Location("Curve.1.start")
    assert start[0] == pytest.approx(0, abs=TOL) and abs(start[1]) == pytest.approx(2, abs=TOL)


def test_a_fillet_keeps_fixh_and_fixv_on_the_far_ends_and_the_corner():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((4, 0), px.FixV, id="a").Line((4, 4), px.FixH, id="b")
    sk.Fillet("a.end", 1)
    assert set(sk.constraints["FixH"]) == {"FixH.1", "FixH.1.corner"}
    assert set(sk.constraints["FixV"]) == {"FixV.1", "FixV.1.corner"}
    sk["Fillet.1.r"] = 2
    sk.Update()
    assert sk.Location("b.end")[0] == pytest.approx(4, abs=TOL)
    assert sk._Geometry("Fillet.1")["center"] == pytest.approx((2, 2), abs=TOL)


def test_moving_the_origin_keeps_what_fixh_holds_in_place():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((4, 3), id="a")
    sk.Constrain("a.end", px.FixH)
    sk.SetOrigin((1, 1))
    sk.Constrain("a", px.Distance, 10)
    assert sk.Location("a.end")[0] == pytest.approx(3, abs=TOL)


def test_a_mirrored_copy_does_not_take_fixh_and_a_mirror_in_place_refuses_it():
    sk = px.Sketch()
    sk.Move((1, 0), px.Fix).Line((1, 3), px.FixH, id="a")
    sk.Mirrored(["a"], px.V)
    assert list(sk.constraints["FixH"]) == ["FixH.1"]
    with pytest.raises(ValueError, match=r"Mirror: .*FixH\.1.* hold the elements where they are; nothing changed"):
        sk.Mirror("a", px.V)


def test_fixh_takes_no_value_and_needs_a_constraint_sketch():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((4, 3), id="a")
    with pytest.raises(ValueError, match="takes no value"):
        sk.Constrain("a.end", px.FixH, 3)
    with pytest.raises(ValueError, match="px.FixV: " + NO_CONSTRAINTS):
        px.QuickSketch((1, 0), px.FixV)


def test_the_plot_marks_fixh_with_an_anchor_and_its_letter():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((4, 3), id="a")
    sk.Constrain("a.end", px.FixH)
    svg = px.ToSVG(sk, show_constraints=True)
    assert any(t.startswith("FixH.1: ") for t in re.findall(r"<title>(.*?)</title>", svg))
    assert ">H<" in svg
