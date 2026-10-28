# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import numpy as np
import pytest

import planex as px
from planex._naca import _mean_line


def thickness_area(a4, t=0.12):
    return 10 * t * (0.2969 * 2 / 3 - 0.1260 / 2 - 0.3516 / 3 + 0.2843 / 4 + a4 / 5)


def max_camber(code):
    """the highest point of the code's camber line, its pieces' expressions evaluated at 400 stations each"""
    points, start = [], 0.0
    for end, camber, _ in _mean_line(code)[0]:
        for i in range(401):
            x = start + (end - start) * i / 400
            points.append((x, eval(camber.replace("^", "**"), {"X": x})))
        start = end
    return max(points, key=lambda p: p[1])


def test_symmetric_airfoils_have_the_exact_area_and_thickness():
    sharp = px.FromNACA("0012")
    assert isinstance(sharp, px.Face) and len(sharp.edges) == 2
    assert sharp.mass == pytest.approx(thickness_area(-0.1036), rel=1e-11)
    assert sharp.bounding_box[1][1] == pytest.approx(0.0600, abs=1e-4)
    blunt = px.FromNACA("0012", trailing_edge=px.Flat)
    assert len(blunt.edges) == 3 and blunt.mass == pytest.approx(thickness_area(-0.1015), rel=1e-11)
    gap = min(e.mass for e in blunt.edges)
    assert gap == pytest.approx(2 * 5 * 0.12 * (0.2969 - 0.1260 - 0.3516 + 0.2843 - 0.1015), rel=1e-6)
    assert px.FromNACA("0012", chord=3).mass == pytest.approx(9 * sharp.mass, rel=1e-9)
    assert all(e.name is None for e in sharp.edges) and all(v.name is None for v in sharp.vertices)


def test_camber_lines_of_4_and_5_digit_airfoils():
    x, y = max_camber("2412")
    assert (x, y) == (pytest.approx(0.4, abs=2e-3), pytest.approx(0.02, abs=1e-6))
    x, y = max_camber("23012")
    assert (x, y) == (pytest.approx(0.15, abs=2e-3), pytest.approx(0.0184, abs=1e-4))
    reflex = px.FromNACA("23112")
    assert len(reflex.edges) == 4
    corners = sorted((v.h, v.v) for v in reflex.vertices)
    assert corners[0] == pytest.approx((0.0, 0.0), abs=1e-15) and corners[-1] == pytest.approx((1.0, 0.0), abs=1e-15)
    cambered = px.FromNACA("4415")
    assert len(cambered.edges) == 4 and cambered.bounding_box[1][1] > -cambered.bounding_box[0][1]


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
    for wrong in ("sharp", "blunt", px.Round):
        with pytest.raises(ValueError, match="trailing_edge is px.Sharp or px.Flat"):
            px.FromNACA("0012", trailing_edge=wrong)
    with pytest.raises(TypeError, match="samples"):
        px.FromNACA("0012", samples=100)


def test_the_offset_of_the_exact_airfoil_keeps_its_distance_at_the_trailing_edge():
    foil = px.FromNACA("0012", chord=100)
    coated = foil.Offset(0.5, inplace=False)
    gaps = np.asarray(foil.Distance(coated.Polylines(1e-3)[0]))
    assert gaps.min() == pytest.approx(0.5, abs=1e-3) and gaps.max() == pytest.approx(0.5, abs=1e-3)
    # Steiner: the parallel set of a convex region at d has the area A + L d + pi d^2
    assert coated.mass == pytest.approx(foil.mass + foil.outer.mass * 0.5 + math.pi * 0.25, rel=1e-5)
