# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

P5 = [(0, 0), (10, 15), (25, 5), (40, 20), (55, 0)]
ARCH = [(0, 0), (20, 10), (40, 0)]
C5 = [(1.2, 0), (0.4, 1.1), (-0.9, 0.7), (-0.8, -0.6), (0.3, -1.0)]
H = 1e-7


def params(pts, closed=False):
    loop = pts + pts[:1] if closed else pts
    t = [0.0]
    for a, b in zip(loop, loop[1:]):
        t.append(t[-1] + math.dist(a, b))
    return t


def at(pts, u, k=0, closed=False, tangents=None, **kw):
    return px._planex._InterpolatedAt(pts, tangents or {}, closed, u, k, style="quintic", **kw)


def curvature(pts, u, **kw):
    (x1, y1), (x2, y2) = at(pts, u, 1, **kw), at(pts, u, 2, **kw)
    return (x1 * y2 - y1 * x2) / math.hypot(x1, y1) ** 3


def jump(pts, u, k, **kw):
    return math.dist(at(pts, u + H, k, **kw), at(pts, u - H, k, **kw))


def test_the_quintic_runs_through_its_points_smooth_to_the_fourth_derivative():
    t = params(P5)
    assert max(math.dist(at(P5, u), p) for u, p in zip(t, P5)) < 1e-12
    for u in t[1:-1]:
        assert all(jump(P5, u, k) < 1e-5 for k in (1, 2, 3, 4))
        assert jump(P5, u, 5) > 1e-6
    for u in (t[0], t[-1]):
        assert math.hypot(*at(P5, u, 3)) < 1e-12 and math.hypot(*at(P5, u, 4)) < 1e-12


def test_three_points_give_the_parabola_through_them():
    c = math.hypot(20, 10)
    assert math.dist(at(ARCH, c / 2), (10, 7.5)) < 1e-12
    assert math.dist(at(ARCH, 1.5 * c), (30, 7.5)) < 1e-12


def test_two_points_give_the_straight_segment_run_evenly():
    a, b = (1, 2), (4, 6)
    for u in (0.0, 1.3, 2.5, 4.1, 5.0):
        assert math.dist(at([a, b], u), (1 + 0.6 * u, 2 + 0.8 * u)) < 1e-12
        assert math.dist(at([a, b], u, 1), (0.6, 0.8)) < 1e-12
    assert math.dist(at([a, b], 0.0, 1, tangents={0: (0, 1)}), (0, 1)) < 1e-12


def test_a_closed_quintic_is_smooth_to_the_fourth_derivative_at_its_start():
    end = params(C5, True)[-1]
    for k in (0, 1, 2, 3, 4):
        assert math.dist(at(C5, 0.0, k, True), at(C5, end, k, True)) < 1e-10


def test_a_tangent_along_the_curve_leaves_it_as_it_is():
    u = params(P5)[2]
    own = at(P5, u, 1)
    t = params(P5)
    for v in [t[-1] * k / 50 for k in range(51)]:
        assert math.dist(at(P5, v, tangents={2: own}), at(P5, v)) < 1e-11


def test_a_tangent_is_an_arrow_as_long_as_the_curves_own_derivative():
    u = params(P5)[2]
    own = math.hypot(*at(P5, u, 1))
    for degrees in (30, 210):
        d = (math.cos(math.radians(degrees)), math.sin(math.radians(degrees)))
        x, y = at(P5, u, 1, tangents={2: d})
        assert math.dist((x, y), (own * d[0], own * d[1])) < 1e-12
        double = at(P5, u, 1, tangents={2: d}, tensions={2: 2.0})
        assert math.dist(double, (2 * x, 2 * y)) < 1e-12


@pytest.mark.parametrize("pts, closed, i, d, radius", [(ARCH, False, 1, (1, 0), -30), (ARCH, False, 1, (1, 0.3), 10),
                                                       (P5, False, 0, (0.5, 1), 50), (P5, False, 4, (0.6, -0.8), -40),
                                                       (C5, True, 0, (-0.3, 1), 2), (C5, True, 2, (-0.5, -0.8), -1.5)])
def test_a_curvature_radius_sets_the_signed_curvature_there(pts, closed, i, d, radius):
    t = params(pts, closed)
    u = t[-1] if not closed and i == len(pts) - 1 else t[i]
    kw = {"closed": closed, "tangents": {i: d}, "curvature_radii": {i: radius}}
    assert curvature(pts, u, **kw) == pytest.approx(1 / radius, rel=1e-11)
    assert math.dist(at(pts, u, **kw), pts[i]) < 1e-12
    x, y = at(pts, u, 1, **kw)
    assert abs(d[0] * y - d[1] * x) < 1e-12 * math.hypot(x, y) and d[0] * x + d[1] * y > 0
    ax, ay = at(pts, u, 2, **kw)
    assert abs(d[0] * ax + d[1] * ay) < 1e-10 * math.hypot(ax, ay)


def test_an_infinite_radius_is_straight_there():
    assert abs(curvature(ARCH, params(ARCH)[1], tangents={1: (1, 0)}, curvature_radii={1: math.inf})) < 1e-15


def test_the_tension_scales_a_given_tangent_in_the_cubic_styles_too():
    u = params(P5)[2]
    for style in ("averaged", "clamped"):
        one = px._planex._InterpolatedAt(P5, {2: (1, 0)}, False, u, 1, style=style)
        two = px._planex._InterpolatedAt(P5, {2: (1, 0)}, False, u, 1, style=style, tensions={2: 2.0})
        assert math.dist(two, (2 * one[0], 2 * one[1])) < 1e-12


def test_px_spline_takes_tensions_and_curvature_radii():
    w = px.Spline(ARCH, tangents={1: (1, 0)}, curvature_radii={1: -30}, style="quintic")
    assert w.Measure(at=(20, 10))["curvature"] == pytest.approx(-1 / 30, rel=1e-9)
    tense = px.Spline(ARCH, tangents={1: (1, 0)}, tensions={1: 1.5}, style="quintic")
    assert abs(tense.mass - px.Spline(ARCH, style="quintic").mass) > 1e-6


def test_settings_that_cannot_hold_raise():
    with pytest.raises(ValueError, match="a curvature radius needs style 'quintic'"):
        px.Spline(ARCH, tangents={1: (1, 0)}, curvature_radii={1: -30}, style="averaged")
    with pytest.raises(ValueError, match="a curvature radius at point 1, which has no tangent"):
        px.Spline(ARCH, curvature_radii={1: -30}, style="quintic")
    with pytest.raises(ValueError, match="a tension at point 0, which has no tangent"):
        px.Spline(ARCH, tangents={1: (1, 0)}, tensions={0: 2}, style="quintic")
    with pytest.raises(ValueError, match="the tension at point 1 is -1.0+; it is positive"):
        px.Spline(ARCH, tangents={1: (1, 0)}, tensions={1: -1}, style="quintic")
    with pytest.raises(ValueError, match="the curvature radius at point 1 is 0.0; it is nonzero"):
        px.Spline(ARCH, tangents={1: (1, 0)}, curvature_radii={1: 0}, style="quintic")


def test_quintic_is_a_style_name():
    before = px.config.spline_style
    try:
        px.config.spline_style = "quintic"
        assert px.config.spline_style == "quintic"
        assert px.Spline(ARCH).mass == px.Spline(ARCH, style="quintic").mass
    finally:
        px.config.spline_style = before
