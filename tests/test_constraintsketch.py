# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

# solved coordinates are exact to Tol of the largest coordinate (here up to 10)
TOL = 1e-13


def rectangle():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0.3, px.Horizontal, id="bottom").Line(0.2, 2, px.Perpendicular) \
        .Line(-4, 0.1, px.Perpendicular, id="top").Close(px.Perpendicular)
    sk.Constrain("bottom", px.Distance, 4, id="w")
    sk.Constrain("bottom.start", "top", px.Distance, 2, id="h")
    return sk


def corners(sk):
    return [v for k in range(1, 5) for v in sk._Location(f"Point.{k}")]


def test_a_rectangle_from_flags_and_two_dimensions():
    sk = rectangle()
    assert corners(sk) == pytest.approx([0, 0, 4, 0, 4, 2, 0, 2], abs=TOL)
    assert (sk.status, sk.dof) == ("full", 0)
    assert sk.elements == {"Point": ["origin", "Point.1", "Point.2", "Point.3", "Point.4"],
                           "Line": ["bottom", "Line.1", "top", "Line.2"], "Axis": ["H", "V"], "Chain": ["Chain.1"]}
    assert set(sk._States().values()) == {"full"}


def test_constraints_print_like_the_call_that_made_them():
    c = rectangle().constraints
    assert list(c) == ["Coincident", "Horizontal", "Perpendicular", "Distance"]
    assert repr(c["Distance"]["w"]) == '"bottom", px.Distance, 4'
    assert repr(c["Distance"]["h"]) == '"bottom.start", "top", px.Distance, 2'
    assert repr(c["Perpendicular"]["Perpendicular.3"]) == '"top", "Line.2", px.Perpendicular'
    assert repr(c["Coincident"]["Coincident.1"]) == '"Point.1", "origin", px.Coincident'


def test_dimensions_change_with_update():
    sk = rectangle()
    sk["w"] = 6
    sk.Update({"h": 3})
    assert corners(sk) == pytest.approx([0, 0, 6, 0, 6, 3, 0, 3], abs=TOL)
    assert (sk["w"], sk["h"]) == (6, 3)


def test_set_values_take_effect_before_the_sketch_is_read():
    sk = rectangle()
    sk["w"] = 5
    assert sk["w"] == 5
    assert sk._Location("Point.2") == pytest.approx((5, 0), abs=TOL)


def test_an_update_without_solution_changes_nothing():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(3, 0, px.Horizontal, id="a").Line(-0.5, 3.8, id="b").Close(id="c")
    for line, length, name in [("a", 3, "A"), ("b", 4, "B"), ("c", 5, "C")]:
        sk.Constrain(line, px.Distance, length, id=name)
    before = [sk._Location(p) for p in ("Point.2", "Point.3")]
    assert before[1] == pytest.approx((3, 4), abs=TOL)
    with pytest.raises(ValueError, match=r"Update\(C = 10\): no solution, nothing changed"):
        sk.Update({"C": 10})
    assert [sk._Location(p) for p in ("Point.2", "Point.3")] == before
    assert sk["C"] == 5


def test_a_redundant_constraint_warns_and_is_listed():
    sk = rectangle()
    with pytest.warns(px.PlanexWarning, match=r"Parallel.1 \(\"bottom\", \"top\", px.Parallel\) is redundant \(consistent\) with "
                                              r"Perpendicular.1, Perpendicular.2"):
        sk.Constrain("bottom", "top", px.Parallel)
    assert sk.status == "full" and sk.dof == 0
    assert sk.redundant == ["Perpendicular.1", "Perpendicular.2", "Parallel.1"]
    assert sk._States()["bottom"] == "full"
    sk.Remove("Parallel.1")
    assert sk.status == "full" and sk.redundant == []


