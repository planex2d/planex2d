# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

occ = pytest.importorskip("netgen.occ")


def named_box(p, q, name):
    box = occ.Box(occ.Pnt(*p), occ.Pnt(*q))
    box.faces.Min(occ.X).name = "xmin"
    box.faces.Max(occ.X).name = "xmax"
    box.faces.Min(occ.Y).name = "ymin"
    box.faces.Max(occ.Y).name = "ymax"
    box.name = name
    return box


def at_height(z):
    return occ.Axes(p=(0, 0, z), n=occ.Z, h=occ.X)


def test_a_box_cut_across_is_a_rectangle_named_after_its_faces():
    s = px.Intersect3d(at_height(1), named_box((0, 0, 0), (2, 3, 4), "block"))
    assert type(s) is px.Shape and len(s.faces) == 1 and s.faces[0].name == "block"
    assert s.mass == pytest.approx(6, abs=1e-12)
    assert sorted(e.name for e in s.edges) == ["xmax", "xmin", "ymax", "ymin"]
    assert all(isinstance(e.edge, px.LineEdge) for e in s.edges)


def test_a_tilted_cut_through_a_cylinder_is_its_ellipse():
    cylinder = occ.Cylinder(occ.Pnt(0, 0, -5), occ.Z, r=1, h=10)
    tilt = math.radians(30)
    plane = occ.Axes(p=(0, 0, 0), n=(0, -math.sin(tilt), math.cos(tilt)), h=occ.X)
    s = px.Intersect3d(plane, cylinder)
    assert len(s.faces) == 1 and s.mass == pytest.approx(math.pi / math.cos(tilt), rel=1e-6)


def test_a_face_in_the_plane_belongs_to_the_cross_section():
    step = occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=2, h=1) + occ.Cylinder(occ.Pnt(0, 0, 1), occ.Z, r=1, h=1)
    assert px.Intersect3d(at_height(1), step).mass == pytest.approx(4 * math.pi, rel=1e-12)
    assert px.Intersect3d(at_height(0.5), step).mass == pytest.approx(4 * math.pi, rel=1e-12)
    assert px.Intersect3d(at_height(1.5), step).mass == pytest.approx(math.pi, rel=1e-12)
    assert px.Intersect3d(at_height(0), step).mass == pytest.approx(4 * math.pi, rel=1e-12)


def test_a_hole_through_the_solid_stays_a_hole():
    block = occ.Box(occ.Pnt(-2, -2, 0), occ.Pnt(2, 2, 1)) - occ.Cylinder(occ.Pnt(0, 0, -1), occ.Z, r=1, h=3)
    s = px.Intersect3d(at_height(0.5), block)
    assert s.mass == pytest.approx(16 - math.pi, rel=1e-12) and len(s.faces[0].inner) == 1


def test_solids_side_by_side_share_their_wall():
    pair = occ.Glue([named_box((0, 0, 0), (1, 1, 1), "a"), named_box((1, 0, 0), (2, 1, 1), "b")])
    s = px.Intersect3d(at_height(0.5), pair)
    assert sorted((f.name, round(f.mass, 12)) for f in s.faces) == [("a", 1), ("b", 1)]
    assert len(s.edges) == 7


def test_a_plane_that_misses_or_only_touches_gives_nothing_and_says_so():
    box = named_box((0, 0, 0), (1, 1, 1), "box")
    with pytest.warns(px.PlanexWarning, match="misses the shape"):
        assert len(px.Intersect3d(at_height(5), box).edges) == 0
    lying = occ.Cylinder(occ.Pnt(0, 0, 0), occ.X, r=1, h=3)
    with pytest.warns(px.PlanexWarning, match="only touches"):
        assert len(px.Intersect3d(at_height(1), lying).faces) == 0


def test_a_free_face_gives_the_line_where_it_meets_the_plane():
    sheet = occ.WorkPlane(occ.Axes(p=(0, 0, 0), n=occ.Y, h=occ.X)).MoveTo(0, -1).Rectangle(3, 2).Face()
    sheet.name = "sheet"
    s = px.Intersect3d(at_height(0), sheet)
    assert len(s.faces) == 0 and len(s.edges) == 1 and s.edges[0].name == "sheet"
    assert s.edges[0].mass == pytest.approx(3, abs=1e-12)


@pytest.mark.constraints
def test_planes_of_planex_objects_and_sketches():
    box = named_box((0, 0, 0), (2, 3, 4), "block")
    assert px.Intersect3d(px.Plane(occ.Axes(p=(0, 0, 2), n=occ.Z, h=occ.X)), box).mass == pytest.approx(6, abs=1e-12)
    assert px.Intersect3d(None, box).mass == pytest.approx(6, abs=1e-12)
    sk = px.Sketch(at_height(2))
    sk.Intersect3d(box)
    assert sorted(e.name for e in sk.Construction().edges) == ["xmax", "xmin", "ymax", "ymin"]
    assert px.Intersect3d(sk, box).mass == pytest.approx(6, abs=1e-12)
    old = px.QuickSketch(at_height(2)).Intersect3d(box, construction=False, name="cut")
    assert old.Face().mass == pytest.approx(6, abs=1e-12) and {e.name for e in old.Face().edges} == {"cut"}
