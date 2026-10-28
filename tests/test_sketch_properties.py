# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

KINDS = [pytest.param(px.Sketch, marks=pytest.mark.constraints, id="Sketch"), px.QuickSketch]
TOL = 1e-12


def plate(make):
    """a 4 x 2 rectangle, its lines r.Line.1 to r.Line.4, the wire id r"""
    sk = make((0, 0))
    sk.Rectangle(4, 2, id="r")
    return sk


@pytest.mark.parametrize("make", KINDS)
def test_every_property_of_an_element_is_writable(make):
    sk = plate(make)
    line = sk("r.Line.1")
    line.name, line.maxh, line.minh, line.hpref, line.layer = "floor", 0.1, 0.01, 1, 2
    line.col, line.partition = (1, 0, 0), [0.5, 0.25]
    expected = ("floor", 0.1, 0.01, 1, 2, [1, 0, 0, 1], [0.25, 0.5])
    assert (line.name, line.maxh, line.minh, line.hpref, line.layer, line.col, line.partition) == expected
    e = sk.Wire("r.Line.1").edges[0]
    assert (e.name, e.maxh, e.minh, e.hpref, e.layer, e.col, e.partition) == expected
    element = sk("r.Line.1")[0]
    assert (element.name, element.maxh) == ("floor", 0.1)
    assert sk(name="floor").ids == sk("r.Line.1").ids
    element.name = None
    assert line.name is None and sk.Wire("r.Line.1").edges[0].name is None


@pytest.mark.parametrize("make", KINDS)
def test_a_bad_value_changes_nothing(make):
    sk = plate(make)
    with pytest.raises(ValueError, match="maxh"):
        sk("r").maxh = -1
    assert sk("r").maxh is None
    with pytest.raises(ValueError, match="partition"):
        sk("r.Line.1").partition = [0.5, 1.5]
    with pytest.raises(TypeError, match="construction is True or False"):
        sk("r.Line.1").construction = 1
    with pytest.raises(ValueError, match="no element 'nothing'"):
        sk("nothing").col = (1, 0, 0)


@pytest.mark.parametrize("make", KINDS)
def test_a_selection_sets_all_and_reads_what_they_share(make):
    sk = plate(make)
    sk.Circle((10, 1), 1, id="far")
    assert [i.local for i in sk("r") if ".Line." in i.local] == [f"r.Line.{k}" for k in range(1, 5)]
    assert [i.local for i in sk("r") if ".Point." in i.local] == [f"r.Point.{k}" for k in range(1, 5)]
    sk("r").col = (0, 0, 1)
    assert sk("r").col == [0, 0, 1, 1]
    assert {tuple(e.col) for e in sk.Wire("r").edges} == {(0, 0, 1, 1)}
    sk("r.Line.2").col = (1, 0, 0)
    assert sk("r").col is None and sk("far").col is None
    sk("r", "far").maxh = 0.5
    assert sk("r", "far").maxh == 0.5


@pytest.mark.parametrize("make", KINDS)
def test_points_take_name_maxh_and_hpref(make):
    sk = plate(make)
    corner = sk(kind="point", near=(4, 0))
    corner.name, corner.maxh, corner.hpref = "tip", 0.05, 1
    assert (corner.name, corner.maxh, corner.hpref) == ("tip", 0.05, 1)
    assert corner.col is None
    with pytest.raises(ValueError, match="a point takes name, maxh and hpref"):
        corner.col = (1, 0, 0)
    sk(kind="point", near=(0, 0)).name = "zero"
    named = {v.name: (v.p.x, v.p.y) for v in sk.ToOCCFace().vertices if v.name}
    assert named == {"tip": pytest.approx((4, 0), abs=TOL), "zero": pytest.approx((0, 0), abs=TOL)}
    sk(kind="point").maxh = 0.2
    assert sk(kind="point").maxh == 0.2 and sk("r").maxh is None


@pytest.mark.parametrize("make", KINDS)
def test_the_properties_reach_the_export(make):
    sk = plate(make)
    sk("r.Line.1").name, sk("r.Line.1").maxh = "floor", 0.1
    floor = [e for e in sk.ToOCCFace().edges if e.name == "floor"]
    assert len(floor) == 1 and floor[0].maxh == pytest.approx(0.1)
    assert [e.name for e in sk.Face().edges].count("floor") == 1


@pytest.mark.parametrize("make", KINDS)
def test_construction_takes_an_element_out_of_the_drawing(make):
    sk = plate(make)
    sk.Circle((2, 1), 0.5, id="hole")
    sk.Circle((10, 1), 1, id="far", construction=True)
    pens, wires = (sk.pen.position, sk.pen.direction), sk.wires
    dof, constraints = (sk.dof, repr(sk.constraints)) if make is px.Sketch else (None, None)
    assert sk.Face().mass == pytest.approx(8 - math.pi / 4, rel=TOL)
    sk("hole").construction = True
    assert sk("hole").construction is True
    assert sk.Face().mass == pytest.approx(8, rel=TOL)
    assert len(sk.Construction().edges) == 2
    assert [g["construction"] for g in sk._Drawing() if g["id"] == "hole"] == [True]
    assert sk.bounding_box == ((0, 0), (4, 2))
    sk("far").construction = False
    assert sk.bounding_box == ((0, 0), (11, 2))
    assert len(sk.Wires()) == 2
    sk("far").construction, sk("hole").construction = True, False
    assert sk.Face().mass == pytest.approx(8 - math.pi / 4, rel=TOL)
    assert ((sk.pen.position, sk.pen.direction), sk.wires) == (pens, wires)
    if make is px.Sketch:
        assert (sk.dof, repr(sk.constraints)) == (dof, constraints)


@pytest.mark.parametrize("make", KINDS)
def test_an_offset_source_can_be_construction_and_its_parts_take_properties(make):
    sk = plate(make)
    sk.Offset("r", 1, id="wall")
    sk("r").construction = True
    assert sk.Face().mass == pytest.approx(8 + 12 + math.pi, rel=TOL)
    if make is px.Sketch:
        sk["wall.d"] = 2
        assert sk.Face().mass == pytest.approx(px.Face([sk.Wire("r")]).Offset(2).mass, rel=TOL)
    sk("wall.r.Line.1").col = (1, 0, 0)
    assert sk("wall.r.Line.1").col == [1, 0, 0, 1]
    sk("wall").construction = True
    assert sk("wall").construction is True and len(sk.Construction().edges) == 12


@pytest.mark.constraints
def test_a_pattern_copy_can_be_construction():
    sk = px.Sketch()
    sk.Circle((2, 0), 0.3, id="bolt")
    sk.CircularPattern("bolt", sk.origin, 4)
    sk("CircularPattern.1.1.bolt").construction = True
    assert len(sk.Construction().edges) == 1
    sk["CircularPattern.1.count"] = 5
    assert len(sk.Construction().edges) == 1 and sk("CircularPattern.1.4.bolt").construction is False


@pytest.mark.constraints
def test_the_sketch_origin_is_no_selectable_point():
    sk = plate(px.Sketch)
    assert "origin" not in [i.local for i in sk(kind="point")]
    assert sk(kind="point", near=(0, 0))[0].local != "origin"


def test_selections_of_a_shape_set_their_edges():
    f = px.Move((0, 0)).Rectangle(2, 1).Face()
    left = f.edges[px.h < 0.5]
    left.name, left.maxh = "left", 0.1
    assert (left.name, left.maxh) == ("left", 0.1)
    assert [e.name for e in f.edges].count("left") == 1
    with pytest.raises(ValueError, match="selects nothing"):
        f.edges[px.h > 100].name = "none"
