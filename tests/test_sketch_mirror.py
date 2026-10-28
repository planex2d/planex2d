# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

TOL = 1e-12


def plate(make=px.QuickSketch):
    sk = make()
    sk.Move((1, 0)).Line((4, 0), id="a").Line((4, 3), id="b").Line((1, 3), id="c").Close(id="d")
    return sk


def test_mirror_of_the_whole_sketch_mirrors_its_plane():
    sk = plate()
    before = sk.Location("a.end")
    sk.Mirror(px.V)
    assert sk.Location("a.end") == before
    assert sk.ToXYZ((4, 0)) == pytest.approx((-4, 0, 0), abs=TOL)
    assert sk.ToXYZ((0, 1)) == pytest.approx((0, 1, 0), abs=TOL)
    assert px.Plane(sk).normal == pytest.approx((0, 0, -1), abs=TOL)
    assert sk.Face().mass == pytest.approx(9, rel=TOL)


def test_mirrored_whole_sketch_is_a_copy():
    sk = plate()
    right = sk.Mirrored(px.H)
    assert right is not sk and right.ToXYZ((4, 3)) == pytest.approx((4, -3, 0), abs=TOL)
    assert sk.ToXYZ((4, 3)) == pytest.approx((4, 3, 0), abs=TOL)


def test_mirror_of_elements_moves_them_in_place():
    sk = plate()
    sk.Circle((6, 1), 0.5, id="hole")
    sk.Move((8, 0)).Arc(1, 90, id="bend")
    x, y = sk._Geometry("bend")["center"]
    sk.Mirror(["hole", "bend"], px.V)
    assert sk._Geometry("hole")["center"] == pytest.approx((-6, 1), abs=TOL)
    assert sk._Geometry("bend")["center"] == pytest.approx((-x, y), abs=TOL)
    assert sk._Geometry("bend")["ccw"] is False
    assert sk.Location("a.end") == pytest.approx((4, 0), abs=TOL)


@pytest.mark.constraints
def test_mirror_in_a_constraint_sketch_keeps_the_constraints_among_the_elements():
    sk = px.Sketch()
    sk.Move((2, 0)).Line(3, 0, id="p").Line(1, 2, id="q")
    sk.Constrain("p", px.Distance, 3, id="lp")
    sk.Constrain("p", "q", px.Angle, 60, id="turn")
    before = {k: sk._Geometry("p")[k] for k in ("start", "end")}
    sk.Mirror(["p", "q"], px.V)
    for k, (x, y) in before.items():
        assert sk._Geometry("p")[k] == pytest.approx((-x, y), abs=TOL)
    assert sk["turn"] == pytest.approx(-60, abs=1e-9) and sk["lp"] == pytest.approx(3)
    sk["lp"] = 4
    assert sk._Geometry("p")["length"] == pytest.approx(4, abs=1e-9) and sk["turn"] == pytest.approx(-60, abs=1e-9)


@pytest.mark.constraints
def test_mirror_held_by_a_constraint_raises_and_changes_nothing():
    sk = px.Sketch()
    sk.Move((2, 0)).Line(3, 0, px.Fix, id="p")
    before = sk._Geometry("p")
    with pytest.raises(ValueError, match=r"Mirror: Fix\.1 hold the elements where they are; nothing changed"):
        sk.Mirror("p", px.V)
    assert sk._Geometry("p") == before


@pytest.mark.constraints
def test_mirrored_copies_live_under_their_call():
    sk = plate(px.Sketch)
    sk.Mirrored(["a", "b"], px.V, id="left")
    assert sk.wires["left"] == ["left.a", "left.b"] and sk.elements.mirrors == ["left"]
    assert sk._Geometry("left.a")["end"] == pytest.approx((-4, 0), abs=TOL)
    sk.Remove("left")
    assert "left.a" not in sk.elements["Line"] and "Mirrored" not in sk.elements


@pytest.mark.constraints
def test_a_mirror_shows_one_tie_and_the_constraints_of_its_originals():
    sk = px.Sketch()
    sk.Move((2, 0)).Line(3, 0, id="p").Line(1, 2, id="q")
    sk.Constrain("p", px.Distance, 3, id="lp")
    sk.Constrain("p", "q", px.Angle, 60, id="turn")
    sk.Mirrored(["p", "q"], px.V)
    c = sk.constraints
    assert repr(c["Symmetric"]["Mirrored.1"]) == '["p", "q"], "Mirrored.1", px.V, px.Symmetric'
    assert repr(c["Distance"]["Mirrored.1.lp"]) == '"Mirrored.1.p", px.Distance, 3, derived=True'
    assert repr(c["Angle"]["Mirrored.1.turn"]) == '"Mirrored.1.p", "Mirrored.1.q", px.Angle, -60, derived=True'
    assert len(c["Symmetric"]) == 1 and "Equal" not in c
    sk["lp"] = 4
    sk.Update()
    assert repr(sk.constraints["Distance"]["Mirrored.1.lp"]) == '"Mirrored.1.p", px.Distance, 4, derived=True'
    with pytest.raises(ValueError, match=r"Mirrored.1.lp is derived from lp of the originals: change that"):
        sk["Mirrored.1.lp"] = 5
    with pytest.raises(ValueError, match=r"Remove\('Mirrored.1.turn'\): Mirrored.1.turn is derived from turn"):
        sk.Remove("Mirrored.1.turn")
    assert any(i["id"] == "Mirrored.1.lp" and i["derived"] for i in sk._Constraints())


@pytest.mark.constraints
def test_free_copies_keep_the_shape_of_their_originals():
    sk = px.Sketch()
    sk.Move((2, 0)).Line(3, 0, id="p").Line(1, 2, px.Perpendicular, id="q")
    sk.Mirrored(["p", "q"], px.V, auto_constraint=False)
    assert "Mirrored.1" not in sk.constraints.get("Symmetric", {})
    assert repr(sk.constraints["Perpendicular"]["Mirrored.1.Perpendicular.1"]) == '"Mirrored.1.p", "Mirrored.1.q", px.Perpendicular'


@pytest.mark.constraints
def test_a_pattern_shows_one_tie_per_copy():
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(1, 0.5)
    sk.LinearPattern("Rectangle.1", px.H, 3, 2)
    ties = sk.constraints["LinearPattern"]
    assert list(ties) == ["LinearPattern.1.1", "LinearPattern.1.2"]
    assert repr(ties["LinearPattern.1.2"]) == '"Rectangle.1", step 2 of "LinearPattern.1"'
    assert "LinearPattern.1.1.Rectangle.1.Perpendicular.1" in sk.constraints["Perpendicular"]
    with pytest.raises(ValueError, match=r"LinearPattern.1.2 is a copy of LinearPattern.1, it follows the pattern: change "
                                         r"LinearPattern.1.spacing or LinearPattern.1.count"):
        sk["LinearPattern.1.2"] = 3
    sk["LinearPattern.1.count"] = 4
    sk.Update()
    assert list(sk.constraints["LinearPattern"]) == ["LinearPattern.1.1", "LinearPattern.1.2", "LinearPattern.1.3"]
