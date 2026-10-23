# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re
import time
import warnings

import pytest

import planex as px
from helpers import point

TOL = 1e-13
NO_CONSTRAINTS = r"a QuickSketch has no constraints; px\.Sketch\(\.\.\.\) has them"


def polygon(sk, n=5):
    sk.Move((1, 0))
    for k in range(1, n):
        sk.Line((math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)))
    return sk.Close()


def l_shape(make):
    return make((0, 0)).Line(1, 0).Line(0, 1)


def test_a_quick_sketch_is_a_sketch_without_constraints():
    sk = px.QuickSketch()
    assert not isinstance(sk, px.Sketch)
    assert repr(sk) == "QuickSketch(3 elements)"
    sk.Move((0, 0)).Line((1, 0))
    assert repr(sk) == "QuickSketch(6 elements)"
    assert type(sk.Copy()) is px.QuickSketch
    assert px.QuickSketch(id="plate").id == "plate"


@pytest.mark.constraints
def test_it_draws_what_a_constraint_sketch_draws():
    quick, full = polygon(px.QuickSketch()), polygon(px.Sketch())
    assert quick.elements == full.elements and quick.locations == full.locations
    assert quick.Face().mass == full.Face().mass


@pytest.mark.parametrize("draw", [lambda sk: sk.Line(1, 0, px.Horizontal), lambda sk: sk.Arc(1, 90, px.Tangent),
                                  lambda sk: sk.Circle(0.5, px.Fix), lambda sk: sk.Move((2, 2), px.Fix),
                                  lambda sk: sk.Close(px.Perpendicular),
                                  lambda sk: sk.Spline([(2, 1), (3, 0)], px.Fix)])
def test_flags_raise_and_draw_nothing(draw):
    sk = l_shape(px.QuickSketch)
    before = sk.locations
    with pytest.raises(ValueError, match=r"^px\.\w+: " + NO_CONSTRAINTS + "$"):
        draw(sk)
    assert sk.locations == before


@pytest.mark.parametrize("call", [lambda sk: sk.LinearPattern("Line.1", px.H, 3, 2.0, auto_constraint=True),
                                  lambda sk: sk.CircularPattern("Line.1", sk.origin, 3, auto_constraint=True),
                                  lambda sk: sk.Mirrored("Line.1", px.V, auto_constraint=True)])
def test_constraint_requests_raise_and_change_nothing(call):
    sk = l_shape(px.QuickSketch)
    before = (sk.locations, sk.elements)
    with pytest.raises(ValueError, match=NO_CONSTRAINTS):
        call(sk)
    assert (sk.locations, sk.elements) == before


def test_a_quick_sketch_has_none_of_the_constraint_calls():
    sk = l_shape(px.QuickSketch)
    for name in ("Constrain", "RigidGroup", "Expression", "Update", "__setitem__", "constraints", "expressions",
                 "dimensions", "auto_constraint", "mode", "jacobian"):
        assert not hasattr(sk, name), name


def test_patterns_and_mirrors_in_a_quick_sketch_tie_nothing():
    sk = px.QuickSketch((0, 0)).Line(1, 0, id="a")
    sk.LinearPattern("a", px.H, 3, 2.0)
    sk.Mirrored("a", px.V)
    assert len(sk.elements["Line"]) == 4 and "Pattern" not in sk.elements


def test_a_symmetry_line_in_a_quick_sketch_is_only_the_line():
    sk = px.QuickSketch((0, 0)).Line(1, 1, id="a")
    sk.Move((3, 0)).Line(-1, 1, id="b")
    sk.SymmetryLine("a", "b")
    assert sk.elements["Axis"] == ["H", "V", "SymmetryLine.1"]
    assert abs(sk._Geometry("SymmetryLine.1")["d"]) == pytest.approx(1.5, abs=TOL)


@pytest.mark.parametrize("make", [px.QuickSketch, pytest.param(px.Sketch, marks=pytest.mark.constraints, id="Sketch")])
def test_a_failed_call_keeps_the_chain_it_ended(make):
    sk = l_shape(make)
    with pytest.raises(ValueError, match="no element 'nothing'"):
        sk.Move("nothing")
    sk.Line(-1, 0).Finish(id_wire="w")
    assert sk.wires == {"w": ["Line.1", "Line.2", "Line.3"]}
    assert sk.Location("Line.3.end") == (0, 1)


def test_a_quick_sketch_runs_no_analysis():
    sk = l_shape(px.QuickSketch)
    assert {g["state"] for g in sk._Drawing()} == {"under"}
    for name in ("_Constraints", "_States", "status", "dof", "redundant", "condition"):
        assert not hasattr(sk, name), name
    assert "<svg" in sk._repr_svg_()


