# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from planex._naca import _sides


def thickness_area(a4, t=0.12):
    return 10 * t * (0.2969 * 2 / 3 - 0.1260 / 2 - 0.3516 / 3 + 0.2843 / 4 + a4 / 5)


def max_camber(code):
    upper, lower, _ = _sides(code, 801, True)
    camber = [((a[0] + b[0]) / 2, (a[1] + b[1]) / 2) for a, b in zip(upper, lower)]
    return max(camber, key=lambda p: p[1])


def test_symmetric_airfoils_have_the_exact_area_and_thickness():
    sharp = px.FromNACA("0012")
    assert isinstance(sharp, px.Face) and len(sharp.edges) == 2
    assert sharp.mass == pytest.approx(thickness_area(-0.1036), rel=1e-5)
    assert sharp.bounding_box[1][1] == pytest.approx(0.0600, abs=1e-4)
    blunt = px.FromNACA("0012", trailing_edge="blunt")
    assert len(blunt.edges) == 3 and blunt.mass == pytest.approx(thickness_area(-0.1015), rel=1e-5)
    gap = min(e.mass for e in blunt.edges)
    assert gap == pytest.approx(2 * 5 * 0.12 * (0.2969 - 0.1260 - 0.3516 + 0.2843 - 0.1015), rel=1e-6)
    assert px.FromNACA("0012", chord=3).mass == pytest.approx(9 * sharp.mass, rel=1e-9)
    assert all(e.name is None for e in sharp.edges) and all(v.name is None for v in sharp.vertices)


def test_camber_lines_of_4_and_5_digit_airfoils():
    x, y = max_camber("2412")
    assert (x, y) == (pytest.approx(0.4, abs=2e-3), pytest.approx(0.02, abs=1e-6))
    x, y = max_camber("23012")
    assert (x, y) == (pytest.approx(0.15, abs=2e-3), pytest.approx(0.0184, abs=1e-4))
    upper, lower, _ = _sides("23112", 101, True)
    assert upper[-1] == lower[-1] == (1.0, 0.0) and upper[0] == lower[0] == (0.0, 0.0)
    cambered = px.FromNACA("4415")
    assert cambered.bounding_box[1][1] > -cambered.bounding_box[0][1]


def test_placement_angle_and_plane():
    f = px.FromNACA("0012", chord=2, angle=10, pivot=0.25, plane=px.YZ)
    te = (0.5 + 1.5 * math.cos(math.radians(10)), -1.5 * math.sin(math.radians(10)))
    ends = [(e.start.h, e.start.v) for e in f.edges] + [(e.end.h, e.end.v) for e in f.edges]
    assert any(p == pytest.approx(te) for p in ends)
    assert f.ToXYZ((1, 0)) == pytest.approx((0, 1, 0))
    assert px.FromNACA("0012", angle=90, pivot=0).bounding_box[0][1] == pytest.approx(-1, abs=1e-9)


def test_errors():
    for code in ("65-210", "641212", "12", 2412):
        with pytest.raises(ValueError, match="4- or 5-digit"):
            px.FromNACA(code)
    with pytest.raises(ValueError, match="position"):
        px.FromNACA("2012")
    with pytest.raises(ValueError, match="mean line"):
        px.FromNACA("21112")
    with pytest.raises(ValueError, match="trailing_edge"):
        px.FromNACA("0012", trailing_edge="round")
    with pytest.raises(ValueError, match="samples"):
        px.FromNACA("0012", samples=2)
