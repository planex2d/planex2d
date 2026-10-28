# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

TOL = 1e-12


def recorded(call):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        call()
    return [str(w.message) for w in caught if issubclass(w.category, px.PlanexWarning)]


def fourbar():
    """crank, coupler and rocker on a fixed ground, the crank's angle to the ground as input"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1.0, 0.3, id="crank")
    sk.Line(3.2, 1.0, id="coupler")
    sk.Line(-0.2, -1.3, id="rocker")
    sk.Move("crank.start", px.Coincident).Line(4.0, 0.0, id="ground", construction=True)
    sk.Constrain("ground.end", px.Fix)
    sk.Constrain("rocker.end", "ground.end", px.Coincident)
    sk.Constrain("crank", px.Distance, 1, id="a")
    sk.Constrain("coupler", px.Distance, 3.5, id="b")
    sk.Constrain("rocker", px.Distance, 3, id="c")
    sk.Constrain("ground", "crank", px.Angle, 20, id="phi")
    return sk


def direction(sk, line):
    g = sk.Geometry(line)
    return g["angle"]


@pytest.mark.constraints
def test_a_fixed_line_keeps_its_ends_when_one_is_merged_into_an_older_point():
    sk = px.Sketch()
    sk.Move((3, 0.5)).Line(1, 1, id="older")
    sk.Move((0, 0)).Line(4, 0, px.Fix, id="ground")
    sk.Constrain("older.end", "ground.end", px.Coincident)
    assert sk.Location("ground.end") == pytest.approx((4, 0), abs=TOL)
    assert sk.Location("older.end") == pytest.approx((4, 0), abs=TOL)


@pytest.mark.constraints
def test_a_fixed_ground_stays_while_the_crank_turns():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1.0, 0.3, id="crank").Line(3.2, 1.0, id="coupler").Line(-0.2, -1.3, id="rocker")
    assert recorded(lambda: sk.Move("crank.start", px.Coincident).Line(4.0, 0.0, px.Fix, id="ground", construction=True)) == [
        "Fix.2 makes Fix.1 redundant (consistent); remove it to keep the sketch minimal"]
    assert sk.redundant == ["Fix.1"]
    sk.Constrain("rocker.end", "ground.end", px.Coincident)
    for name, value in (("crank", 1), ("coupler", 3.5), ("rocker", 3)):
        sk.Constrain(name, px.Distance, value)
    sk.Constrain("ground", "crank", px.Angle, 20, id="phi")
    for k in range(1, 73):
        sk["phi"] = 20 + 5 * k
        sk.Update()
    assert sk.Location("ground.end") == pytest.approx((4, 0), abs=TOL)


@pytest.mark.constraints
def test_an_angle_holds_its_branch_and_is_exact():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0, px.Fix, id="base").Move((0, 0)).Line(1, 0.6, id="arm")
    sk.Constrain("base", "arm", px.Angle, 30, id="lift")
    assert direction(sk, "arm") == pytest.approx(30, abs=1e-10)
    sk.Update({"lift": 200})
    assert direction(sk, "arm") == pytest.approx(-160, abs=1e-10)
    # drawn the other way: the angle counts on that branch (the arm points away from it)
    back = px.Sketch()
    back.Move((0, 0)).Line(2, 0, px.Fix, id="base").Move((1, 0.6)).Line(-1, -0.6, id="arm")
    back.Constrain("base", "arm", px.Angle, 30, id="lift")
    assert direction(back, "arm") == pytest.approx(-150, abs=1e-10)


@pytest.mark.constraints
def test_a_big_update_walks_there_on_its_branch():
    swept = fourbar()
    for k in range(1, 37):
        swept.Update({"phi": 20 + 5 * k})
    jumped = fourbar()
    jumped.Update({"phi": 200})
    assert jumped.Location("coupler.end") == pytest.approx(swept.Location("coupler.end"), abs=1e-9)
    stepped = fourbar()
    stepped.Update({"phi": 200}, steps=8)
    assert stepped.Location("coupler.end") == pytest.approx(swept.Location("coupler.end"), abs=1e-9)


@pytest.mark.constraints
def test_nearly_singular_sketches_warn():
    sk = px.Sketch()
    sk.Vertex("A", (0, 0))
    sk.Vertex("B", (2 - 1e-8, 0))
    sk.Vertex("P", (1, 0.01))
    for p in ("A", "B"):
        sk.Constrain(p, px.Fix)
    sk.Constrain("P", "A", px.Distance, 1)
    assert sk.condition < 1e3
    with pytest.warns(px.PlanexWarning, match="nearly singular"):
        sk.Constrain("P", "B", px.Distance, 1)
    assert sk.condition > 1e3
    # only the constraint that made it so warns
    sk.Vertex("Q", (5, 5))
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Constrain("Q", px.Fix)
    assert fourbar().condition < 100


@pytest.mark.constraints
def test_a_rigid_group_keeps_its_shape_and_moves_as_one():
    sk = px.Sketch()
    # a slender triangle: lengths alone could flip it
    sk.Move((0, 0)).Line(4, 0, id="long").Line(-2, 0.05, id="b").Close(id="c")
    sides = [sk.Geometry(i)["length"] for i in ("long", "b", "c")]
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.RigidGroup("Chain.1", id="body")
    assert (sk.status, sk.dof) == ("under", 3)
    sk.Constrain("long.start", px.Fix)
    sk.Constrain(px.H, "long", px.Angle, 0, id="turn")
    assert (sk.status, sk.dof) == ("full", 0)
    sk.Update({"turn": 170})
    assert [sk.Geometry(i)["length"] for i in ("long", "b", "c")] == pytest.approx(sides, abs=1e-12)
    assert direction(sk, "long") == pytest.approx(170, abs=1e-10)
    tip = sk.Location("b.end")
    assert tip[1] < sk.Location("b.start")[1]
    assert repr(sk.constraints["RigidGroup"]["body"]).endswith("px.RigidGroup")
    sk.Remove("body")
    # its three points, less the fixed one and the angle
    assert sk.dof == 3


@pytest.mark.constraints
def test_reference_dimensions_measure():
    sk = fourbar()
    sk.Constrain("coupler.end", "crank.start", px.Distance, id="span", reference=True)
    sk.Constrain("ground", "rocker", px.Angle, id="rock", reference=True)
    dof = sk.dof
    assert dof == 0 and "span" in sk.dimensions and "rock" in sk.dimensions
    assert sk["span"] == pytest.approx(math.dist(sk.Location("coupler.end"), sk.Location("crank.start")), abs=TOL)
    sk["phi"] = 60
    assert sk["span"] == pytest.approx(math.dist(sk.Location("coupler.end"), sk.Location("crank.start")), abs=TOL)
    assert repr(sk.constraints["Distance"]["span"]) == '"coupler.end", "crank.start", px.Distance, reference=True'
    with pytest.raises(ValueError, match="a reference dimension measures, it is not set"):
        sk["span"] = 3
    with pytest.raises(ValueError, match="a reference measures, it takes no value"):
        sk.Constrain("crank", px.Distance, 2, reference=True)
    assert f"(span = {sk['span']:g})" in sk.ToSVG(show_constraints=True)


@pytest.mark.constraints
def test_reading_the_solved_geometry():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, id="base").Arc(1, 90, id="bend")
    sk.Circle((1, 2), 0.5, id="bore")
    sk.Move((0, 3)).Spline([(1, 3.5), (2, 3)], id="wave")
    sk.Constrain("base", px.Distance, 4, id="w")
    where = sk.locations
    for ref in ("origin", "Point.1", "base.start", "base.end", "base.mid", "bend.center", "bend.end", "bore.center",
                "wave.start", "wave.mid", "wave.p1", "wave.p2"):
        assert ref in where
    assert where["base.mid"] == pytest.approx((2, 0), abs=TOL) and sk.Location("bore.center") == (1, 2)
    g = sk.Geometry("base")
    assert (g["length"], g["angle"]) == pytest.approx((4, 0))
    assert sk.Geometry("bend")["sweep"] == pytest.approx(90)
    assert sk.dimensions == {"w": pytest.approx(4)}


def test_no_warning_for_edges_of_no_length():
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        px.Spline([(0, 0), (350, 18), (750, 10), (1150, -60)]).Offset(12)


def pulled_through(**options):
    """a line whose end is pulled back past its start: it turns round on the way"""
    sk = px.Sketch(**options)
    sk.Move((0.5, 0), px.Fix).Line(0.5, 0, id="l")
    sk.Constrain("l", px.Horizontal)
    sk.Constrain("l.end", px.V, px.Distance, 1, id="x")
    return sk


@pytest.mark.constraints
def test_turning_round_in_an_update_warns_or_is_refused():
    sk = pulled_through()
    with pytest.warns(px.PlanexWarning, match=r"Update\(x = 0.1\): l turned round \(180 degrees in one smallest step\)"):
        sk.Update({"x": 0.1})
    assert direction(sk, "l") == pytest.approx(180)
    strict = pulled_through(error_on_branch_change=True)
    assert strict.error_on_branch_change
    with pytest.raises(ValueError, match="l turned round .* nothing changed \\(error_on_branch_change\\)"):
        strict.Update({"x": 0.1})
    assert strict["x"] == 1 and strict.Location("l.end") == pytest.approx((1, 0), abs=TOL)


@pytest.mark.constraints
def test_a_constraint_that_turns_a_line_round_warns_or_is_refused():
    for strict in (False, True):
        sk = px.Sketch(error_on_branch_change=strict)
        sk.Move((0, 0), px.Fix).Line(1, 0, id="l")
        sk.Constrain("l", px.Distance, 1)
        sk.Vertex("P", (-0.6, 0.8))
        sk.Constrain("P", px.Fix)
        if strict:
            with pytest.raises(ValueError, match=r"l turned round \(127 degrees\): the solution is not the branch drawn"):
                sk.Constrain("l.end", "P", px.Coincident)
            assert "Coincident" not in sk.constraints and direction(sk, "l") == pytest.approx(0, abs=1e-12)
        else:
            with pytest.warns(px.PlanexWarning, match=r"l turned round \(127 degrees\)"):
                sk.Constrain("l.end", "P", px.Coincident)
            assert sk.Location("l.end") == pytest.approx((-0.6, 0.8), abs=TOL)


@pytest.mark.constraints
def test_a_growing_ring_keeps_its_bus_outside():
    """big steps of the radius would put the bus waveguide inside the ring: the steps are halved instead"""
    sk = px.Sketch(error_on_branch_change=True)
    sk.Circle((0, 0), 10.25, id="ro")
    sk.Constrain("ro.center", "origin", px.Coincident)
    sk.Constrain("ro", px.Radius, 10.25, id="Ro")
    sk.Move((-15, -11.25)).Rectangle(30, 0.5)
    bottom, _, top, _ = sk.wires["Rectangle.1"]
    sk.Constrain(bottom, px.Horizontal)
    sk.Constrain(bottom, top, px.Distance, 0.5)
    sk.Constrain(bottom, px.Distance, 30)
    sk.Constrain(bottom + ".mid", px.V, px.Distance, 0)
    sk.Move((0, -10.25)).Line(0, -0.25, id="gap", construction=True)
    sk.Constrain("gap.start", "ro", px.Coincident)
    sk.Constrain("gap.end", top, px.Coincident)
    sk.Constrain("ro.center", "gap", px.Coincident)
    sk.Constrain("gap", top, px.Perpendicular)
    sk.Constrain("gap", px.Distance, 0.18, id="g")
    sk.Update({"Ro": 20.25}, steps=40)
    assert direction(sk, "gap") == pytest.approx(-90, abs=1e-9)
    assert sk.Location(top + ".mid") == pytest.approx((0, -20.43), abs=1e-9)


@pytest.mark.constraints
def test_fixing_an_arc_keeps_it():
    sk = px.Sketch()
    sk.Move((1, 0.5)).Arc(0.8, 100, id="a")
    drawn = sk.Geometry("a")
    sk.Constrain("a", px.Fix)
    assert sk.dof == 0
    fixed = sk.Geometry("a")
    assert fixed.keys() == drawn.keys()
    for key in drawn:
        assert fixed[key] == pytest.approx(drawn[key], abs=TOL)


@pytest.mark.constraints
def test_a_rigid_group_that_takes_dof_is_not_redundant():
    sk = px.Sketch()
    sk.Move((1, 1)).Rectangle(3, 2, id="r")
    assert sk.dof == 5
    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter("always")
        sk.RigidGroup("r", id="G")
    assert sk.dof == 3
    assert sk.redundant == ["r.Perpendicular.1", "r.Perpendicular.2", "r.Perpendicular.3"]
    assert not [x for x in w if str(x.message).startswith("G (") and "is redundant" in str(x.message)]


@pytest.mark.constraints
def test_fixes_that_hold_geometry_are_not_redundant():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(10, 0, px.Fix, id="a")
    sk.Move((0, 2)).Line(10, 0, px.Fix, id="b")
    sk.Circle((5, 1.1), 0.9, id="c")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("c", "a", px.Tangent)
        sk.Constrain("c", "b", px.Tangent)
        sk.Constrain("c", px.Radius, 1)
    assert sk.dof == 1
    assert sk.redundant == ["Tangent.1", "Tangent.2", "Radius.1"]


@pytest.mark.constraints
def test_duplicate_constraints_stay_redundant():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 0, id="l")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("l", px.Horizontal)
        sk.Constrain("l", px.Horizontal)
    assert (sk.dof, sk.redundant) == (3, ["Horizontal.1", "Horizontal.2"])


def fixed_line():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(2, 1, id="l")
    sk.Constrain("l.start", px.Fix)
    return sk


@pytest.mark.constraints
def test_a_fix_of_a_whole_line_names_the_fix_of_its_start_it_made_redundant():
    sk = fixed_line()
    assert recorded(lambda: sk.Constrain("l", px.Fix)) == [
        "Fix.2 makes Fix.1 redundant (consistent); remove it to keep the sketch minimal"]
    assert sk.redundant == ["Fix.1"] and sk.dof == 0


@pytest.mark.constraints
def test_a_rigid_group_names_the_constraints_it_made_redundant_once():
    sk = px.Sketch()
    sk.Move((1, 1)).Rectangle(3, 2, id="r")
    assert recorded(lambda: sk.RigidGroup("r", id="G")) == [
        "G makes r.Perpendicular.1, r.Perpendicular.2, r.Perpendicular.3 redundant (consistent); remove them to keep the sketch "
        "minimal"]
    later = recorded(lambda: (sk.Constrain("r.Line.1.start", px.Fix), sk.Constrain(px.H, "r.Line.1", px.Angle, 30, id="turn"),
                              sk.Update({"turn": 45})))
    assert not [m for m in later if "redundant" in m]
    assert sk["turn"] == 45 and sk.dof == 0
    assert sk.redundant == ["r.Perpendicular.1", "r.Perpendicular.2", "r.Perpendicular.3"]


@pytest.mark.constraints
def test_a_constraint_that_is_redundant_itself_gives_only_that_warning():
    sk = fixed_line()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("l", px.Fix)
    messages = recorded(lambda: sk.Constrain("l.end", px.Fix))
    assert len(messages) == 1
    assert messages[0].startswith("Fix.3 (") and messages[0].endswith(") is redundant (consistent) with Fix.1, Fix.2")
    assert sk.redundant == ["Fix.1", "Fix.2", "Fix.3"]


@pytest.mark.constraints
def test_building_a_32_gon_constraint_by_constraint_is_fast():
    import time

    n = 32
    sk = px.Sketch()
    t = time.perf_counter()
    sk.Move((1, 0))
    for k in range(1, n):
        a = 2 * math.pi * k / n
        sk.Line((math.cos(a), math.sin(a)))
    sk.Close()
    for k in range(1, n):
        sk.Constrain(f"Line.{k}", f"Line.{k + 1}", px.Equal)
    assert time.perf_counter() - t < 2.0


@pytest.mark.parametrize("x0", [0.0, 1000.0])
@pytest.mark.constraints
def test_short_lines_far_from_the_origin_take_parallel_and_angle(x0):
    s = 1e-4
    sk = px.Sketch()
    sk.Move((x0, 0)).Line(s, 0, px.Horizontal, id="a").Line(0.1 * s, s, id="r").Line(s, 0.2 * s, id="tail")
    sk.Constrain("a", "tail", px.Parallel)
    sk.Constrain("a", "r", px.Angle, 60)
    g = {k: sk.Geometry(k)["angle"] for k in ("a", "r", "tail")}
    assert math.sin(math.radians(g["tail"] - g["a"])) == pytest.approx(0, abs=1e-9)
    assert math.cos(math.radians(g["r"] - g["a"])) == pytest.approx(0.5, abs=1e-9)


@pytest.mark.constraints
def test_a_strict_sketch_plots_while_it_can_still_move():
    pytest.importorskip("matplotlib")
    import matplotlib
    matplotlib.use("Agg")
    sk = px.Sketch(mode="strict")
    sk.Move((0, 0)).Rectangle(2, 1)
    assert sk.dof > 0
    ax = px.Plot(sk)
    assert sum(len(c.get_segments()) for c in ax.collections) == 4
    with pytest.raises(ValueError, match="mode='strict'"):
        sk.Wires()