def test_a_contradiction_is_kept_suspended_and_changes_no_geometry():
    sk = rectangle()
    before = corners(sk)
    with pytest.warns(px.PlanexWarning, match=r'"bottom", px.Distance, 7: no solution'):
        sk.Constrain("bottom", px.Distance, 7)
    assert corners(sk) == before
    assert sk.status == "over" and "Distance.1" in sk.constraints["Distance"]


def test_a_drawing_call_whose_flag_fails_draws_nothing():
    sk = px.Sketch()
    with pytest.raises(ValueError, match="px.Tangent relates it to the element before it, this chain has none"):
        sk.Move((1, 1)).Line(3, px.Tangent)
    assert "Line" not in sk.elements
    sk.Line(3, 0)
    with pytest.raises(ValueError, match="cannot relate these elements"):
        sk.Line(0, 3, px.Tangent)
    assert sk.elements["Line"] == ["Line.1"]
    assert sk.pen.position == pytest.approx((4, 1))


def test_ids():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Line(0, 1, id="side")
    assert sk.elements["Line"] == ["Line.1", "side"]
    sk.Line(1, 0)
    assert sk.elements["Line"][-1] == "Line.2"
    with pytest.raises(ValueError, match="id 'side' is taken"):
        sk.Line(1, 1, id="side")
    for dotted in ("Line.1", "Line.9", "a.end"):
        with pytest.raises(ValueError, match=r"id '%s' contains '\.'" % dotted):
            sk.Line(1, 1, id=dotted)
    with pytest.raises(ValueError, match="no element 'nope'"):
        sk.Constrain("nope", px.Horizontal)
    with pytest.raises(ValueError, match="no such element or constraint"):
        sk.Remove("nope")
    with pytest.raises(ValueError, match="own axes and origin stay"):
        sk.Remove("H")


def test_values_and_their_errors():
    sk = rectangle()
    with pytest.raises(ValueError, match=r"'Horizontal.1' \(px.Horizontal\) has no value"):
        sk["Horizontal.1"]
    with pytest.raises(ValueError, match="no dimension 'nope'"):
        sk["nope"] = 1
    with pytest.raises(ValueError, match="a length or radius must be positive"):
        sk["w"] = -1
    with pytest.raises(ValueError, match="needs a value"):
        sk.Constrain("bottom", px.Distance)
    with pytest.raises(TypeError, match="flags .* come after"):
        sk.Move((0, 5)).Line(px.Horizontal, 4)


def test_coincident_points_share_their_place_until_it_is_removed():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0, px.Fix).Move((2.3, 0.4)).Line(1, 1)
    sk.Constrain("Line.1.end", "Line.2.start", px.Coincident)
    assert sk._Location("Line.2.start") == pytest.approx((2, 0), abs=TOL)
    sk.Remove("Coincident.1")
    sk.Constrain("Line.1.end", "Line.2.start", px.Distance, 0.5)
    assert math.dist(sk._Location("Line.1.end"), sk._Location("Line.2.start")) == pytest.approx(0.5, abs=TOL)


def test_a_point_on_the_v_axis():
    sk = px.Sketch()
    sk.Move((0.3, 1)).Line(2, 0)
    sk.Constrain("Line.1.start", px.V, px.Coincident)
    assert sk._Location("Line.1.start")[0] == pytest.approx(0, abs=TOL)
    assert repr(sk.constraints["Coincident"]["Coincident.1"]) == '"Line.1.start", px.V, px.Coincident'


def test_a_line_tangent_to_a_circle_and_ending_on_it():
    sk = px.Sketch()
    sk.Circle(0, 0, 1, px.Fix)
    sk.Move((0.1, 1.2)).Line(3, -0.1)
    sk.Constrain("Line.1.start", "Circle.1", px.Coincident)
    sk.Constrain("Line.1", "Circle.1", px.Tangent)
    # the start slides along the circle, the end along the line
    assert (sk.status, sk.dof) == ("under", 2)
    (x, y), (ex, ey) = sk._Location("Line.1.start"), sk._Location("Line.1.end")
    assert math.hypot(x, y) == pytest.approx(1, abs=TOL)
    assert x * (ex - x) + y * (ey - y) == pytest.approx(0, abs=TOL)


