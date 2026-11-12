# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import math
import warnings

import pytest

import planex as px
from planex import _svg

pytestmark = pytest.mark.constraints

TOL = 1e-9


def constraints_of(sk):
    return {k: v for kind in sk.constraints.values() for k, v in kind.items()}


def x_split_on_both_sides():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((40, 30))
    sk.Move((0, 30)).Line((40, 0))
    sk.Split("Line.1", "Line.2")
    return sk


def test_an_x_split_on_both_sides_keeps_no_redundant_coincidence_and_warns_of_none():
    sk = x_split_on_both_sides()
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        sk.Split("Line.2", "Line.1.1")
    assert sk.redundant == []
    assert set(constraints_of(sk)) == {"Coincident.2", "Coincident.3", "Coincident.4"}


def test_the_tie_of_the_two_cut_points_can_be_removed_and_they_part():
    sk = x_split_on_both_sides()
    sk.Split("Line.2", "Line.1.1")
    sk.Remove("Coincident.3")
    sk.Drag("Point.6", (25, 19))
    assert sk("Point.5")[0].p == pytest.approx((20, 15), abs=TOL)
    assert sk("Point.6")[0].p == pytest.approx((25, 19), abs=TOL)
    assert sk.redundant == []


def test_split_takes_over_the_coincidence_of_a_line_end_put_on_the_line_it_cuts():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((40, 0))
    sk.Move((20, 10)).Line((20, 0))
    sk.Constrain("Line.2.end", "Line.1", px.Coincident)
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        sk.Split("Line.1", "Line.2")
    assert sk.redundant == []
    assert "Coincident.2" not in constraints_of(sk)


def test_a_fixed_line_trimmed_at_one_end_stays_fixed_where_its_piece_lies():
    sk = px.Sketch().Move((0, 0)).Line((40, 0))
    sk.Constrain("Line.1", px.Fix)
    sk.Move((20, -10)).Line((20, 10))
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        sk.Trim(("Line.1", (35, 0)), "Line.2")
    assert "Fix.1" in constraints_of(sk)
    sk.Drag("Line.2.start", (30, -10))
    assert sk("Line.1.start")[0].p == pytest.approx((0, 0), abs=TOL)
    assert sk("Line.1.end")[0].p == pytest.approx((20, 0), abs=TOL)


def trimmed_quietly(sk, *args):
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        sk.Trim(*args)
    assert "Fix.1" in constraints_of(sk)


def arc_of(sk, name):
    return [sk(f"{name}.{part}")[0].p for part in ("start", "end", "center")]


def test_a_fixed_circle_trimmed_where_trimmed_lines_end_stays_fixed_where_its_arc_lies():
    sk = px.Sketch(auto_constraint=False)
    sk.Circle((0, 0), 175, px.Fix, id="ring")
    sk.Move((0, -265)).Line((0, 300), id="side")
    sk.Trim(("side", (0, 77.8)))
    trimmed_quietly(sk, ("ring", (-175, 0)))
    held = [(0, -175), (0, 175), (0, 0)]
    assert arc_of(sk, "ring") == [pytest.approx(p, abs=TOL) for p in held]
    assert sk.dof == 2
    sk.Drag("side.start", (30, -265))
    assert arc_of(sk, "ring") == [pytest.approx(p, abs=TOL) for p in held]


def test_a_fixed_circle_trimmed_between_two_line_ends_on_it_keeps_its_long_arc():
    sk = px.Sketch()
    sk.Circle((0, 0), 10, px.Fix, id="bore")
    sk.Move((-2, 12)).Line((-2, 96 ** 0.5), id="left")
    sk.Move((2, 12)).Line((2, 96 ** 0.5), id="right")
    trimmed_quietly(sk, ("bore", (0, 10)))
    held = [(-2, 96 ** 0.5), (2, 96 ** 0.5), (0, 0)]
    assert arc_of(sk, "bore") == [pytest.approx(p, abs=TOL) for p in held]
    assert sk.dof == 4
    sk.Drag("left.start", (-5, 15))
    assert arc_of(sk, "bore") == [pytest.approx(p, abs=TOL) for p in held]


