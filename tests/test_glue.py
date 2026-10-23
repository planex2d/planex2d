# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import circle, rect, square


def named(obj, name):
    obj.name = name
    obj.edges.name = name
    return obj


def test_an_inclusion_keeps_its_name_and_its_edges_in_either_order():
    air, iron = named(square(0, 0, 4), "air"), named(square(1, 1, 2), "iron")
    for parts in ([air, iron], [iron, air]):
        g = px.Glue(parts)
        assert sorted((f.name, round(f.mass, 9)) for f in g.faces) == [("air", 12), ("iron", 4)]
        assert len(g.edges) == 8 and sum(e.name == "iron" for e in g.edges) == 4
        shape = g.ToOCCShape()
        assert len(shape.faces) == 2 and len(set(e for f in shape.faces for e in f.edges)) == 8


def test_an_inclusion_in_a_corner_keeps_its_name_in_either_order():
    air, iron = named(square(0, 0, 4), "air"), named(square(0, 0, 2), "iron")
    for parts in ([air, iron], [iron, air]):
        g = px.Glue(parts)
        assert sorted((f.name, round(f.mass, 9)) for f in g.faces) == [("air", 12), ("iron", 4)]
        assert len(g.edges) == 8 and sum(e.name == "iron" for e in g.edges) == 4


def test_overlapping_parts_give_both_differences_and_the_overlap_named_by_the_first():
    a, b = named(square(0, 0, 2), "A"), named(square(1, 1, 2), "B")
    g = px.Glue([a, b])
    assert [(f.name, round(f.mass, 9)) for f in g.faces] == [("A", 3), ("A", 1), ("B", 3)]
    assert sorted(e.name for e in g.faces[1].edges) == ["A", "A", "B", "B"]
    assert len(g.edges) == 12 and g.mass == pytest.approx(7)
    g = px.Glue([b, a])
    assert [(f.name, round(f.mass, 9)) for f in g.faces] == [("B", 3), ("B", 1), ("A", 3)]
    assert sorted(e.name for e in g.faces[1].edges) == ["A", "A", "B", "B"]


def test_three_overlapping_discs_give_seven_faces_named_by_the_first_disc_covering_them():
    a, b, c = (named(circle(x, y, 1), n) for x, y, n in ((0, 0, "a"), (1, 0, "b"), (0.5, math.sqrt(3) / 2, "c")))
    g = px.Glue([a, b, c])
    assert [f.name for f in g.faces] == ["a", "a", "a", "a", "b", "b", "c"]
    assert sum(f.mass for f in g.faces) == pytest.approx(px.Unify([a, b, c]).mass, rel=1e-12)
    area = {n: sum(f.mass for f in g.faces if f.name == n) for n in "abc"}
    assert area["a"] == pytest.approx(math.pi, rel=1e-12)
    assert area["b"] == pytest.approx((b - a).mass, rel=1e-12)
    assert area["c"] == pytest.approx(px.Subtract([c, a, b]).mass, rel=1e-12)


def test_the_first_part_names_the_edge_touching_faces_share():
    a, b = named(square(0, 0), "a"), named(square(1, 0), "b")
    for parts, name in (([a, b], "a"), ([b, a], "b")):
        g = px.Glue(parts)
        assert [e.name for e in g.edges if e.start.x == 1 and e.end.x == 1] == [name]


def test_a_wire_through_an_overlap_splits_every_face_it_crosses():
    a, b = named(square(0, 0, 2), "A"), named(square(1, 1, 2), "B")
    g = px.Glue([a, b, px.Line((-1, 1.5), (4, 1.5))])
    assert sorted((f.name, round(f.mass, 9)) for f in g.faces) == [("A", 0.5), ("A", 0.5), ("A", 0.5), ("A", 2.5),
                                                                   ("B", 0.5), ("B", 2.5)]


def test_touching_faces_share_their_edge():
    g = px.Glue([named(square(0, 0), "a"), named(square(1, 0), "b")])
    assert len(g.faces) == 2 and len(g.edges) == 7
    assert sorted(f.name for f in g.faces) == ["a", "b"]
    assert len((g + square(5, 0)).faces) == 2 and len((g + square(0, 0)).faces) == 1


