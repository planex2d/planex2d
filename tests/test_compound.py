# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import copy
import math
import operator

import pytest

import planex as px
from helpers import square

NO_BOOLEANS = r"^a Compound has no booleans, merge it first with px\.Unify\(comp\)$"


def compound():
    return px.Compound([square(0, 0), px.QuickSketch().Circle((3, 3), 1).Face()])


def operands():
    return [square(0.5, 0), square(0.5, 0) + square(5, 5), px.Line((0.5, -1), (0.5, 2))]


@pytest.mark.parametrize("op", [operator.add, operator.sub, operator.mul], ids=["+", "-", "*"])
@pytest.mark.parametrize("k", range(3), ids=["face", "shape", "wire"])
def test_a_compound_on_the_left_of_an_operator_raises(op, k):
    comp, f = compound(), operands()[k]
    with pytest.raises(TypeError, match=NO_BOOLEANS):
        op(comp, f)


@pytest.mark.parametrize("op", [operator.add, operator.sub, operator.mul], ids=["+", "-", "*"])
@pytest.mark.parametrize("k", range(3), ids=["face", "shape", "wire"])
def test_a_compound_on_the_right_of_an_operator_raises(op, k):
    comp, f = compound(), operands()[k]
    with pytest.raises(TypeError, match=NO_BOOLEANS):
        op(f, comp)


def test_wire_plus_keeps_its_unions_and_errors():
    w = px.Line((0, 1), (2, 1))
    u = w + px.Line((1, 0), (1, 2))
    assert isinstance(u, px.Shape) and len(u.faces) == 0 and len(u.edges) == 4
    chains = px.Line((1, 0), (1, 2)) + px.Line((5, 5), (6, 5))
    assert isinstance(chains, px.Shape)
    u = w + chains
    assert isinstance(u, px.Shape) and len(u.faces) == 0 and len(u.edges) == 5
    view = (square(0, 0) + square(3, 0)).faces[0]
    assert isinstance(view, px.FaceView)
    for face in (square(0, 0), view):
        with pytest.raises(TypeError, match=r"^wire \+ face is not supported$"):
            w + face
    with pytest.raises(TypeError, match="^expected a Face, Shape or Wire$"):
        w + 5


def test_sum_with_a_compound_raises():
    with pytest.raises(TypeError, match=NO_BOOLEANS):
        sum([compound(), square(0.5, 0)])


@pytest.mark.parametrize("call", [px.Subtract, px.Intersect])
def test_subtract_and_intersect_refuse_a_compound_anywhere_in_the_list(call):
    for parts in ([compound(), square(0.5, 0)], [square(0.5, 0), compound()], [square(0.5, 0), px.Compound([])],
                  compound()):
        with pytest.raises(TypeError, match=NO_BOOLEANS):
            call(parts)


def test_unify_and_glue_still_take_compounds():
    comp, f = compound(), square(0.5, 0)
    assert px.Unify([comp, f]).mass == pytest.approx(1.5 + math.pi, rel=1e-14)
    assert px.Unify(comp).mass == pytest.approx(1 + math.pi, rel=1e-14)
    glued = px.Glue([comp, f])
    assert isinstance(glued, px.Shape) and glued.mass == pytest.approx(1.5 + math.pi, rel=1e-14)
    nested = px.Compound([comp, px.Compound([f])])
    assert px.Unify([nested]).mass == pytest.approx(1.5 + math.pi, rel=1e-14)


def test_the_objects_a_compound_is_made_of_stay_where_they_are():
    f, w = square(0, 0), px.Line((0.5, -1), (0.5, 2))
    c = px.Compound([f, w])
    assert c[0] is not f and c[1] is not w
    c.Translate((1, 0))
    assert c.bounding_box == ((1, -1), (2, 2))
    assert f.bounding_box == ((0, 0), (1, 1)) and w.bounding_box == ((0.5, -1), (0.5, 2))


def test_a_repeated_part_and_a_part_shared_with_a_nested_compound_move_once():
    f = square(0, 0)
    twice = px.Compound([f, f])
    twice.Translate((1, 0))
    assert twice.bounding_box == ((1, 0), (2, 1)) and f.bounding_box == ((0, 0), (1, 1))
    inner = px.Compound([f])
    outer = px.Compound([inner, f])
    outer.Rotate((0, 0), 90)
    (x0, y0), (x1, y1) = outer.bounding_box
    assert (x0, y0, x1, y1) == pytest.approx((-1, 0, 0, 1), abs=1e-15)
    assert inner.bounding_box == ((0, 0), (1, 1))


def test_parts_keep_their_ids():
    f = square(0, 0)
    f.id = "rotor"
    sk = px.QuickSketch(id="stator")
    sk.Move((0, 0)).Line((1, 0))
    inner = px.Compound([f], id="inner")
    c = px.Compound([inner, f, sk])
    assert c[0] is not inner and c[0].id == "inner" and c[0][0].id == "rotor"
    assert c[1].id == "rotor" and [e.id for e in c[1].edges] == [e.id for e in f.edges]
    assert isinstance(c[2], px.QuickSketch) and c[2] is not sk and c[2].id == "stator"
    assert px.Compound([px.Compound([f])])[0]._id is None


def test_making_a_compound_gives_no_ids():
    px.ResetIdCounters()
    f = square(0, 0)
    c = px.Compound([f])
    assert f.id == "Face.1" and c[0].id == "Face.2"


def test_an_import_list_numbers_its_wires_in_its_kind(tmp_path):
    pytest.importorskip("ezdxf")
    px.ResetIdCounters()
    px.WriteDXF(px.Compound([square(0, 0), square(3, 0)]), tmp_path / "two.dxf")
    c = px.FromDXF(tmp_path / "two.dxf")
    assert isinstance(c, list) and sorted(p.id for p in c) == ["DXF.1", "DXF.2"]


@pytest.mark.parametrize("copier", [lambda o: o.Copy(), copy.copy, copy.deepcopy], ids=["Copy", "copy", "deepcopy"])
def test_copies_of_a_compound_are_independent_with_fresh_ids(copier):
    f = square(0, 0)
    f.id = "rotor"
    c = px.Compound([f, px.Compound([f])], id="pair")
    d = copier(c)
    assert d is not c and d[0] is not c[0] and d[1][0] is not c[1][0]
    assert d.id != "pair" and d[0].id.startswith("Copy.") and d[1][0].id.startswith("Copy.")
    d.Translate((5, 0))
    assert c.bounding_box == ((0, 0), (1, 1)) and d.bounding_box == ((5, 0), (6, 1))
    moved = c.Translated((0, 2))
    assert moved.bounding_box == ((0, 2), (1, 3)) and c.bounding_box == ((0, 0), (1, 1))
