# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import area, line, point


def polygon(points):
    p = px.Wire()
    for a, b in zip(points, points[1:] + points[:1]):
        p.Add(line(a, b))
    return p


def sweep(arc):
    a = (arc.start.x - arc.center.x, arc.start.y - arc.center.y)
    b = (arc.end.x - arc.center.x, arc.end.y - arc.center.y)
    c = a[0] * b[1] - a[1] * b[0]
    d = a[0] * b[0] + a[1] * b[1]
    s = math.atan2(c if arc.ccw else -c, d)
    return s + 2 * math.pi if s < 0 else s


def circle_points(n, r=1.0, start=0.0, end=2 * math.pi, center=(0.0, 0.0)):
    return [(center[0] + r * math.cos(start + (end - start) * k / n), center[1] + r * math.sin(start + (end - start) * k / n))
            for k in range(n + 1)]


def test_tessellated_circle_becomes_two_half_arcs():
    p = polygon(circle_points(400)[:-1])
    p.FitArcs(tol=1e-6, max_edge_length=0.1)
    assert p.lines == []
    assert len(p.arcs) == 2
    for a in p.arcs:
        assert point(a.center) == pytest.approx((0, 0), abs=1e-9)
        assert a.radius == pytest.approx(1, abs=1e-9)
        assert sweep(a) == pytest.approx(math.pi)
    assert area(p.ToOCCFace()) == pytest.approx(math.pi, rel=1e-9)


def test_a_ring_with_an_odd_segment_count_becomes_three_arcs():
    p = polygon(circle_points(69)[:-1])
    p.FitArcs(tol=1e-6, max_edge_length=0.2)
    assert p.lines == []
    assert len(p.arcs) == 3
    assert all(a.radius == pytest.approx(1, abs=1e-9) for a in p.arcs)
    assert sum(sweep(a) for a in p.arcs) == pytest.approx(2 * math.pi)
    assert max(sweep(a) for a in p.arcs) <= math.pi + 1e-9


def test_tessellated_corner_arc_is_tangent_to_the_straight_sides():
    corner = circle_points(10, 0.1, 0, math.pi / 2, (0.9, 0.9))
    p = polygon([(0, 0), (1, 0)] + corner + [(0, 1)])
    p.FitArcs(tol=1e-6, max_edge_length=0.1)
    assert len(p.lines) == 4
    [a] = p.arcs
    assert point(a.center) == pytest.approx((0.9, 0.9), abs=1e-9)
    assert a.radius == pytest.approx(0.1, abs=1e-9)
    for v in (a.start, a.end):
        t = a.Tangent(v)
        if abs(v.x - 1) < 1e-9:
            assert abs(t.x) < 1e-9
        else:
            assert abs(t.y) < 1e-9


def test_collinear_small_edges_become_one_line():
    p = polygon([(0.1 * k, 0.0) for k in range(10)] + [(1, 0), (1, 1), (0, 1)])
    p.FitArcs(tol=1e-9, max_edge_length=0.2)
    assert p.arcs == []
    assert len(p.lines) == 4


def test_arcs_are_limited_to_half_circles():
    p = polygon(circle_points(30, 1.0, 0, 1.5 * math.pi) + [(0, 0)])
    p.FitArcs(tol=1e-6, max_edge_length=0.2)
    assert len(p.lines) == 2
    assert len(p.arcs) == 2
    assert sorted(sweep(a) for a in p.arcs) == pytest.approx([math.pi / 2, math.pi])


def test_a_chain_that_is_no_arc_is_left_alone():
    p = polygon([(0, 0), (0.03, 0.004), (0.06, 0.002), (0.09, 0), (0.5, 0), (0.5, 0.5), (0, 0.5)])
    p.FitArcs(tol=1e-5, max_edge_length=0.1)
    assert len(p.lines) == 7
    assert p.arcs == []


def test_a_split_chord_does_not_break_the_arc_after_merging_collinear_edges():
    corner = circle_points(10, 0.1, 0, math.pi / 2, (0.9, 0.9))
    split = ((corner[4][0] + corner[5][0]) / 2, (corner[4][1] + corner[5][1]) / 2)
    corner.insert(5, split)
    p = polygon([(0, 0), (1, 0)] + corner + [(0, 1)])
    fitted = p.ToOCCFace(fit_arcs=dict(max_edge_length=0.1, tol=1e-6))
    assert len(fitted.edges) == 5
    assert len(p.ToOCCFace(merge_collinear_closer=0, fit_arcs=dict(max_edge_length=0.1, tol=1e-6)).edges) > 5


def test_export_fits_arcs_on_a_copy():
    p = polygon(circle_points(400)[:-1])
    face = p.ToOCCFace(fit_arcs=dict(max_edge_length=0.1, tol=1e-6))
    assert len(face.edges) == 2
    assert len(p.lines) == 400
    assert area(face) == pytest.approx(math.pi, rel=1e-9)
