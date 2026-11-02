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


def knife():
    """a triangle with two knife edges of 30.96 degrees and an obtuse corner"""
    return px.QuickSketch().Move((0, 0)).Line((10, 0)).Line((5, 3)).Close().Face()


def test_min_thickness_says_when_its_minimum_is_a_knife_edge_tip():
    for method in ("ball", "ray"):
        with pytest.warns(px.PlanexWarning, match=r"the minimum .* is the tip of the corner at \(0, 0\) \(30\.96 degrees\); ignore_corners=31"):
            assert knife().Measure(pm.MinThickness(method=method)) < 1e-6
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert rect().Measure(pm.MinThickness()) == pytest.approx(1, rel=1e-9)
        assert ring().Measure(pm.MinThickness()) == pytest.approx(1, rel=1e-9)


def test_wall_thickness_by_ball_and_ray():
    assert rect().Measure(pm.MinThickness(ignore_corners=100)) == pytest.approx(1, rel=1e-9)
    assert rect().Measure(pm.MinThickness()) == pytest.approx(1, rel=1e-9)
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


def test_max_thickness_finds_a_disc_that_touches_reflex_corners_only():
    # a V notch from the middle of each side, tips 30 from the center: the largest disc touches the four tips only
    notched = px.Face([px.FromPolyline([(0, 0), (45, 0), (50, 20), (55, 0), (100, 0), (100, 45), (80, 50), (100, 55),
                                       (100, 100), (55, 100), (50, 80), (45, 100), (0, 100), (0, 55), (20, 50), (0, 45)],
                                      closed=True)])
    assert notched.Measure(pm.MaxThickness()) == pytest.approx(60, rel=1e-9)


def test_wall_thickness_of_a_spline_face_follows_its_curves():
    # the samples and the discs follow the spline's approximation, the result its exact curve: no sample collapses to
    # zero in the middle of a side, and the results match the face drawn as lines within 1e-4
    face = px.FromNACA("2412", chord=100)
    lines = px.Face([px.FromPolyline(loop[:-1], closed=True) for loop in face.Polylines(1e-4)])
    with pytest.warns(px.PlanexWarning, match="tip of the corner"):
        data = pm.MinThickness()(face, plot=True)
    with pytest.warns(px.PlanexWarning, match="tip of the corner"):
        tip = lines.Measure(pm.MinThickness())
    away = [v for (x, _), v in zip(data["points"], data["values"]) if x < 95]
    assert len(away) > 300 and min(away) > 0.4
    assert data["value"] == pytest.approx(tip, rel=1e-6) and data["marks"][0][0] == pytest.approx((100, 0), abs=1e-6)
    assert face.Measure(pm.MaxThickness()) == pytest.approx(lines.Measure(pm.MaxThickness()), rel=1e-4)


def test_ignore_corners_does_not_depend_on_how_the_outline_is_split():
    # a sharp corner holds a disc whose touching points all lie in its wedge; a disc that also touches the far wall is
    # that wall's, also on a plate barely longer than it is wide
    for length in (1.5, 2.0, 4.0):
        assert rect(length).Measure(pm.MinThickness(ignore_corners=91)) == pytest.approx(1, rel=1e-9)
        assert rect(length).Measure(pm.MinThickness(method="ray", ignore_corners=91)) == pytest.approx(1, rel=1e-9)
    # an airfoil: the taper behind the sharp trailing edge is the corner's, the thinnest wall left is where the leading
    # edge's sides first face each other; as lines within 1e-4 the same (their own corners walk to their walls)
    face = px.FromNACA("2412", chord=100)
    lines = px.Face([px.FromPolyline(loop[:-1], closed=True) for loop in face.Polylines(1e-4)])
    splines, polyline = (pm.MinThickness(ignore_corners=30)(f, plot=True) for f in (face, lines))
    assert splines["value"] == pytest.approx(4.0, rel=1e-2) and splines["value"] == pytest.approx(polyline["value"], rel=2e-3)
    assert splines["marks"][0][0][0] < 3 and polyline["marks"][0][0][0] < 3


def ngon(k, r):
    return px.Face([px.FromPolyline([(r * math.cos(2 * math.pi * i / k), r * math.sin(2 * math.pi * i / k))
                                     for i in range(k)], closed=True)])


def test_corners_and_bends_measure_the_wall_they_lead_to():
    # a disc counts where the points it touches lie more than 90 degrees apart; a corner's or a bend's disc grows along
    # the medial axis to the first one that does, so an outline measures the same drawn as lines, coarse or fine
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        bracket = px.Face([px.FromPolyline([(0, 0), (10, 0), (10, 1), (1, 1), (1, 10), (0, 10)], closed=True)])
        assert bracket.Measure(pm.MinThickness()) == pytest.approx(1, rel=1e-9)
        for k in (16, 64):
            assert ngon(k, 10).Measure(pm.MinThickness()) == pytest.approx(20 * math.cos(math.pi / k), rel=1e-9)
        assert px.QuickSketch().Circle(10).Face().Measure(pm.MinThickness()) == pytest.approx(20, rel=1e-9)
        rounded = px.Face([px.FromPolyline([(0, 0), (4, 0), (4, 1), (0, 1)], closed=True)])
        for corner in ((0, 0), (4, 0), (4, 1), (0, 1)):
            rounded.Fillet(corner, 0.3)
        for method in ("ball", "ray"):
            assert rounded.Measure(pm.MinThickness(method=method)) == pytest.approx(1, rel=1e-9)


def test_a_thin_wall_between_samples_is_found_where_another_sample_is_thinner():
    # neck A, 0.5 between two small holes, widens fast; neck B, 0.6 between two large ones, slowly: the samples read B
    # as the thinner, the refinement of every local minimum finds A
    plate = px.QuickSketch().Rectangle(100, 40).Face()
    for c, r in (((20, 20), 1), ((22.5, 20), 1), ((50, 20), 10), ((70.6, 20), 10)):
        plate = plate - px.QuickSketch().Move(c).Circle(r).Face()
    data = pm.MinThickness(samples=400)(plate, plot=True)
    assert min(data["values"][:-1]) > 0.6
    # the place to about the square root of the value's precision: the wall widens quadratically from the neck
    assert data["value"] == pytest.approx(0.5, abs=1e-9) and data["discs"][-1][:2] == pytest.approx((21.25, 20), abs=1e-6)
