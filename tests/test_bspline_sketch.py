# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9
POLES = [(0, 0), (3, 4), (7, 4), (10, 0)]
CAM = [(0, 0), (4, 3), (8, 0), (4, -3)]


def worst_gap(a, b, n=40):
    """the largest distance from n + 1 points along wire a to wire b"""
    length = a.Measure()["length"]
    return max(b.Measure(at=a.Measure(at=length * k / n)["point"])["distance"] for k in range(n + 1))


def test_a_bspline_runs_from_its_first_pole_to_its_last_as_px_bspline():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    assert sk.Location("s.start") == (0, 0) and sk.Location("s.end") == (10, 0) and sk.Location("s.p1") == (3, 4)
    assert worst_gap(sk.Wire("s"), px.BSpline(POLES)) < TOL
    assert sk.elements["Line"] == ["s.leg.0", "s.leg.1", "s.leg.2"]
    assert sk.Wire("s").Measure()["length"] == pytest.approx(px.BSpline(POLES).Measure()["length"], rel=1e-12)


def test_px_bspline_takes_clamped_uniform_knots_by_default():
    assert worst_gap(px.BSpline(POLES), px.BSpline(POLES, [0, 0, 0, 0, 1, 1, 1, 1], 3)) < TOL
    five = [(0, 0), (2, 3), (5, 3), (7, 0), (9, 2)]
    assert worst_gap(px.BSpline(five, degree=2), px.BSpline(five, [0, 0, 0, 1 / 3, 2 / 3, 1, 1, 1], 2)) < TOL


def test_a_leg_held_horizontal_makes_the_end_tangent_horizontal():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    sk.Constrain("s.leg.0", px.Horizontal)
    assert sk.Location("s.p0")[1] == pytest.approx(sk.Location("s.p1")[1], abs=TOL)
    assert sk.Measure("s", at=0)["tangent_hv"] == pytest.approx((1, 0), abs=TOL)


def test_a_fixed_pole_stays_while_a_leg_dimension_changes():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    sk.Constrain("s.p1", px.Fix)
    sk.Constrain("s.leg.2", px.Distance, 5, id="leg")
    sk.Update({"leg": 6})
    assert sk.Location("s.p1") == pytest.approx((3, 4), abs=TOL)
    assert math.dist(sk.Location("s.p2"), sk.Location("s.p3")) == pytest.approx(6)
    assert sk.Location("s.end") == sk.Location("s.p3")


def test_a_periodic_bspline_is_closed_and_has_no_start():
    sk = px.Sketch()
    sk.BSpline(CAM, periodic=True, id="cam")
    curve = px.BSpline(CAM, periodic=True)
    assert sk.Face().Measure()["area"] == pytest.approx(curve.Face().Measure()["area"], rel=1e-12)
    assert worst_gap(sk.Wire("cam"), curve) < TOL
    assert sk.elements["Line"] == ["cam.leg.0", "cam.leg.1", "cam.leg.2", "cam.leg.3"]
    with pytest.raises(ValueError, match="a periodic BSpline has no start or end"):
        sk.Location("cam.start")


def test_constraints_on_a_periodic_bspline_solve():
    sk = px.Sketch()
    sk.BSpline(CAM, periodic=True, id="cam")
    sk.Constrain("cam.p0", px.Fix)
    sk.Constrain("cam.leg.3", px.Horizontal)
    sk.Constrain("cam.leg.0", px.Distance, 5, id="a")
    sk.Update({"a": 6})
    assert sk.Location("cam.p0") == pytest.approx((0, 0), abs=TOL) and sk.Location("cam.p3")[1] == pytest.approx(0, abs=TOL)
    assert math.dist(sk.Location("cam.p0"), sk.Location("cam.p1")) == pytest.approx(6)
    assert not any(sk.Diagnose().values())


def test_quicksketch_draws_the_same_bspline():
    qs = px.QuickSketch()
    qs.BSpline(POLES, id="s")
    assert worst_gap(qs.Wire("s"), px.BSpline(POLES)) < TOL


def test_knots_that_do_not_start_at_zero_give_the_same_curve():
    sk = px.Sketch()
    sk.BSpline(POLES, [5, 5, 5, 5, 7, 7, 7, 7], 3, id="s")
    assert worst_gap(sk.Wire("s"), px.BSpline(POLES)) < TOL
    assert sk.Measure("s", at=0)["tangent_hv"] == pytest.approx((0.6, 0.8), abs=TOL)


def test_tangent_to_the_element_before_and_after():
    sk = px.Sketch()
    sk.Move((-5, 1)).Line((0, 0), id="before").BSpline(POLES, px.Tangent, id="s").Line((14, -3), px.Tangent, id="after")
    length = sk.Measure("s")["length"]
    assert sk.Measure("before", at=0)["tangent_hv"] == pytest.approx(sk.Measure("s", at=0)["tangent_hv"], abs=TOL)
    assert sk.Measure("after", at=0)["tangent_hv"] == pytest.approx(sk.Measure("s", at=length)["tangent_hv"], abs=TOL)


