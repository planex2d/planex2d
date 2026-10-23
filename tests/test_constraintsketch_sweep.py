# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def bend(sweep=90.0, r=1.0, ccw=True):
    """an arc of radius r from the fixed start (0, 0) about its fixed centre (0, r) (clockwise: (0, -r)), its radius
    a dimension: only its end can move, along its circle"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Arc(r, sweep if ccw else -sweep, id="bend")
    sk.Constrain("bend.center", px.Fix)
    sk.Constrain("bend", px.Radius, r, id="r")
    return sk


def end(sweep, r=1.0):
    """where the end of bend is at the sweep (degrees, clockwise negative)"""
    t = math.radians(sweep)
    return (r * math.sin(t), r * (1 - math.cos(t))) if sweep > 0 else (-r * math.sin(t), -r * (1 - math.cos(t)))


def test_the_sweep_of_an_arc_is_a_dimension():
    sk = bend()
    assert sk.dof == 1
    sk.Constrain("bend", px.Angle, 120, id="sw")
    assert (sk.dof, sk["sw"]) == (0, 120)
    assert sk.Geometry("bend")["sweep"] == pytest.approx(120, abs=1e-12)
    assert sk.Location("bend.end") == pytest.approx((math.sqrt(3) / 2, 1.5), abs=1e-12)
    assert repr(sk.constraints["Angle"]["sw"]) == '"bend", px.Angle, 120'


def test_a_reference_sweep_measures_it():
    sk = bend()
    sk.Constrain("bend", px.Angle, id="measured", reference=True)
    assert sk["measured"] == pytest.approx(90, abs=1e-12) and sk.dof == 1
    sk.Constrain("bend", px.Angle, 120, id="sw")
    sk.Update({"sw": 200})
    assert sk["measured"] == pytest.approx(200, abs=1e-12)
    assert sk.dimensions["measured"] == pytest.approx(sk.Geometry("bend")["sweep"], abs=1e-12)


def test_update_walks_the_sweep_round():
    sk = bend()
    sk.Constrain("bend", px.Angle, 90, id="sw")
    for target in (150, 180, 270, 350, 359, 30, 180):
        sk.Update({"sw": target})
        assert sk.Geometry("bend")["sweep"] == pytest.approx(target, abs=1e-10)
        assert sk.Location("bend.end") == pytest.approx(end(target), abs=1e-12)
    sk.Update({"sw": 60}, steps=8)
    assert sk.Location("bend.end") == pytest.approx(end(60), abs=1e-12)


def test_a_half_turn_and_the_drawing_branch():
    sk = bend(170)
    sk.Constrain("bend", px.Angle, 180, id="sw")
    assert sk.Location("bend.end") == pytest.approx((0, 2), abs=1e-12)
    assert sk.Location("bend.center") == pytest.approx((0, 1), abs=1e-12)


def test_a_clockwise_sweep_is_negative():
    sk = bend(ccw=False)
    assert sk.Geometry("bend")["sweep"] == pytest.approx(-90, abs=1e-12)
    sk.Constrain("bend", px.Angle, id="measured", reference=True)
    assert sk["measured"] == pytest.approx(-90, abs=1e-12)
    sk.Constrain("bend", px.Angle, -120, id="sw")
    assert sk["measured"] == pytest.approx(-120, abs=1e-12)
    assert end(-120) == pytest.approx((math.sqrt(3) / 2, -1.5), abs=1e-15)
    assert sk.Location("bend.end") == pytest.approx(end(-120), abs=1e-12)
    sk.Update({"sw": -300})
    assert sk.Geometry("bend")["sweep"] == pytest.approx(-300, abs=1e-10)
    with pytest.raises(ValueError, match=re.escape("sw = 30: the sweep of a clockwise arc lies in (-360, 0) degrees")):
        sk["sw"] = 30
    with pytest.raises(ValueError, match=re.escape('"bend", px.Angle, 90: the sweep of a clockwise arc lies in (-360, 0) degrees')):
        sk.Constrain("bend", px.Angle, 90)


def test_a_sweep_lies_strictly_within_a_turn():
    sk = bend()
    for bad in (0, 360, 400, -90):
        with pytest.raises(ValueError, match=re.escape("px.Angle, %g: the sweep of a counterclockwise arc lies in (0, 360) degrees" % bad)):
            sk.Constrain("bend", px.Angle, bad)
    sk.Constrain("bend", px.Angle, 90, id="sw")
    with pytest.raises(ValueError, match=re.escape("sw = 360: the sweep of a counterclockwise arc lies in (0, 360) degrees")):
        sk.Update({"sw": 360})
    assert sk["sw"] == 90
    sk.Move((5, 5)).Circle(1, id="ring")
    with pytest.raises(ValueError, match="cannot relate these elements"):
        sk.Constrain("ring", px.Angle, 90)


def test_both_ends_and_the_radius_make_the_sweep_redundant():
    sk = px.Sketch()
    sk.Move((0, 0)).Arc(1, 90, id="bend")
    sk.Constrain("bend.start", px.Fix)
    sk.Constrain("bend.end", px.Fix)
    sk.Constrain("bend", px.Radius, 1, id="r")
    assert sk.dof == 0
    with pytest.warns(px.PlanexWarning, match=re.escape('sw ("bend", px.Angle, 90) is redundant (consistent)')):
        sk.Constrain("bend", px.Angle, 90, id="sw")
    assert "sw" in sk.redundant
    with pytest.warns(px.PlanexWarning, match="no solution"):
        sk.Constrain("bend", px.Angle, 100)
    assert sk.Geometry("bend")["sweep"] == pytest.approx(90, abs=1e-12) and sk.status == "over"


def test_trim_removes_the_sweep_with_the_old_ends():
    sk = bend()
    sk.Constrain("bend", px.Angle, 90, id="sw")
    sk.Move((0.5, -1)).Line(0, 3, id="cut")
    with pytest.warns(px.PlanexWarning, match=re.escape("Trim('bend'): removed sw, they held the old ends of 'bend'")):
        sk.Trim(("bend", (0.9, 0.6)), "cut")
    assert "sw" not in sk.dimensions and "r" in sk.dimensions


def test_the_plot_and_the_svg_label_the_sweep():
    sk = bend()
    sk.Constrain("bend", px.Angle, 120, id="sw")
    assert "sw = 120°" in sk.ToSVG(show_constraints=True)
    assert any("sw = 120°" in a.text for a in sk.Plot(show_constraints=True, backend="plotly").layout.annotations)
    other = bend()
    other.Constrain("bend", px.Angle, 100)
    assert ">100°<" in other.ToSVG(show_constraints=True)