def test_a_fillet_drawn_with_flags():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(3.8, 0.2, px.Horizontal, id="bottom").Arc(0.9, 95, px.Tangent, id="fillet") \
        .Line(0.1, 2, px.Vertical, px.Tangent, id="right")
    sk.Constrain("fillet", px.Radius, 1, id="r")
    sk.Constrain("bottom", px.Distance, 4)
    sk.Constrain("right", px.Distance, 2)
    assert (sk.status, sk.dof) == ("full", 0)
    g = sk._Geometry("fillet")
    assert g["center"] == pytest.approx((4, 1), abs=TOL)
    assert sk._Location("right.end") == pytest.approx((5, 3), abs=TOL)


def test_removing_an_element_takes_its_constraints_and_unused_ends():
    sk = rectangle()
    sk.Move((5, 5)).Line(1, 0, px.Horizontal, id="lone")
    sk.Remove("lone")
    assert sk.elements["Point"] == ["origin", "Point.1", "Point.2", "Point.3", "Point.4"]
    assert list(sk.constraints["Horizontal"]) == ["Horizontal.1"]
    sk.Remove("top")
    assert "h" not in sk.constraints["Distance"]
    assert "Perpendicular.2" not in sk.constraints["Perpendicular"]
    assert sk.elements["Point"] == ["origin", "Point.1", "Point.2", "Point.3", "Point.4"]
    with pytest.raises(ValueError, match="ends there, remove it first"):
        sk.Remove("Point.2")


def test_a_negative_distance_switches_sides_once():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0, px.Fix).Move((0.5, 0.7)).Line(0.2, 0)
    sk.Constrain("Line.2.start", "Line.1", px.Distance, 1, id="d")
    assert sk._Location("Line.2.start") == pytest.approx((0.5, 1), abs=TOL)
    sk["d"] = -1
    for _ in range(2):
        sk.Update()
        assert sk._Location("Line.2.start") == pytest.approx((0.5, -1), abs=TOL)


def test_the_height_between_two_lines_made_parallel_by_the_corners_is_not_redundant():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0.3, px.Horizontal, id="bottom").Line(0.2, 2, px.Perpendicular) \
        .Line(-4, 0.1, px.Perpendicular, id="top").Close(px.Perpendicular)
    sk.Constrain("bottom", px.Distance, 4)
    sk.Constrain("bottom", "top", px.Distance, 2)
    assert (sk.status, sk.dof) == ("full", 0)
    assert corners(sk) == pytest.approx([0, 0, 4, 0, 4, 2, 0, 2], abs=TOL)


def test_a_distance_between_lines_that_are_not_parallel_holds_the_second_ones_middle():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, px.Fix, id="a").Move((0, 1)).Line(4, 0.6, id="b")
    sk.Constrain("a", "b", px.Distance, 2)
    (x0, y0), (x1, y1) = sk._Location("b.start"), sk._Location("b.end")
    assert (y0 + y1) / 2 == pytest.approx(2, abs=TOL)
    assert y1 - y0 == pytest.approx(0.6, abs=1e-12)
    assert (sk.status, sk.dof) == ("under", 3)
    sk.Constrain("a", "b", px.Parallel)
    assert y1 - y0 != 0
    assert sk._Location("b.end")[1] - sk._Location("b.start")[1] == pytest.approx(0, abs=TOL)
    assert sk.dof == 2


