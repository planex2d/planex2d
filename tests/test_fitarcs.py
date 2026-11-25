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
    p.FitArcs(tol=1e-4, max_edge_length=0.1)
    assert p.lines == []
    assert len(p.arcs) == 2
    for a in p.arcs:
        assert point(a.center) == pytest.approx((0, 0), abs=1e-9)
        assert a.radius == pytest.approx(1, abs=1e-9)
        assert sweep(a) == pytest.approx(math.pi)
    assert area(p.ToOCCFace()) == pytest.approx(math.pi, rel=1e-9)


def test_a_ring_with_an_odd_segment_count_becomes_three_arcs():
    p = polygon(circle_points(69)[:-1])
    p.FitArcs(tol=2e-3, max_edge_length=0.2)
    assert p.lines == []
    assert len(p.arcs) == 3
    assert all(a.radius == pytest.approx(1, abs=1e-9) for a in p.arcs)
    assert sum(sweep(a) for a in p.arcs) == pytest.approx(2 * math.pi)
    assert max(sweep(a) for a in p.arcs) <= math.pi + 1e-9


def test_tessellated_corner_arc_is_tangent_to_the_straight_sides():
    corner = circle_points(10, 0.1, 0, math.pi / 2, (0.9, 0.9))
    p = polygon([(0, 0), (1, 0)] + corner + [(0, 1)])
    p.FitArcs(tol=1e-3, max_edge_length=0.1)
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
    p.FitArcs(tol=5e-3, max_edge_length=0.2)
    assert len(p.lines) == 2
    assert len(p.arcs) == 2
    assert sorted(sweep(a) for a in p.arcs) == pytest.approx([math.pi / 2, math.pi])


def test_a_chain_that_is_no_arc_is_left_alone():
    p = polygon([(0, 0), (0.03, 0.004), (0.06, 0.002), (0.09, 0), (0.5, 0), (0.5, 0.5), (0, 0.5)])
    p.FitArcs(tol=1e-5, max_edge_length=0.1)
    assert len(p.lines) == 7
    assert p.arcs == []


def test_a_split_chord_does_not_break_the_arc_merged_or_not():
    corner = circle_points(10, 0.1, 0, math.pi / 2, (0.9, 0.9))
    split = ((corner[4][0] + corner[5][0]) / 2, (corner[4][1] + corner[5][1]) / 2)
    corner.insert(5, split)
    p = polygon([(0, 0), (1, 0)] + corner + [(0, 1)])
    # the split point lies inside the arc by the chord's sagitta, 3.1e-4: within tol it is a point of the arc
    fitted = p.ToOCCFace(fit_arcs=dict(max_edge_length=0.1, tol=1e-3))
    assert len(fitted.edges) == 5
    assert len(p.ToOCCFace(merge_collinear_closer=0, fit_arcs=dict(max_edge_length=0.1, tol=1e-3)).edges) == 5


def test_export_fits_arcs_on_a_copy():
    p = polygon(circle_points(400)[:-1])
    face = p.ToOCCFace(fit_arcs=dict(max_edge_length=0.1, tol=1e-4))
    assert len(face.edges) == 2
    assert len(p.lines) == 400
    assert area(face) == pytest.approx(math.pi, rel=1e-9)


@pytest.mark.parametrize("radius", [0.05, 1.0, 10.0])
def test_a_fine_tessellation_is_fitted_and_an_octagon_kept_at_any_scale(radius):
    pts = [(radius * math.cos(2 * math.pi * k / 64), radius * math.sin(2 * math.pi * k / 64)) for k in range(64)]
    gon = px.FromPolyline(pts, closed=True).FitArcs(tol=1e-2 * radius)
    assert len(gon.edges) < 8
    octagon = px.FromPolyline([(radius * math.cos(math.pi * k / 4), radius * math.sin(math.pi * k / 4)) for k in range(8)],
                              closed=True).FitArcs(tol=1e-2 * radius)
    assert len(octagon.edges) == 8 and all(isinstance(e.edge, px.LineEdge) for e in octagon.edges)
    absolute = px.FromPolyline(pts, closed=True).FitArcs(tol=1e-2 * radius, max_edge_length=1e-9)
    assert len(absolute.edges) == 64


