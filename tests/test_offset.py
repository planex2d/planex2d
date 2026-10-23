# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import random

import pytest

import planex as px
from helpers import open_vertices, polygon_area, shortest_edge, stadium_area


def test_straight_trace():
    p = px.Move((0, 0)).Line(1).Line(1).Line(1).Wire().Offset(0.25)
    assert (len(p.lines), len(p.arcs)) == (2, 2)
    assert polygon_area(p) == pytest.approx(stadium_area(3, 0.25))


def test_trace_with_a_right_angle():
    d = 0.5
    p = px.Move((0, 0)).Line((4, 0)).Line((4, 3)).Wire().Offset(d)
    assert (len(p.lines), len(p.arcs)) == (4, 3)
    outer_join = math.pi * d * d / 4
    inner_corner = d * d
    assert polygon_area(p) == pytest.approx(stadium_area(7, d) + outer_join - inner_corner)


def test_smooth_trace_has_the_tube_area():
    d = 0.25
    p = px.Move((0, 0)).Line(2).Arc(1, 90).Line(2).Wire().Offset(d)
    assert polygon_area(p) == pytest.approx(stadium_area(4 + math.pi / 2, d))


def test_arc_offset_area():
    d, r = 0.5, 2
    p = px.Arc(px.Vertex((r, 0)), px.Vertex((0, r)), px.Vertex((0, 0))).Offset(d)
    assert polygon_area(p) == pytest.approx(math.pi / 2 * 2 * r * d + math.pi * d * d)


def test_circle_offset_is_a_ring():
    p = px.QuickSketch().Circle(0, 0, 2).Wire().Offset(0.5)
    face = p.ToOCCFace()
    assert len(face.faces) == 1
    assert len(face.wires) == 2
    assert polygon_area(p) == pytest.approx(math.pi * (2.5 ** 2 - 1.5 ** 2))


def test_offset_of_one_chain():
    p = px.Move((0, 0)).Line(1).Finish().Move((5, 5)).Line(2, id="b").Edge("b").Offset(0.1)
    assert polygon_area(p) == pytest.approx(stadium_area(2, 0.1))


@pytest.mark.parametrize("x", [0.5, 3.0, 3.3, 5.0, 7.123456])
def test_t_junction_of_two_traces(x):
    d = 0.2
    p = px.Move((0, 0)).Line((10, 0)).Wire().Offset(d) + px.Move((x, 4)).Line((x, 0)).Wire().Offset(d)
    assert shortest_edge(p) > 1e-6
    overlap = math.pi * d * d / 2 + 2 * d * d
    assert polygon_area(p) == pytest.approx(stadium_area(10, d) + stadium_area(4, d) - overlap)


@pytest.mark.parametrize("angle", range(5, 180, 10))
@pytest.mark.parametrize("rotation", [0, 33, 45])
def test_angled_t_junctions_export_cleanly(angle, rotation):
    r, t = math.radians(rotation), math.radians(angle + rotation)
    end = (5 * math.cos(r), 5 * math.sin(r))
    trunk = px.Move((0, 0)).Line((10 * math.cos(r), 10 * math.sin(r))).Wire().Offset(0.2)
    branch = px.Move((end[0] + 4 * math.cos(t), end[1] + 4 * math.sin(t))).Line(end).Wire().Offset(0.2)
    p = trunk + branch
    assert shortest_edge(p) > 1e-6
    assert polygon_area(p) > 0


def test_long_stroke_crossing_a_cap_at_a_shallow_angle():
    d = 0.099949
    short = px.Move((238.099092, 20.519136)).Line((238.299244, 20.519136)).Wire().Offset(d)
    long = px.Move((238.299244, 20.319492)).Line((368.099086, 20.319492)).Wire().Offset(d)
    assert open_vertices(short + long) == []


def test_touching_strokes_merge():
    n, d = 5, 0.5
    p = sum(px.Move((i, 0)).Line((i, 30)).Wire().Offset(d) for i in range(n))
    assert (len(p.lines), len(p.arcs)) == (2, 2 * n)
    assert polygon_area(p) == pytest.approx(n * stadium_area(30, d))


