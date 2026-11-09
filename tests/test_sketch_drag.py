# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import math
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-9


def polyline():
    sk = px.Sketch((0, 0), auto_constraint=False)
    sk.Line(20, 0).Line(0, 10).Line(20, 0)
    return sk


def hv_rectangle():
    sk = px.Sketch((0, 0), auto_constraint=False)
    sk.Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Vertical, id="right") \
        .Line(-4, 0, px.Horizontal, id="top").Close(px.Vertical, id="left")
    return sk


def point_on_circle(center_fixed=True):
    sk = px.Sketch(auto_constraint=False)
    sk.Circle((0, 0), 5).Vertex("p", (5, 0))
    sk.Constrain("Circle.1", px.Radius, 5)
    sk.Constrain("p", "Circle.1", px.Coincident)
    if center_fixed:
        sk.Constrain("Circle.1.center", px.Fix)
    return sk


def test_a_dragged_corner_of_a_free_polyline_moves_alone_and_its_two_lines_stretch():
    sk = polyline()
    sk.Drag("Line.2.end", (25, 15))
    assert [sk.Location(p) for p in ("Line.1.start", "Line.1.end", "Line.2.start", "Line.2.end", "Line.3.end")] == [
        pytest.approx(p, abs=TOL) for p in ((0, 0), (20, 0), (20, 0), (25, 15), (40, 10))]
    assert sk.Location("Line.3.start") == pytest.approx((25, 15), abs=TOL)


def test_a_dragged_corner_of_an_hv_rectangle_with_a_fixed_corner_changes_width_and_height():
    sk = hv_rectangle()
    sk.Constrain("bottom.start", px.Fix)
    dof = sk.dof
    sk.Drag("right.end", (6, 3))
    assert [sk.Location(p) for p in ("bottom.start", "bottom.end", "right.end", "top.end")] == [
        pytest.approx(p, abs=TOL) for p in ((0, 0), (6, 0), (6, 3), (0, 3))]
    assert sk.dof == dof and sk.status == "under"


def test_a_dragged_corner_of_an_hv_rectangle_without_a_fixed_corner_changes_its_size_the_opposite_corner_stays():
    sk = hv_rectangle()
    sk.Drag("right.end", (6, 3))
    assert [sk.Location(p) for p in ("bottom.start", "bottom.end", "right.end", "top.end")] == [
        pytest.approx(p, abs=TOL) for p in ((0, 0), (6, 0), (6, 3), (0, 3))]


def test_a_point_whose_h_is_fixed_follows_the_drag_only_in_v():
    sk = px.Sketch((0, 0), auto_constraint=False)
    sk.Line(10, 5)
    sk.Constrain("Line.1.end", px.FixH)
    sk.Drag("Line.1.end", (13, 8))
    assert sk.Location("Line.1.end") == pytest.approx((10, 8), abs=TOL)
    assert sk.Location("Line.1.start") == pytest.approx((0, 0), abs=TOL)


def test_a_point_on_a_fixed_circle_dragged_outside_goes_to_the_nearest_place_on_it():
    sk = point_on_circle()
    sk.Drag("p", (10, 10))
    s = 5 / math.sqrt(2)
    assert sk.Location("p") == pytest.approx((s, s), abs=TOL)
    assert sk.Location("Circle.1.center") == pytest.approx((0, 0), abs=TOL)
    assert sk.Geometry("Circle.1")["r"] == pytest.approx(5, abs=TOL)


def test_a_free_circle_comes_along_with_a_point_on_it():
    sk = point_on_circle(center_fixed=False)
    sk.Drag("p", (10, 10))
    assert sk.Location("p") == pytest.approx((10, 10), abs=TOL)
    cx, cy = sk.Location("Circle.1.center")
    assert math.hypot(10 - cx, 10 - cy) == pytest.approx(5, abs=TOL)


def test_a_line_dragged_by_both_ends_moves_its_neighbours_ends_only():
    sk = polyline()
    sk.Drag(["Line.2.start", "Line.2.end"], [(22, 2), (22, 12)])
    assert [sk.Location(p) for p in ("Line.1.start", "Line.1.end", "Line.3.start", "Line.3.end")] == [
        pytest.approx(p, abs=TOL) for p in ((0, 0), (22, 2), (22, 12), (40, 10))]


def test_a_drag_adds_no_constraint_and_no_warning():
    sk = hv_rectangle()
    before = repr(sk.constraints)
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        sk.Drag("right.end", (6, 3))
    assert caught == [] and repr(sk.constraints) == before


def test_constrain_after_a_drag_still_moves_the_drawn_shapes_along():
    sk = polyline()
    sk.Drag("Line.2.end", (25, 15))
    sk.Vertex("far", (100, 50)).Constrain("far", px.Fix)
    sk.Constrain("Line.1.start", "far", px.Coincident)
    assert [sk.Location(p) for p in ("Line.1.end", "Line.2.end", "Line.3.end")] == [
        pytest.approx(p, abs=1e-6) for p in ((120, 50), (125, 65), (140, 60))]


def test_drag_takes_points_only_and_one_place_per_point():
    sk = polyline()
    with pytest.raises(ValueError, match="follows the ends"):
        sk.Drag("Line.1.mid", (5, 5))
    with pytest.raises(ValueError, match="is no point"):
        sk.Drag("Line.1", (5, 5))
    with pytest.raises(ValueError, match="2 points, 1 places"):
        sk.Drag(["Line.1.end", "Line.2.end"], [(5, 5)])
    with pytest.raises(TypeError, match="no list of places"):
        sk.Drag(["Line.1.end", "Line.2.end"], (5, 5))
    assert sk.Location("Line.1.end") == pytest.approx((20, 0), abs=TOL)
