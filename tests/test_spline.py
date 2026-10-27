# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import netgen.occ as occ
import pytest

import planex as px
from helpers import area, point, polygon_area, rect

POINTS = [(3, 2), (2, 1.5), (1, 2), (0, 1)]


def outline(lib):
    if lib is not px:
        return lib.WorkPlane().MoveTo(0, 0).LineTo(4, 0).LineTo(4, 1).Spline(POINTS).Close()
    # netgen's curve exactly: OCC's interpolation through the pen's point and POINTS, as one spline edge
    w = px.Line((0, 0), (4, 0))
    for e in (px.Line((4, 0), (4, 1)), px.Spline([(4, 1), *POINTS], style="clamped"), px.Line(POINTS[-1], (0, 0))):
        w.Add(e)
    return w


P = outline(px).Face()
P_AREA = area(outline(occ).Face())


def test_spline_is_one_edge():
    assert (len(P.lines), len(P.splines)) == (3, 1)
    s = P.splines[0]
    assert point(s.start) == (4, 1)
    assert point(s.end) == (0, 1)
    assert [point(v) for v in s.points] == [(4, 1)] + POINTS
    assert s.napprox > len(POINTS)


def test_spline_exports_as_the_interpolated_curve():
    assert polygon_area(P) == pytest.approx(P_AREA, rel=1e-12)
    assert len(outline(px).ToOCCWire().edges) == 4
    assert len(outline(px).ToOCCWire(approximation=True).edges) > 4


def test_spline_tangents():
    s = px.Move((0, 0)).Spline([(2, 1), (4, 0)], tangents={0: (1, 0)}).Wire().splines[0]
    assert point(s.Tangent(s.start)) == pytest.approx((1, 0))


def test_spline_can_start_away_from_the_position():
    s = px.Move((5, 5)).Spline([(0, 0), (1, 1), (2, 0)], start_from_localpos=False).Wire().splines[0]
    assert point(s.start) == (0, 0)


def test_spline_ending_at_the_start_closes_the_wire():
    wp = px.Move((0, 0)).Line((2, 0)).Spline([(1, 1), (0, 0)])
    assert len(wp.ToOCCWires().wires) == 1
    assert polygon_area(wp.Face()) > 0


PERIODIC = [(1, 0), (0, 1), (-1, 0), (0, -1)]


def test_periodic_spline_is_one_closed_edge_and_two_in_occ():
    p = px.QuickSketch().Spline(PERIODIC, periodic=True, start_from_localpos=False).Face()
    assert len(p.splines) == 1 and len(p.ToOCCFace().edges) == 2
    assert polygon_area(p) > 0


def test_periodic_spline_exports_the_periodic_curve():
    # the C2 periodic cubic at chord length parameters through four points on the unit circle encloses 61/20,
    # whichever point comes first (netgen's periodic interpolation depends on that)
    for k in range(4):
        p = px.QuickSketch().Spline(PERIODIC[k:] + PERIODIC[:k], periodic=True, start_from_localpos=False).Face()
        assert polygon_area(p) == pytest.approx(3.05, rel=1e-12)
        assert p.mass == pytest.approx(3.05, rel=1e-6)


def test_spline_rotate():
    s = P.Rotated((0, 0), 90).splines[0]
    assert point(s.start) == pytest.approx((-1, 4))


def test_rotated_copy_exports_a_rotated_curve_and_leaves_the_original():
    r = P.Rotated((0, 0), 30)
    assert polygon_area(r) == pytest.approx(P_AREA, rel=1e-9)
    assert polygon_area(r + P.Rotated((0, 0), 30)) == pytest.approx(P_AREA, rel=1e-9)
    assert polygon_area(P) == pytest.approx(P_AREA, rel=1e-12)


def test_spline_with_itself():
    assert len((P + P).splines) == 1
    assert polygon_area(P + P) == pytest.approx(P_AREA, rel=1e-12)
    assert len((P * P).splines) == 1
    assert polygon_area(P - P) == 0


R = rect(1.5, 0.5, 1, 3)


def test_spline_cut_by_a_rectangle():
    assert len((P * R).splines) == 1
    assert len((P - R).splines) == 2


def test_spline_inside_a_longer_piece_of_the_same_spline():
    q = P * R
    assert len((P + q).splines) == 3
    assert len((P - q).splines) == 2


def test_exported_spline_pieces_follow_the_original_curve():
    assert polygon_area(P * R) + polygon_area(P - R) == pytest.approx(P_AREA, rel=1e-6)
    assert polygon_area(P + P * R) == pytest.approx(P_AREA, rel=1e-6)


# a spline's offset is one spline per side, no longer arcs and lines from its approximation
def test_spline_offset_is_one_spline_per_side():
    trace = px.Move((0, 3)).Line(1).Spline([(2, 3.5), (3, 3)], tangents={0: (1, 0)}).Line(1).Wire().Offset(0.1)
    assert len(trace.splines) == 2
    assert not any(e.from_spline for e in trace.lines + trace.arcs)
    plain, fitted = trace.ToOCCFace(), trace.ToOCCFace(fit_splines=True)
    assert area(fitted) == pytest.approx(area(plain), rel=1e-4)


def test_remove_small_edges_keeps_the_offset_splines():
    trace = px.Move((0, 3)).Line(1).Spline([(2, 3.5), (3, 3)], tangents={0: (1, 0)}).Line(1).Wire().Offset(0.1)
    n = len(trace.splines)
    trace.RemoveSmallEdges(10)
    assert len(trace.splines) == n


def test_nearly_straight_splines_are_approximated_by_lines_not_far_centered_arcs():
    s = px.BSpline([(0, 0), (30, 1e-9), (60, -1e-9), (90, 0)], [0, 0, 0, 0, 1, 1, 1, 1], 3)
    parts = s.edges[0].edge.approximation
    assert all(isinstance(a, px.LineEdge) or a.radius < 1e6 for a in parts)
    # the tolerance follows the largest coordinate involved: a far arc center would swallow the small edge
    tiny = px.Line((90, 0), (90.004, 0))
    back = px.Move((90.004, 0)).Line((90.004, 10)).Line((0, 10)).Line((0, 0)).Wire()
    loop = px.Unify([s, tiny, back])
    assert isinstance(loop, px.Shape) and len(loop.wires) == 1
    assert loop.Wire().IsClosed() and len(loop.edges) == 5
    assert len(s.Intersections(tiny)) == 1


# the crossing of an axis-parallel line with an arc is exact at the arc's scale, whatever the line's length (the widths
# fall in different binades)
@pytest.mark.parametrize("half_width", [10, 1e3, 1e6, 3e6, 7.7e6, 1.3e8])
def test_split_points_found_against_a_much_longer_edge(half_width):
    # the cut locates the crossings with the tolerance of the long edge; the spline must accept them
    r = P * rect(-half_width, 1.8, 2 * half_width, 0.1)
    assert len(r.splines) == 4
    assert r.mass == pytest.approx((P * rect(-10, 1.8, 20, 0.1)).mass, rel=1e-12)
