# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-12


def same(plane, origin, normal, h):
    assert plane.origin == pytest.approx(origin, abs=TOL)
    assert plane.normal == pytest.approx(normal, abs=TOL)
    assert plane.h == pytest.approx(h, abs=TOL)


X_AXIS = px.Line((0, 0), (1, 0))
Z_AXIS = px.Line((0, 0), (1, 0), plane=px.ZX)


def test_a_plane_and_a_point_or_an_offset_give_the_parallel_plane():
    same(px.Plane(px.XY, (1, 2, 5)), (0, 0, 5), (0, 0, 1), (1, 0, 0))
    same(px.Plane(px.XY, 2), (0, 0, 2), (0, 0, 1), (1, 0, 0))
    same(px.Plane(px.XY, -1.5), (0, 0, -1.5), (0, 0, 1), (1, 0, 0))
    same(px.Plane(px.XY, (1, 2, 5), origin=(1, 1, 0), axis=(0, 1, 0)), (1, 1, 5), (0, 0, 1), (0, 1, 0))
    tilted = px.Plane(origin=(1, 2, 3), normal=(1, 1, 0))
    n = (2 ** -0.5, 2 ** -0.5, 0)
    moved = px.Plane(tilted, 3)
    assert moved.origin == pytest.approx(tuple(o + 3 * c for o, c in zip(tilted.origin, n)), abs=TOL)
    assert moved.h == pytest.approx(tilted.h, abs=TOL)


def test_two_lines_meeting_or_parallel():
    same(px.Plane(X_AXIS, Z_AXIS), (0, 0, 0), (0, 1, 0), (1, 0, 0))
    same(px.Plane(Z_AXIS, X_AXIS), (0, 0, 0), (0, 1, 0), (1, 0, 0))
    above = px.Line((0, 0), (1, 0), plane=px.Plane(px.XY, 2))
    same(px.Plane(X_AXIS, above), (0, 0, 0), (0, 1, 0), (1, 0, 0))


def test_a_line_of_several_collinear_edges_is_a_line():
    line = px.FromPolyline([(0, 0), (1, 0), (3, 0)])
    same(px.Plane(line, (0, 0, 3)), (0, 0, 0), (0, 1, 0), (1, 0, 0))


def test_three_points_and_a_line_and_a_point():
    same(px.Plane((0, 0, 1), (1, 0, 1), (0, 1, 1)), (0, 0, 1), (0, 0, 1), (1, 0, 0))
    same(px.Plane((0, 1, 1), (1, 0, 1), (0, 0, 1)), (0, 0, 1), (0, 0, 1), (1, 0, 0))
    same(px.Plane(X_AXIS, (0, 0, 3)), (0, 0, 0), (0, 1, 0), (1, 0, 0))
    same(px.Plane((0, 0, 0), (0, 1, 0), (0, 0, 1)), (0, 0, 0), (1, 0, 0), (0, 1, 0))
    p = px.Plane((1, 0, 0), (0, 1, 0), (0, 0, 1), axis=(0, 0, 1))
    assert p.normal == pytest.approx((3 ** -0.5,) * 3, abs=TOL)
    assert p.origin == pytest.approx((1 / 3, 1 / 3, 1 / 3), abs=TOL)
    assert math.isclose(sum(a * b for a, b in zip(p.h, p.normal)), 0, abs_tol=TOL)


def test_a_point_and_a_normal():
    same(px.Plane((0, 0, 5), (0, 0, 1)), (0, 0, 5), (0, 0, 1), (1, 0, 0))
    same(px.Plane((1, 1, 1), (0, 0, -1)), (0, 0, 1), (0, 0, -1), (1, 0, 0))


def test_a_curved_wire_gives_its_plane():
    same(px.Plane(px.Circle((0, 0), 1), (0, 0, 3)), (0, 0, 3), (0, 0, 1), (1, 0, 0))


@pytest.mark.parametrize("call, error, message", [
    (lambda: px.Plane(X_AXIS, px.Line((0, 0), (0, 1), plane=px.Plane(px.XY, 1))), ValueError, "neither meet nor are parallel"),
    (lambda: px.Plane(X_AXIS, px.Line((2, 0), (5, 0))), ValueError, "lie on one line"),
    (lambda: px.Plane(X_AXIS, (4, 0, 0)), ValueError, "the point lies on the line"),
    (lambda: px.Plane((0, 0, 0), (1, 1, 1), (2, 2, 2)), ValueError, "the three points lie on one line"),
    (lambda: px.Plane(X_AXIS, 2), TypeError, r"Plane\(px.Plane\(wire\), offset\)"),
    (lambda: px.Plane(px.XY, (0, 0, 1), normal=(0, 0, 1)), TypeError, "normal= goes with origin= alone"),
    (lambda: px.Plane(px.XY, (0, 0, 1), (0, 1, 0)), TypeError, "three points"),
    (lambda: px.Plane((0, 0, 0), (0, 0, 1), (0, 1, 0), (1, 0, 0)), TypeError, "at most three positional arguments"),
])
def test_errors(call, error, message):
    with pytest.raises(error, match=message):
        call()


def test_offset_is_gone():
    assert not hasattr(px.XY, "Offset")
