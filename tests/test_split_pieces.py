# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

TOL = 1e-12


def crossed(make=px.QuickSketch):
    sk = make()
    sk.Move((0, 0)).Line((4, 0), id="a")
    sk.Move((1, -1)).Line((1, 1), id="x1").Move((3, -1)).Line((3, 1), id="x2")
    return sk


def test_split_pieces_are_numbered_and_the_id_is_their_wire():
    sk = crossed()
    sk.Split("a", ["x1", "x2"])
    assert sk.wires["a"] == ["a.1", "a.2", "a.3"] and "a" not in sk.elements["Line"]
    assert sorted(i.local for i in sk("a")) == ["a.1", "a.2", "a.3"]
    assert sk.Measure("a")["length"] == pytest.approx(4) and sk.Location("a.mid") == pytest.approx((2, 0), abs=TOL)
    assert sk("a.end")[0] == pytest.approx((4, 0), abs=TOL)
    sk.Move((2, -1)).Line((2, 1), id="x3")
    sk.Split("a.2", "x3")
    assert sk.wires["a.2"] == ["a.2.1", "a.2.2"] and sk.wires["a"] == ["a.1", "a.2.1", "a.2.2", "a.3"]
    with pytest.raises(ValueError, match=r"Trim\('a'\): it is cut into a.1, a.2.1, a.2.2, a.3 already; cut one of them"):
        sk.Trim("a")


def test_trimmed_pieces_count_on():
    sk = crossed()
    sk.Trim(("a", (0.5, 0)), "x1")
    sk.Trim(("a", (3.5, 0)), "x2")
    assert sorted(sk.elements["Line"])[:3] == ["a", "a.trimmed.1", "a.trimmed.2"]
    assert sk.Measure("a")["length"] == pytest.approx(2)


def test_removing_the_wire_removes_the_pieces_and_their_span():
    sk = crossed()
    sk.Split("a", ["x1", "x2"])
    sk.Remove("a")
    assert not any(i.startswith("a") for i in sk.elements["Line"]) and "a" not in sk.wires
    with pytest.raises(ValueError, match="no element 'a.end'"):
        sk.Location("a.end")


@pytest.mark.constraints
def test_split_without_ties_lets_the_pieces_move_apart():
    tied, free = crossed(px.Sketch), crossed(px.Sketch)
    tied.Split("a", ["x1", "x2"])
    free.Split("a", ["x1", "x2"], auto_constraint=False)
    assert free.dof > tied.dof


@pytest.mark.constraints
def test_split_without_ties_of_a_copied_element_raises():
    sk = crossed(px.Sketch)
    sk.Mirrored("a", px.V)
    with pytest.raises(ValueError, match=r"auto_constraint=False: Mirrored.1 follow it as it is"):
        sk.Split("a", ["x1", "x2"], auto_constraint=False)
    sk.Split("a", ["x1", "x2"])
    assert sk.wires["a"] == ["a.1", "a.2", "a.3"]
