# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

meshing = pytest.importorskip("netgen.meshing")
IdentificationType = meshing.IdentificationType


def xy(v):
    return (v.x, v.y)


def plate(w=2.0, h=1.0, x=0.0):
    f = px.QuickSketch((x, 0)).Rectangle(w, h).Face()
    for e in f.edges:
        m = e.midpoint
        e.name = {x: "left", x + w: "right"}.get(m.x, {0.0: "bottom", h: "top"}.get(m.y))
    return f


def identified_points(obj, maxh=0.25):
    mesh = px.OCCGeometry(obj).GenerateMesh(maxh=maxh)
    points = mesh.Points()
    return [(points[a].p, points[b].p) for a, b, *_ in mesh.GetIdentifications()]


def test_transformations():
    assert xy(px.Translation(1, 2)((0.5, 0))) == (1.5, 2)
    assert xy(px.Rotation(90)((1, 0))) == pytest.approx((0, 1), abs=1e-15)
    assert xy(px.Rotation(90, center=(1, 1))((2, 1))) == pytest.approx((1, 2), abs=1e-15)
    t = px._planex.Transformation(((0, -1), (1, 0)), (3, 4))
    assert t.matrix == ((0, -1), (1, 0)) and t.vector == (3, 4)
    assert xy(t.Inverse()(t((0.3, 0.7)))) == pytest.approx((0.3, 0.7), abs=1e-15)
    assert xy((px.Translation(1, 0) * px.Rotation(90))((1, 0))) == pytest.approx((1, 1), abs=1e-15)
    assert repr(px.Translation(1, 0)).startswith("Transformation(matrix=((1.0, 0.0), (0.0, 1.0)), vector=(1.0, 0.0)")


def test_the_map_between_two_sides():
    a, b = px.Line((0, 0), (0, 1)), px.Line((2, 0), (2, 1))
    t = px.GetTransformation(a, b)
    assert t.matrix == ((1, 0), (0, 1)) and t.vector == (2, 0)
    assert px.GetTransformation(a, px.Line((2, 1), (2, 0))).vector == (2, 0)
    back = px.GetTransformation(a, b, reverse=True)
    assert sum(back.matrix, ()) == pytest.approx((-1, 0, 0, -1), abs=1e-15) and xy(back((0, 0))) == pytest.approx((2, 1), abs=1e-15)
    turned = px.GetTransformation(px.Line((1, 0), (2, 0)), px.Line((0, 1), (0, 2)))
    assert sum(turned.matrix, ()) == pytest.approx((0, -1, 1, 0), abs=1e-15) and turned.vector == pytest.approx((0, 0), abs=1e-15)


def test_arcs_map_by_a_similarity_and_an_l_shape_by_an_affine_map():
    arc = px.Arc((1, 0), (0, 1), (0, 0))
    assert px.GetTransformation(arc, arc.Translated((5, 1))).vector == pytest.approx((5, 1), abs=1e-15)
    corner = [px.Line((0, 0), (1, 0)), px.Line((1, 0), (1, 1))]
    stretched = [px.Line((0, 0), (2, 0)), px.Line((2, 0), (2, 1))]
    t = px.GetTransformation(corner, stretched)
    assert sum(t.matrix, ()) == pytest.approx((2, 0, 0, 1), abs=1e-14) and t.vector == pytest.approx((0, 0), abs=1e-14)


def test_sides_that_do_not_match_say_so():
    with pytest.raises(ValueError, match="2 vertices.*3"):
        px.GetTransformation(px.Line((0, 0), (1, 0)), [px.Line((0, 1), (1, 1)), px.Line((1, 1), (2, 1))])
    with pytest.raises(ValueError, match="no map takes"):
        px.GetTransformation([px.Line((0, 0), (1, 0)), px.Line((1, 0), (3, 0))], [px.Line((0, 1), (1.5, 1)), px.Line((1.5, 1), (3, 1))])
    with pytest.raises(ValueError, match="connected chain"):
        px.GetTransformation([px.Line((0, 0), (1, 0)), px.Line((0, 2), (1, 2))], [px.Line((0, 1), (1, 1)), px.Line((1, 1), (2, 1))])
    with pytest.raises(ValueError, match="spline"):
        px.GetTransformation(px.Spline([(0, 0), (1, 1), (2, 0)]), px.Spline([(0, 1), (1, 2), (2, 1)]))


