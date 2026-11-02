# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
import planex.measurements as pm

TOL = 1e-9


def polygon(points):
    return px.Face([px.FromPolyline(points, closed=True)])


def all_three(obj):
    return obj.Measure(pm.ConvexHull()), obj.Measure(pm.EnclosingCircle()), obj.Measure(pm.EnclosingRectangle())


def test_an_l_bracket():
    hull, circle, rect = all_three(polygon([(0, 0), (10, 0), (10, 2), (2, 2), (2, 8), (0, 8)]))
    assert hull.Measure()["area"] == pytest.approx(56) and len(hull.edges) == 5
    assert circle["center_hv"] == pytest.approx((5, 4), abs=TOL) and circle["radius"] == pytest.approx(math.sqrt(41))
    assert (rect["length"], rect["width"], rect["angle"]) == pytest.approx((10, 8, 0), abs=TOL)
    assert rect["center_hv"] == pytest.approx((5, 4), abs=TOL) and rect["face"].Measure()["area"] == pytest.approx(80)


def test_arcs_stay_arcs():
    slot = px.QuickSketch().Line(10).Arc(2, 180).Line(10).Arc(2, 180)
    hull, circle, rect = all_three(slot)
    assert hull.Measure()["area"] == pytest.approx(40 + 4 * math.pi) and len(hull.arcs) == 2
    assert circle["center_hv"] == pytest.approx((5, 2), abs=TOL) and circle["radius"] == pytest.approx(7)
    assert (rect["length"], rect["width"], rect["angle"]) == pytest.approx((14, 4, 0), abs=TOL)
    disc = px.Face([px.Circle((1, 2), 3)])
    hull, circle, rect = all_three(disc)
    assert hull.Measure()["area"] == pytest.approx(9 * math.pi)
    assert circle["center"] == pytest.approx((1, 2, 0), abs=TOL) and circle["radius"] == pytest.approx(3)
    # every angle gives the same square: the first, 0
    assert (rect["length"], rect["width"], rect["angle"]) == pytest.approx((6, 6, 0), abs=TOL)
    plate = polygon([(0, 0), (4, 0), (4, 2), (0, 2)])
    for corner in ((0, 0), (4, 0), (4, 2), (0, 2)):
        plate.Fillet(corner, 0.5)
    hull, circle, _ = all_three(plate)
    assert hull.Measure()["area"] == pytest.approx(8 - (4 - math.pi) * 0.25)
    assert circle["radius"] == pytest.approx(math.sqrt(1.5 ** 2 + 0.5 ** 2) + 0.5)


def test_the_rectangle_turns_with_the_part():
    _, _, rect = all_three(polygon([(0, 0), (4, 0), (4, 2), (0, 2)]).Rotated((0, 0), 30))
    assert (rect["length"], rect["width"], rect["angle"]) == pytest.approx((4, 2, 30), abs=TOL)
    assert rect["center_hv"] == pytest.approx((2 * math.cos(math.radians(30)) - math.sin(math.radians(30)),
                                               2 * math.sin(math.radians(30)) + math.cos(math.radians(30))), abs=TOL)


def test_several_faces_and_a_spline_profile():
    two = px.Glue([px.Face([px.Circle((0, 0), 1)]), px.Face([px.Circle((6, 0), 2)])])
    _, circle, rect = all_three(two)
    assert circle["center_hv"] == pytest.approx((3.5, 0), abs=TOL) and circle["radius"] == pytest.approx(4.5)
    assert rect["length"] * rect["width"] < 9 * 4
    naca = px.FromNACA("2412", 100)
    naca = naca if isinstance(naca, px.Face) else px.Face([naca])
    hull, circle, rect = all_three(naca)
    # a spline by its approximation: within spline_tol of the size (1e-6 of the chord) of its exact curve
    assert hull.Covers(naca) and hull.Measure()["area"] == pytest.approx(naca.Measure()["area"], rel=1e-5)
    exact = [tuple(p[:2]) for loop in naca.Polylines(1e-6) for p in loop]
    assert max(hull.Distance(p) for p in exact if not hull.Contains(p)) < 2e-4
    assert circle["radius"] == pytest.approx(50, rel=1e-3) and rect["width"] == pytest.approx(12, rel=1e-3)


def test_stored_and_refused():
    sk = px.QuickSketch().Line(10).Arc(2, 180).Line(10).Arc(2, 180)
    sk.Measure(pm.EnclosingCircle(), id="stock")
    assert sk.measures["stock"]["radius"] == pytest.approx(7)
    with pytest.raises(ValueError, match="the convex hull has no area"):
        px.Line((0, 0), (1, 1)).Measure(pm.ConvexHull())
    with pytest.raises(TypeError, match=r"ConvexHull\(\) takes no arguments"):
        pm.ConvexHull(polygon([(0, 0), (1, 0), (0, 1)]))
