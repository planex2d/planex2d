# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

bd = pytest.importorskip("build123d")

import planex as px  # noqa: E402
from helpers import square  # noqa: E402


def approx3(t):
    return pytest.approx(t, abs=1e-12)


def test_to_build123d_face_wire_and_shape():
    face = px.Circle((0, 0), 2).Face() - px.Circle((0.5, 0), 0.5).Face()
    b = face.ToBuild123d()
    assert isinstance(b, bd.Face) and b.is_valid
    assert b.area == pytest.approx(face.mass, rel=1e-12)
    w = px.ToBuild123d(square(0, 0, 3).Wire())
    assert isinstance(w, bd.Wire) and w.length == pytest.approx(12)
    assert w.is_closed
    hook = px.ToBuild123d(px.FromPolyline([(0, 0), (1, 0), (1, 1)]))
    assert isinstance(hook, bd.Wire) and not hook.is_closed and hook.length == pytest.approx(2)
    two = px.Glue([square(0, 0), square(1, 0)]).ToBuild123d()
    assert isinstance(two, bd.Compound) and len(two.faces()) == 2 and len(two.edges()) == 7
    assert square(0, 0, 2).ToBuild123d(scale=1000).area == pytest.approx(4e6)


def test_to_build123d_extrudes():
    solid = bd.extrude(square(0, 0, 2).ToBuild123d(), 3)
    assert solid.is_valid and solid.volume == pytest.approx(12)


def test_from_build123d_box_faces_on_the_sketch_plane():
    faces = bd.Box(10, 20, 5).faces()
    expected = [(faces.sort_by(bd.Axis.Z)[-1], (0, 0, 2.5), (1, 0, 0), (0, 1, 0), 200),
                (faces.sort_by(bd.Axis.Z)[0], (0, 0, -2.5), (1, 0, 0), (0, -1, 0), 200),
                (faces.sort_by(bd.Axis.X)[-1], (5, 0, 0), (0, 1, 0), (0, 0, 1), 100)]
    for f, origin, h, v, area in expected:
        face = px.FromBuild123d(f)
        assert isinstance(face, px.Face) and face.mass == pytest.approx(area)
        plane = px.Plane(px.QuickSketch(face))
        assert plane.origin == approx3(origin) and plane.h == approx3(h) and plane.v == approx3(v)


def test_from_build123d_list_wire_and_round_trip():
    top = (bd.Box(10, 10, 1) - bd.Cylinder(2, 1)).faces().sort_by(bd.Axis.Z)[-1]
    assert px.FromBuild123d(top).mass == pytest.approx(100 - 4 * math.pi)
    assert px.FromBuild123d([top]).mass == pytest.approx(100 - 4 * math.pi)
    outline = px.FromBuild123d(top.outer_wire(), scale=0.001)
    assert isinstance(outline, px.Wire) and outline.mass == pytest.approx(0.04)
    face = square(0, 0, 4) - px.Circle((1, 1), 0.5).Face()
    back = px.FromBuild123d(face.ToBuild123d())
    assert back.mass == pytest.approx(face.mass, rel=1e-12) and px.Plane(back).h == approx3((1, 0, 0))


def test_from_build123d_rejects():
    with pytest.raises(ValueError, match="one plane"):
        px.FromBuild123d(bd.Box(1, 1, 1))
    with pytest.raises(TypeError, match="no build123d shape"):
        px.FromBuild123d([5])
    with pytest.raises(ValueError, match="no shapes"):
        px.FromBuild123d([])