def test_the_pen_goes_on_along_what_was_drawn_last():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(3, 4, id="a").Line(5, id="b")
    assert sk.Location("b.end") == pytest.approx((6, 8), abs=TOL)
    sk.Arc(1, 90, id="c").Line(1, id="d")
    assert sk.Geometry("d")["angle"] == pytest.approx(math.degrees(math.atan2(4, 3)) + 90, abs=1e-12)
    sk.Spline([(9, 9), (10, 11)], id="e").Line(1, id="f")
    (x0, y0), (x1, y1) = sk.Geometry("e")["path"][-2:]
    assert sk.Geometry("f")["angle"] == pytest.approx(math.degrees(math.atan2(y1 - y0, x1 - x0)), abs=1)
    sk.Move((0, 0)).Line(2, 0).Line(0, 2).Close(id="back").Line(1, id="on")
    assert sk.Geometry("on")["angle"] == pytest.approx(-135, abs=1e-12)
    sk.Move((20, 0)).Line(1, 1).Rectangle(2, 1).Line(1, id="after")
    assert sk.Geometry("after")["angle"] == pytest.approx(45, abs=1e-12)
    sk.Move((30, 0)).Line(1, 0, id="g").Line(2, perpendicular="g", id="up").Line(1, id="past")
    assert sk.Geometry("past")["angle"] == pytest.approx(sk.Geometry("up")["angle"], abs=1e-12)
    assert abs(sk.Geometry("up")["angle"]) == pytest.approx(90, abs=1e-12)


def test_drawing_calls_take_what_the_turtle_sketch_takes():
    sk = px.Sketch()
    sk.Move((1, 2), dir=(0, 1)).Line(1, id="l")
    sk.Move((5, 5), dir=(-1, 0)).Line(2, id="m")
    assert sk.Location("l.end") == pytest.approx((1, 3), abs=TOL) and sk.Location("m.end") == pytest.approx((3, 5), abs=TOL)
    sk.Move((0, 0), dir=(1, 0)).Arc(1, 90, id="a")
    sk.Move((0, 8), dir=(1, 0)).Arc(2, 45, id="b")
    sk.Move((0, 5), dir=(1, 0)).Arc(sweep=90, center=(0, 4), id="c")
    assert sk.Geometry("a")["end"] == pytest.approx((1, 1), abs=TOL)
    assert (sk.Geometry("b")["r"], sk.Geometry("b")["sweep"]) == pytest.approx((2, 45))
    assert sk.Geometry("c")["end"] == pytest.approx((-1, 4), abs=TOL)
    with pytest.raises(TypeError, match="multiple values for argument 'sweep'"):
        sk.Arc(1, 2, sweep=3)
    with pytest.raises(ValueError, match="the direction has no length"):
        sk.Move((0, 0), dir=(0, 0))


def test_a_negative_radius_starts_the_arc_backwards_as_in_the_turtle_sketch():
    sk = px.Sketch()
    sk.Move((0, 0)).Arc(-1, 90, id="a").Line(1, id="on")
    g = sk.Geometry("a")
    assert g["end"] == pytest.approx((-1, -1), abs=TOL)
    assert g["center"] == pytest.approx((0, -1), abs=TOL) and g["sweep"] == pytest.approx(90)
    # the pen leaves along the arc's end
    assert sk.Geometry("on")["angle"] == pytest.approx(-90, abs=1e-12)


def test_strict_mode_exports_only_fully_constrained_sketches():
    sk = px.Sketch(mode="strict")
    assert sk.mode == "strict" and sk.error_on_branch_change
    sk.Move(sk.origin, px.Coincident).Line(4, 0.3, px.Horizontal, id="bottom").Line(0.2, 2, px.Perpendicular) \
        .Line(-4, 0.1, px.Perpendicular, id="top").Close(px.Perpendicular, id="left")
    sk.Vertex("P", (1, 1))
    for export in (sk.Face, sk.Shape, sk.Wire, sk.Wires, sk.Construction, sk.ToOCCFace, sk.ToOCCShape, sk.ToOCCWire, sk.ToOCCWires,
                   lambda: sk.Edge("top"), sk.ToShapely):
        with pytest.raises(ValueError, match=r"mode='strict' and the sketch is not fully constrained \(4 degrees of freedom; "
                                             r"free: bottom, Line.1, top, left, P\)"):
            export()
    # reading stays allowed
    assert sk.dof == 4 and sk.Measure("bottom")["length"] > 0 and sk.Location("top.end")
    assert "<svg" in sk.ToSVG()
    sk.Constrain("bottom", px.Distance, 4)
    sk.Constrain("bottom", "top", px.Distance, 2)
    sk.Constrain("P", px.Fix)
    # a consistent redundancy is no reason to refuse
    with pytest.warns(px.PlanexWarning, match="redundant"):
        sk.Constrain("bottom", "top", px.Parallel)
    assert sk.Face().mass == pytest.approx(8, abs=1e-12)


