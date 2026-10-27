# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import pytest

import planex as px

pytestmark = pytest.mark.constraints


def free_curve():
    sk = px.Sketch()
    sk.Curve(("t", "0.1*t^2"), (0, 10))
    sk.Remove(next(iter(sk.constraints["Fix"])))
    return sk


def test_a_curve_fixed_by_default_has_no_free_motion():
    sk = px.Sketch()
    sk.Curve(("t", "0.1*t^2"), (0, 10))
    assert sk.dof == 0 and sk.FreeMotions() == []


def test_a_free_curve_moves_as_a_rigid_body_by_its_ends():
    sk = free_curve()
    motions = sk.FreeMotions()
    assert sk.dof == 3 and [m["drag"] for m in motions] == ["Curve.1.start.h", "Curve.1.start.v", "Curve.1.end.h"]
    assert all(m["moves"] == ["Curve.1"] and not any("origin" in k for k in m["velocity"]) for m in motions)
    assert motions[0]["velocity"]["Curve.1.end"] == pytest.approx((1, 0), abs=1e-9)
    assert "Curve.1.angle" in motions[2]["velocity"]


def test_suggestions_place_a_free_curve_by_its_ends():
    sk = free_curve()
    found = sk.SuggestConstraints(geometric=False)
    assert len(found) == 3 and all("origin" not in g["call"] for g in found)
    assert [g["removes"] for g in found] == ["Curve.1.start.h", "Curve.1.start.v", "Curve.1.end.h"]
    sk.AutoConstrain(geometric=False)
    assert (sk.dof, sk.redundant) == (0, [])


def test_the_free_motions_plot_of_a_curve():
    pytest.importorskip("matplotlib")
    ax = free_curve().Plot(show_free_motions=True)
    assert len(ax.get_legend().get_texts()) == 3


def test_diagnose_a_closed_and_an_open_curve():
    sk = px.Sketch()
    sk.Curve(("10*cos(t)", "10*sin(t)"), (0, 360))
    assert all(found == [] for found in sk.Diagnose().values())
    sk.Curve(("t + 20", "0.1*t^2"), (0, 10))
    assert [f["ids"] for f in sk.Diagnose()["open_ends"]] == [("Curve.2",), ("Curve.2",)]


def test_auto_constraint_takes_a_curve_on_a_chain():
    sk = px.Sketch(auto_constraint=True)
    sk.Line(5, 0).Curve(("t + 5", "0.1*t^2"), (0, 10))
    assert sk.redundant == [] and sk.status in ("under", "full")
    assert sk.Location("Curve.1.start") == pytest.approx((5, 0), abs=1e-12)


@pytest.mark.parametrize("make", [px.Sketch, px.QuickSketch])
def test_a_curve_is_drawn_as_its_spline(make):
    pytest.importorskip("matplotlib")
    sk = make()
    sk.Curve(("t", "0.1*t^2"), (0, 10))
    (g,) = [g for g in sk._Drawing() if g["id"] == "Curve.1"]
    assert g["kind"] == "Spline" and len(g["path"]) > 2
    ax = sk.Plot()
    drawn = [tuple(p) for c in ax.collections if hasattr(c, "get_segments") for s in c.get_segments() for p in s]
    assert max(p[0] for p in drawn) == pytest.approx(10) and max(p[1] for p in drawn) == pytest.approx(10)
    assert "<path" in sk._repr_svg_()

