# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


@pytest.mark.parametrize("call", [
    lambda: px.FromNACA("0012", plane=1),
    lambda: px.FromShapely(None, plane=1),
])
def test_an_invalid_plane_names_the_public_call(call):
    with pytest.raises(TypeError, match=r"^From(NACA|Shapely): plane is a Plane"):
        call()


def test_a_tangent_target_of_another_form_is_named_as_an_id():
    with pytest.raises(TypeError, match=r"a target is an id, an Edge"):
        px.QuickSketch().Move((0, 0)).Line(1, tangent=1)


def test_config_refuses_an_unknown_setting():
    with pytest.raises(AttributeError, match=r"^config\.spline_tolerance: no such setting; the settings are .*spline_tol"):
        px.config.spline_tolerance = 1e-3
    before = px.config.spline_tol
    px.config.spline_tol = before
    assert px.config.spline_tol == before


def test_a_field_on_a_wire_says_it_needs_an_area():
    w = px.FromPolyline([(0, 0), (1, 0), (1, 1)])
    with pytest.raises(TypeError, match=r"needs an area: a Face, Shape or sketch, not a Wire"):
        px.PlotField(px.measurements.MinThickness(), w)


def test_hydraulic_diameter_is_a_factory():
    face = px.QuickSketch().Move((0, 0)).Rectangle(4, 2).Face()
    assert face.Measure(px.measurements.HydraulicDiameter()) == pytest.approx(4 * 8 / 12, rel=1e-14)
    assert px.measurements.HydraulicDiameter()(face) == pytest.approx(8 / 3, rel=1e-14)
    with pytest.raises(TypeError, match=r"^HydraulicDiameter\(\) takes no arguments: the measurement is HydraulicDiameter\(\)"):
        face.Measure(px.measurements.HydraulicDiameter)