@pytest.mark.parametrize("start", [(1, 2), [1, 2], px.Vertex((1, 2))])
@pytest.mark.constraints
def test_a_point_first_starts_the_sketch_there(start):
    for make in (px.Sketch, px.QuickSketch):
        sk = make(start, dir=(0, 2))
        assert sk.pen.position == (1, 2) and sk.pen.direction == (0, 1)
        sk.Line(1)
        assert sk.Location("Line.1.end") == (1, 3)


def test_a_numpy_point_first():
    np = pytest.importorskip("numpy")
    assert px.QuickSketch(np.array([1.0, 2.0])).pen.position == (1, 2)


@pytest.mark.constraints
def test_the_start_point_takes_the_flags_of_move_to():
    sk = px.Sketch((1, 0), px.Fix)
    assert list(sk.constraints) == ["Fix"] and sk.dof == 0
    with pytest.raises(ValueError, match="px.Fix: " + NO_CONSTRAINTS):
        px.QuickSketch((1, 0), px.Fix)


@pytest.mark.constraints
def test_anything_else_first_is_the_plane():
    from netgen import occ

    axes = occ.Axes((0, 0, 2), n=occ.Z, h=occ.X)
    drawn = px.Sketch(axes).Move((0, 0)).Line(1, 0)
    for sk in (px.QuickSketch(axes), px.QuickSketch(drawn), px.Sketch(axes)):
        assert sk.ToXYZ((1, 2)) == pytest.approx((1, 2, 2), abs=TOL)
    assert px.QuickSketch(px.XY).ToXYZ((1, 2)) == pytest.approx((1, 2, 0), abs=TOL)
    assert px.QuickSketch(drawn).elements == {"Point": ["origin"], "Axis": ["H", "V"]}
    on = px.QuickSketch(axes).Move((1, 2))
    assert on.ToXYZ(on.pen.position) == pytest.approx((1, 2, 2), abs=TOL)


@pytest.mark.parametrize("args, kwargs, error, match", [
    (("origin",), {}, TypeError, "a string is no start point and no plane"),
    (((1, 2, 3),), {}, ValueError, "ToHV"),
    ((1, 2), {}, TypeError, "the start point is one argument"),
    ((None, True), {}, TypeError, r"starts at \(h, v\)"),
    ((px.XY, px.Fix), {}, TypeError, r"starts at \(h, v\)"),
    ((), {"dir": (0, 1)}, TypeError, "dir= goes with a start point"),
    (((0, 0),), {"dir": (0, 0)}, ValueError, "the direction has no length"),
])
@pytest.mark.constraints
def test_what_the_constructors_refuse(args, kwargs, error, match):
    for make in (px.Sketch, px.QuickSketch):
        with pytest.raises(error, match=match):
            make(*args, **kwargs)


def test_constraint_settings_need_the_constraints():
    with pytest.raises(ValueError, match=r"QuickSketch\(auto_constraint=True\)"):
        px.QuickSketch(auto_constraint=True)
    with pytest.raises(ValueError, match=r"QuickSketch\(mode='strict'\)"):
        px.QuickSketch(mode="strict")


def test_a_quick_rectangle_and_trim():
    sk = px.QuickSketch((0, 0)).Rectangle(2, 1)
    assert sk.Face().mass == pytest.approx(2, abs=TOL)
    sk.Move((1, 2)).Line(0, -3, id="post")
    sk.Trim("post")
    assert sk._Geometry("post")["end"] == pytest.approx((1, 0), abs=TOL)
    assert sk._Geometry("post.trimmed.1")["end"] == pytest.approx((1, -1), abs=TOL)


def test_vertex_properties_reach_the_export():
    sk = px.QuickSketch((0, 0)).Rectangle(2, 1)
    sk(kind="point", near=(2, 0)).name = "corner"
    named = {v.name: (v.p.x, v.p.y) for v in sk.ToOCCFace().vertices if v.name}
    assert named == {"corner": pytest.approx((2, 0), abs=TOL)}
    assert len(sk.Wires()) == 1 and sk.bounding_box == ((0, 0), (2, 1))


def test_a_helper_line_of_zero_length_draws_nothing():
    sk = px.QuickSketch((0, 0)).Line(2, 0, id="a")
    with pytest.warns(px.PlanexWarning, match="zero length move"):
        sk.Line(0, angle=45)
    assert sk.elements["Line"] == ["a"] and sk.pen.position == (2, 0)