def test_zigzag_fill_has_no_internal_edges():
    p = px.Move((0, 0)).Line((0, 10)).Line((1, 10)).Line((1, 0)).Line((2, 0)).Line((2, 10)).Wire().Offset(0.5)
    internal = [l for l in p.lines if l.start.x == l.end.x and l.start.x in (0.5, 1.5)]
    assert internal == []
    assert polygon_area(p) == pytest.approx(31 + math.pi / 2)


def random_trace(rng):
    wp = px.Move((0, 0))
    for _ in range(40):
        wp.Turn(rng.choice([0, 30, 45, -45, 90, -90, 135]))
        if rng.random() < 0.3:
            wp.Arc(rng.uniform(0.5, 2), rng.choice([45, 90, -90, 180, -60]))
        else:
            wp.Line(rng.uniform(0.3, 2))
    return wp


@pytest.mark.parametrize("seed", range(20))
def test_random_traces_export_cleanly(seed):
    p = px.Unify([w.Offset(0.2) for w in random_trace(random.Random(seed)).Wires()])
    assert shortest_edge(p) > 1e-6
    assert polygon_area(p) > 0


def test_consecutive_strokes_with_slightly_different_directions_stay_closed():
    a = px.Move((65.11844, 61.36984)).Line((65.26079, 61.9011)).Wire().Offset(0.15)
    b = px.Move((65.26079, 61.9011)).Line((65.2802, 61.97354)).Wire().Offset(0.15)
    assert open_vertices(a + b) == []


def test_half_circles_whose_ends_miss_their_circle_offset_to_a_ring():
    """a section of a STEP rook: two half arcs about (0, 0) whose radius (from the start) and end disagree by 8e-13,
    more than rounding: the arcs are re-centred so both ends lie on them, and the band is a ring, not the disc"""
    r1, r2, y = 13.739999999999672, 13.740000000000494, -1.6826647020285238e-15
    w = px.Arc((r1, 0.0), (-r2, y), (0.0, 0.0), reverse=True, tol=1e-12)
    w.Add(px.Arc((-r2, y), (r1, 0.0), (0.0, 0.0), reverse=True, tol=1e-12))
    for e in w.edges:
        g = e.edge
        assert math.hypot(g.end.x - g.center.x, g.end.y - g.center.y) == pytest.approx(g.radius, abs=1e-13)
    r = (r1 + r2) / 2
    assert w.Offset(0.225).mass == pytest.approx(math.pi * ((r + 0.225) ** 2 - (r - 0.225) ** 2), abs=1e-9)


def test_an_arc_offset_as_wide_as_its_radius_covers_its_inside():
    half = px.Arc((1, 0), (-1, 0), (0, 0))
    assert half.Offset(1).mass == pytest.approx(3 * math.pi, rel=1e-12)
    lens = 2 * 4 * math.acos(0.5) - 2 * math.sqrt(3)
    assert half.Offset(2).mass == pytest.approx(9 * math.pi / 2 + 4 * math.pi - lens / 2, rel=1e-12)
    chain = px.Move((-2, 0)).Line(2).Arc(1, 90).Line(2).Wire().Offset(1.5)
    assert len(chain.faces) == 1 and len(chain.faces[0].inner) == 0


def test_face_offsets_add_or_take_away_the_band_of_the_boundary():
    plate = px.QuickSketch((-1, -1)).Rectangle(2, 2).Face()
    plate.name = "plate"
    for e in plate.edges:
        e.name = "bottom" if e.midpoint.y == -1 else "side"
    grown = plate.Offset(0.5, inplace=False)
    assert grown.mass == pytest.approx(4 + 8 * 0.5 + math.pi * 0.25, rel=1e-12)
    shrunk = plate.Offset(-0.5, inplace=False)
    assert shrunk.mass == pytest.approx(1, rel=1e-12)
    assert [f.name for f in grown.faces] == ["plate"] and [f.name for f in shrunk.faces] == ["plate"]
    assert {e.name for e in shrunk.edges if e.midpoint.y == pytest.approx(-0.5)} == {"bottom"}
    assert len(plate.Offset(-1.5, inplace=False).faces) == 0
    assert plate.Offset(0).mass == pytest.approx(4)