def test_the_mode_can_change():
    sk = px.Sketch(error_on_branch_change=False, mode="strict")
    assert not sk.error_on_branch_change
    sk.Move((0, 0)).Line(1, 0)
    sk.mode = "normal"
    assert sk.Face is not None and len(sk.Wires()) == 1
    sk.mode = "strict"
    assert sk.error_on_branch_change
    with pytest.raises(ValueError, match="not fully constrained"):
        sk.Wires()
    with pytest.raises(ValueError, match="'normal' or 'strict'"):
        sk.mode = "lenient"


def recorded_warnings(call):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        call()
    return [str(w.message) for w in caught]


@pytest.mark.parametrize("auto", [False, True])
@pytest.mark.parametrize("ang, center", [(360, (0, 1)), (-360, (0, -1)), (math.nextafter(360.0, 0.0), (0, 1)), (720, (0, 1))])
def test_a_full_turn_is_one_circle_through_the_pen(auto, ang, center):
    sk = px.Sketch(auto_constraint=auto)
    sk.Move((0, 0))
    assert recorded_warnings(lambda: sk.Arc(1, ang)) == []
    assert sk.elements["Circle"] == ["Circle.1"] and "Arc" not in sk.elements
    assert sk._Geometry("Circle.1")["center"] == pytest.approx(center, abs=TOL)
    assert sk.pen.position == (0, 0) and sk.pen.direction == (1, 0)
    [edge] = sk.Face().edges
    assert isinstance(edge.edge, px.CircleEdge) and edge.edge.ccw == (ang > 0)
    assert tuple(edge.start) == pytest.approx((0, 0), abs=TOL)
    assert list(sk.constraints) == ["Coincident"]
    assert sk.Face().mass == pytest.approx(math.pi, rel=1e-12)


@pytest.mark.parametrize("r", [1, -1])
def test_a_full_turn_after_a_line_is_tangent_to_it_at_the_line_end(r):
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0, id="l")
    assert recorded_warnings(lambda: sk.Arc(r, 360)) == []
    assert sk._Geometry("Circle.1")["center"] == pytest.approx((2, r), abs=TOL)
    assert sorted(sk.constraints) == ["Coincident", "Tangent"]
    assert sk.pen.position == (2, 0) and sk.pen.direction == (1, 0)
    sk.Line(1, 0, id="m")
    assert sk.Location("m.start") == (2, 0)


def test_the_circle_of_a_full_turn_starts_where_its_pen_point_is_solved():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(2, 0, px.Horizontal, id="l").Arc(1, 360)
    sk.Constrain("l", px.Distance, 5)
    sk.Constrain("Circle.1", px.Radius, 1)
    assert sk.dof == 0
    assert sk._Geometry("Circle.1")["center"] == pytest.approx((5, 1), abs=TOL)
    [edge] = sk.Edge("Circle.1").edges
    assert tuple(edge.start) == pytest.approx((5, 0), abs=TOL)


def test_a_full_turn_takes_the_drawing_flags_and_an_id():
    sk = px.Sketch()
    sk.Move((0, 0))
    assert recorded_warnings(lambda: sk.Arc(1, 360, px.Fix)) == []
    assert sk.dof == 0 and len(sk.constraints["Fix"]) == 2
    named = px.Sketch().Arc(1, 360, id="hole")
    assert named.elements["Circle"] == ["hole"]
