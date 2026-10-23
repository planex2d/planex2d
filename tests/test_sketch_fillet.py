# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def plate(make=px.QuickSketch):
    """the square (0, 0) to (4, 4), drawn a, b, c, d counterclockwise from the origin"""
    sk = make()
    sk.Move((0, 0)).Line((4, 0), id="a").Line((4, 4), id="b").Line((0, 4), id="c").Close(id="d")
    return sk


def drawing(sk):
    return {g["id"]: (g["kind"], bool(g["construction"])) for g in sk._Drawing()}


def test_a_sketch_fillet_names_the_arc_and_keeps_the_corner_as_construction():
    sk = plate()
    assert sk.Fillet("a.end", 1) is sk
    d = drawing(sk)
    assert d["Fillet.1"] == ("Arc", False)
    assert d["a"] == ("Line", False) and d["b"] == ("Line", False)
    assert d["a.trimmed.1"] == ("Line", True) and d["b.trimmed.1"] == ("Line", True)
    assert sk.Location("a.trimmed.1.end") == pytest.approx((4, 0), abs=TOL)
    assert sk.Location("a.end") == pytest.approx((3, 0), abs=TOL) and sk.Location("b.start") == pytest.approx((4, 1), abs=TOL)
    g = sk._Geometry("Fillet.1")
    assert g["r"] == pytest.approx(1, abs=TOL) and g["center"] == pytest.approx((3, 1), abs=TOL)
    assert sk.Face().mass == pytest.approx(16 - (1 - math.pi / 4), rel=TOL)


def test_a_sketch_chamfer_by_two_lengths_and_reversed():
    sk = plate().Chamfer("b.end", 1, d2=2)
    # b is drawn before c: d on b, d2 on c
    assert sk.Location("b.end") == pytest.approx((4, 3), abs=TOL) and sk.Location("c.start") == pytest.approx((2, 4), abs=TOL)
    assert drawing(sk)["Chamfer.1"] == ("Line", False)
    assert sk.Face().mass == pytest.approx(15, rel=TOL)
    back = plate().Chamfer("b.end", 1, d2=2, reverse=True)
    assert back.Location("b.end") == pytest.approx((4, 2), abs=TOL) and back.Location("c.start") == pytest.approx((3, 4), abs=TOL)


def test_several_corners_count_on_and_take_ids():
    sk = plate().Fillet(["a.end", (4, 4)], 0.5)
    assert {"Fillet.1", "Fillet.2"} <= set(drawing(sk))
    sk.Fillet("c.end", 0.5, id="round")
    assert drawing(sk)["round"] == ("Arc", False)
    assert sk.Face().mass == pytest.approx(16 - 3 * 0.25 * (1 - math.pi / 4), rel=TOL)


def test_a_corner_that_is_no_corner_raises_and_changes_nothing():
    sk = plate()
    before = drawing(sk)
    with pytest.raises(ValueError, match="Fillet"):
        sk.Fillet("a.end", 5)
    with pytest.raises(ValueError, match="Fillet"):
        sk.Fillet((2, 2), 1)
    assert drawing(sk) == before
    open_chain = px.QuickSketch().Move((0, 0)).Line((4, 0), id="a").Line((4, 4), id="b")
    with pytest.raises(ValueError):
        open_chain.Fillet("a.start", 1)


def test_a_fillet_may_use_an_element_up():
    sk = plate().Fillet(["a.end", "b.end", "c.end", "d.end"], 2)
    assert sk.Face().mass == pytest.approx(4 * math.pi, rel=TOL)
    assert sum(1 for kind, construction in drawing(sk).values() if kind == "Arc" and not construction) == 4


def test_a_sketch_fillet_at_a_spline_corner():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((4, 0), id="a").Line((4, 4), id="b")
    sk.Spline([(2, 5), (0, 4)], id="s")
    sk.Line((0, 0), id="d")
    area = sk.Face().mass
    sk.Fillet("b.end", 0.3)
    g = sk._Geometry("Fillet.1")
    assert g["r"] == pytest.approx(0.3, abs=TOL)
    assert sk.Face().mass < area