def test_a_bspline_names_its_poles_but_no_tangents_and_keeps_its_poles():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    with pytest.raises(ValueError, match="a BSpline runs through no pole but its ends; its tangents there are its first and "
                                         "last legs, 's.leg.0' and 's.leg.2'"):
        sk.Location("s.t1")
    with pytest.raises(ValueError, match="s has it as a pole"):
        sk.Remove(sk.elements["Point"][2])
    cam = px.Sketch()
    cam.BSpline(CAM, periodic=True, id="cam")
    with pytest.raises(ValueError, match="a periodic BSpline runs through none of its poles; its legs 'cam.leg.0' to 'cam.leg.3'"):
        cam.Location("cam.t1")


def test_a_trimmed_bspline_keeps_its_curve_up_to_the_cut():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    sk.Move((5, -1)).Line((5, 5), id="cut")
    sk.Trim(("s", (8, 1)), "cut")
    assert sk.Location("s.end") == pytest.approx((5, 3), abs=TOL)
    assert worst_gap(sk.Wire("s"), px.BSpline(POLES)) < TOL


def test_mirrored_copies_are_bsplines_that_follow_their_original():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    sk.Mirrored(["s"], px.V)
    copy = [x for x in sk.elements["BSpline"] if x != "s"][0]
    assert worst_gap(sk.Wire(copy), px.BSpline([(-h, v) for h, v in POLES])) < TOL
    sk.Constrain("s.p1", px.V, px.Distance, 4)
    assert sk.Location(copy + ".p1") == pytest.approx((-4, 4), abs=TOL)


def test_copies_get_their_own_legs():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    sk.Mirrored(["s", "s.leg.1"], px.V)
    copy = [x for x in sk.elements["BSpline"] if x != "s"][0]
    legs = [x for x in sk.elements["Line"] if x.startswith(copy + ".leg.")]
    assert legs == [copy + ".leg.0", copy + ".leg.1", copy + ".leg.2"] and len(sk.elements["Line"]) == 6
    for k, leg in enumerate(legs):
        assert sk.Location(leg + ".start") == pytest.approx(sk.Location(copy + ".p%d" % k), abs=TOL)
        assert sk.Location(leg + ".end") == pytest.approx(sk.Location(copy + ".p%d" % (k + 1)), abs=TOL)


def test_a_bspline_is_of_its_own_kind():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(1, 1), (2, 0)]).BSpline([(2, 0), (3, 2), (5, 2), (6, 0)])
    assert sk.elements["Spline"] == ["Spline.1"] and sk.elements["BSpline"] == ["BSpline.1"]
    assert sk.elements.bsplines == ["BSpline.1"]
    assert [e.local for e in sk(kind="bspline")] == ["BSpline.1"]
    assert sk.elements["Line"] == ["BSpline.1.leg.0", "BSpline.1.leg.1", "BSpline.1.leg.2"]


def test_a_constraint_on_a_leg_moves_only_the_poles_it_needs():
    sk = px.Sketch()
    sk.Move((-6, 0), px.Fix).Line((0, 0), px.Horizontal, id="inlet")
    sk.BSpline([(0, 0), (4, 0.5), (7, 5), (12, 5)], px.Tangent, id="s").Line((16, 5), px.Tangent, id="outlet")
    assert sk.Location("s.p1") == pytest.approx((4, 0), abs=TOL)
    assert sk.Location("s.p2") == pytest.approx((7, 5), abs=TOL) and sk.Location("s.p3") == pytest.approx((12, 5), abs=TOL)
    assert sk.Location("outlet.end") == pytest.approx((16, 5), abs=TOL)


def test_remove_takes_the_poles_and_legs_along():
    sk = px.Sketch()
    sk.Move((-5, 0)).Line((0, 0), id="before").BSpline(POLES, id="s")
    sk.Constrain("s.leg.1", px.Horizontal)
    sk.Remove("s")
    assert "BSpline" not in sk.elements and sk.elements["Line"] == ["before"]
    assert len(sk.elements["Point"]) == 3
    assert sk.Location("before.end") == (0, 0) and not sk.constraints.get("Horizontal")


def test_the_plot_draws_a_bspline_within_its_tolerance():
    sk = px.Sketch()
    sk.BSpline(POLES, id="s")
    path = sk.Geometry("s")["path"]
    curve = px.BSpline(POLES)
    # 2e-4 of the size of the box around the poles, 10
    gaps = [curve.Measure(at=((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))["distance"] for a, b in zip(path, path[1:])]
    assert max(gaps) <= 2e-3 and 16 < len(path) < 200
    assert path[0] == (0, 0) and path[-1] == (10, 0)


def test_a_bspline_takes_degree_plus_one_poles_and_fitting_knots():
    with pytest.raises(ValueError, match="3 poles of degree 3; it needs a degree of 1 at least and degree \\+ 1 poles"):
        px.Sketch().BSpline(POLES[:3])
    with pytest.raises(ValueError, match="4 poles of degree 3 need 8 knots, there are 2"):
        px.Sketch().BSpline(POLES, [0, 1])


def test_a_periodic_bspline_has_no_ends_for_the_relations_it_gets():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((10, 0)).Line((10, 5)).Line((0, 5)).Close()
    sk.BSpline(CAM, periodic=True, id="cam")
    assert sk.Location("cam.p0") == pytest.approx((0, 0), abs=TOL)
    free = px.Sketch(auto_constraint=False)
    free.Move((0, 0)).Line((10, 0)).Line((10, 5)).Line((0, 5)).Close()
    free.BSpline(CAM, periodic=True, id="cam")
    found = free.SuggestConstraints()
    assert found and not any(f["kind"] == "Tangent" and '"cam"' in f["call"] for f in found)