def test_a_helper_call_costs_the_same_on_a_large_sketch():
    def per_call(n):
        sk = px.QuickSketch((0, 0))
        for _ in range(n):
            sk.Line(1, 0)
        sk.Move((0, 10)).Line(100, 0, id="ref")
        sk.Circle(0, 20, 1, id="hub")
        best = math.inf
        for _ in range(5):
            t = time.perf_counter()
            for k in range(20):
                sk.Move((k, 12)).Line("ref", perpendicular="ref")
                sk.Move((k, 14)).Line(tangent="hub")
                sk.Move((k, 15)).Line(2, parallel="ref")
            best = min(best, time.perf_counter() - t)
        return best

    assert per_call(10_000) < 3 * per_call(10)


@pytest.mark.parametrize("make", [px.QuickSketch, pytest.param(px.Sketch, marks=pytest.mark.constraints, id="Sketch")])
def test_construction_elements_draw_with_their_own_pen(make):
    sk = make((0, 0)).Line(4, 0, id="a").Line(1, 3, construction=True, id="c1").Line(0, 1, construction=True, id="c2")
    sk.Line(0, 2, id="b").Line(1, 0, construction=True, id="c3")
    ends = {i: (sk.Location(i + ".start"), sk.Location(i + ".end")) for i in ("a", "c1", "c2", "b", "c3")}
    assert ends == {"a": ((0, 0), (4, 0)), "c1": ((4, 0), (5, 3)), "c2": ((5, 3), (5, 4)), "b": ((4, 0), (4, 2)),
                    "c3": ((4, 2), (5, 2))}
    # the runs start on the pen's point and chain like a drawn chain: six points and the origin
    assert len(sk.elements["Point"]) == 7 and sk.pen.position == (4, 2)
    sk.Close(id="back")
    assert (sk.Location("back.start"), sk.Location("back.end")) == ((4, 2), (0, 0))
    assert sk.Face().mass == pytest.approx(4)


def test_a_move_ends_the_construction_run_and_turns_turn_both_pens():
    sk = px.QuickSketch((0, 0)).Line(1, 1, construction=True, id="c1").Move((5, 0)).Line(0, 1, construction=True, id="c2")
    assert sk.Location("c2.start") == (5, 0)
    sk = px.QuickSketch((0, 0)).Line(1, 0, construction=True, id="c").Direction(0, 1).Line(1, construction=True, id="d")
    sk.Line(2, id="n")
    assert (sk.Location("d.start"), sk.Location("d.end")) == ((1, 0), (1, 1))
    assert (sk.Location("n.start"), sk.Location("n.end")) == ((0, 0), (0, 2))


def test_construction_helpers_and_failures_keep_both_pens():
    sk = px.QuickSketch((0, 0)).Line(4, 0, id="base").Line(0, 2, construction=True, id="up")
    sk.Line("base", perpendicular="base", construction=True, id="down")
    assert (sk.Location("down.start"), sk.Location("down.end")) == ((4, 2), (4, 0))
    with pytest.raises(ValueError, match="the direction at the start runs along the chord"):
        sk.Arc((4, 3), tangent=(0, 1), construction=True)
    sk.Line(1, 0, construction=True, id="on").Line(0, 1, id="n")
    assert sk.Location("on.start") == (4, 0) and (sk.Location("n.start"), sk.Location("n.end")) == ((4, 0), (4, 1))


@pytest.mark.parametrize("draw", [
    lambda sk: sk.Move((math.nan, 0)),
    lambda sk: sk.Line(math.nan),
    lambda sk: sk.Line(1, math.inf),
    lambda sk: sk.Line((math.nan, 1)),
    lambda sk: sk.Arc(math.nan, 90),
    lambda sk: sk.Arc((1, math.nan), tangent=(0, 1)),
    lambda sk: sk.Circle(0, 0, math.nan),
    lambda sk: sk.Rectangle(math.nan, 1),
    lambda sk: sk.Spline([(2, 1), (math.nan, 2)]),
    lambda sk: sk.Spline([(2, 1), (3, 2)], tangents={0: (math.nan, 1)}),
    lambda sk: sk.Direction(math.nan, 1),
    lambda sk: sk.Turn(math.inf),
    lambda sk: sk.Vertex("p", (math.nan, 0)),
    lambda sk: sk.Line(math.nan, angle=30),
    lambda sk: sk.Line(1, math.nan, construction=True),
], ids=["moveto", "line-length", "line-dx-dy", "line-point", "arc", "arc-point", "circle", "rectangle", "spline",
        "spline-tangent", "direction", "rotate", "vertex", "helper", "construction"])