def test_a_hole_smaller_than_the_offset_closes():
    holed = px.QuickSketch((-2, -2)).Rectangle(4, 4).Face() - px.QuickSketch().Circle(0, 0, 0.5).Face()
    grown = holed.Offset(1)
    assert len(grown.faces) == 1 and len(grown.faces[0].inner) == 0
    assert grown.mass == pytest.approx(16 + 16 + math.pi, rel=1e-12)


def test_offset_of_glued_faces_asks_to_unify_first():
    a = px.QuickSketch((0, 0)).Rectangle(1, 1).Face()
    b = px.QuickSketch((1, 0)).Rectangle(1, 1).Face()
    b.name = "b"
    with pytest.raises(ValueError, match="Unify"):
        px.Glue([a, b]).Offset(0.1)
    assert px.Unify([a, b]).Offset(0.1).mass == pytest.approx(2 + 6 * 0.1 + math.pi * 0.01, rel=1e-12)


@pytest.mark.parametrize("d", [1.0, 1.5, 3.0])
@pytest.mark.parametrize("case", ["arc", "chain"])
def test_bands_wider_than_the_radius_are_netgens_offsets(case, d):
    occ = pytest.importorskip("netgen.occ")
    if case == "arc":
        source, wp = px.Arc((1, 0), (0, 1), (0, 0)), occ.WorkPlane().MoveTo(1, 0).Direction(0, 1).Arc(1, 90)
    else:
        source, wp = px.Move((-2, 0)).Line(2).Arc(1, 90).Line(2).Wire(), occ.WorkPlane().MoveTo(-2, 0).Line(2).Arc(1, 90).Line(2)
    band, theirs = source.Offset(d), wp.Finish().Offset(d).Face()
    ours = band.ToOCCShape()

    def area(s):
        return sum(f.mass for f in s.faces)

    assert band.mass == pytest.approx(theirs.mass, rel=1e-12)
    assert area(ours - theirs) == pytest.approx(0, abs=1e-12) and area(theirs - ours) == pytest.approx(0, abs=1e-12)


@pytest.mark.parametrize("d", [0.6, 0.7, 1.0, 3.0])
def test_a_spline_offset_past_its_smallest_curvature_radius_is_one_clean_face(d):
    shapely = pytest.importorskip("shapely")
    w = px.Spline([px.Vertex((0, 0)), px.Vertex((2, 2)), px.Vertex((4, 0)), px.Vertex((6, 2))])
    band = w.Offset(d)
    assert len(band.faces) == 1 and band.faces[0].outer.IsClosed()
    curve = shapely.LineString(w.edges[0].edge.Sample(1e-7)).buffer(d, quad_segs=2048)
    assert band.mass == pytest.approx(curve.area, rel=1e-5)
    assert px.ToShapely(band, 1e-6).symmetric_difference(curve).area < 1e-4


def test_offset_works_in_place_unless_asked_for_a_new_object():
    f = px.Move((0, 0)).Rectangle(2, 2).Face()
    f.id = "plate"
    assert f.Offset(0.5) is f and f.id == "plate"
    assert f.mass == pytest.approx(4 + 8 * 0.5 + math.pi * 0.25, rel=1e-12)
    s = px.Shape(px.Move((0, 0)).Rectangle(2, 2).Face())
    band = s.Offset(0.5, inplace=False)
    assert band is not s and s.mass == pytest.approx(4) and band.mass == pytest.approx(f.mass)
    with pytest.raises(ValueError, match="inplace=False"):
        px.Move((0, 0)).Rectangle(2, 2).Face().Offset(-1.5)