def test_a_fixed_circle_cut_four_times_trimmed_by_a_quarter():
    sk = px.Sketch(auto_constraint=False)
    sk.Circle((0, 0), 175, px.Fix, id="ring")
    sk.Move((0, -265)).Line((0, 300), id="side")
    sk.Move((-265, 0)).Line((300, 0), id="across")
    trimmed_quietly(sk, ("ring", (-120, 120)))
    assert arc_of(sk, "ring") == [pytest.approx(p, abs=TOL) for p in [(-175, 0), (0, 175), (0, 0)]]
    assert sk.dof == 6


def test_removing_an_offset_s_last_piece_removes_the_whole_offset():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(10, 8), (20, 0)])
    sk.Offset(["Spline.1"], (2, (5, 10)))
    sk.Remove("Offset.1.Spline.1")
    ids = [i for v in sk.elements.values() for i in v]
    assert not [i for i in ids if i.startswith("Offset.1")]
    assert constraints_of(sk) == {}


def test_an_offset_stays_while_one_of_its_pieces_is_left():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((20, 0)).Line((20, 10))
    sk.Offset(["Line.1", "Line.2"], (2, (10, 5)))
    sk.Remove("Offset.1.Line.1")
    ids = [i for v in sk.elements.values() for i in v]
    assert "Offset.1.Line.2" in ids and "Offset.1.d" in constraints_of(sk)
    sk.Remove("Offset.1.Line.2")
    assert not [i for v in sk.elements.values() for i in v if i.startswith("Offset.1")]


def test_a_removed_point_no_longer_coarsens_the_drawn_curves():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(10, 8), (20, 0)])
    plain = len(sk._Geometry("Spline.1")["path"])
    sk.Vertex("far", (1e5, 0))
    assert len(sk._Geometry("Spline.1")["path"]) < plain
    sk.Remove("far")
    assert len(sk._Geometry("Spline.1")["path"]) == plain


def test_a_symmetric_keeps_its_sign_when_its_line_is_split():
    sk = px.Sketch()
    sk.Move((20, -10)).Line((20, 40))
    sk.Move((0, 15)).Line((40, 15))
    sk.Move((5, 25)).Line((12, 35))
    sk.Move((28, 26)).Line((35, 34))
    sk.Constrain("Line.3", "Line.4", "Line.1", px.Symmetric)
    sk.Split("Line.1", "Line.2")
    everything = {g["id"]: g for g in sk._Drawing()}
    chevrons = [item for group in _svg._signs(sk, everything) for item in group["items"] if item[0] == "chevron"]
    assert len(chevrons) == 2


def eight(moved):
    pts = [(10 * math.cos(math.radians(a)), 5 * math.sin(math.radians(2 * a))) for a in (60, 120, 180, 240, 300)]
    if moved:
        pts[0] = (pts[0][0] + 0.7, pts[0][1])
    return pts


def test_tangent_propagation_runs_through_a_spline_s_pieces_in_a_shape():
    shape = px.Sketch((10, 0)).Spline(eight(True), periodic=True, name="eight").Shape()
    assert len(shape.edges) == 4
    seed = shape.edges[1]
    for continuity in (px.Tangent, px.Curvature):
        assert len(shape.edges(seed, propagate=continuity)) == 4
    assert len(shape.edges(seed, propagate=px.Coincident)) == 2


def test_a_symmetric_figure_eight_is_refused_as_one_wire():
    pts = [(10 * math.cos(math.radians(a)), 5 * math.sin(math.radians(2 * a))) for a in (0, 60, 120, 180, 240, 300)]
    with pytest.raises(ValueError, match="crosses or touches itself"):
        px.Spline(pts, periodic=True)
    sk = px.Sketch(pts[0]).Spline(pts[1:], periodic=True)
    with pytest.raises(ValueError, match="crosses or touches itself"):
        sk.Wire("Spline.1")
    lobes = sk.Wires()
    assert len(lobes) == 2
    assert lobes[0].Measure()["length"] == pytest.approx(lobes[1].Measure()["length"], rel=1e-9)


def test_a_closed_spline_that_does_not_cross_itself_is_still_one_wire():
    pts = [(10 * math.cos(math.radians(a)), 5 * math.sin(math.radians(a))) for a in range(0, 360, 30)]
    assert px.Spline(pts, periodic=True).IsClosed()


