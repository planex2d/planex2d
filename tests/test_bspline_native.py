# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""px.BSpline without OCC: its curve against OCC's own b-spline of the same data (px._planex._OccBSpline) while OCC is
at hand, and its checks."""
import math
import random

import pytest

import planex as px


def at(knots, poles, p, u, weights=None):
    """de Boor, rational with weights"""
    n = len(poles)
    k = max(i for i in range(p, n) if knots[i] <= u) if u < knots[n] else n - 1
    w = weights or [1.0] * n
    d = [(poles[j][0] * w[j], poles[j][1] * w[j], w[j]) for j in range(k - p, k + 1)]
    for r in range(1, p + 1):
        for j in range(p, r - 1, -1):
            i = k - p + j
            den = knots[i + p - r + 1] - knots[i]
            a = (u - knots[i]) / den if den else 0.0
            d[j] = tuple((1 - a) * d[j - 1][c] + a * d[j][c] for c in range(3))
    return d[p][0] / d[p][2], d[p][1] / d[p][2]


def off(mine, occ, p):
    """the largest distance of the two curves at the same parameters"""
    lo, hi = mine["knots"][p], mine["knots"][len(mine["poles"])]
    assert (lo, hi) == pytest.approx((occ["knots"][p], occ["knots"][len(occ["poles"])]), abs=1e-15)
    us = [lo + (hi - lo) * k / 64 for k in range(65)]
    return max(math.dist(at(mine["knots"], mine["poles"], p, u, mine["weights"]),
                         at(occ["knots"], occ["poles"], p, u, occ["weights"] or None)) for u in us)


CASES = [(3, 5, [1, 1, 1, 1, 1, 1], False), (2, 4, [1, 1, 1, 1, 1], False), (3, 6, [2, 1, 2, 1, 2], False),
         (2, 5, [1, 2, 1, 1, 1], False), (3, 5, [1, 1, 1, 1, 1, 1], True), (2, 6, [2, 2, 2, 2], True)]


@pytest.mark.parametrize("p, n, mults, rational", CASES)
def test_a_periodic_bspline_is_occs_curve(p, n, mults, rational):
    rng = random.Random(n * 10 + p)
    knots = [float(u) * 0.7 for u, m in enumerate(mults) for _ in range(m)]
    poles = [(math.cos(2 * math.pi * i / n) * (1 + 0.3 * rng.random()), math.sin(2 * math.pi * i / n) * (1 + 0.3 * rng.random()))
             for i in range(n)]
    weights = [0.5 + rng.random() for _ in range(n)] if rational else None
    w = px.BSpline(poles, knots, p, weights=weights, periodic=True)
    assert len(w.edges) == 1 and w.IsClosed()
    mine = w.edges[0].edge.bspline
    mine["weights"] = mine["weights"] or None
    assert off(mine, px._planex._OccBSpline(poles, knots, p, weights or [], True), p) < 1e-13


def test_an_open_bspline_keeps_its_data():
    poles, knots = [(0, 0), (1, 2), (3, 2), (4, 0), (5, 1)], [0, 0, 0, 0, 0.4, 1, 1, 1, 1]
    b = px.BSpline(poles, knots, 3).edges[0].edge.bspline
    occ = px._planex._OccBSpline(poles, knots, 3, [], False)
    assert b["knots"] == occ["knots"] == knots
    assert [tuple(q) for q in b["poles"]] == [tuple(q) for q in occ["poles"]] == [tuple(map(float, q)) for q in poles]


@pytest.mark.parametrize("args, message", [
    (([(0, 0), (1, 1), (2, 0)], [0, 0, 1, 0.5, 1, 1], 2), "the knots decrease at knot 3"),
    (([(0, 0), (1, 1), (2, 0), (3, 1), (4, 0)], [0, 0, 0, 0.5, 0.5, 0.5, 1, 1, 1], 2), "knot 0.500000 repeats 3 times"),
    (([(0, 0), (1, 1), (2, 0)], [0, 0, 0, 0.5, 1, 1, 1], 2), "3 poles of degree 2 need 6 knots, there are 7"),
    (([(0, 0), (1, 1), (2, 0)], [0, 0, 0, 1, 1, 1], 2, [1, 0, 1]), "weight 1 is not positive"),
])
def test_bad_data_names_what_is_wrong(args, message):
    with pytest.raises(ValueError, match="^invalid b-spline: " + message):
        px.BSpline(*args)


def test_a_bad_periodic_bspline_names_what_is_wrong():
    square = [(1, 0), (0, 1), (-1, 0), (0, -1)]
    with pytest.raises(ValueError, match="first and last knot repeat as often"):
        px.BSpline(square, [0, 0, 1, 2, 3, 4], 2, periodic=True)
    with pytest.raises(ValueError, match="4 poles of a periodic b-spline need 4 knots before the last, there are 3"):
        px.BSpline(square, [0, 1, 2, 3], 2, periodic=True)
