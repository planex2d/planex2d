# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import arc, ends, line, point

S = math.sqrt(0.5)


def test_vertex_compares_exactly():
    assert px.Vertex((1, 2)) == px.Vertex((1, 2))
    assert not px.Vertex((1, 2)) == px.Vertex((1, 2 + 1e-15))


def test_config_can_be_changed():
    old = px.config.k
    px.config.k = 20.0
    assert px.config.k == 20.0
    px.config.k = old
    assert px.config.k == old


def test_line_properties():
    l = line((0, 0), (2, 0))
    assert ends(l) == ((0, 0), (2, 0))
    assert point(l.MidPoint()) == (1, 0)
    assert point(l.Tangent(px.Vertex((1, 0)))) == (1, 0)
    assert point(l.Tangent(px.Vertex((1, 1)))) == (0, 0)
    assert not l.from_spline


def test_arc_properties():
    a = arc((1, 0), (0, 1), (0, 0))
    assert a.radius == 1
    assert a.ccw
    assert point(a.MidPoint()) == pytest.approx((S, S))
    assert point(a.Tangent(px.Vertex((1, 0)))) == pytest.approx((0, 1))
    assert point(a.Tangent(px.Vertex((-1, 0)))) == (0, 0)


def test_clockwise_arc_goes_the_other_way():
    a = arc((1, 0), (0, 1), (0, 0), ccw=False)
    assert not a.ccw
    assert point(a.MidPoint()) == pytest.approx((-S, -S))
    assert point(a.Tangent(px.Vertex((1, 0)))) == pytest.approx((0, -1))


def test_arc_accepts_radii_that_differ_by_rounding_and_keeps_the_center():
    c, r = (3.7, -1.3), 2.5
    start = (c[0] + r * math.cos(0.1), c[1] + r * math.sin(0.1))
    end = (c[0] + r * math.cos(2.0), c[1] + r * math.sin(2.0))
    a = px.Arc(start, end, c).arcs[0]
    assert point(a.center) == c
    assert ends(a) == (start, end)


def test_arc_with_a_larger_tol_moves_the_center_onto_the_bisector():
    a = px.Arc((1, 0), (0, 1.001), (0, 0), tol=0.01).arcs[0]
    nx, ny = -1.001, -1.0
    k = (0.5 * nx + 0.5005 * ny) / (nx * nx + ny * ny)
    assert point(a.center) == pytest.approx((0.5 - k * nx, 0.5005 - k * ny), abs=1e-14)
    assert point(a.center) != (0, 0)
    assert math.dist(point(a.start), point(a.center)) == pytest.approx(math.dist(point(a.end), point(a.center)), rel=1e-14)


def test_arc_beyond_tol_raises_and_names_both_radii():
    with pytest.raises(ValueError, match=r"the start is 1 and the end 2 from the center"):
        px.Arc((1, 0), (0, 2), (0, 0))
    with pytest.raises(ValueError, match=r"the start is 1 and the end 2 from the center.* tol = 0\.5"):
        px.Arc((1, 0), (0, 2), (0, 0), tol=0.5)
    assert px.Arc((1, 0), (0, 2), (0, 0), tol=1.0, id="wide").id == "wide"


@pytest.mark.parametrize("tol", [-1.0, float("nan"), float("inf")])
def test_arc_tol_must_be_at_least_zero_and_finite(tol):
    with pytest.raises(ValueError, match="tol"):
        px.Arc((1, 0), (0, 1), (0, 0), tol=tol)


def test_arc_edge_checks_its_radii_as_arc():
    c, r = (3.7, -1.3), 2.5
    start = (c[0] + r * math.cos(0.1), c[1] + r * math.sin(0.1))
    end = (c[0] + r * math.cos(2.0), c[1] + r * math.sin(2.0))
    assert point(px.ArcEdge(start, end, c).center) == c
    with pytest.raises(ValueError, match=r"^ArcEdge: the start is 1 and the end 2 from the center, they differ by more "
                                         r"than tol = 2e-14$"):
        px.ArcEdge((1, 0), (0, 2), (0, 0))
    moved = px.ArcEdge((1, 0), (0, 1.001), (0, 0), tol=0.01)
    assert point(moved.center) != (0, 0)
    assert math.dist(point(moved.start), point(moved.center)) == pytest.approx(math.dist(point(moved.end), point(moved.center)), rel=1e-14)
    for tol in (-1.0, float("nan"), float("inf")):
        with pytest.raises(ValueError, match=r"^ArcEdge\(tol\): tol must be at least 0 and finite$"):
            px.ArcEdge((1, 0), (0, 1), (0, 0), tol=tol)


def test_rotated_returns_a_rotated_copy():
    p = px.Wire()
    p.Add(line((1, 0), (2, 0)))
    r = p.Rotated((0, 0), 90).lines[0]
    assert ends(p.lines[0]) == ((1, 0), (2, 0))
    assert point(r.start) == pytest.approx((0, 1))
    assert point(r.end) == pytest.approx((0, 2))
    q = px.Wire()
    q.Add(arc((2, 0), (0, 2), (1, 1)))
    a = q.Rotate((1, 1), 180).arcs[0]
    assert point(a.start) == pytest.approx((0, 2))
    assert point(a.center) == pytest.approx((1, 1))


def test_line_offset_is_a_stadium():
    p = line((0, 0), (4, 0)).Offset(0.5)
    assert len(p.lines) == 2
    assert len(p.arcs) == 2
    assert {ends(l) for l in p.lines} == {((0, -0.5), (4, -0.5)), ((4, 0.5), (0, 0.5))}
    assert {point(a.center) for a in p.arcs} == {(0, 0), (4, 0)}
    assert all(a.radius == 0.5 for a in p.arcs)


def test_arc_offset_is_an_annular_sector_with_round_caps():
    p = arc((2, 0), (0, 2), (0, 0)).Offset(0.5)
    assert len(p.lines) == 0
    assert sorted(a.radius for a in p.arcs) == pytest.approx([0.5, 0.5, 1.5, 2.5])


def test_offset_needs_a_positive_distance():
    with pytest.raises(ValueError):
        line((0, 0), (1, 0)).Offset(0)
    with pytest.raises(ValueError):
        arc((1, 0), (0, 1), (0, 0)).Offset(-1)


def test_arc_offset_beyond_the_radius_covers_the_centre():
    band = arc((1, 0), (0, 1), (0, 0)).Offset(1)
    assert len(band.faces) == 1 and len(band.faces[0].inner) == 0 and band.Contains((0.1, 0.1))
