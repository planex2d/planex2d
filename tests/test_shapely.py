# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import pathlib

import pytest

shapely = pytest.importorskip("shapely")

import planex as px  # noqa: E402
from helpers import polygon_area, square  # noqa: E402


def test_to_shapely_faces_with_holes_and_sampled_arcs():
    p = square(0, 0, 3) - square(1, 1, 1)
    g = p.ToShapely()
    assert g.geom_type == "Polygon"
    assert len(g.interiors) == 1
    assert g.area == pytest.approx(8)
    two = square(0, 0) + square(5, 0)
    assert two.ToShapely().geom_type == "MultiPolygon"
    disc = px.QuickSketch().Circle((0, 0), 1).Face()
    assert disc.ToShapely(tol=1e-4).area == pytest.approx(math.pi, abs=1e-3)
    assert disc.ToShapely(tol=1e-6).area == pytest.approx(math.pi, abs=1e-5)


def test_to_shapely_wires_and_compounds():
    w = px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Wire()
    line = w.ToShapely()
    assert line.geom_type == "LineString" and line.length == pytest.approx(2)
    c = px.Compound([square(0, 0, 2), px.Line((-1, 1), (3, 1))])
    g = c.ToShapely()
    assert g.geom_type == "GeometryCollection"
    assert sorted(part.geom_type for part in g.geoms) == ["LineString", "Polygon"]
    assert px.Move((0, 0)).Rectangle(1, 1).ToShapely().area == pytest.approx(1)


def test_from_shapely_polygons_and_lines():
    box = shapely.box(0, 0, 3, 3).difference(shapely.box(1, 1, 2, 2))
    p = px.FromShapely(box)
    assert isinstance(p, px.Shape) and len(p.faces) == 1
    assert p.mass == pytest.approx(8)
    assert len(p.edges) == 8
    multi = px.FromShapely(shapely.MultiPolygon([shapely.box(0, 0, 1, 1), shapely.box(5, 0, 6, 1)]))
    assert isinstance(multi, px.Shape) and len(multi.faces) == 2
    ws = px.FromShapely(shapely.LineString([(0, 0), (1, 0), (1, 1)]))
    assert isinstance(ws, list) and len(ws) == 1 and ws[0].mass == pytest.approx(2)
    c = px.FromShapely(shapely.GeometryCollection([shapely.box(0, 0, 2, 2), shapely.LineString([(-1, 1), (3, 1)])]))
    assert isinstance(c, px.Compound) and len(c) == 2
    assert len(c[0].faces[0].edges) == 4 and len(c[1].wires) == 1
    round_trip = px.FromShapely(p.ToShapely())
    assert round_trip.mass == pytest.approx(8)


def test_glued_faces_sharing_edges_give_a_geometry_collection():
    left, right = px.Move((0, 0)).Rectangle(1, 1).Face(), px.Move((1, 0)).Rectangle(1, 1).Face()
    glued = px.Glue([left, right]).ToShapely()
    assert glued.geom_type == "GeometryCollection" and glued.is_valid and len(glued.geoms) == 2
    assert sorted(g.area for g in glued.geoms) == pytest.approx([1, 1])
    assert px.Unify([left, right]).ToShapely().geom_type == "Polygon"
    apart = px.Glue([left, right.Translated((2, 0))]).ToShapely()
    assert apart.geom_type == "MultiPolygon" and apart.is_valid


def test_a_geometry_collection_comes_back_as_a_compound():
    """shapely stores every part of a collection with its own coordinates: side-sharing parts stay apart (glue them
    for a conforming mesh), and a glued Shape survives the round trip"""
    a, b = shapely.box(0, 0, 1, 1), shapely.box(1, 0, 2, 1)
    c = px.FromShapely(shapely.GeometryCollection([a, b, shapely.LineString([(0, 2), (2, 2)])]))
    assert isinstance(c, px.Compound) and len(c.parts) == 3
    assert [round(p.mass, 12) for p in c.parts[:2]] == [1, 1]
    glued = px.Glue([px.QuickSketch().Rectangle(1, 1).Face(), px.QuickSketch().Move((1, 0)).Rectangle(1, 1).Face()])
    back = px.FromShapely(px.ToShapely(glued))
    assert isinstance(back, px.Compound) and len(back.parts) == 2
    assert len(px.Glue(back.parts).faces) == 2
    # a MultiPolygon is a region: one Shape as before
    assert isinstance(px.FromShapely(shapely.MultiPolygon([a, shapely.box(3, 0, 4, 1)])), px.Shape)


def test_to_shapely_keeps_a_sliver_narrower_than_1e_9():
    corners = [(1.0, 0.0), (-0.22252093395631434, 0.9749279121818236), (0.6234898018587334, -0.7818314824680299)]
    a = px.QuickSketch().Move(tuple(corners[0])).Line(corners[1]).Line(corners[2]).Close().Face()
    b = a.Rotated(corners[2], math.degrees(1e-10))
    sliver = a - b
    assert sliver.mass == pytest.approx(1.9e-10, rel=1e-3)
    g = sliver.ToShapely()
    assert g.is_valid and g.area == pytest.approx(sliver.mass, rel=1e-6)


def test_a_sliver_thinner_than_tol_is_sampled_finer_with_a_warning():
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).parent / "fuzz"))
    import fuzzlib

    spec = (("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "nudged turn",
            ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0)
    a, b = fuzzlib.build_pair(spec)
    d = a - b
    with pytest.warns(px.PlanexWarning, match=r"^ToShapely: face 0 is thinner than tol=1e-06, its chords cross \(Self-intersection"):
        g = d.ToShapely(tol=1e-6)
    assert g.is_valid and all(p.is_valid for p in shapely.get_parts(g))
    assert g.area == pytest.approx(d.mass, rel=1e-6)