def test_numbers_that_are_not_finite_raise_and_change_nothing(draw):
    sk = px.QuickSketch((0, 0)).Line(1, 0)
    before = sk.elements
    with pytest.raises(ValueError, match="not finite"):
        draw(sk)
    assert sk.elements == before and sk.pen.position == (1, 0) and sk.pen.direction == (1, 0)
    with pytest.raises(ValueError, match="not finite"):
        px.QuickSketch((math.nan, 0))
    with pytest.raises(ValueError, match="not finite"):
        px.QuickSketch((0, 0), dir=(math.inf, 1))


def test_a_rectangle_side_within_rounding_of_zero_raises():
    sk = px.QuickSketch((0, 0))
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        with pytest.raises(ValueError, match="within rounding of 0"):
            sk.Rectangle(0, 1)
    assert "Line" not in sk.elements


def test_a_quick_sketch_plots_free_without_constraint_signs():
    pytest.importorskip("plotly")
    sk = px.QuickSketch((0, 0)).Rectangle(2, 1)
    assert {g["state"] for g in sk._Drawing() if g["kind"] == "Line"} == {"under"}
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        sk.Plot(backend="plotly")
        sk.Plot(show_constraints=False, backend="plotly")
        sk._repr_svg_()
    with pytest.warns(px.PlanexWarning, match="^a QuickSketch has no constraints, show_constraints is ignored$"):
        sk.Plot(show_constraints=True, backend="plotly")
    with pytest.warns(px.PlanexWarning, match="show_constraints is ignored"):
        sk.ToSVG(show_constraints=True)


def test_a_sketch_cuts_faces_and_wires():
    cutter = px.QuickSketch((1, -1)).Line(0, 3)
    parts = px.QuickSketch((0, 0)).Rectangle(2, 1).Face().Split(cutter)
    assert sorted(sum(f.mass for f in p.faces) for p in parts) == pytest.approx([1, 1])


def test_until_meets_lines_arcs_circles_and_splines_not_axes_or_points():
    sk = px.QuickSketch((-3, 0)).Vertex("p", (-1, 0)).Vertex("q", (0, 1)).SymmetryLine("p", "q")
    sk.Circle(4, 0, 1, construction=True).Move((-3, 0))
    assert sk.Line(until=px.first).pen.position == pytest.approx((3, 0), abs=1e-12)
    with pytest.raises(ValueError, match=r"^Arc\(1.0, until=px.first\): nothing ahead$"):
        px.QuickSketch((0, 0)).Line(1, 0).Arc(1, until=px.first)


def test_reverse_runs_an_arc_between_given_ends_clockwise():
    with pytest.raises(TypeError, match=r"Arc\(ccw=...\): ccw is gone"):
        px.QuickSketch((0, 0)).Arc(1, center=(0, 1), sweep=90, ccw=False)
    e = px.QuickSketch().Arc((1, 0), (0, 1), center=(0, 0), reverse=True).Wire().edges[-1]
    assert not e.edge.ccw and e.midpoint.x < 0 and e.midpoint.y < 0


def test_an_unknown_id_names_the_call():
    sk = px.QuickSketch((0, 0)).Line(1, 0, id="a")
    for call, text in [(lambda: sk.Line(tangent="zz"), "Line(tangent='zz')"),
                       (lambda: sk.Arc(1, center="zz"), "Arc(1.0, center='zz')"),
                       (lambda: sk.Trim("a", "zz"), "Trim(a='a', b='zz')"),
                       (lambda: sk.Split("zz"), "Split(a='zz')"),
                       (lambda: sk.SymmetryLine("a", "zz"), "SymmetryLine(a='a', b='zz')")]:
        with pytest.raises(ValueError, match="^" + re.escape(text + ": no element 'zz'") + "$"):
            call()
    for call, text in [(lambda: sk.Vertex("a"), "Vertex('a'): id 'a' is taken"),
                       (lambda: sk.Move("zz"), "Move('zz'): no element 'zz'"),
                       (lambda: sk.Edge("zz"), "Edge('zz'): no element 'zz'")]:
        with pytest.raises(ValueError, match="^" + re.escape(text) + "$"):
            call()


def test_move_takes_every_pen_move():
    sk = px.QuickSketch().Move((1, 2))
    assert point(sk.pen.position) == pytest.approx((1, 2))
    assert point(sk.Move(3, 4).pen.position) == pytest.approx((4, 6))
    assert point(sk.Move(5).pen.position) == pytest.approx((9, 6))
    sk.Line(1, id="a")
    assert point(sk.Move("a.start").pen.position) == pytest.approx((9, 6))
    assert point(sk.Move(2, dir=(0, 1)).pen.position) == pytest.approx((11, 6))
    assert point(sk.Move(1).pen.position) == pytest.approx((11, 7))
    assert point(px.Move((3, 4)).pen.position) == pytest.approx((3, 4))

