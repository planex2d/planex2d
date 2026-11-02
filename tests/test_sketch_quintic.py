# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

ARCH = [(0, 0), (20, 10), (40, 0)]


def curvature(sk, at, wire="Spline.1"):
    return sk.Wire(wire).Measure(at=at)["curvature"]


def arch(sketch=px.Sketch, **kw):
    sk = sketch()
    sk.Spline(ARCH, start_from_localpos=False, style="quintic", **kw)
    return sk


def test_a_curvature_radius_is_a_dimension_the_sketch_holds_and_updates():
    sk = arch(tangents={1: (1, 0)}, curvature_radii={1: -30})
    assert sk.dimensions["CurvatureRadius.1"] == pytest.approx(-30)
    assert curvature(sk, (20, 10)) == pytest.approx(-1 / 30, rel=1e-9)
    sk.Update({"CurvatureRadius.1": -12})
    assert curvature(sk, (20, 10)) == pytest.approx(-1 / 12, rel=1e-9)
    sk.Update({"CurvatureRadius.1": math.inf})
    assert abs(curvature(sk, (20, 10))) < 1e-12


@pytest.mark.parametrize("radius", [0.005, 0.008, 0.01, 0.012, -0.01])
@pytest.mark.parametrize("tangent", [(1, 0), (1, -1.86e-16)])
def test_a_curvature_radius_holds_in_a_sketch_drawn_in_meters(radius, tangent):
    sk = px.Sketch(auto_constraint=False)
    sk.Spline([(0, 0), (0.02, 0.01), (0.04, 0)], start_from_localpos=False, style="quintic", tangents={1: tangent},
              curvature_radii={1: radius})
    assert curvature(sk, (0.02, 0.01)) == pytest.approx(1 / radius, rel=1e-9)


def test_a_quicksketch_bends_the_spline_without_a_dimension():
    sk = arch(px.QuickSketch, tangents={1: (1, 0)}, curvature_radii={1: 25})
    assert curvature(sk, (20, 10)) == pytest.approx(1 / 25, rel=1e-9)


def test_a_curvature_radius_dimension_gives_the_spline_its_tangent_and_curvature():
    sk = arch()
    sk.Constrain("Spline.1.p1", px.CurvatureRadius, -40)
    assert curvature(sk, (20, 10)) == pytest.approx(-1 / 40, rel=1e-9)
    assert near(sk, (0, 0)) and near(sk, (40, 0))


def near(sk, at):
    return min(math.dist(p, at) for p in sk.locations.values()) < 1e-9


def test_a_reference_curvature_radius_measures_the_spline():
    sk = arch()
    sk.Constrain("Spline.1.p1", px.CurvatureRadius, reference=True)
    assert sk.dimensions["CurvatureRadius.1"] == pytest.approx(-20, rel=1e-9)


def test_a_curvature_radius_needs_a_quintic():
    sk = px.Sketch()
    sk.Spline(ARCH, start_from_localpos=False, style="averaged")
    with pytest.raises(ValueError, match="style 'quintic'"):
        sk.Constrain("Spline.1.p1", px.CurvatureRadius, -40)
    with pytest.raises(ValueError, match="nonzero"):
        arch().Constrain("Spline.1.p1", px.CurvatureRadius, 0)
    with pytest.raises(ValueError, match="which has no tangent"):
        arch(curvature_radii={1: -30})


def test_a_tension_stays_with_the_spline():
    plain, tense = arch(tangents={1: (1, 0)}), arch(tangents={1: (1, 0)}, tensions={1: 2.0})
    assert abs(curvature(tense, (20, 10)) - curvature(plain, (20, 10))) > 1e-4
    assert "Tension" not in str(tense.dimensions)


def test_a_mirror_bends_the_copy_the_other_way_and_follows_the_original():
    sk = arch(tangents={1: (1, 0)}, curvature_radii={1: -30})
    sk.Mirrored("Spline.1", px.H)
    copy = "Mirrored.1.Spline.1"
    assert curvature(sk, (20, -10), copy) == pytest.approx(1 / 30, rel=1e-9)
    sk.Update({"CurvatureRadius.1": -15})
    assert curvature(sk, (20, -10), copy) == pytest.approx(1 / 15, rel=1e-9)


def test_a_free_quintic_bends_into_an_arc_of_held_radius():
    sk = px.Sketch()
    sk.Spline([(0, 0), (10, 4), (20, 5)], start_from_localpos=False, style="quintic")
    sk.Arc((30, -5), center=(20, -5), id="arc")
    sk.Constrain("arc", px.Radius, 10)
    sk.Constrain("Spline.1", "arc", px.Curvature)
    joint = sk.Location("arc.start")
    assert abs(curvature(sk, joint) - sk.Wire("arc").Measure(at=joint)["curvature"]) < 1e-9
    assert abs(abs(sk.Wire("arc").Measure(at=joint)["curvature"]) - 0.1) < 1e-9