def circle_centres(sk):
    places = sorted((round(c[0], 9) + 0.0, round(c[1], 9) + 0.0) for c in (sk(i)[0].center for i in sk.elements["Circle"]))
    return [x for p in places for x in p]


@pytest.mark.parametrize("make", [px.Sketch, px.QuickSketch])
def test_a_linear_pattern_reversed_places_its_copies_the_other_way(make):
    sk = make().Circle((0, 0), 2)
    sk.LinearPattern(["Circle.1"], "H", 3, 10, reverse=True)
    assert circle_centres(sk) == pytest.approx([-20, 0, -10, 0, 0, 0], abs=TOL)


def test_a_reversed_grid_keeps_its_way_when_its_count_changes():
    sk = px.Sketch().Circle((0, 0), 2)
    sk.LinearPattern(["Circle.1"], "H", 2, 10, dir2="V", count2=2, spacing2=8, reverse=True, reverse2=True)
    assert circle_centres(sk) == pytest.approx([-10, -8, -10, 0, 0, -8, 0, 0], abs=TOL)
    sk["LinearPattern.1.count"] = 3
    sk.Update()
    xs = sorted({round(sk(i)[0].center[0], 9) for i in sk.elements["Circle"]})
    assert xs == pytest.approx([-20, -10, 0], abs=TOL)


def test_reverse2_without_a_second_direction_is_refused():
    with pytest.raises(TypeError, match="reverse2"):
        px.Sketch().Circle((0, 0), 2).LinearPattern(["Circle.1"], "H", 3, 10, reverse2=True)


def spline_and_offset():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline([(10, 12), (20, 2), (30, 10)])
    sk.Offset("Spline.1", (2, (15, 12)), both_sides=False)
    return sk


@pytest.mark.parametrize("continuity", [px.Tangent, px.Curvature])
def test_a_connect_joins_an_offset_curve_and_follows_its_distance(continuity):
    sk = spline_and_offset()
    sk.Connect("Spline.1.end", "Offset.1.Spline.1.end", continuity)
    sk["Offset.1.d"] = 2.5
    sk.Update()
    assert sk.redundant == []
    assert sk("Connect.1.end")[0].p == pytest.approx(sk("Offset.1.Spline.1.end")[0].p, abs=TOL)


def test_a_curvature_fillet_joins_a_line_and_an_offset_curve():
    sk = spline_and_offset()
    sk.Line((-5, -8), (-1, 1))
    sk.Constrain("Line.1.end", "Offset.1.Spline.1.start", px.Coincident)
    sk.Fillet("Line.1.end", 1, px.Curvature)
    assert sk.redundant == []


@pytest.mark.parametrize("continuity", [px.Tangent, px.Curvature])
def test_a_chain_through_a_connect_curve_offsets_as_one(continuity):
    sk = px.Sketch()
    sk.Move((40, 0)).Line((60, 0))
    sk.Move((70, 15)).Line((90, 15))
    sk.Connect("Line.1.end", "Line.2.start", continuity)
    dof = sk.dof
    sk.Offset(["Line.1", "Connect.1", "Line.2"], (2, (62, 9)), both_sides=False)
    assert sk.dof == dof
    assert "Offset.1.Connect.1" in [i for v in sk.elements.values() for i in v]


def spline_conversions():
    return sum(t["counts"] for t in px.debug.Timers() if t["name"] == "planex Spline FromCurve")


def test_a_drag_leaves_the_offset_s_shape_check_to_the_next_change():
    sk = spline_and_offset()
    px.debug.ResetTimers()
    for k in range(5):
        sk.Drag("Point.3", (20, 2.5 + 0.1 * k))
    assert sk("Point.3")[0].p == pytest.approx((20, 2.9), abs=TOL)
    assert spline_conversions() == 0
    sk.Constrain("Point.1", px.Fix)
    assert spline_conversions() > 0


def test_a_drag_that_gives_an_offset_curve_a_cusp_is_refused():
    sk = spline_and_offset()
    before = sk("Point.3")[0].p
    sk.Drag("Point.3", (20, -10))
    assert sk("Point.3")[0].p == pytest.approx(before, abs=TOL)
