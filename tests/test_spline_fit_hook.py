# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

BUMP = [(0, 0), (3, 1.5), (6, 0.5), (9, 2)]


@pytest.fixture
def no_spline_fit():
    was = px._planex._UseSplineFit(False)
    yield
    px._planex._UseSplineFit(was)


def circle_of_lines(n=400):
    wp = px.Move((1, 0))
    for k in range(1, n):
        wp.Line((math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)))
    return wp.Close().Face()


def test_the_spline_fit_is_loaded_with_the_module():
    assert px._planex._UseSplineFit(True)
    fitted = circle_of_lines().Simplified(fit_splines=dict(max_edge_length=0.1))
    assert len(fitted.edges) < 400 and fitted.splines


def test_simplify_without_the_spline_fit_raises_naming_the_integration(no_spline_fit):
    face = circle_of_lines()
    with pytest.raises(RuntimeError, match=r"^fit_splines: fitting splines needs planex's OCC integration \(netgen and OCC\), "):
        face.Simplified(fit_splines=dict(max_edge_length=0.1))
    with pytest.raises(RuntimeError, match=r"^fit_splines: "):
        face.outer.Simplified(fit_splines=True)
    assert len(face.Simplified().edges) == 400


def quiet_edges(sk, name):
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        return sk.Wire(name).edges


def offset_error(sk, source, offset, d):
    edges = sk.Wire(source).edges
    return max(abs(min(e.Distance(p) for e in edges) - d) for edge in sk.Wire(offset).edges for p in edge.Sample(1e-3))


def cam(angle):
    rho = 30 + 3 * math.sin(math.radians(2 * angle)) + 2 * math.cos(math.radians(3 * angle))
    return (rho * math.cos(math.radians(angle)), rho * math.sin(math.radians(angle)))


@pytest.mark.constraints
def test_an_offset_curve_without_the_spline_fit_goes_out_as_one_quintic_within_spline_tol(no_spline_fit):
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline(BUMP[1:], px.Fix, id="s")
    sk.Offset("s", 0.4, id="b")
    edges = quiet_edges(sk, "b.left.s")
    assert len(edges) == 1 and isinstance(edges[0].edge, px.SplineEdge) and edges[0].edge.bspline["degree"] == 5
    assert offset_error(sk, "s", "b.left.s", 0.4) <= px.config.spline_tol * 10
    source = sk.Wire("s").edges[0].edge
    for at, near in ((edges[0].edge.start, source.start), (edges[0].edge.end, source.end)):
        t, u = edges[0].edge.Tangent(at), source.Tangent(near)
        assert t.x * u.y - t.y * u.x == pytest.approx(0, abs=1e-9) and t.x * u.x + t.y * u.y > 0
    px._planex._UseSplineFit(True)
    fitted = sk.Wire("b.left.s").edges
    assert len(fitted) == 1 and isinstance(fitted[0].edge, px.SplineEdge)
    assert (fitted[0].edge.start, fitted[0].edge.end) == (edges[0].edge.start, edges[0].edge.end)


@pytest.mark.constraints
def test_a_closed_offset_curve_without_the_spline_fit_is_one_closed_quintic_as_occ_s_fit(no_spline_fit):
    sk = px.Sketch()
    sk.Curve(cam, (0, 360), id="pitch", construction=True)
    sk.Offset("pitch", -5, id="profile")
    sk("profile").construction = False
    edges = quiet_edges(sk, "profile.pitch")
    assert len(edges) == 1 and isinstance(edges[0].edge, px.SplineEdge) and edges[0].edge.bspline["degree"] == 5
    assert edges[0].edge.start == edges[0].edge.end
    assert offset_error(sk, "pitch", "profile.pitch", 5) <= px.config.spline_tol * 70
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        area = sk.Face().mass
    px._planex._UseSplineFit(True)
    assert sk.Face().mass == pytest.approx(area, rel=1e-5)