def test_a_wire_through_a_face_splits_it_and_a_free_edge_stays_inside():
    plate = named(square(0, 0, 2), "plate")
    split = px.Glue([plate, px.Line((1, -1), (1, 3))])
    assert sorted(round(f.mass, 9) for f in split.faces) == [2, 2] and {f.name for f in split.faces} == {"plate"}
    assert len(split.wires) == 2 + 1
    crack = px.Glue([plate, px.Line((0.5, 1), (1.5, 1))])
    assert len(crack.faces) == 1 and crack.mass == pytest.approx(4)
    face = crack.ToOCCShape().faces[0]
    assert len(face.edges) == 5
    mesh = px.OCCGeometry(crack).GenerateMesh(maxh=0.4)
    pts = [tuple(round(c, 9) for c in p.p) for p in mesh.Points()]
    assert (0.5, 1, 0) in pts and (1.5, 1, 0) in pts


def test_booleans_keep_the_faces_of_a_glued_shape():
    g = px.Glue([named(square(0, 0, 2), "a"), named(square(2, 0, 2), "b")])
    cut = g - rect(-1, 0.5, 6, 1)
    assert len(cut.faces) == 4 and cut.mass == pytest.approx(4)
    assert sorted(f.name for f in cut.faces) == ["a", "a", "b", "b"]
    assert len(cut.edges) == 4 * 4 - 2
    common = g * rect(1, 0.5, 2, 1)
    assert sorted((f.name, round(f.mass, 9)) for f in common.faces) == [("a", 1), ("b", 1)]
    assert len(common.edges) == 7
    merged = g + square(5, 0)
    assert len(merged.faces) == 2 and merged.mass == pytest.approx(9)


def test_gluing_a_glued_shape_keeps_its_parts():
    g = px.Glue([named(square(0, 0), "a"), named(square(1, 0), "b")])
    again = px.Glue([g, named(square(0.5, 0.5), "c")])
    assert sorted((f.name, round(f.mass, 9)) for f in again.faces) == [("a", 0.25), ("a", 0.75), ("b", 0.25), ("b", 0.75),
                                                                       ("c", 0.5)]


def test_an_interface_normal_points_out_of_the_part_that_names_it():
    ngs = pytest.importorskip("ngsolve")
    air = px.QuickSketch((-2, -2)).Rectangle(4, 4).Face()
    air.name = "air"
    for e in air.edges:
        e.name = "outer"
    coil = px.QuickSketch((-0.5, -0.5)).Rectangle(1, 1).Face()
    coil.name = "coil"
    for e in coil.edges:
        e.name = "coil"
    mesh = ngs.Mesh(px.OCCGeometry(px.Glue([air, coil])).GenerateMesh(maxh=0.3))
    n = ngs.specialcf.normal(2)

    def flux(name):
        return ngs.Integrate(ngs.x * n[0], mesh, ngs.BND, definedon=mesh.Boundaries(name))

    assert flux("coil") == pytest.approx(1, abs=1e-9)
    assert flux("outer") == pytest.approx(16, abs=1e-9)


def test_a_shared_edge_normal_points_out_of_the_part_that_names_it():
    ngs = pytest.importorskip("ngsolve")

    def flux(g, name):
        mesh = ngs.Mesh(px.OCCGeometry(g).GenerateMesh(maxh=0.3))
        n = ngs.specialcf.normal(2)
        return ngs.Integrate(ngs.x * n[0], mesh, ngs.BND, definedon=mesh.Boundaries(name))

    a, b = named(square(0, 0), "a"), named(square(1, 0), "b")
    assert flux(px.Glue([a, b]), "a") == pytest.approx(1, abs=1e-9)
    assert flux(px.Glue([b, a]), "b") == pytest.approx(1, abs=1e-9)
    a, b = named(square(0, 0, 2), "A"), named(square(1, 1, 2), "B")
    g = px.Glue([a, b])
    assert flux(g, "A") == pytest.approx(4, abs=1e-9) and flux(g, "B") == pytest.approx(4, abs=1e-9)
