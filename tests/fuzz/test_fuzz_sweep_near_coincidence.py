# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Near-coincidence sweeps: a shape against a copy moved by 1e-3 ... 1e-15, at scales 1e-6 ... 1e6 and for several k.
Every point compares with the exact result within what Tol(S) allows, so a result may never jump."""
import math

import pytest

import planex as px

pytest.importorskip("hypothesis")
shapely = pytest.importorskip("shapely")

import fuzzlib  # noqa: E402

SHIFTS = [10.0 ** -k for k in range(3, 16)]
SCALES = [10.0 ** p for p in range(-6, 7)]


def near(value, expected, tol, what):
    assert abs(value - expected) <= tol, (what, value, expected, tol)


def circle(x, y, r):
    return px.QuickSketch().Circle((x, y), r).Face()


def square(x, y, s):
    return px.Move((x, y)).Rectangle(s, s).Face()


def turned(points, center, angle):
    c, s = math.cos(angle), math.sin(angle)
    return [(center[0] + c * (x - center[0]) - s * (y - center[1]), center[1] + s * (x - center[0]) + c * (y - center[1]))
            for x, y in points]


def lens(r, d):
    return 2 * r * r * math.acos(d / (2 * r)) - d / 2 * math.sqrt(4 * r * r - d * d)


@pytest.mark.parametrize("shift", SHIFTS, ids=lambda s: f"{s:.0e}")
def test_circle_and_its_copy_turned_about_its_centre(shift):
    a = circle(0.3, -0.2, 1.0)
    b = a.Rotated((0.3, -0.2), math.degrees(shift))
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), math.pi, tol, "common")
    near(fuzzlib.area(a + b), math.pi, tol, "union")
    near(fuzzlib.area(a - b), 0.0, tol, "difference")


@pytest.mark.parametrize("shift", SHIFTS, ids=lambda s: f"{s:.0e}")
def test_circle_and_its_copy_moved(shift):
    a = circle(0.3, -0.2, 1.0)
    b = a.Translated((shift, 0.0))
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), lens(1.0, shift), tol, "common")
    near(fuzzlib.area(a + b), 2 * math.pi - lens(1.0, shift), tol, "union")


TURNS = [m * 10.0 ** -n for n in range(3, 16) for m in (1.0, 2.5, 5.0)]


def turn_param(angle):
    return pytest.param(angle, id=f"{angle:.1e}")


@pytest.mark.parametrize("angle", [turn_param(a) for a in TURNS])
def test_circle_and_its_copy_turned_about_another_point(angle):
    a = circle(0.4, 0.0, 0.5)
    b = a.Rotated((0.0, 0.0), math.degrees(angle))
    d = 2 * 0.4 * math.sin(angle / 2)
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), lens(0.5, d), tol, "common")
    near(fuzzlib.area(a + b), 2 * math.pi * 0.25 - lens(0.5, d), tol, "union")
    near(fuzzlib.area(a - b), math.pi * 0.25 - lens(0.5, d), tol, "difference")


@pytest.mark.parametrize("shift", SHIFTS, ids=lambda s: f"{s:.0e}")
def test_square_and_its_copy_moved_along_an_edge(shift):
    a = square(0.0, 0.0, 1.0)
    b = a.Translated((shift, 0.0))
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), 1.0 - shift, tol, "common")
    near(fuzzlib.area(a + b), 1.0 + shift, tol, "union")
    near(fuzzlib.area(a - b), shift, tol, "difference")


@pytest.mark.parametrize("shift", SHIFTS, ids=lambda s: f"{s:.0e}")
def test_star_and_its_copy_turned_about_a_vertex(shift):
    points = fuzzlib.star_points((0.2, 0.1, 0.4, (0.9, 0.5, 1.0, 0.6, 0.8, 0.45), (1.0, 0.7, 0.9, 0.5, 1.0, 0.6), 1.3))
    a = fuzzlib.polygon(points)
    b = a.Rotated(points[2], math.degrees(shift))
    sa, sb = shapely.Polygon(points), shapely.Polygon(turned(points, points[2], shift))
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), sa.intersection(sb).area, tol, "common")
    near(fuzzlib.area(a + b), sa.union(sb).area, tol, "union")
    near(fuzzlib.area(a - b), sa.difference(sb).area, tol, "difference")


@pytest.mark.parametrize("scale", SCALES, ids=lambda s: f"{s:.0e}")
def test_overlapping_squares_at_every_scale(scale):
    corners = [(0.0, 0.0), (scale, 0.0), (scale, scale), (0.0, scale)]
    a = square(0.0, 0.0, scale)
    b = square(0.0, 0.0, scale).Rotated((0.5 * scale, 0.5 * scale), 30).Translated((0.25 * scale, 0.0))
    moved = [(x + 0.25 * scale, y) for x, y in turned(corners, (0.5 * scale, 0.5 * scale), math.radians(30))]
    sa, sb = shapely.Polygon(corners), shapely.Polygon(moved)
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), sa.intersection(sb).area, tol, "common")
    near(fuzzlib.area(a + b), sa.union(sb).area, tol, "union")
    near(fuzzlib.area(a - b) / scale ** 2, sa.difference(sb).area / scale ** 2, tol / scale ** 2, "difference")


@pytest.fixture
def restore_k():
    k = px.config.k
    yield
    px.config.k = k


@pytest.mark.parametrize("k", [1.0, 10.0, 100.0, 1000.0])
@pytest.mark.parametrize("shift", [1e-9, 1e-12, 1e-13, 1e-14], ids=lambda s: f"{s:.0e}")
def test_square_and_its_copy_moved_for_every_k(k, shift, restore_k):
    px.config.k = k
    a = square(0.0, 0.0, 1.0)
    b = a.Translated((shift, 0.0))
    tol = fuzzlib.area_tol(a, b)
    near(fuzzlib.area(a * b), 1.0 - shift, tol, "common")
    near(fuzzlib.area(a + b), 1.0 + shift, tol, "union")
    near(fuzzlib.area(a - b), shift, tol, "difference")
