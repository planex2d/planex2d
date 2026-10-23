# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def drawn():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((4, 0)).Line((4, 3)).Close(id_wire="outline")
    sk.Move((10, 0)).Line((12, 0))
    sk.Move((20, 0)).Rectangle(2, 1)
    sk.Offset("outline", 1, id="wall")
    return sk


def test_the_groups_are_listed_by_the_call_that_made_them():
    sk = drawn()
    e = sk.elements
    assert e["Chain"] == ["outline", "Chain.2"] and e["Rectangle"] == ["Rectangle.1"] and e["Offset"] == ["wall"]
    assert e["Line"][:3] == ["Line.1", "Line.2", "Line.3"]
    assert (e.chains, e.rectangles, e.offsets, e.lines) == (e["Chain"], e["Rectangle"], e["Offset"], e["Line"])
    assert e.linear_patterns == [] and e.mirrors == [] and "LinearPattern" not in e
    assert {"chains", "lines", "linear_patterns", "axes", "projections"} <= set(dir(e))
    with pytest.raises(AttributeError, match="has no 'Chain'"):
        e.Chain


def test_a_removed_group_and_a_failed_call_leave_no_entry():
    sk = drawn()
    sk.Remove("Rectangle.1")
    assert "Rectangle" not in sk.elements
    with pytest.raises(ValueError):
        sk.Move((30, 0)).Rectangle(0, 1)
    assert "Rectangle" not in sk.elements and sk.elements.chains == ["outline", "Chain.2"]


@pytest.mark.constraints
def test_a_pattern_is_listed_by_its_call():
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(1, 1)
    sk.LinearPattern("Rectangle.1", px.H, 3, 2)
    assert sk.elements.linear_patterns == ["LinearPattern.1"] and "Pattern" not in sk.elements
