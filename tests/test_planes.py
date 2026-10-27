# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest
from netgen import occ

import planex as px


def block():
    return occ.Box((0, 0, 0), (2, 3, 4))


def test_planes():
    assert (px.XY.normal, px.XY.h) == ((0, 0, 1), (1, 0, 0))
    assert (px.YZ.normal, px.YZ.h, px.YZ.v) == ((1, 0, 0), (0, 1, 0), (0, 0, 1))
    assert (px.ZX.normal, px.ZX.h, px.ZX.v) == ((0, 1, 0), (0, 0, 1), (1, 0, 0))
    p = px.Plane(origin=(1, 2, 3), normal=(0, 0, 2))
    assert p.origin == (1, 2, 3) and p.normal == (0, 0, 1) and p.h == pytest.approx((1, 0, 0))
    assert px.Plane().normal == (0, 0, 1)
    assert px.Plane(normal=(1, 0, 0)).h == pytest.approx((0, 1, 0))
    assert px.Plane(normal=(0, 0, 1), axis=(1, 1, 5)).h == pytest.approx((2**-0.5, 2**-0.5, 0))
    assert px.Plane(block().faces.Max(occ.Z)).origin == pytest.approx((0, 0, 4))
    assert px.Plane(px.XY, origin=(3, 3, 3)).origin == pytest.approx((3, 3, 0))
    assert px.Plane(px.XY, 2).origin == (0, 0, 2) and px.XY.origin == (0, 0, 0)
    assert px.XY.Translated((1, 0, 0)).origin == (1, 0, 0)
    assert px.XY.Rotated((0, 0, 0), (1, 0, 0), 90).normal == pytest.approx((0, -1, 0))
    m = px.XY.Mirrored((0, 0, 1), (0, 0, 1))
    assert m.origin == pytest.approx((0, 0, 2)) and m.normal == pytest.approx((0, 0, -1)) and m.h == pytest.approx((1, 0, 0))
    assert px.XY.ToXYZ((1, 2)) == (1, 2, 0) and isinstance(px.XY.ToOCCAxes(), occ.Axes)
    with pytest.raises(ValueError, match="zero"):
        px.Plane(normal=(0, 0, 0))
    with pytest.raises(ValueError, match="perpendicular"):
        px.Plane(px.XY, axis=(0, 0, 1))


def test_everything_with_axes_takes_planes_and_objects_with_a_plane():
    top = block().faces.Max(occ.Z)
    lifted = px.Plane(origin=(0, 0, 4))
    face = px.QuickSketch(lifted).Rectangle(1, 1).Face()
    sources = (lifted, top, occ.Axes((0, 0, 4), occ.Z, occ.X), face, px.Shape(face), face.edges[0].Wire(),
               px.QuickSketch(lifted), px.Compound([face, face.Translated((5, 0))]))
    for axes in sources:
        assert px._planex.AxesOf(axes).origin[2] == pytest.approx(4)
        assert px.Line((0, 0), (1, 0), plane=axes).ToXYZ((1, 0)) == pytest.approx((1, 0, 4))
        assert px.QuickSketch(axes).ToXYZ((1, 1)) == pytest.approx((1, 1, 4))
    assert px.Arc((1, 0), (-1, 0), (0, 0), plane=top).ToXYZ((0, 0))[2] == pytest.approx(4)
    assert px.Spline([(0, 0), (1, 1), (2, 0)], plane=lifted).ToXYZ((0, 0))[2] == pytest.approx(4)
    assert px.BSpline([(0, 0), (1, 1), (2, 0)], [0, 0, 0, 1, 1, 1], 2, plane=lifted).ToXYZ((0, 0))[2] == pytest.approx(4)
    assert px.Project(block(), plane=top)[0].ToXYZ((0, 0))[2] == pytest.approx(4)
    assert px._planex.AxesOf(None).origin[2] == 0
    with pytest.raises(ValueError, match="different planes"):
        px._planex.AxesOf(px.Compound([face, face.Translated((0, 0, 1))]))
    with pytest.raises(TypeError, match="plane is a Plane"):
        px.Line((0, 0), (1, 0), plane=5)


def test_plot3d_draws_planes_as_squares():
    from planex._plot3d import _plot3d_compound

    f = px.Move((0, 0)).Rectangle(4, 2).Face()
    pmin, pmax = _plot3d_compound([f, px.Plane(px.XY, 1), px.YZ]).bounding_box
    assert pmax[2] == pytest.approx(2, abs=0.05) and pmin[1] == pytest.approx(-2, abs=0.05)
    assert _plot3d_compound(px.XY, plane_size=3).bounding_box[1][0] == pytest.approx(1.5, abs=0.05)


def test_plane_coordinates_are_h_and_v_and_points_are_one_tuple():
    p = px.Plane(origin=(1, 2, 3), normal=(0, 0, 1))
    assert (p.h, p.v) == ((1, 0, 0), (0, 1, 0)) and not hasattr(p, "u")
    assert p.ToXYZ((1, 2)) == (2, 4, 3) and p.ToXYZ([1, 2]) == (2, 4, 3)
    face = px.QuickSketch(p).Move((0, 0)).Rectangle(1, 1).Face()
    assert face.ToHV((2, 4, 9)) == pytest.approx((1, 2)) and face.ToHV([(2, 4, 9)]) == [pytest.approx((1, 2))]
    assert face.ToXYZ([(1, 2), (0, 0)]) == [(2, 4, 3), (1, 2, 3)]
    with pytest.raises(TypeError):
        p.ToXYZ(1, 2)
    with pytest.raises(TypeError):
        face.ToHV(2, 4, 9)
    assert not hasattr(face, "ToUV")


def test_every_plane_argument_takes_everything_with_a_plane():
    base = px.Plane(origin=(0, 0, 2), normal=(0, 0, 1))
    sketch = px.QuickSketch(base)
    sketch.Move((0, 0)).Rectangle(1, 1)
    face = sketch.Face()
    planes = {"Plane": base, "sketch": sketch, "face": face, "shape": sketch.Shape(), "wire": face.outer,
              "compound": px.Compound([face]), "occ.Axes": occ.Axes(p=(0, 0, 2), n=occ.Z, h=occ.X),
              "planar OCC face": face.ToOCCFace()}
    calls = {"QuickSketch": lambda p: px.QuickSketch(p).Move((0, 0)).Line((1, 0)).Wire(),
             "Sketch": lambda p: px.Sketch(p).Move((0, 0)).Line((1, 0)).Wire(),
             "Line": lambda p: px.Line((0, 0), (1, 0), plane=p),
             "Arc": lambda p: px.Arc((1, 0), (-1, 0), (0, 0), plane=p),
             "Polyline": lambda p: px.FromPolyline([(0, 0), (1, 0), (1, 1)], plane=p),
             "Spline": lambda p: px.Spline([(0, 0), (1, 1), (2, 0)], plane=p),
             "FromNACA": lambda p: px.FromNACA("0012", plane=p)}
    for name, plane in planes.items():
        for call, make in calls.items():
            if call == "Sketch" and not hasattr(px._planex, "Sketch"):
                continue
            assert make(plane).ToXYZ((0, 0))[2] == pytest.approx(2), (call, name)
