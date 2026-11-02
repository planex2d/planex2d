# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px


def quarter(r):
    """a quarter circle of radius r as a parametric curve (a spline fitted to it)"""
    return px.Curve((f"{r}*cos(t)", f"{r}*sin(t)"), (0, 90))


def test_two_curves_have_a_smallest_and_a_largest_distance():
    inner, outer = quarter(5), quarter(7)
    # every point of one lies 2 from the other, on its radius
    assert inner.Distance(outer) == pytest.approx(2, abs=1e-9)
    assert inner.Hausdorff(outer) == pytest.approx(2, abs=1e-9)
    a, b = inner.Measure(to=outer)["points"]
    assert math.hypot(a[0], a[1]) == pytest.approx(5, abs=1e-9) and math.hypot(b[0], b[1]) == pytest.approx(7, abs=1e-9)
    assert a[0] * b[1] - a[1] * b[0] == pytest.approx(0, abs=1e-8)


def test_the_largest_distance_of_curves_that_part_is_where_they_part_most():
    # v = h^2 and v = 2 h^2 on [0, 1]: they meet at the origin and part to (1, 1) and (1, 2); the upper end (1, 2) is
    # farthest from the lower curve, which comes nearest it at its own end (1, 1): 1 apart
    low, high = px.Curve(("t", "t^2"), (0, 1)), px.Curve(("t", "2*t^2"), (0, 1))
    assert low.Distance(high) == pytest.approx(0, abs=1e-12)
    assert high.Hausdorff(low) == pytest.approx(1, abs=1e-9)
