# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import ends, point, polygon_area, segments, square


def d_shape():
    # a half disc: a line and an arc
    return px.Move((0, -1)).Line((0, 1)).Arc((0, -1), tangent=(1, 0)).Face()


def occ_points(shape):
    return sorted({(round(v.p.x, 9) + 0.0, round(v.p.y, 9) + 0.0, round(v.p.z, 9) + 0.0) for v in shape.vertices})


def flat(box):
    return [c for corner in box for c in corner]


def test_mirror_in_the_plane_flips_arcs():
    p = d_shape()
    m = p.Mirrored((0, 0), (0, 1))
    a = m.arcs[0]
    assert point(a.center) == (0, 0)
    assert a.ccw != p.arcs[0].ccw
    assert a.MidPoint().x == pytest.approx(-1)
    assert flat(m.bounding_box) == pytest.approx([-1, -1, 0, 1])
    assert m.mass == pytest.approx(math.pi / 2)
    assert polygon_area(m) == pytest.approx(math.pi / 2)


def test_mirror_at_an_axis_through_the_origin_twice_is_exact():
    p = square(0.3, 0.7, 2.1) + square(1, 1)
    assert segments(p.Mirrored((0, 0), (0, 3)).Mirrored((0, 0), (0, -1))) == segments(p)
    assert segments(p.Mirrored((0, 0), (2, 0)).Mirrored((0, 0), (1, 0))) == segments(p)
    back = p.Mirrored((0.5, 0), (0, 1)).Mirrored((0.5, 0), (0, 1))
    assert sorted(c for s in segments(back) for q in s for c in q) == pytest.approx(
        sorted(c for s in segments(p) for q in s for c in q))


def test_mirror_at_a_slanted_line():
    w = px.Line((1, 0), (2, 0)).Mirror((0, 0), (1, 1))
    (s, e) = ends(w.edges[0].edge)
    assert s == pytest.approx((0, 1))
    assert e == pytest.approx((0, 2))


def test_mirror_and_scale_of_a_spline():
    pts = [(0, 0), (1, 1), (2, 0.5), (3, 2)]
    w = px.Spline(pts)
    m = w.Mirrored((0, 0), (0, 1))
    s = m.edges[0].edge
    assert point(s.start) == (0, 0)
    assert point(s.end) == pytest.approx((-3, 2))
    assert flat(s.bspline["poles"]) == pytest.approx(flat((-x, y) for x, y in w.edges[0].edge.bspline["poles"]))
    big = w.Scaled((0, 0), 2)
    assert point(big.edges[0].edge.end) == pytest.approx((6, 4))
    assert big.mass == pytest.approx(2 * w.mass)
    e = big.ToOCCWire().edges[0]
    assert (e.end.x, e.end.y) == pytest.approx((6, 4))


def test_mirror_in_3d_mirrors_the_points_in_space():
    p = d_shape()
    for point3, normal, mirror in [((0, 0, 1), (0, 0, 1), lambda x, y, z: (x, y, 2 - z)),
                                   ((0, 0, 0), (1, 0, 0), lambda x, y, z: (-x, y, z)),
                                   ((0, 0, 0), (1, 1, 0), lambda x, y, z: (-y, -x, z))]:
        m = p.Mirrored(point3, normal)
        expected = sorted({tuple(round(c, 9) + 0.0 for c in mirror(*q)) for q in occ_points(p.ToOCCFace())})
        assert occ_points(m.ToOCCFace()) == expected
        assert polygon_area(m) == pytest.approx(math.pi / 2)
    m = p.Mirrored((0, 0, 1), (0, 0, 1))
    assert m.Normal() == pytest.approx((0, 0, -1))
    assert m.ToXYZ((0.5, -0.25)) == pytest.approx((0.5, 0.25, 2))


def test_scale_in_the_plane():
    p = d_shape().Scale((1, 0), 2)
    assert flat(p.bounding_box) == pytest.approx([-1, -2, 1, 2])
    assert p.arcs[0].radius == pytest.approx(2)
    assert p.mass == pytest.approx(2 * math.pi)


def test_scale_in_3d_scales_around_the_point():
    p = square(0, 0)
    s = p.Scaled((0, 0, 1), 2)
    assert occ_points(s.ToOCCFace()) == [(0, 0, -1), (0, 2, -1), (2, 0, -1), (2, 2, -1)]
    assert s.mass == pytest.approx(4)


@pytest.mark.parametrize("factor", [0, -1, float("nan")])
def test_scale_needs_a_positive_factor(factor):
    p = square(0, 0)
    with pytest.raises(ValueError, match="positive"):
        p.Scale((0, 0), factor)
    with pytest.raises(ValueError, match="positive"):
        p.Scale((0, 0, 0), factor)
    assert p.bounding_box == ((0, 0), (1, 1))


def test_zero_mirror_directions_raise():
    with pytest.raises(ValueError):
        square(0, 0).Mirror((0, 0), (0, 0))
    with pytest.raises(ValueError):
        square(0, 0).Mirror((0, 0, 0), (0, 0, 0))


def test_scale_scales_maxh_and_minh_only_when_asked():
    w = px.Move((0, 0)).Line((1, 0), maxh=0.1, minh=0.01).Line((1, 1)).Wire()
    assert [(e.maxh, e.minh) for e in w.Scaled((0, 0), 2).edges] == [(0.1, 0.01), (None, None)]
    assert [e.maxh for e in w.Scaled((0, 0), 2, scale_maxh=True).edges] == pytest.approx([0.2, None])
    assert [e.minh for e in w.Scaled((0, 0), 2, scale_maxh=True).edges] == pytest.approx([0.01, None])
    assert [e.minh for e in w.Scaled((0, 0, 0), 3, scale_minh=True).edges] == pytest.approx([0.03, None])
    assert [(e.maxh, e.minh) for e in w.edges] == [(0.1, 0.01), (None, None)]
    p = square(0, 0)
    p.maxh, p.minh = 0.5, 0.05
    s = p.Scaled((0, 0), 2, scale_maxh=True, scale_minh=True)
    assert (s.maxh, s.minh) == pytest.approx((1.0, 0.1))
    assert (p.Scaled((0, 0), 2).maxh, p.maxh) == (0.5, 0.5)


def test_compound_and_edge_views():
    c = px.Compound([square(0, 0, 2), px.Line((-1, 1), (3, 1))])
    assert flat(c.Mirrored((0, 0), (0, 1)).bounding_box) == pytest.approx([-3, 0, 1, 2])
    assert flat(c.Scaled((0, 0), 0.5).bounding_box) == pytest.approx([-0.5, 0, 1.5, 1])
    e = square(0, 0).edges[0]
    assert e.Mirrored((5, 0), (0, 1)).bounding_box[0][0] == pytest.approx(10 - e.bounding_box[1][0])
    assert e.Scaled((0, 0), 3).mass == pytest.approx(3 * e.mass)
