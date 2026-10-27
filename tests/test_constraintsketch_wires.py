# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-12


def tooth():
    """a tooth on the H axis, named at the end of its chain"""
    sk = px.Sketch()
    sk.Move((1, 0)).Line(1, 0, id="flank").Line(0, 1).Line(-1, 0).Close(id="back", id_wire="tooth")
    return sk


def test_the_end_of_a_chain_names_it():
    sk = tooth()
    assert sk.wires == {"tooth": ["flank", "Line.1", "Line.2", "back"]}
    sk.Move((5, 5)).Line(1, 0).Arc(1, 90).Finish(id_wire="hook")
    sk.Move((9, 9)).Rectangle(2, 1, id="pad")
    assert sk.wires["hook"] == ["Line.3", "Arc.1"]
    assert sk.wires["pad"] == [f"pad.Line.{k}" for k in range(1, 5)]
    assert len(sk.Wire("tooth").edges) == 4 and sk.Measure("pad")["length"] == pytest.approx(6, abs=TOL)


def test_a_wire_id_is_checked_like_any_id():
    sk = tooth()
    with pytest.raises(ValueError, match="id 'flank' is taken"):
        sk.Move((5, 5)).Line(1, 0).Finish(id_wire="flank")
    with pytest.raises(ValueError, match="id 'tooth' is taken"):
        sk.Move((5, 5)).Line(1, 0, id="tooth")
    with pytest.raises(ValueError, match="no chain is drawn to name"):
        sk.Finish(id_wire="nothing")
    # a failing Close draws nothing
    sk.Move((0, 5)).Line(1, 0).Line(0, 1)
    with pytest.raises(ValueError, match="id 'tooth' is taken"):
        sk.Close(id_wire="tooth")
    assert "Line.6" not in sk.elements["Line"]


def test_group_calls_take_wire_ids():
    sk = tooth()
    count = lambda: sum(len(ids) for kind, ids in sk.elements.items() if kind in ("Point", "Line", "Arc", "Circle", "Spline"))
    before = count()
    sk.CircularPattern("tooth", sk.origin, 4)
    assert count() - before == 3 * 4 * 2
    sk = tooth()
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.RigidGroup("tooth", id="body")
    assert sk.dof == 3
    before = set(sk.elements["Line"])
    sk.Mirrored("tooth", px.V)
    copies = [i for i in sk.elements["Line"] if i not in before]
    assert len(copies) == 4
    assert sk.Location(copies[0] + ".end") == pytest.approx((-2, 0), abs=TOL)


def test_removing_a_wire_removes_its_elements_and_an_element_leaves_its_wire():
    sk = tooth()
    sk.Move((5, 5)).Line(1, 0, id="a").Line(0, 1, id="b").Finish(id_wire="hook")
    sk.Remove("b")
    assert sk.wires["hook"] == ["a"]
    sk.Remove("a")
    assert "hook" not in sk.wires
    sk.Remove("tooth")
    assert sk.wires == {} and sk.elements.get("Line", []) == []


def test_every_pen_chain_is_a_wire_named_chain_k():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Line(0, 1)
    assert sk.wires == {"Chain.1": ["Line.1", "Line.2"]}
    sk.Line(-1, 0)
    assert sk.wires == {"Chain.1": ["Line.1", "Line.2", "Line.3"]}
    sk.Move((5, 5)).Line(1, 0).Arc(1, 90)
    assert sk.wires["Chain.2"] == ["Line.4", "Arc.1"]
    sk.Move((9, 9)).Line(1, 0)
    sk.Move((0, 8)).Line(1, 0).Line(0, 1).Close()
    assert sk.wires["Chain.3"] == ["Line.5"]
    assert sk.wires["Chain.4"] == ["Line.6", "Line.7", "Line.8"]
    assert len(sk.Wire("Chain.4").edges) == 3 and len(sk.wires) == 4
    sk.Circle((20, 20), 1)
    sk.Line(1, 0)
    assert len(sk.wires) == 5 and sk.wires["Chain.5"] == ["Line.9"]


def test_id_wire_replaces_chain_k_and_the_number_is_used():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Finish(id_wire="first")
    sk.Move((5, 5)).Line(1, 0)
    assert sk.wires == {"first": ["Line.1"], "Chain.2": ["Line.2"]}
    sk.Move((8, 8)).Line(1, 0).Line(0, 1).Close(id_wire="tri")
    assert sk.wires["tri"] == ["Line.3", "Line.4", "Line.5"] and "Chain.3" not in sk.wires


