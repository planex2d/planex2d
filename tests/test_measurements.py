# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px
import planex.measurements as pm


def rect(w=4.0, h=1.0):
    return px.QuickSketch().Rectangle(w, h).Face()


def ring():
    return px.QuickSketch().Circle((0, 0), 2).Face() - px.QuickSketch().Circle((0, 0), 1).Face()


def test_min_thickness_says_when_its_minimum_is_a_corner_tip():
    with pytest.warns(px.PlanexWarning, match=r"the minimum .* is the tip of the corner at .* \(90 degrees\); ignore_corners=91"):
        rect().Measure(pm.MinThickness())
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert rect().Measure(pm.MinThickness(ignore_corners=91)) == pytest.approx(1, rel=1e-9)
        assert ring().Measure(pm.MinThickness()) == pytest.approx(1, rel=1e-9)


def test_wall_thickness_by_ball_and_ray():
    assert rect().Measure(pm.MinThickness(ignore_corners=100)) == pytest.approx(1, rel=1e-9)
    with pytest.warns(px.PlanexWarning, match="tip of the corner"):
        assert rect().Measure(pm.MinThickness()) < 0.05
    assert rect().Measure(pm.MinThickness(method="ray")) == pytest.approx(1, rel=1e-12)
    assert rect().Measure(pm.MaxThickness(method="ray")) == pytest.approx(4, rel=1e-12)
    for method in ("ball", "ray"):
        assert ring().Measure(pm.MinThickness(method=method)) == pytest.approx(1, rel=1e-9)
    data = pm.MinThickness(ignore_corners=100)(rect(), plot=True, samples=100)
    assert set(data) >= {"points", "values", "label", "value", "marks"}
    assert len(data["points"]) == len(data["values"]) > 0 and data["value"] == pytest.approx(1)
    with pytest.raises(ValueError, match="method"):
        pm.MinThickness(method="sphere")


def test_hydraulic_diameter_hausdorff_and_clearance():
    assert rect(4, 1).Measure(pm.HydraulicDiameter()) == pytest.approx(2 * 4 * 1 / 5, rel=1e-12)
    a, b = px.Line((0, 0), (2, 0)), px.Line((0, 1), (2, 1.5))
    assert a.Hausdorff(b) == pytest.approx(1.5, rel=1e-9)
    arc = px.Arc((-1, 0), (1, 0), (0, 0), reverse=True)
    assert px.Line((-1, 0), (1, 0)).Hausdorff(arc) == pytest.approx(1, rel=1e-9)
    s = px.Glue([px.Line((0, 0), (2, 0)), px.Line((0, 1), (2, 1.5))])
    s.edges[0].name, s.edges[1].name = "low", "high"
    assert s.Measure(pm.Clearance("low", "high")) == pytest.approx(1, rel=1e-12)
    assert s.Measure(pm.Hausdorff("low", "high")) == pytest.approx(1.5, rel=1e-9)


def test_stored_measurements():
    f = rect()
    assert f.Measure(pm.HydraulicDiameter(), id="dh") == pytest.approx(1.6)
    m = f.measures
    assert m["dh"] == pytest.approx(1.6) and m["area"] == pytest.approx(4)
    f.Scale((0, 0), 2)
    assert f.measures["dh"] == pytest.approx(3.2)
    f.RemoveMeasure("dh")
    assert "dh" not in f.measures
    with pytest.raises(ValueError, match="no stored measurement"):
        f.RemoveMeasure("dh")
    with pytest.raises(TypeError, match="callable"):
        f.Measure("x", id="y")


def test_corner_angles_and_radii():
    assert [round(v.angle, 9) for v in rect().vertices] == [90] * 4
    ell = px.QuickSketch().Move((0, 0)).Line((2, 0)).Line((2, 1)).Line((1, 1)).Line((1, 2)).Line((0, 2)).Close().Face()
    assert sorted(round(v.angle, 9) for v in ell.vertices) == [90] * 5 + [270]
    slot = px.QuickSketch().Move((0, 0)).Line((2, 0)).Arc(0.5, 180).Line((0, 1)).Arc(0.5, 180).Face()
    assert {v.radius for v in slot.vertices} == {0.5}
    assert rect().vertices[0].radius is None


def test_torsion_constant_of_a_circle_and_a_square():
    pytest.importorskip("ngsolve")
    circle = px.QuickSketch().Circle((0, 0), 1).Face()
    assert circle.Measure(pm.TorsionConstant()) == pytest.approx(math.pi / 2, rel=1e-4)
    assert rect(1, 1).Measure(pm.TorsionConstant(maxh=0.05)) == pytest.approx(0.140577, rel=1e-3)


@pytest.mark.parametrize("backend", ["matplotlib", "plotly"])
def test_plot_colours_the_face_by_a_measurement(backend):
    pytest.importorskip("matplotlib")
    if backend == "plotly":
        pytest.importorskip("plotly")
    import matplotlib
    matplotlib.use("Agg")
    part = ring()
    shown = px.PlotField(pm.MinThickness(), part, samples=120, backend=backend)
    if backend == "plotly":
        heat = [t for t in shown.data if t.type == "heatmap"]
        assert len(heat) == 1 and "wall thickness" in shown.layout.title.text
        import numpy as np
        z = np.array(heat[0].z, dtype=float)
        assert np.nanmin(z) == pytest.approx(1, rel=1e-6) and np.isnan(z).any()
    else:
        assert shown.get_title().startswith("wall thickness")
    edges = px.PlotField(pm.MinThickness(method="ray"), part, samples=60, backend=backend, on="edges")
    assert edges is not None
    with pytest.raises(TypeError, match="plot=True"):
        px.PlotField(pm.HydraulicDiameter(), part)


def test_max_thickness_of_a_filleted_l_bracket_is_exact():
    # the largest disc touches both outer sides and the inner fillet: 2 (15 sqrt2 - 5) / (1 + sqrt2)
    face = px.Face([px.FromPolyline([(0, 0), (40, 0), (40, 10), (10, 10), (10, 30), (0, 30)], closed=True)])
    face.Fillet((10, 10), 5)
    exact = 2 * (15 * math.sqrt(2) - 5) / (1 + math.sqrt(2))
    assert face.Measure(pm.MaxThickness()) == pytest.approx(exact, rel=1e-12)


def test_hausdorff_takes_the_highest_of_several_peaks():
    curve = px.Spline([(0, 0), (2, 0.8), (4, 0), (4.3, 1.0), (4.6, 0), (10, 0)])
    far = (4.4, -100.0)
    edge = curve.edges[0]
    # the curve's exact points, as Hausdorff samples them
    dense = max(math.dist(edge.ClosestPoint(tuple(edge.PointAt(k / 20000))[:2]), far) for k in range(20001))
    assert pm.Hausdorff(lambda o: curve, lambda o: px.Circle(far, 1e-9))(None) == pytest.approx(dense - 1e-9, abs=1e-6)
