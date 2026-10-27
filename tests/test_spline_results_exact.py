# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""a spline's tangent, Hausdorff distance and fitted approximation follow its exact curve"""
import math

import pytest

import planex as px


def ellipse():
    w = math.cos(math.radians(45))
    return px.BSpline([(5, 0), (5, 3), (0, 3), (-5, 3), (-5, 0), (-5, -3), (0, -3), (5, -3), (5, 0)],
                      [0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4], 2, weights=[1, w, 1, w, 1, w, 1, w, 1])


def test_a_spline_s_tangent_is_the_exact_curve_s():
    edge = ellipse().edges[0].edge
    for s in [0.1 * k for k in range(1, 63)]:
        t = edge.Tangent((5 * math.cos(s), 3 * math.sin(s)))
        exact = (-5 * math.sin(s), 3 * math.cos(s))
        n = math.hypot(*exact)
        assert t.x * exact[1] / n - t.y * exact[0] / n == pytest.approx(0, abs=1e-12)
        assert t.x * exact[0] + t.y * exact[1] > 0


def test_a_spline_s_tangent_off_the_curve_is_the_nearest_point_s():
    edge = ellipse().edges[0].edge
    t = edge.Tangent((6, 0))
    assert (t.x, t.y) == pytest.approx((0, 1), abs=1e-12)


def test_spline_takes_tangents_none():
    a = px.Spline([(0, 0), (1, 1), (2, 0)], tangents=None)
    b = px.Spline([(0, 0), (1, 1), (2, 0)])
    assert a.Hausdorff(b) == pytest.approx(0, abs=1e-14)


def test_hausdorff_of_a_spline_and_its_copy_is_zero():
    stations = [(0, 30), (10, 30), (25, 22), (30, 16), (50, 12), (80, 12)]
    smooth = px.Spline(stations, tangents={0: (1, 0), 5: (1, 0)})
    longer = px.Spline(stations, tangents={0: (10, 0), 5: (10, 0)})
    assert smooth.Hausdorff(smooth.Copy()) < 1e-12
    assert smooth.Hausdorff(longer) < 1e-12


@pytest.mark.occ
def test_a_fitted_spline_s_approximation_follows_its_curve():
    # a circle's polygon with every second vertex pushed out: the fit smooths it, the input lies 5e-4 off the curve
    pts = [((1 + 5e-4 * (k % 2)) * math.cos(2 * math.pi * k / 200), (1 + 5e-4 * (k % 2)) * math.sin(2 * math.pi * k / 200))
           for k in range(200)]
    face = px.Face([px.FromPolyline(pts, closed=True)])
    fitted = face.Simplified(remove_edges_smaller=0, fit_splines=dict(max_edge_length=0.1, tol=1e-3, ignore_edges_smaller=1e-4))
    splines = [e for e in fitted.edges if isinstance(e.edge, px.SplineEdge)]
    assert splines
    worst = 0.0
    for e in splines:
        for k in range(101):
            p = e.PointAt(k / 100)
            q = e.ClosestPoint((p.x, p.y))
            worst = max(worst, math.hypot(p.x - q[0], p.y - q[1]))
    assert worst < 1e-5
