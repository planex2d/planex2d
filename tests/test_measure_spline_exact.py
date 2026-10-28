# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""measurements of a spline go to its exact curve: a solved contact measures 0 to rounding, not spline_tol"""
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def cam():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(15, 12), (30, 6), (45, 16), (60, 10)], id="cam")
    sk.Constrain("cam.p4", px.Fix)
    sk.Constrain("cam.p2", px.H, px.Distance, 6, id="dip")
    sk.Constrain(px.H, "cam.t0", px.Angle, 40, id="rise")
    sk.Circle((30, 14.5), 4, id="roller")
    sk.Constrain("roller", px.Radius, 4)
    sk.Constrain("roller.center", px.V, px.Distance, 30, id="x_roller")
    sk.Constrain("roller", "cam", px.Tangent)
    sk.Move((12, 30)).Line(0, -15, px.Vertical, id="probe")
    sk.Constrain("probe.start", px.H, px.Distance, 30)
    sk.Constrain("probe.end", "cam", px.Coincident)
    sk.Constrain("probe", px.V, px.Distance, 12)
    return sk


def test_a_solved_contact_with_a_spline_measures_zero():
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk = cam()
        sk.Update({"dip": 11, "rise": 15})
    assert sk.Measure("roller", to="cam")["distance"] == pytest.approx(0, abs=1e-10)
    assert sk.Measure("probe.end", to="cam")["distance"] == pytest.approx(0, abs=1e-10)


def test_a_point_on_the_exact_curve_is_on_the_spline():
    w = px.Spline([(0, 0), (10, 5), (20, -3), (30, 4)])
    s = w.edges[0].edge
    b = s.bspline
    poles, knots = b["poles"], b["knots"]
    # the curve's point at the middle of its range, by de Boor (cubic, not rational)
    u = 0.5 * (knots[3] + knots[len(poles)])
    k = max(i for i in range(3, len(poles)) if knots[i] <= u)
    d = [list(poles[j]) for j in range(k - 3, k + 1)]
    for r in range(1, 4):
        for j in range(3, r - 1, -1):
            i = k - 3 + j
            a = (u - knots[i]) / (knots[i + 4 - r] - knots[i])
            d[j] = [(1 - a) * d[j - 1][c] + a * d[j][c] for c in (0, 1)]
    p = tuple(d[3])
    assert w.Distance(p) == pytest.approx(0, abs=1e-12)
    assert w.ClosestPoint(p) == pytest.approx(p, abs=1e-12)
