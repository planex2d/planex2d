# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

occ = pytest.importorskip("netgen.occ")


def named_plate():
    f = occ.WorkPlane().Rectangle(2, 1).Face()
    f.name, f.maxh = "plate", 0.2
    f.edges.Min(occ.X).name = "left"
    return f


def test_a_planar_face_keeps_its_names_and_properties():
    s = px.FromOCC(named_plate())
    assert type(s) is px.Shape and s.mass == pytest.approx(2, rel=1e-14)
    assert [(f.name, f.maxh) for f in s.faces] == [("plate", 0.2)]
    assert sorted(e.name for e in s.edges if e.name) == ["left"]


def test_faces_sharing_an_edge_and_holes():
    a = occ.WorkPlane().Rectangle(1, 1).Face()
    a.name = "a"
    b = occ.WorkPlane().MoveTo(1, 0).Rectangle(1, 1).Face()
    b.name = "b"
    s = px.FromOCC(occ.Glue([a, b]))
    assert sorted((f.name, round(f.mass, 12)) for f in s.faces) == [("a", 1), ("b", 1)] and len(s.edges) == 7
    holed = occ.WorkPlane().Rectangle(4, 4).Face() - occ.WorkPlane().MoveTo(2, 2).Circle(0.5).Face()
    s = px.FromOCC(holed)
    assert s.mass == pytest.approx(16 - math.pi / 4, rel=1e-12) and len(s.faces[0].inner) == 1


def test_a_face_on_a_tilted_plane_and_free_wires():
    plane = occ.Axes(p=(0, 0, 1), n=(0, 1, 1), h=occ.X)
    s = px.FromOCC(occ.WorkPlane(plane).Rectangle(2, 3).Face())
    assert s.mass == pytest.approx(6, rel=1e-12)
    assert s.Normal() == pytest.approx((0, math.sqrt(0.5), math.sqrt(0.5)), abs=1e-14)
    w = occ.WorkPlane().MoveTo(0, 0).Line(2).Line(0, 1).Finish().Wire()
    s = px.FromOCC(w)
    assert len(s.faces) == 0 and len(s.edges) == 2 and sum(e.mass for e in s.edges) == pytest.approx(3)


def test_reading_a_step_file_keeps_the_names(tmp_path):
    named_plate().WriteStep(str(tmp_path / "plate.step"))
    s = px.FromOCC(tmp_path / "plate.step")
    assert s.mass == pytest.approx(2, rel=1e-12) and [f.name for f in s.faces] == ["plate"]
    assert "left" in {e.name for e in s.edges}


def test_what_is_not_planar_raises():
    with pytest.raises(ValueError, match="one plane.*Intersect3d"):
        px.FromOCC(occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(1, 1, 1)))
    two = occ.Compound([occ.WorkPlane().Rectangle(1, 1).Face(), occ.WorkPlane(occ.Axes(p=(0, 0, 1), n=occ.Z, h=occ.X)).Rectangle(1, 1).Face()])
    with pytest.raises(ValueError, match="Intersect3d"):
        px.FromOCC(two)
    dome = occ.Sphere(occ.Pnt(0, 0, 0), 1) * occ.Box(occ.Pnt(-2, -2, 0), occ.Pnt(2, 2, 2))
    curved = [f for f in dome.faces if abs(f.center.z) > 1e-9][0]
    with pytest.raises(ValueError, match="curved|one plane"):
        px.FromOCC(curved)
