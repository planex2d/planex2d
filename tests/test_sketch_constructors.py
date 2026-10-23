# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest
from netgen import occ

import planex as px

TOL = 1e-12


def top_of_box():
    return occ.Box((0, 0, 0), (2, 3, 4)).faces.Max(occ.Z)


def face_with_hole():
    return px.QuickSketch().Rectangle(4, 3).Circle(2, 1.5, 0.5).Face()


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_plane_keyword_is_gone(make):
    with pytest.raises(TypeError):
        make(plane=px.XY)


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_top_face_of_a_box(make):
    top = top_of_box()
    sk = make(top)
    assert sk.ToXYZ((0, 0)) == pytest.approx((0, 0, 4), abs=TOL)
    assert sk.ToXYZ((1, 2)) == pytest.approx((1, 2, 4), abs=TOL)
    assert sk.elements == {"Point": ["origin"], "Axis": ["H", "V"]}
    copied = make(top, copy=True)
    assert len(copied.elements["Line"]) == 4
    assert copied.Face().mass == pytest.approx(6, abs=1e-9)


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_copy_of_a_face_with_a_hole(make):
    face = face_with_hole()
    assert face.mass == pytest.approx(12 - math.pi * 0.25, abs=1e-9)
    sk = make(face, copy=True)
    assert sk.Face().mass == pytest.approx(face.mass, abs=TOL)


def test_copy_of_a_face_with_auto_constraint():
    drawn = px.QuickSketch().Rectangle(4, 3).Circle(2, 1.5, 0.5)
    expected = px.Sketch(drawn, copy=True, auto_constraint=True)
    sk = px.Sketch(face_with_hole(), copy=True, auto_constraint=True)
    assert sk.redundant == []
    assert (sk.dof, sk.status) == (expected.dof, expected.status)
    assert sk.constraints.keys() == expected.constraints.keys()


def test_list_of_occ_wires():
    rectangle = occ.WorkPlane().Rectangle(2, 1).Wire()
    circle = occ.WorkPlane().MoveTo(5, 5).Circle(0.5).Wire()
    sk = px.QuickSketch([rectangle, circle], copy=True)
    assert len(sk.elements["Line"]) == 4
    assert len(sk.elements["Circle"]) == 1
    assert len(px.Sketch((rectangle, circle), copy=True).elements["Circle"]) == 1
    lifted = occ.WorkPlane(occ.Axes((0, 0, 1), occ.Z, occ.X)).Rectangle(1, 1).Wire()
    for make in (px.QuickSketch, px.Sketch):
        with pytest.raises(ValueError, match="different planes"):
            make([rectangle, lifted], copy=True)


def test_copy_of_a_plane_has_nothing_to_copy():
    with pytest.warns(px.PlanexWarning, match="nothing to copy"):
        sk = px.QuickSketch(px.Plane(origin=(0, 0, 1)), copy=True)
    assert sk.elements == {"Point": ["origin"], "Axis": ["H", "V"]}
    assert sk.ToXYZ((1, 2)) == pytest.approx((1, 2, 1), abs=TOL)


def test_start_and_nothing():
    sk = px.QuickSketch((2, 3))
    assert sk.pen.position == pytest.approx((2, 3), abs=TOL)
    assert sk.ToXYZ((2, 3)) == pytest.approx((2, 3, 0), abs=TOL)
    empty = px.QuickSketch()
    assert empty.elements == {"Point": ["origin"], "Axis": ["H", "V"]}
    assert empty.ToXYZ((1, 2)) == pytest.approx((1, 2, 0), abs=TOL)


def test_sketch_as_the_first_argument():
    drawn = px.QuickSketch(px.Plane(origin=(0, 0, 2))).Rectangle(2, 1)
    empty = px.QuickSketch(drawn)
    assert empty.elements == {"Point": ["origin"], "Axis": ["H", "V"]}
    assert empty.ToXYZ((1, 2)) == pytest.approx((1, 2, 2), abs=TOL)
    copied = px.QuickSketch(drawn, copy=True)
    assert copied.Face().mass == pytest.approx(2, abs=TOL)