@pytest.mark.parametrize("kind", [px.Face, px.Shape])
def test_periodic_sides_mesh_as_pairs(kind):
    obj = plate() if kind is px.Face else px.Shape(plate())
    obj.Identify("left", "right")
    assert obj.identifications == {"left-right": ("left", "right", px.Auto, IdentificationType.PERIODIC)}
    pairs = identified_points(obj)
    assert len(pairs) >= 5
    for p, q in pairs:
        assert (q[0] - p[0], q[1] - p[1]) == pytest.approx((2, 0), abs=1e-12)


def test_an_explicit_map_must_pair_every_piece():
    f = plate()
    f.Identify("left", "right", px.Translation(2, 0), name="periodic", type=IdentificationType.CLOSEEDGES)
    assert f.identifications["periodic"][2].vector == (2, 0) and f.identifications["periodic"][3] == IdentificationType.CLOSEEDGES
    f.Identify("left", "right", px.Translation(3, 0), name="periodic")
    with pytest.raises(RuntimeError, match="identification 'periodic': 0 pairs"):
        f.ToOCCShape()
    f.Identify("left", "right", (((1, 0), (0, 1)), (2, 0)), name="periodic")
    assert len(identified_points(f)) >= 5


def test_motions_move_the_stored_map():
    f = plate()
    f.Identify("left", "right", px.Translation(2, 0))
    f.Rotate((0, 0), 90)
    assert f.identifications["left-right"][2].vector == pytest.approx((0, 2), abs=1e-15)
    f.Scale((0, 0), 2)
    assert f.identifications["left-right"][2].vector == pytest.approx((0, 4), abs=1e-15)
    for p, q in identified_points(f, maxh=0.5):
        assert (q[0] - p[0], q[1] - p[1]) == pytest.approx((0, 4), abs=1e-12)
    g = plate()
    g.Identify("left", "right")
    g.Mirror((0, 0), (0, 1))
    assert g.identifications["left-right"][2] == px.Auto
    for p, q in identified_points(g):
        assert (q[0] - p[0], q[1] - p[1]) == pytest.approx((-2, 0), abs=1e-12)


def test_booleans_splits_and_copies_keep_them():
    f = plate()
    f.Identify("left", "right")
    hole = px.QuickSketch().Circle((1, 0.5), 0.2).Face()
    s = f - hole
    assert list(s.identifications) == ["left-right"] and len(identified_points(s)) >= 5
    assert list((hole + f).identifications) == ["left-right"]
    assert list(f.Copy().identifications) == ["left-right"] and list(px.Shape(f).Face().identifications) == ["left-right"]
    pieces = f.Split(px.Line((1, -1), (1, 2)))
    assert all(list(p.identifications) == ["left-right"] for p in pieces)
    assert list(px.Glue([f, hole]).identifications) == ["left-right"]


def test_remove_and_errors():
    f = plate()
    f.Identify("left", "right")
    f.RemoveIdentification("left-right")
    assert f.identifications == {}
    with pytest.raises(KeyError):
        f.RemoveIdentification("left-right")
    with pytest.raises(ValueError, match="no edge is named 'west'"):
        f.Identify("west", "right")
    for e in f.edges:
        if e.name in ("top", "bottom"):
            e.name = "rest"
    with pytest.raises(ValueError, match="connected chain"):
        f.Identify("left", "rest")
    with pytest.raises(TypeError, match="Copy"):
        px.Shape(f).faces[0].Identify("left", "right")
