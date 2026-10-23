# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import numpy as np
import pytest
import planex as px


def plane_z1():
    return px.Plane(origin=(0, 0, 1), normal=(0, 0, 1))


def tilted():
    return px.Plane(origin=(0, 0, 0), normal=(0, 1, 1))


def ends(sk):
    return sk.Location("Line.1.start"), sk.Location("Line.1.end")


def test_move_line_xyz_equals_hv():
    a = px.QuickSketch(plane_z1()).Move((1, 2, 1)).Line((3, 2, 1))
    b = px.QuickSketch(plane_z1()).Move((1, 2)).Line((3, 2))
    assert ends(a) == ends(b)


def test_tilted_plane_to_xyz_roundtrip():
    plane = tilted()
    p = plane.ToXYZ((2, 3))
    a = px.QuickSketch(plane).Move(p).Line(plane.ToXYZ((5, 3)))
    b = px.QuickSketch(plane).Move((2, 3)).Line((5, 3))
    for u, v in zip(ends(a), ends(b)):
        assert u == pytest.approx(v, abs=1e-12)


def test_point_off_plane_raises():
    sk = px.QuickSketch(plane_z1()).Move((0, 0))
    with pytest.raises(ValueError, match=r"ToHV.*|0\.5") as e:
        sk.Line((1, 2, 1.5))
    assert "ToHV" in str(e.value) and "0.5" in str(e.value)


def test_circle_keywords_and_sketch():
    a = px.Sketch(plane_z1()).Move((1, 2, 1)).Line((3, 2, 1))
    b = px.Sketch(plane_z1()).Move((1, 2)).Line((3, 2))
    assert ends(a) == ends(b)
    c = px.QuickSketch(plane_z1()).Circle(2, center=(1, 2, 1))
    d = px.QuickSketch(plane_z1()).Circle(2, center=(1, 2))
    assert c.Location("Circle.1.center") == d.Location("Circle.1.center")
    with pytest.raises(ValueError, match="ToHV"):
        px.Sketch(plane_z1()).Move((1, 2, 1)).Line((3, 2, 2))


def test_edge_constructors_with_plane():
    plane = plane_z1()
    a = px.Line((1, 2, 1), (3, 2, 1), plane=plane)
    b = px.Line((1, 2), (3, 2), plane=plane)
    assert np.array_equal(a.edges[0].Sample(1e-3), b.edges[0].Sample(1e-3))
    px.Arc((1, 0, 1), (0, 1, 1), (0, 0, 1), plane=plane)
    px.Circle((0, 0, 1), 2, plane=plane)
    with pytest.raises(ValueError, match="ToHV"):
        px.Line((1, 2, 3), (3, 2, 1), plane=plane)


def test_face_contains_xyz():
    plane = plane_z1()
    face = px.QuickSketch(plane).Rectangle(4, 4).Face()
    inside = plane.ToXYZ((1, 1))
    assert face.Contains(inside) == face.Contains((1, 1))
    assert face.Contains((100, 100, 1)) is False


def test_sketch_start_xyz():
    a = px.QuickSketch((1, 2, 0))
    b = px.QuickSketch((1, 2))
    assert a.pen.position == b.pen.position
    assert px.Sketch((1, 2, 0)).pen.position == b.pen.position
    with pytest.raises(ValueError, match="ToHV"):
        px.QuickSketch((1, 2, 0.5))


def test_spline_polygon_bspline_xyz():
    plane = plane_z1()
    xyz = [(0, 0, 1), (1, 1, 1), (2, 0, 1)]
    hv = [(0, 0), (1, 1), (2, 0)]
    for make in (lambda p: px.Spline(p, plane=plane), lambda p: px.FromPolyline(p, plane=plane),
                 lambda p: px.BSpline(p, [0, 0, 0, 1, 1, 1], 2, plane=plane)):
        a, b = make(xyz), make(hv)
        assert np.array_equal(a.edges[0].Sample(1e-3), b.edges[0].Sample(1e-3))
    assert np.array_equal(px.FromPolyline(np.array(xyz, float), plane=plane).edges[0].Sample(1e-3),
                          px.FromPolyline(hv, plane=plane).edges[0].Sample(1e-3))
    with pytest.raises(ValueError, match="ToHV"):
        px.Spline([(0, 0, 1), (1, 1, 2), (2, 0, 1)], plane=plane)
