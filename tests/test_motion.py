# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest
from netgen import occ

import planex as px
from helpers import ends, point, polygon_area, seg, segments, square


def corners(polygon):
    return {(round(l.start.x, 12), round(l.start.y, 12)) for l in polygon.lines}


def test_rotate_in_the_plane_changes_the_polygon_in_place_and_rotated_copies():
    p = square(0, 0)
    r = p.Rotated((0, 0), 90)
    assert corners(p) == {(0, 0), (1, 0), (1, 1), (0, 1)}
    assert corners(r) == {(0, 0), (0, 1), (-1, 1), (-1, 0)}
    assert p.Rotate((1, 1), 180) is not None
    assert corners(p) == {(2, 2), (1, 2), (1, 1), (2, 1)}
    assert polygon_area(p) == pytest.approx(1)


def test_translate_in_the_plane():
    p = square(0, 0)
    assert segments(p.Translated((2, 3))) == segments(square(2, 3))
    p.Translate((-1, 0))
    assert corners(p) == {(-1, 0), (0, 0), (0, 1), (-1, 1)}
    w = px.Move((0, 0)).Line((1, 0)).Wire().Translated((0, 1))
    assert ends(w.edges[0].edge) == ((0, 1), (1, 1))


def test_motion_in_3d_moves_only_the_plane():
    p = square(0, 0)
    moved = p.Translated((0, 0, 2))
    assert corners(moved) == corners(p)
    assert moved.Normal() == pytest.approx((0, 0, 1))
    assert all(v.p.z == pytest.approx(2) for v in moved.ToOCCFace().vertices)
    tilted = p.Rotated((0, 0, 0), (1, 0, 0), 90)
    assert tilted.Normal() == pytest.approx((0, -1, 0))
    assert corners(tilted) == corners(p)
    assert all(v.p.y == pytest.approx(0) for v in tilted.ToOCCFace().vertices)
    assert {round(v.p.z, 12) for v in tilted.ToOCCFace().vertices} == {0, 1}
    with pytest.raises(ValueError, match="different planes"):
        p + tilted


def test_workplane_motion_moves_the_cursor_and_the_wires():
    wp = px.Move((0, 0)).Rectangle(1, 1).Move((3, 0))
    wp.Translate((0, 2))
    assert wp.ToXYZ(wp.pen.position) == pytest.approx((3, 2, 0))
    assert wp.ToXYZ(wp.chain_start) == pytest.approx((3, 2, 0))
    assert corners(wp.Face()) == {(0, 0), (1, 0), (1, 1), (0, 1)} and wp.Face().center == pytest.approx((0.5, 2.5, 0))
    wp.Translate((0, 0, 5))
    assert wp.Normal() == pytest.approx((0, 0, 1))
    assert all(v.p.z == pytest.approx(5) for v in wp.ToOCCFace().vertices)
    assert all(v.p.z == pytest.approx(5) for v in wp.Wire().ToOCCWire().vertices)
    wp.Turn(90)
    assert point(wp.pen.direction) == pytest.approx((0, 1))


def test_normal_is_the_plane_normal():
    assert px.QuickSketch().Normal() == (0, 0, 1)
    assert square(0, 0).Wire().Normal() == (0, 0, 1)
    lifted = px.QuickSketch(occ.Axes((0, 0, 0), occ.X, occ.Y)).Rectangle(1, 1)
    assert lifted.Face().Normal() == pytest.approx((1, 0, 0))


def test_open_ends_closed_and_small_edges():
    w = px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Wire()
    assert w.OpenEnds() == [(0, False), (1, True)]
    assert not w.IsClosed()
    assert px.Move((0, 0)).Rectangle(1, 1).Wire().IsClosed()
    assert square(0, 0).IsClosed()
    p = px.FromPolyline([(0, 2), (0, 0), (2, 0), (2, 2), (2.001, 2)])
    assert p.SmallEdges(0.01) == [3]
    assert p.OpenEnds() == [(0, False), (3, True)]
    assert not p.IsClosed()
    assert px.Move((0, 0)).Rectangle(2, 0.001).Wire().SmallEdges(0.01) == [1, 3]


def test_properties_set_on_the_wire_or_the_edge_list_apply_to_every_edge():
    w = px.Move((0, 0)).Rectangle(1, 1).Wire()
    assert w.name is None
    w.name = "outer"
    w.maxh = 0.2
    assert [e.name for e in w.edges] == ["outer"] * 4
    assert w.maxh == 0.2
    w.edges[0].maxh = 0.1
    assert w.maxh is None
    assert w.name == "outer"
    p = w.Face()
    p.edges.col = (0, 1, 0)
    assert p.edges.col == [0, 1, 0, 1]
    p.edges.name = None
    assert p.edges.name is None
    assert all(e.name is None for e in p.edges)
    face = p.ToOCCFace()
    assert all(e.maxh in (pytest.approx(0.1), pytest.approx(0.2)) for e in face.edges)
    assert all(e.col == pytest.approx([0, 1, 0, 1]) for e in face.edges)


def test_plane_coordinates_round_trip():
    wp = px.QuickSketch(occ.Axes((1, 2, 3), occ.Y, occ.Z))
    assert wp.ToXYZ((0, 0)) == pytest.approx((1, 2, 3))
    assert wp.ToXYZ((1, 0)) == pytest.approx((1, 2, 4))
    assert wp.ToXYZ([(0, 1), (2, 2)]) == [pytest.approx((2, 2, 3)), pytest.approx((3, 2, 5))]
    assert wp.ToHV((1, 2, 4)) == pytest.approx((1, 0))
    assert wp.ToHV([(2, 7, 3)]) == [pytest.approx((0, 1))]
    p = square(0, 0)
    assert p.ToXYZ((0.5, 0.5)) == (0.5, 0.5, 0.0)
    assert p.Wire().ToHV((0.5, 0.5, 9.0)) == (0.5, 0.5)


def test_intersections_report_points_without_cutting():
    a = px.Move((0, 0)).Rectangle(2, 2).Wire()
    b = px.Move((1, 1)).Rectangle(2, 2).Wire()
    pts = sorted(point(v) for v in a.Intersections(b))
    assert pts == [pytest.approx((1, 2)), pytest.approx((2, 1))]
    assert len(a.edges) == 4 and len(b.edges) == 4
    c = px.Move((1, 0)).Rectangle(1, 1).Wire()
    assert sorted(point(v) for v in a.Intersections(c)) == [(1, 0), (2, 0), (2, 1)]
    assert square(0, 0, 2).Intersections(px.Line((1, -1), (1, 3))) == [px.Vertex((1, 0)), px.Vertex((1, 2))]
    assert a.Intersections(px.Move((5, 5)).Rectangle(1, 1).Wire()) == []
    with pytest.raises(ValueError, match="different planes"):
        a.Intersections(px.Line((0, 0), (1, 1), plane=occ.Axes((0, 0, 1), occ.Z, occ.X)))


def test_translate_takes_a_vector_or_its_parts():
    f = px.Move((0, 0)).Rectangle(1, 1).Face()
    assert f.Translated((2, 3)).bounding_box == f.Translated(2, 3).bounding_box == ((2, 3), (3, 4))
    assert f.Translated((0, 0, 1)).ToOCCFace().center.z == pytest.approx(f.Translated(0, 0, 1).ToOCCFace().center.z) == pytest.approx(1)
    assert repr(px.Translation(2, 3)) == repr(px.Translation((2, 3)))

