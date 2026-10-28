# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-9


def drags(sk):
    return [m["drag"] for m in sk.FreeMotions()]


def moved_at(velocity, drag):
    """how far a motion with this velocity moves the value drag names"""
    reference, _, coordinate = drag.rpartition(".")
    if coordinate in ("h", "v"):
        return velocity.get(reference, (0, 0))[("h", "v").index(coordinate)]
    return velocity.get(drag, 0)


def hv_rectangle():
    sk = px.Sketch()
    sk.Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Vertical, id="right") \
        .Line(-4, 0, px.Horizontal, id="top").Close(px.Vertical, id="left")
    return sk


def test_an_hv_rectangle_shifts_then_takes_width_and_height():
    sk = hv_rectangle()
    motions = sk.FreeMotions()
    assert [m["drag"] for m in motions] == ["bottom.start.h", "bottom.start.v", "bottom.end.h", "right.end.v"]
    assert len(motions) == sk.dof == 4
    corners = ["bottom.start", "bottom.end", "right.end", "top.end"]
    shift_h, shift_v, width, height = (m["velocity"] for m in motions)
    assert {p: pytest.approx(shift_h[p], abs=TOL) for p in corners} == {p: (1, 0) for p in corners}
    assert {p: pytest.approx(shift_v[p], abs=TOL) for p in corners} == {p: (0, 1) for p in corners}
    assert width.keys() == {"bottom.end", "right.end"} and height.keys() == {"right.end", "top.end"}
    assert width["bottom.end"] == pytest.approx((1, 0), abs=TOL) and width["right.end"] == pytest.approx((1, 0), abs=TOL)
    assert height["top.end"] == pytest.approx((0, 1), abs=TOL)
    assert motions[0]["moves"] == ["bottom", "right", "top", "left"]
    assert motions[2]["moves"] == ["bottom", "right", "top"] and motions[3]["moves"] == ["right", "top", "left"]


def test_a_free_circle_shifts_and_grows():
    sk = px.Sketch()
    sk.Circle((1, 2), 0.5)
    motions = sk.FreeMotions()
    assert [m["drag"] for m in motions] == ["Circle.1.center.h", "Circle.1.center.v", "Circle.1.r"]
    assert motions[2]["velocity"] == {"Circle.1.r": pytest.approx(1.0)}
    assert motions[2]["moves"] == ["Circle.1"]


def test_a_free_line_shifts_turns_about_its_start_and_stretches():
    sk = px.Sketch()
    sk.Line(4, 0, id="a")
    motions = sk.FreeMotions()
    assert [m["drag"] for m in motions] == ["a.start.h", "a.start.v", "a.end.v", "a.end.h"]
    assert motions[2]["velocity"] == {"a.end": pytest.approx((0, 1), abs=TOL)}
    assert motions[3]["velocity"] == {"a.end": pytest.approx((1, 0), abs=TOL)}


def test_a_pinned_rectangle_keeps_width_and_height():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Vertical, id="right") \
        .Line(-4, 0, px.Horizontal, id="top").Close(px.Vertical)
    assert drags(sk) == ["bottom.end.h", "right.end.v"] and sk.dof == 2


def test_a_fixed_point_leaves_the_rotation_about_it():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(3, 1, id="arm")
    sk.Constrain("arm", px.Distance, 4)
    (turn,) = sk.FreeMotions()
    assert turn["drag"] == "arm.end.v" and sk.dof == 1
    h, v = sk.Location("arm.end")
    dh, dv = turn["velocity"]["arm.end"]
    assert dh * h + dv * v == pytest.approx(0, abs=TOL) and dv == pytest.approx(1)


def test_a_square_on_a_slant_turns():
    sk = px.Sketch()
    sk.Line(3, 1, id="a").Line(-1, 3, px.Perpendicular, id="b").Line(-3, -1, px.Perpendicular, id="c").Close(px.Perpendicular)
    sk.Constrain("a", "b", px.Equal)
    motions = sk.FreeMotions()
    assert len(motions) == sk.dof == 4
    assert [m["drag"] for m in motions[:2]] == ["a.start.h", "a.start.v"]
    turn = motions[2]["velocity"]
    assert motions[2]["drag"] == "b.end.h"
    x0, y0 = sk.Location("a.start")
    rates = []
    for point, (dh, dv) in turn.items():
        h, v = sk.Location(point)
        assert dh * (h - x0) + dv * (v - y0) == pytest.approx(0, abs=TOL)
        rates.append((dv * (h - x0) - dh * (v - y0)) / ((h - x0) ** 2 + (v - y0) ** 2))
    assert turn.keys() == {"a.end", "b.end", "c.end"} and rates == pytest.approx([-0.25] * 3)


def test_a_dimensioned_sketch_has_no_free_motion():
    sk = hv_rectangle()
    sk.Constrain("bottom.start", "origin", px.Coincident)
    sk.Constrain("bottom", px.Distance, 4)
    sk.Constrain("right", px.Distance, 2)
    assert sk.dof == 0 and sk.FreeMotions() == []


@pytest.mark.parametrize("draw", [
    lambda sk: sk.Move((0, 0), px.Fix).Line(2, 0, px.Horizontal).Arc(1, 90, px.Tangent).Line(0, 2),
    lambda sk: sk.Move((0, 3)).Spline([(1, 3.5), (2, 3), (3, 4)], id="wave"),
    lambda sk: sk.Line(3, 0).Line(0, 2).Close().Circle((5, 5), 1).Circle((8, 5), 1),
])
def test_one_motion_per_degree_of_freedom(draw):
    sk = px.Sketch()
    draw(sk)
    motions = sk.FreeMotions()
    assert len(motions) == sk.dof
    assert len({m["drag"] for m in motions}) == len(motions)
    for m in motions:
        assert m["moves"]


def test_each_motion_after_the_rigid_ones_holds_every_other_drag():
    sk = px.Sketch()
    sk.Line(3, 0).Line(0, 2).Close().Circle((5, 5), 1)
    motions = sk.FreeMotions()
    assert len(motions) == sk.dof == 9
    assert drags(sk)[:5] == ["Line.1.start.h", "Line.1.start.v", "Line.1.end.v", "Circle.1.center.h", "Circle.1.center.v"]
    for m in motions:
        assert moved_at(m["velocity"], m["drag"]) == pytest.approx(1)
    for m in motions[5:]:
        for other in motions:
            if other is not m:
                assert moved_at(m["velocity"], other["drag"]) == pytest.approx(0, abs=TOL), (m["drag"], other["drag"])


def test_a_quick_sketch_has_no_free_motions():
    assert not hasattr(px.QuickSketch(), "FreeMotions")