def fixed_plate():
    """plate in a constraint sketch with a and c fixed (so b and d too), so only the fillet or chamfer can move"""
    sk = px.Sketch()
    sk.Move((0, 0)).Line((4, 0), px.Fix, id="a").Line((4, 4), id="b").Line((0, 4), px.Fix, id="c")
    sk.Close(id="d")
    return sk


@pytest.mark.constraints
def test_the_radius_of_a_sketch_fillet_drives_it():
    sk = fixed_plate().Fillet("a.end", 1)
    assert sk["Fillet.1.r"] == pytest.approx(1)
    sk["Fillet.1.r"] = 2
    sk.Update()
    g = sk._Geometry("Fillet.1")
    assert g["r"] == pytest.approx(2, abs=TOL) and g["center"] == pytest.approx((2, 2), abs=TOL)
    assert sk.Location("a.end") == pytest.approx((2, 0), abs=TOL) and sk.Location("b.start") == pytest.approx((4, 2), abs=TOL)
    assert sk.Location("a.trimmed.1.end") == pytest.approx((4, 0), abs=TOL)
    assert sk.Face().mass == pytest.approx(16 - 4 * (1 - math.pi / 4), rel=TOL)


@pytest.mark.constraints
def test_the_values_of_a_sketch_chamfer_are_editable():
    sk = fixed_plate().Chamfer("b.end", 1, d2=2)
    assert sk["Chamfer.1.d"] == pytest.approx(1) and sk["Chamfer.1.d2"] == pytest.approx(2)
    sk["Chamfer.1.d"] = 0.5
    sk.Update()
    assert sk.Location("b.end") == pytest.approx((4, 3.5), abs=TOL) and sk.Location("c.start") == pytest.approx((2, 4), abs=TOL)
    sk["Chamfer.1.reverse"] = True
    sk.Update()
    assert sk.Location("b.end") == pytest.approx((4, 2), abs=TOL) and sk.Location("c.start") == pytest.approx((3.5, 4), abs=TOL)
    angled = fixed_plate().Chamfer("b.end", 1, angle=45)
    assert angled["Chamfer.1.angle"] == pytest.approx(45)
    assert angled.Location("c.start") == pytest.approx((3, 4), abs=TOL)


@pytest.mark.constraints
def test_a_sketch_fillet_follows_its_lines():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((4, 0), px.Fix, id="a").Line((4, 4), id="b")
    sk.Constrain("b", px.Vertical)
    sk.Constrain("b", px.Distance, 4, id="len")
    sk.Fillet("a.end", 1)
    sk["len"] = 6
    sk.Update()
    g = sk._Geometry("Fillet.1")
    assert g["r"] == pytest.approx(1, abs=TOL) and g["center"] == pytest.approx((3, 1), abs=TOL)


def test_the_new_element_takes_the_properties_given():
    sk = plate()
    sk("a").maxh, sk("b").maxh, sk("a").name = 0.2, 0.1, "floor"
    sk.Fillet("a.end", 1)
    assert (sk("Fillet.1").name, sk("Fillet.1").maxh) == ("floor", 0.1)
    sk.Fillet("b.end", 1, name="round", maxh=0.5)
    assert (sk("Fillet.2").name, sk("Fillet.2").maxh) == ("round", 0.5)
    sk.Chamfer(["c.end", "d.end"], 0.5, name=["r1", "r2"], col=[(1, 0, 0), (0, 0, 1)], layer=3)
    assert [sk(i).name for i in ("Chamfer.1", "Chamfer.2")] == ["r1", "r2"]
    assert [sk(i).layer for i in ("Chamfer.1", "Chamfer.2")] == [3, 3]
    assert sk("Chamfer.2").col[:3] == pytest.approx((0, 0, 1))
    with pytest.raises(ValueError, match="one value for all or one per corner"):
        plate().Fillet(["a.end", "b.end"], 1, name=["x", "y", "z"])
    with pytest.raises(TypeError, match="partition"):
        plate().Fillet("a.end", 1, partition=[0.5])


@pytest.mark.constraints
def test_a_constraint_sketch_fillet_takes_the_properties_given():
    sk = fixed_plate().Fillet(["a.end", "b.end"], 1, name=["r1", "r2"])
    assert [sk(i).name for i in ("Fillet.1", "Fillet.2")] == ["r1", "r2"]