def test_a_construction_chain_is_a_wire_of_its_own():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Line(0, 1, construction=True).Line(1, 0)
    assert sk.wires == {"Chain.1": ["Line.1", "Line.3"], "Chain.2": ["Line.2"]}


def test_a_failing_call_changes_no_wire():
    sk = tooth()
    sk.Move((5, 5)).Line(1, 0)
    sk.Move((0, 5)).Line(1, 0).Line(0, 1)
    wires = dict(sk.wires)
    with pytest.raises(ValueError, match="id 'tooth' is taken"):
        sk.Close(id_wire="tooth")
    assert sk.wires == wires and sk.wires["Chain.3"] == ["Line.4", "Line.5"]
    sk.Close()
    assert sk.wires["Chain.3"] == ["Line.4", "Line.5", "Line.6"]
    with pytest.raises(ValueError, match=r"id 'Chain.3' contains '\.'"):
        sk.Move((9, 9)).Line(1, 0).Finish(id_wire="Chain.3")
    assert sk.wires["Chain.3"] == ["Line.4", "Line.5", "Line.6"] and sk.wires["Chain.4"] == ["Line.7"]


def test_a_rectangle_is_its_own_wire_and_not_part_of_a_chain():
    sk = px.Sketch()
    sk.Move((1, 1)).Rectangle(4, 2)
    corners = [((1, 1), (5, 1)), ((5, 1), (5, 3)), ((5, 3), (1, 3)), ((1, 3), (1, 1))]
    assert sk.wires == {"Rectangle.1": [f"Rectangle.1.Line.{k}" for k in range(1, 5)]}
    for k, (start, end) in enumerate(corners, 1):
        assert sk.Location(f"Rectangle.1.Line.{k}.start") == pytest.approx(start, abs=TOL)
        assert sk.Location(f"Rectangle.1.Line.{k}.end") == pytest.approx(end, abs=TOL)
    sk.Move((10, 0)).Rectangle(2, 1).Rectangle(1, 1, id="plate")
    assert sk.wires["Rectangle.2"] == [f"Rectangle.2.Line.{k}" for k in range(1, 5)]
    assert sk.wires["plate"] == [f"plate.Line.{k}" for k in range(1, 5)] and "Rectangle.3" not in sk.wires
    assert sk.elements["Line"] == [f"{w}.Line.{k}" for w in ("Rectangle.1", "Rectangle.2", "plate") for k in range(1, 5)]
    assert list(sk.constraints["Perpendicular"]) == [f"{w}.Perpendicular.{k}" for w in ("Rectangle.1", "Rectangle.2", "plate") for k in (1, 2, 3)]
    assert [i for i in sk.elements["Point"] if i.startswith("Rectangle.1.")] == [f"Rectangle.1.Point.{k}" for k in range(1, 5)]
    assert [i for i in sk.elements["Point"] if i.startswith("plate.")] == [f"plate.Point.{k}" for k in range(1, 4)]
    sk.Line(0, 1)
    assert sk.wires["Chain.1"] == ["Line.1"] and len(sk.wires) == 4


def test_a_rectangle_inside_a_chain_leaves_the_chain_as_it_is():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Rectangle(1, 1).Line(0, 1)
    assert sk.wires["Chain.1"] == ["Line.1", "Line.2"]
    assert sk.wires["Rectangle.1"] == [f"Rectangle.1.Line.{k}" for k in range(1, 5)]
    assert sk.Location("Line.2.start") == pytest.approx((1, 0), abs=TOL)


def test_a_rectangle_is_named_with_id_not_id_wire():
    sk = px.Sketch().Rectangle(4, 2, id="plate")
    with pytest.raises(TypeError, match=r"Rectangle\(id_wire=\.\.\.\).*name it with id="):
        sk.Rectangle(1, 1, id_wire="x")
    with pytest.raises(ValueError, match="id 'plate' is taken"):
        sk.Rectangle(1, 1, id="plate")
    assert list(sk.wires) == ["plate"] and len(sk.elements["Line"]) == 4
    sk.Remove("plate")
    assert sk.wires == {}


def test_the_last_call_is_gone():
    assert not hasattr(px.Sketch(), "Last")
