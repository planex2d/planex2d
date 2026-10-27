# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

TOL = 1e-12


def outline():
    return px.QuickSketch((1, 1)).Line(4).Turn(90).Line(3).Turn(90).Line(4).Close().Wire()


def plate():
    return px.QuickSketch().Rectangle(4, 3).Circle((2, 1.5), 0.5).Face()


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_wire_comes_in_as_elements(make):
    sk = make()
    assert sk.Add(outline()) is sk
    assert len(sk.elements["Line"]) == 4
    assert sk.Face().mass == pytest.approx(12, abs=TOL)


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_face_brings_its_holes(make):
    sk = make().Add(plate())
    assert sk.Face().mass == pytest.approx(plate().mass, abs=TOL)


def test_the_elements_move_like_drawn_ones():
    sk = px.Sketch().Add(outline())
    assert sk.dof == 8
    first = sk.elements["Line"][0]
    sk.Constrain(first, px.Distance, 5)
    assert sk.dof == 7


def test_auto_constraint_relates_what_comes_in_to_itself_and_the_rest():
    sk = px.Sketch(auto_constraint=True).Line((0, 2), (10, 2))
    sk.Add(px.QuickSketch((5, 5), dir=(0, 1)).Line(2).Arc(1, 90).Line(2).Wire())
    sk.Add(px.QuickSketch((5, 4)).Line(0, -2).Wire())
    assert len(sk.constraints["Tangent"]) == 2
    assert [repr(c).endswith('"Line.1", px.Coincident') for c in sk.constraints["Coincident"].values()] == [True]
    assert sk.redundant == []


def test_lists_compounds_and_other_types_raise():
    sk = px.Sketch()
    with pytest.raises(TypeError, match=r"glue them into one shape first: sk.Add\(px.Glue\(parts\)\)"):
        sk.Add([outline(), outline()])
    with pytest.raises(TypeError, match=r"px.Glue\(parts\)"):
        sk.Add(px.Compound([outline()]))
    with pytest.raises(TypeError, match="a Wire, Face or Shape, not int"):
        sk.Add(1)
    assert "Line" not in sk.elements


@pytest.mark.occ
def test_an_occ_shape_is_projected_instead():
    from netgen import occ

    with pytest.raises(TypeError, match=r"sk.Project\(shape, construction=False\)"):
        px.Sketch().Add(occ.Box((0, 0, 0), (1, 1, 1)))


def test_another_plane_raises_and_the_same_plane_takes_the_sketchs_axes():
    lifted = px.QuickSketch(px.Plane(px.XY, 1.0)).Rectangle(2, 1).Wire()
    with pytest.raises(ValueError, match="another plane"):
        px.Sketch().Add(lifted)
    for plane in (px.Plane(px.XY, origin=(5, 0, 0), axis=(0, 1, 0)), px.Plane((0, 0, 0), (0, 1, 0), (1, 0, 0))):
        sk = px.Sketch(plane).Add(outline())
        assert sk.Face().center == pytest.approx((3, 2.5, 0), abs=TOL)
        assert sk.Face().mass == pytest.approx(12, abs=TOL)
