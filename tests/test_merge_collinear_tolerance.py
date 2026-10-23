# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pathlib

import numpy as np
import pytest

import planex as px

COAST = np.loadtxt(pathlib.Path(__file__).parent / "data" / "britain-coast.csv", delimiter=",", comments="#")


def segments(shape):
    return np.array([[e.start.x, e.start.y, e.end.x, e.end.y] for e in shape.edges])


def farthest(points, shape):
    """the largest distance of a point from the shape's edges"""
    s = segments(shape)
    a, d = s[:, :2], s[:, 2:] - s[:, :2]
    p = points[:, None, :]
    t = np.clip(((p - a) * d).sum(-1) / (d * d).sum(-1), 0.0, 1.0)
    return np.sqrt((((a + t[..., None] * d) - p) ** 2).sum(-1)).min(axis=1).max()


@pytest.mark.parametrize("tol", [0.5, 1, 2, 5])
def test_merging_stays_within_tol_of_what_the_call_was_given(tol):
    coast = px.FromPolyline(COAST, closed=True)
    once = coast.Copy().MergeCollinearEdges(tol)
    assert len(once.edges) < len(coast.edges)
    assert farthest(COAST, once) <= tol * (1 + 1e-12)
    corners = segments(once)[:, :2]
    twice = once.Copy().MergeCollinearEdges(tol)
    assert farthest(corners, twice) <= tol * (1 + 1e-12)


def test_a_slow_arc_merges_only_as_far_as_its_sagitta_allows():
    r, tol = 100.0, 0.01
    a = np.linspace(0, np.pi / 2, 2001)
    arc = px.FromPolyline(np.column_stack([r * np.cos(a), r * np.sin(a)]))
    merged = arc.MergeCollinearEdges(tol)
    # a chord of sagitta tol spans 2 acos(1 - tol / r) of the arc
    span = 2 * np.arccos(1 - tol / r)
    assert len(merged.edges) == pytest.approx((np.pi / 2) / span, abs=2)
    assert farthest(np.column_stack([r * np.cos(a), r * np.sin(a)]), merged) <= tol * (1 + 1e-12)
