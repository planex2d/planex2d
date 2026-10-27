# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Plot samples arcs and circles by a tolerance from the drawn object's size: a chord lies at most 1e-4 of the bounding
box's diagonal off its arc, whatever the circle's radius"""
import math

import planex as px
from planex import _plot


def drawn(obj):
    polylines, faces, marks = _plot._scene(obj, "black", "black", "black", None, None, False, None)
    return [pts for lines in polylines.values() for pts in lines]


def sagittas(pts, center, r):
    return [r - math.dist(center, ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)) for p, q in zip(pts, pts[1:])]


def test_a_circle_in_view_is_drawn_within_the_tolerance_and_not_finer():
    (pts,) = drawn(px.Face([px.Circle((0, 0), 10)]))
    tol = 1e-4 * math.dist((-10, -10), (10, 10))
    assert all(abs(math.dist((0, 0), p) - 10) < 1e-12 for p in pts)
    assert 0.5 * tol < max(sagittas(pts, (0, 0), 10)) <= tol * (1 + 1e-9)


def test_a_small_circle_in_a_large_drawing_gets_fewer_points():
    shape = px.Glue([px.Face([px.Circle((0, 0), 10)]), px.Face([px.Circle((100, 0), 0.5)])])
    tol = 1e-4 * math.dist((-10, -10), (100.5, 10))
    (small,) = [pts for pts in drawn(shape) if math.dist(pts[0], (100, 0)) < 1]
    assert len(small) < 64
    assert max(sagittas(small, (100, 0), 0.5)) <= tol * (1 + 1e-9)