def test_long_lines_turning_like_a_coarse_arc_stay_lines():
    # three chords of a circle of radius 56.3 on a long loop: short against 1/20 of the loop, turning 15 degrees, each
    # 0.47 off the circle in its middle, far beyond tol (a copper zone's outline, where an arc 3.1 mm off was fitted)
    side = circle_points(3, 56.3, -0.39, 0.39)
    p = polygon(side + [(-200.0, side[-1][1]), (-200.0, side[0][1])])
    before = area(p.ToOCCFace())
    p.FitArcs(tol=2e-2)
    assert p.arcs == []
    assert len(p.lines) == 6
    assert area(p.ToOCCFace()) == pytest.approx(before, rel=1e-12)


def test_a_tessellation_is_fitted_when_its_chords_lie_within_tol():
    # a 64-gon of radius 1: its chords lie 1.2e-3 inside the circle in their middle
    fitted = polygon(circle_points(64)[:-1]).FitArcs(tol=2e-3)
    assert fitted.lines == [] and len(fitted.arcs) == 2
    assert all(a.radius == pytest.approx(1, abs=1e-9) for a in fitted.arcs)
    kept = polygon(circle_points(64)[:-1]).FitArcs(tol=1e-3)
    assert kept.arcs == [] and len(kept.lines) == 64


@pytest.mark.parametrize("part", [1, 2, 4])
def test_a_whole_half_or_quarter_circle_is_fitted_alike(part):
    # 32 sides per turn of radius 1: the chords lie 4.8e-3 inside the circle in their middle
    pts = circle_points(32 // part, 1.0, 0.0, 2 * math.pi / part)
    w = px.FromPolyline(pts[:-1] if part == 1 else pts, closed=part == 1).FitArcs(tol=1e-2)
    assert w.lines == [] and w.arcs


@pytest.mark.parametrize("scale", [1e-3, 1.0, 1e3])
def test_the_default_tol_is_relative_to_the_size_of_what_is_fitted(scale):
    # a hole of radius 1 with 24 sides in a 100 x 80 plate: its chords lie 8.6e-3 inside the circle, the default tol is
    # 1e-4 of the plate's diagonal, 1.28e-2, in any units
    hole = [(50 * scale + x * scale, 40 * scale + y * scale) for x, y in circle_points(24)[:-1]]
    corners = [(0, 0), (100 * scale, 0), (100 * scale, 80 * scale), (0, 80 * scale)]
    plate = px.Shape([px.FromPolyline(corners, closed=True)]) - px.Shape([px.FromPolyline(hole, closed=True)])
    fitted = plate.FitArcs()
    assert [type(e.edge).__name__ for e in fitted.edges].count("ArcEdge") == 2
    assert [type(e.edge).__name__ for e in fitted.edges].count("LineEdge") == 4


def test_kicads_coarsest_circle_chains_and_a_square_does_not():
    # KiCad draws a circle with 8 segments at least (45 degree turns): fitted within tol at the default max_edge_angle
    # of 60 degrees, not at 30; a square's 90 degree corners never chain, however large tol
    octagon = circle_points(8)[:-1]
    assert polygon(octagon).FitArcs(tol=0.1).lines == []
    assert len(polygon(octagon).FitArcs(tol=0.1, max_edge_angle=30).lines) == 8
    assert len(polygon([(0, 0), (1, 0), (1, 1), (0, 1)]).FitArcs(tol=1.0).lines) == 4


def test_fitting_again_leaves_the_fitted_arcs_alone():
    # five chords turning 15 degrees at each vertex, 0.134, 0.305, 0.305, 0.305 and 0.134 long (as on a board's
    # copper, in mm): the three long ones lie within tol of an arc, the short ones on a tighter curve. Fitted again,
    # an arc that joined a chain with its neighbors would be fitted once more and move by up to tol again
    pts, heading = [(0.0, 0.0)], 0.0
    for length in (0.134, 0.305, 0.305, 0.305, 0.134):
        heading += math.radians(15)
        pts.append((pts[-1][0] + length * math.cos(heading), pts[-1][1] + length * math.sin(heading)))
    fit = dict(tol=0.01, ignore_edges_smaller=1e-6)
    once = px.FromPolyline(pts, closed=False).FitArcs(**fit)
    assert len(once.arcs) == 1 and len(once.lines) == 2
    twice = px.FromPolyline(pts, closed=False).FitArcs(**fit).FitArcs(**fit)
    assert len(twice.arcs) == 1 and len(twice.lines) == 2
    middle = ((pts[2][0] + pts[3][0]) / 2, (pts[2][1] + pts[3][1]) / 2)
    assert twice.Distance(middle) <= 0.01
