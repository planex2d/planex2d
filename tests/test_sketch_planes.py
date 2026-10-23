# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest
from netgen import occ

import planex as px


def block():
    return occ.Box((0, 0, 0), (2, 3, 4))


def test_a_sketch_on_a_face_takes_its_outward_normal_and_the_projected_origin_and_x_axis():
    top = px.QuickSketch(block().faces.Max(occ.Z))
    assert top.Normal() == pytest.approx((0, 0, 1)) and top.ToXYZ((1, 2)) == pytest.approx((1, 2, 4))
    bottom = px.QuickSketch(block().faces.Min(occ.Z))
    assert bottom.Normal() == pytest.approx((0, 0, -1)) and bottom.ToXYZ((1, 1)) == pytest.approx((1, -1, 0))
    side = px.QuickSketch(block().faces.Max(occ.X))
    assert side.Normal() == pytest.approx((1, 0, 0)) and side.ToXYZ((1, 1)) == pytest.approx((2, 1, 1))
    assert px.QuickSketch(block().faces.Max(occ.X)).Rectangle(1, 1).Face().center == pytest.approx((2, 0.5, 0.5))


def test_origin_and_axis_are_projected_onto_the_plane():
    top = block().faces.Max(occ.Z)
    s = px.QuickSketch(px.Plane(top, origin=(1, 1, 10), axis=(1, 1, 0)))
    assert s.ToXYZ((0, 0)) == pytest.approx((1, 1, 4))
    assert s.ToXYZ((math.sqrt(2), 0)) == pytest.approx((2, 2, 4))
    assert px.QuickSketch(px.Plane(top, axis=occ.Y)).ToXYZ((1, 0)) == pytest.approx((0, 1, 4))
    assert px.QuickSketch(px.Plane(top, origin=occ.Pnt(2, 3, 0))).ToXYZ((0, 0)) == pytest.approx((2, 3, 4))
    with pytest.raises(ValueError, match="perpendicular"):
        px.QuickSketch(px.Plane(top, axis=(0, 0, 1)))
    with pytest.raises(ValueError, match="one plane"):
        px.QuickSketch(block())
    with pytest.raises(ValueError, match="one plane"):
        px.QuickSketch(occ.Cylinder((0, 0, 0), occ.Z, 1, 2).faces[0])
    circle = occ.Circle((0, 0), 1).Face().Move((0, 0, 3)).edges[0]
    assert px.QuickSketch(circle).ToXYZ((1, 0))[2] == pytest.approx(3)


def test_a_sketch_on_a_planex_object_has_its_axes():
    axes = occ.Axes((0, 0, 1), occ.Z, occ.Y)
    f = px.QuickSketch(axes).Rectangle(2, 1).Face()
    for on in (f, px.Shape(f), f.edges[0].Wire(), px.QuickSketch(axes).Line((1, 0))):
        s = px.QuickSketch(on)
        assert s.ToXYZ((1, 2)) == pytest.approx((-2, 1, 1)) and s.Wires() == []
    assert px.QuickSketch(px.Plane(f, origin=(5, 5, 0))).ToXYZ((0, 0)) == pytest.approx((5, 5, 1))
    assert px.QuickSketch(px.Plane(f, axis=occ.X)).ToXYZ((1, 0)) == pytest.approx((1, 0, 1))


def test_set_origin_moves_the_axes_and_keeps_the_drawing():
    s = px.Move((0, 0)).Line((2, 0)).Line((2, 1))
    s.SetOrigin((2, 0))
    assert s.ToXYZ((0, 0)) == pytest.approx((2, 0, 0))
    assert s.pen.position == (0, 1)
    s.Line((0, 2)).Line((-2, 2)).Close()
    assert s.Face().center == pytest.approx((1, 1, 0)) and s.Face().mass == pytest.approx(4)
    p = px.QuickSketch(occ.Box((0, 0, 0), (4, 4, 1)).faces.Max(occ.Z)).Vertex("corner", (4, 4))
    p.SetOrigin("corner")
    assert p.ToXYZ((0, 0)) == pytest.approx((4, 4, 1)) and p.points["corner"] == (0, 0)
