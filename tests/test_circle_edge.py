# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def kinds(obj):
    return sorted(type(e.edge).__name__ for e in obj.edges)


def test_a_circle_is_one_edge_without_a_vertex():
    w = px.Circle((1, 2), 3)
    assert len(w.edges) == 1 and isinstance(w.edges[0].edge, px.CircleEdge)
    c = w.edges[0].edge
    assert (c.center.x, c.center.y, c.radius, c.ccw) == pytest.approx((1, 2, 3, True))
    assert (c.origin.x, c.origin.y) == pytest.approx((4, 2), abs=TOL)
    assert len(w.vertices) == 0
    assert w.mass == pytest.approx(6 * math.pi, rel=TOL)
    assert px.Circle((0, 0), 1, reverse=True).edges[0].edge.ccw is False


def test_a_circle_face_and_a_circular_hole_stay_one_edge():
    disc = px.Circle((0, 0), 1).Face()
    assert len(disc.edges) == 1 and disc.mass == pytest.approx(math.pi, rel=TOL)
    plate = px.Move((-2, -2)).Rectangle(4, 4).Face() - disc
    assert kinds(plate).count("CircleEdge") == 1 and len(plate.edges) == 5
    assert plate.mass == pytest.approx(16 - math.pi, rel=TOL)


def test_a_cut_circle_becomes_arcs():
    crossed = px.Circle((0, 0), 1).Face() * px.Move((-2, -2)).Rectangle(2, 4).Face()
    assert kinds(crossed) == ["ArcEdge", "LineEdge"] and crossed.mass == pytest.approx(math.pi / 2, rel=TOL)
    touched = px.Circle((0, 0), 1).Face() + px.Move((1, -1)).Rectangle(2, 2).Face()
    assert kinds(touched).count("ArcEdge") == 2 and "CircleEdge" not in kinds(touched)
    assert touched.mass == pytest.approx(math.pi + 4, rel=TOL)


def test_the_offset_of_a_circle_is_a_circle():
    grown = px.Circle((0, 0), 1).Face().Offset(0.5, inplace=False)
    assert kinds(grown) == ["CircleEdge"] and grown.edges[0].edge.radius == pytest.approx(1.5, rel=TOL)
    assert grown.mass == pytest.approx(math.pi * 2.25, rel=TOL)


def test_circles_take_ids_and_move():
    w = px.Circle((0, 0), 2, id="ring")
    assert w.id == "ring"
    moved = w.Translated((1, 1))
    assert (moved.edges[0].edge.center.x, moved.edges[0].edge.center.y) == pytest.approx((1, 1))
    mirrored = w.Mirrored((5, 0), (0, 1))
    assert mirrored.edges[0].edge.ccw is False
