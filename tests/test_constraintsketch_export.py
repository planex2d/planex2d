# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def plate():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(3.7, 0.25, px.Horizontal, id="bottom").Arc(0.9, 95, px.Tangent, id="fillet") \
        .Line(-0.1, 1.7, px.Vertical, px.Tangent, id="right", name="wall").Line(-4.6, 0.3, px.Perpendicular, id="top") \
        .Close(px.Perpendicular, id="left")
    sk.Constrain("fillet", px.Radius, 1)
    sk.Constrain("bottom.start", "right", px.Distance, 5)
    sk.Constrain("bottom", "top", px.Distance, 3)
    return sk


def test_the_face_of_the_solved_drawing():
    f = plate().Face()
    assert f.mass == pytest.approx(15 - (1 - math.pi / 4), rel=1e-14)
    assert sorted(e.name or "" for e in f.edges) == ["", "", "", "", "wall"]


def test_construction_elements_stay_out_of_faces_and_wires():
    sk = plate()
    sk.Move((1, 1)).Line(2, 0, construction=True)
    assert len(sk.Construction().edges) == 1
    assert len(sk.Face().edges) == 5
    assert sum(len(w.edges) for w in sk.Wires()) == 5


def test_wires_are_the_chains_that_hang_together():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Line(0, 1)
    sk.Move((3, 0)).Line(1, 0)
    sk.Circle((5, 5), 1)
    assert [len(w.edges) for w in sk.Wires()] == [2, 1, 1]
    with pytest.raises(ValueError, match=r"Wire\(\): the drawn edges form 3 wires; sk.Wires\(\) returns them as a list"):
        sk.Wire()
    sk.Constrain("Line.2.end", "Line.3.start", px.Coincident)
    assert [len(w.edges) for w in sk.Wires()] == [3, 1]


def link():
    sk = px.Sketch()
    sk.Circle((0, 0), 1.5, id="boss1", construction=True)
    sk.Circle((6, 0), 1.0, id="boss2", construction=True)
    sk.Line(tangent=("boss1", "boss2"), id="upper")
    sk.Line(tangent=("boss1", "boss2"), reverse=True, id="lower")
    sk.Arc("lower", "upper", coincident="boss2", id="end2")
    sk.Arc("upper", "lower", coincident="boss1", id="end1")
    sk.Circle((0, 0), 0.6, id="hole1").Circle((6, 0), 0.4, id="hole2")
    return sk


@pytest.mark.parametrize("unify", [True, False])
def test_the_link_is_its_outline_and_two_holes(unify):
    wires = link().Wires(unify=unify)
    assert sorted((w.IsClosed(), len(w.edges)) for w in wires) == [(True, 1), (True, 1), (True, 4)]


def rib():
    """a 4 x 2 outline drawn as one chain, a rib from the middle of its bottom up to (2, 1)"""
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, id="bottom").Line(0, 2).Line(-4, 0).Close()
    sk.Move((2, 0)).Line(0, 1, id="rib")
    return sk


def test_a_rib_ending_on_an_outline():
    sk = rib()
    wires = sk.Wires()
    assert sorted((w.IsClosed(), len(w.edges), w.mass) for w in wires) == [(False, 1, 1), (True, 5, 12)]
    assert sorted((w.IsClosed(), len(w.edges), w.mass) for w in sk.Wires(unify=False)) == [(False, 1, 1), (True, 4, 12)]
    with pytest.raises(ValueError, match=r"ToOCCWire\(\): the drawn edges form 2 wires"):
        sk.ToOCCWire()


def test_an_x_is_cut_and_its_chains_turn_at_the_crossing():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 2, id="a")
    sk.Move((0, 2)).Line(2, -2, id="b")
    wires = sk.Wires()
    assert [len(w.edges) for w in wires] == [2, 2]
    assert [w.mass for w in wires] == pytest.approx([math.sqrt(8)] * 2, rel=1e-14)
    drawn = sk.Wires(unify=False)
    assert [len(w.edges) for w in drawn] == [1, 1]
    assert [w.mass for w in drawn] == pytest.approx([math.sqrt(8)] * 2, rel=1e-14)


def test_an_overlap_drawn_twice_is_kept_once():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0)
    sk.Move((1, 0)).Line(2, 0)
    wires = sk.Wires()
    assert [len(w.edges) for w in wires] == [1] and wires[0].mass == pytest.approx(3, rel=1e-15)
    assert [w.mass for w in sk.Wires(unify=False)] == pytest.approx([2, 2], rel=1e-15)


@pytest.mark.parametrize("unify", [True, False])
def test_ends_within_rounding_are_joined_a_larger_gap_stays_open(unify):
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Move((1 + 1e-15, 0)).Line(0, 1)
    assert [len(w.edges) for w in sk.Wires(unify=unify)] == [2]
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Move((1 + 1e-6, 0)).Line(0, 1)
    assert [len(w.edges) for w in sk.Wires(unify=unify)] == [1, 1]


def test_kept_as_drawn_a_chain_crossing_itself_raises():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 2, id="a").Line(0, -2, id="b").Line(-2, 2, id="c").Close(id="d")
    for call in (sk.Wires, sk.Wire, sk.ToOCCWires, sk.ToOCCWire):
        with pytest.raises(ValueError, match=r"\(unify=False\): '[ac]' and '[ac]' cross, touch or overlap in one chain; .*"
                                             r"unify=True cuts"):
            call(unify=False)
    wires = sk.Wires()
    assert [(w.IsClosed(), len(w.edges)) for w in wires] == [(True, 3), (True, 3)]
    assert sorted(w.Face().mass for w in wires) == pytest.approx([1, 1], rel=1e-14)


def test_crossings_are_cut():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 2).Line(0, -2).Line(-2, 2).Close()
    assert sorted(f.mass for f in sk.Shape().faces) == pytest.approx([1, 1], rel=1e-14)
    with pytest.raises(ValueError, match="bound 2 regions"):
        sk.Face()


def test_occ_exports():
    sk = plate()
    assert type(sk.ToOCCFace()).__name__ == "Face"
    assert type(sk.ToOCCWire()).__name__ == "Wire" and type(sk.ToOCCWire(unify=False)).__name__ == "Wire"
    with pytest.raises(ValueError, match="nothing is drawn yet"):
        px.Sketch().Wire()


def test_the_svg_shows_states_construction_and_a_numbered_grid():
    sk = plate()
    svg = sk._repr_svg_()
    assert svg.count('stroke="#2b8a3e"') == 5
    sk.Move((1, 1)).Line(2, 0, construction=True)
    sk.Move((1, 2)).Line(1, 0.5)
    svg = px.ToSVG(sk)
    assert 'stroke-dasharray="6 4"' in svg and 'stroke="black"' in svg
    assert ">5</text>" in svg and ">0</text>" in svg
    assert 'stroke-dasharray="4 3"' in svg and 'stroke-dasharray="2 3"' in svg
    assert px.Sketch()._repr_svg_() is None
