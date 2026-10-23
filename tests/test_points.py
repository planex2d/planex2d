# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import numpy as np
import pytest

import planex as px
from helpers import square

FORMS = {
    "tuple": lambda p: tuple(p),
    "list": lambda p: list(p),
    "numpy": lambda p: np.array(p, dtype=float),
    "numpy float32": lambda p: np.array(p, dtype=np.float32),
    "numpy int": lambda p: np.array(p, dtype=np.int64),
}


def xy(v):
    return (v.x, v.y)


@pytest.fixture(params=list(FORMS))
def form(request):
    return FORMS[request.param]


def test_vertex(form):
    assert xy(px.Vertex(form((1, 2)))) == (1, 2)


def test_line_and_arc_factories(form):
    l = px.Line(form((0, 0)), form((2, 0))).edges[0].edge
    assert (xy(l.start), xy(l.end)) == ((0, 0), (2, 0))
    a = px.Arc(form((1, 0)), form((-1, 0)), form((0, 0))).edges[0].edge
    assert xy(a.center) == (0, 0)
    assert a.radius == pytest.approx(1)


def test_edge_classes(form):
    l = px.LineEdge(form((0, 0)), form((1, 0)))
    assert xy(l.Tangent(form((0, 0)))) == pytest.approx((1, 0))
    a = px.ArcEdge(form((1, 0)), form((0, 1)), form((0, 0)))
    assert xy(a.MidPoint()) == pytest.approx((2 ** -0.5, 2 ** -0.5))


def test_spline_points_and_tangents(form):
    pts = [(0, 0), (1, 1), (2, 0), (3, 1)]
    s = px.Spline([form(p) for p in pts], tangents={0: form((1, 0))}).edges[0].edge
    assert (xy(s.start), xy(s.end)) == ((0, 0), (3, 1))
    assert xy(s.Tangent(s.start)) == pytest.approx((1, 0))


@pytest.mark.parametrize("dtype", [float, np.int64])
def test_point_lists_as_numpy_arrays(dtype):
    pts = np.array([(0, 0), (1, 1), (2, 0), (3, 1)], dtype=dtype)
    s = px.Spline(pts).edges[0].edge
    assert (xy(s.start), xy(s.end)) == ((0, 0), (3, 1))
    b = px.BSpline(np.array([(0, 0), (1, 2), (2, 0)], dtype=dtype), [0, 0, 0, 1, 1, 1], 2).edges[0].edge
    assert (xy(b.start), xy(b.end)) == ((0, 0), (2, 0))
    wp = px.Move((0, 0)).Spline(pts[1:], tangents={0: np.array((1, 0))})
    assert wp.pen.position == (3, 1)
    w = px.Line((0, 0), (1, 0))
    assert w.ToXYZ(np.array([(0, 0), (1, 0)], dtype=dtype)) == [(0, 0, 0), (1, 0, 0)]
    assert w.ToHV(np.array([(1, 2, 0)], dtype=dtype)) == [(1, 2)]


def test_workplane_arc_tangent(form):
    wp = px.Move((0, 0)).Arc((1, 1), tangent=form((1, 0)))
    assert wp.pen.position == (1, 1)


def test_motion_and_conversions(form):
    p = square(0, 0)
    p.Translate(form((1, 2)))
    assert p.bounding_box == ((1, 2), (2, 3))
    p.Rotate(form((1, 2)), 90)
    assert p.bounding_box[0] == pytest.approx((0, 2))
    assert p.Translated(form((0, 0, 1))).Normal() == pytest.approx((0, 0, 1))
    assert p.Rotated(form((0, 0, 0)), form((1, 0, 0)), 90).Normal() == pytest.approx((0, -1, 0))
    assert p.ToXYZ([form((1, 1))]) == [(1, 1, 0)]
    assert p.ToHV([form((1, 1, 0))]) == [(1, 1)]
    e = p.edges[0]
    assert e.Translated(form((1, 0))).bounding_box[0][0] == pytest.approx(e.bounding_box[0][0] + 1)
    assert e.Rotated(form((0, 0)), 180).bounding_box[1][0] == pytest.approx(-e.bounding_box[0][0])


def test_strings_are_not_points():
    with pytest.raises(TypeError):
        px.Line("ab", (1, 0))
