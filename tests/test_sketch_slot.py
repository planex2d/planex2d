# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9
PARTS = ("spine", "left", "right", "startcap", "endcap")


def drawing(sk):
    return {g["id"]: (g["kind"], bool(g["construction"])) for g in sk._Drawing()}


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_slot_is_its_spine_with_both_sides_and_round_caps(make):
    sk = make().Move((0, 0)).Slot(10, 4)
    d = drawing(sk)
    assert d["Slot.1.spine"] == ("Line", True)
    assert d["Slot.1.left"] == ("Line", False) and d["Slot.1.right"] == ("Line", False)
    assert d["Slot.1.startcap"] == ("Arc", False) and d["Slot.1.endcap"] == ("Arc", False)
    assert sk.Face().mass == pytest.approx(40 + 4 * math.pi, rel=TOL)
    assert tuple(sk.pen.position) == pytest.approx((10, 0), abs=TOL)
    assert "Slot" in sk.elements and "Offset" not in sk.elements


def test_slots_take_the_forms_of_line_and_the_id():
    sk = px.QuickSketch().Move((0, 0)).Slot((0, 10), 2, id="groove")
    assert {"groove." + p for p in PARTS} <= set(drawing(sk))
    assert sk.Face().mass == pytest.approx(20 + math.pi, rel=TOL)
    two = px.QuickSketch().Slot((0, 0), (3, 4), 2)
    assert two.Face().mass == pytest.approx(10 + math.pi, rel=TOL)
    rel = px.QuickSketch().Move((0, 0)).Slot(3, 4, 2)
    assert rel.Face().mass == pytest.approx(10 + math.pi, rel=TOL)


def test_an_arc_slot_follows_its_arc():
    sk = px.QuickSketch().Move((0, 0)).ArcSlot(10, 90, 2)
    d = drawing(sk)
    assert d["ArcSlot.1.spine"] == ("Arc", True) and d["ArcSlot.1.left"][0] == "Arc"
    assert sk.Face().mass == pytest.approx(2 * 5 * math.pi + math.pi, rel=TOL)


def test_a_slot_that_cannot_be_raises():
    with pytest.raises(ValueError, match="Slot"):
        px.QuickSketch().Move((0, 0)).Slot(10, 0)
    with pytest.raises(ValueError, match="ArcSlot"):
        px.QuickSketch().Move((0, 0)).ArcSlot(1, 90, 3)


@pytest.mark.constraints
def test_the_width_of_a_sketch_slot_drives_it():
    sk = px.Sketch().Slot((0, 0), (10, 0), 4, px.Fix)
    assert sk["Slot.1.w"] == pytest.approx(4)
    sk["Slot.1.w"] = 6
    sk.Update()
    assert sk.Face().mass == pytest.approx(60 + 9 * math.pi, rel=TOL)
    sk.Remove("Slot.1")
    assert not any(i.startswith("Slot.1") for i in drawing(sk))
