# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

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


@pytest.mark.constraints
def test_an_offset_curve_without_the_spline_fit_goes_out_as_its_arcs(no_spline_fit):
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline(BUMP[1:], px.Fix, id="s")
    sk.Offset("s", 0.4, id="b")
    with pytest.warns(px.PlanexWarning, match=r"b\.left\.s: no spline fit without planex's OCC integration, it goes out as its "
                                              r"\d+ arcs and lines"):
        edges = sk.Wire("b.left.s").edges
    assert len(edges) > 1 and not any(isinstance(e.edge, px.SplineEdge) for e in edges)
    px._planex._UseSplineFit(True)
    fitted = sk.Wire("b.left.s").edges
    assert len(fitted) == 1 and isinstance(fitted[0].edge, px.SplineEdge)
