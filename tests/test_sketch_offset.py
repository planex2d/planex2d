# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re
import warnings

import pytest

import planex as px

REL = 1e-12


def rectangle(make=px.Sketch):
    sk = make((0, 0))
    right_angles = (px.Perpendicular,) if make is px.Sketch else ()
    sk.Line(10, 0).Line(0, 5, *right_angles).Line(-10, 0, *right_angles).Close(*right_angles, id_wire="r")
    return sk


def rounded(make=px.Sketch):
    """14 x 9, corners rounded with r = 2: area 110 + 4 pi, perimeter 30 + 4 pi"""
    sk = make((0, 0))
    sk.Move((2, 0)).Line(10, 0).Arc(2, 90).Line(0, 5).Arc(2, 90).Line(-10, 0).Arc(2, 90).Line(0, -5).Arc(2, 90)
    sk.Finish(id_wire="rr")
    return sk


def area(sk, id):
    return px.Face([sk.Wire(id)]).mass


def edges_of(sk, id):
    return sorted(i.local for i in sk(id) if i.kind != "point")


@pytest.mark.constraints
def test_the_outer_offset_of_a_rectangle_is_the_exact_offset():
    sk = rectangle()
    dof = sk.dof
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Offset("r", 1, id="wall")
    assert sk.elements["Offset"] == ["wall"]
    edges = sorted(i.local for i in sk("wall") if i.kind != "point")
    assert edges == sorted(f"wall.{p}" for k in range(1, 5) for p in (f"Line.{k}", f"Line.{k}.corner"))
    assert "wall.outer" not in sk.wires
    assert area(sk, "wall") == pytest.approx(50 + 30 + math.pi, rel=REL)
    assert area(sk, "wall") == pytest.approx(px.Face([sk.Wire("r")]).Offset(1).mass, rel=REL)
    assert sk.Wire("wall").mass == pytest.approx(30 + 2 * math.pi, rel=REL)
    assert (sk.dof, sk.redundant) == (dof, [])
    assert sk["wall.d"] == 1
    assert "Offset" not in sk.constraints
    assert sk._Geometry("wall.Line.1.corner")["center"] == pytest.approx((10, 0), abs=1e-12)


@pytest.mark.constraints
def test_the_offset_follows_the_source_and_its_distance():
    sk = rectangle()
    sk.Constrain("Line.1", px.Distance, 10, id="l")
    sk.Constrain("Line.2", px.Distance, 5, id="w")
    sk.Offset("r", 1, id="wall")
    sk.Update({"l": 12, "w": 4, "wall.d": 2})
    assert area(sk, "wall") == pytest.approx(48 + 2 * 16 * 2 + 4 * math.pi, rel=REL)
    assert area(sk, "wall") == pytest.approx(px.Face([sk.Wire("r")]).Offset(2).mass, rel=REL)


@pytest.mark.constraints
def test_an_inner_offset_meets_at_sharp_corners():
    sk = rectangle()
    sk.Constrain("Line.1", px.Distance, 10)
    sk.Constrain("Line.2", px.Distance, 5)
    sk.Offset("r", -1)
    assert sorted(i.local for i in sk("Offset.1") if i.kind != "point") == [f"Offset.1.Line.{k}" for k in range(1, 5)]
    assert area(sk, "Offset.1") == pytest.approx(8 * 3, rel=REL)
    sk["Offset.1.d"] = 1.5
    assert area(sk, "Offset.1") == pytest.approx(7 * 2, rel=REL)
    assert area(sk, "Offset.1") == pytest.approx(px.Face([sk.Wire("r")]).Offset(-1.5).mass, rel=REL)
    assert sk.redundant == []


@pytest.mark.constraints
def test_a_clockwise_chain_keeps_outer_outside():
    sk = px.Sketch((0, 0))
    sk.Line(0, 5).Line(10, 0).Line(0, -5).Close(id_wire="cw")
    sk.Offset("cw", 1, id="out")
    sk.Offset("cw", -1, id="in")
    assert area(sk, "out") == pytest.approx(50 + 30 + math.pi, rel=REL)
    assert area(sk, "in") == pytest.approx(24, rel=REL)


@pytest.mark.parametrize("d, expected", [(1, 140 + 9 * math.pi), (-1, 80 + math.pi)])
@pytest.mark.constraints
def test_a_rounded_rectangle_keeps_its_arcs_concentric(d, expected):
    sk = rounded()
    dof = sk.dof
    sk.Offset("rr", d, id="o")
    side = "o"
    assert sorted(i.local for i in sk("o") if i.kind != "point") == sorted(f"o.{e}" for k in range(1, 5) for e in (f"Line.{k}", f"Arc.{k}"))
    assert area(sk, "o") == pytest.approx(expected, rel=REL)
    assert area(sk, "o") == pytest.approx(px.Face([sk.Wire("rr")]).Offset(d).mass, rel=REL)
    for k in range(1, 5):
        assert sk._Geometry(f"{side}.Arc.{k}")["center"] == sk._Geometry(f"Arc.{k}")["center"]
        assert sk._Geometry(f"{side}.Arc.{k}")["r"] == pytest.approx(2 + d, rel=REL)
    assert (sk.dof, sk.redundant) == (dof, [])


@pytest.mark.constraints
def test_an_offset_of_an_offset():
    sk = rectangle()
    sk.Offset("r", 1, id="wall")
    sk.Offset("wall", 1, id="skin")
    assert area(sk, "skin") == pytest.approx(50 + 60 + 4 * math.pi, rel=REL)
    sk["wall.d"] = 2
    assert area(sk, "skin") == pytest.approx(px.Face([sk.Wire("r")]).Offset(3).mass, rel=REL)
    assert sk.redundant == []


@pytest.mark.constraints
def test_a_circle_gets_a_concentric_circle():
    sk = px.Sketch()
    sk.Circle((0, 0), 3, id="c")
    sk.Constrain("c", px.Radius, 3, id="rc")
    sk.Offset("c", 1, id="ring")
    sk.Offset("c", -1, id="core")
    assert [i.local for i in sk("ring") if i.kind != "point"] == ["ring.c"]
    assert sk._Geometry("ring.c")["r"] == pytest.approx(4, rel=REL)
    sk.Update({"rc": 5})
    assert sk._Geometry("ring.c")["r"] == pytest.approx(6, rel=REL)
    assert sk._Geometry("core.c")["r"] == pytest.approx(4, rel=REL)
    assert sk._Geometry("core.c")["center"] == sk._Geometry("c")["center"]
    with pytest.raises(ValueError, match="nothing is left inside"):
        sk.Offset("c", -5)


@pytest.mark.constraints
def test_the_distance_is_a_dimension_for_expressions():
    sk = rectangle()
    sk.Constrain("Line.1", px.Distance, 10)
    sk.Constrain("Line.2", px.Distance, 5, id="w")
    sk.Offset("r", 1, id="wall")
    sk.Expression("wall.d = w/5")
    sk.Update({"w": 10})
    assert sk["wall.d"] == pytest.approx(2, rel=REL)
    assert area(sk, "wall") == pytest.approx(100 + 2 * 20 * 2 + 4 * math.pi, rel=REL)


@pytest.mark.constraints
def test_the_distance_stays_positive():
    sk = rectangle()
    sk.Offset("r", 1, id="wall")
    with pytest.raises(ValueError, match="positive"):
        sk["wall.d"] = -1
    with pytest.raises(ValueError, match="must not be 0"):
        sk.Offset("r", 0)


@pytest.mark.constraints
def test_a_quicksketch_makes_the_same_elements_without_ties():
    sk, qs = rectangle(), rectangle(px.QuickSketch)
    sk.Offset("r", 1, id="wall")
    qs.Offset("r", 1, id="wall")
    assert qs.elements.offsets == sk.elements.offsets == ["wall"]
    assert "wall.outer" not in qs.wires and sorted(qs.wires["wall"]) == edges_of(sk, "wall")
    assert [i.local for i in qs("wall")] == [i.local for i in sk("wall")]
    for e in qs.wires["wall"]:
        for end in ("start", "end"):
            assert qs._Geometry(e)[end] == pytest.approx(sk._Geometry(e)[end], abs=1e-12)
    assert area(qs, "wall") == pytest.approx(area(sk, "wall"), rel=REL)
    qs.Remove("wall")
    with pytest.raises(ValueError, match="no element 'wall'"):
        qs("wall")


@pytest.mark.constraints
def test_the_sides_and_the_whole_offset_are_selections():
    sk = rectangle()
    sk.Offset("r", 1, id="wall")
    made = sorted(i.local for i in sk("wall"))
    assert len(made) == 16
    assert [i.local for i in sk("wall.Line.1")] == ["wall.Line.1", "wall.Line.1.corner"]


@pytest.mark.constraints
def test_the_offset_takes_the_properties_of_its_source():
    sk = px.Sketch((0, 0))
    sk.Line(10, 0, name="floor", maxh=0.5).Line(0, 5, name="side").Line(-10, 0, name="roof")
    sk.Close(name="back", id_wire="r")
    sk.Offset("r", 1, id="o")

    def edge(i):
        return sk.Wire(i).edges[0]

    assert (edge("o.Line.1").name, edge("o.Line.1").maxh) == ("floor", 0.5)
    assert edge("o.Line.1.corner").name == "floor"
    assert edge("o.Line.4.corner").name == "back"
    sk.Offset("r", 2, id="p", name="coat")
    assert {e.name for e in sk.Wire("p").edges} == {"coat"}
    assert edge("p.Line.1").maxh == 0.5


@pytest.mark.constraints
def test_the_pens_stay():
    sk = px.Sketch((0, 0))
    sk.Circle((0, 0), 3, id="c")
    sk.Move((5, 5)).Line(1, 0)
    loc, heading = sk.pen.position, sk.pen.direction
    sk.Offset("c", 1)
    assert (sk.pen.position, sk.pen.direction) == (loc, heading)
    sk.Line(0, 1, id="up")
    assert sk.Location("up.start") == sk.Location("Line.1.end") == loc


@pytest.mark.constraints
def test_remove_takes_the_whole_offset_and_frees_its_ids():
    sk = rectangle()
    sk.Constrain("Line.1", px.Distance, 10, id="l")
    sk.Constrain("Line.2", px.Distance, 5)
    dof = sk.dof
    sk.Offset("r", 1, id="wall")
    sk.Remove("wall")
    with pytest.raises(ValueError, match="no element 'wall'"):
        sk("wall")
    assert "Offset" not in sk.elements and "wall.outer" not in sk.wires
    assert sk.dof == dof
    sk.Update({"l": 12})
    assert sk._Geometry("Line.1")["length"] == pytest.approx(12, rel=REL)
    sk.Offset("r", 1, id="wall")
    assert area(sk, "wall") == pytest.approx(60 + 34 + math.pi, rel=REL)


@pytest.mark.constraints
def test_a_source_stays_until_its_offset_goes():
    sk = rectangle()
    sk.Constrain("Line.1", px.Distance, 10, id="l")
    sk.Constrain("Line.2", px.Distance, 5)
    sk.Offset("r", 1, id="wall")
    hint = (r"Remove\('Line.2'\): the offset 'wall' is made from it; keep it as construction with "
            r"sk\('Line.2'\).construction = True, or Remove\('wall'\) first")
    with pytest.raises(ValueError, match=hint):
        sk.Remove("Line.2")
    with pytest.raises(ValueError, match=r"Remove\('r'\): the offset 'wall' is made from it; .* sk\('r'\)"):
        sk.Remove("r")
    assert len(sk("wall")) == 16 and sk.wires["r"] == [f"Line.{k}" for k in range(1, 5)]
    sk("Line.2").construction = True
    sk.Update({"l": 12})
    assert area(sk, "wall") == pytest.approx(60 + 34 + math.pi, rel=REL)
    assert area(sk, "wall") == pytest.approx(px.Face([sk.Wire("r")]).Offset(1).mass, rel=REL)
    sk.Offset("wall", 1, id="skin")
    with pytest.raises(ValueError, match=r"Remove\('wall'\): the offset 'skin' is made from it"):
        sk.Remove("wall")
    assert len(sk("wall")) == 16
    sk.Remove("skin")
    sk.Remove("wall")
    sk.Remove("Line.2")
    assert "Offset" not in sk.elements and "Line.2" not in sk.elements["Line"]


def test_a_quicksketch_source_goes_alone():
    qs = rectangle(px.QuickSketch)
    qs.Offset("r", 1, id="wall")
    qs.Remove("Line.2")
    assert len(qs.wires["wall"]) == 8


@pytest.mark.constraints
def test_the_parts_of_an_offset_stay_with_it():
    sk = rectangle()
    sk.Offset("r", 1, id="wall")
    for part in ("wall.Line.1", "wall.Line.1.corner", "wall.Point.1", "wall.tie.1"):
        with pytest.raises(ValueError, match=r"part of the offset 'wall'; Remove\('wall'\)"):
            sk.Remove(part)
    with pytest.raises(ValueError, match="part of the offset 'wall'"):
        sk.Trim("wall.Line.1")
    assert len(sk("wall")) == 16


@pytest.mark.constraints
def test_the_source_is_one_chain():
    sk = rectangle()
    sk.Circle((20, 0), 1, id="c")
    sk.Move((30, 0)).Line(1, 0, id="a")
    with pytest.raises(ValueError, match="not one chain"):
        sk.Offset(["Line.1", "Line.3"], 1)
    with pytest.raises(ValueError, match="offset it alone"):
        sk.Offset(["c", "r"], 1)
    with pytest.raises(ValueError, match="no line, arc, circle or spline"):
        sk.Offset("Point.1", 1)


def chain_L(make=px.Sketch):
    """Line.1 (0, 0) to (10, 0), Line.2 up to (10, 5): it turns left"""
    sk = make((0, 0))
    sk.Line(10, 0).Line(0, 5)
    sk.Finish(id_wire="L")
    return sk


@pytest.mark.constraints
def test_a_line_gets_a_band_with_round_caps():
    sk = px.Sketch((0, 0))
    sk.Line(10, 0, id="a")
    dof = sk.dof
    sk.Offset("a", 1, id="slot")
    assert (sk.wires["slot.left"], sk.wires["slot.right"]) == (["slot.left.a"], ["slot.right.a"])
    assert {"slot.startcap", "slot.endcap"} <= set(sk.elements["Arc"])
    assert area(sk, "slot") == pytest.approx(20 + math.pi, rel=REL)
    assert area(sk, "slot") == pytest.approx(sk.Wire("a").Offset(1).mass, rel=REL)
    assert sk._Geometry("slot.left.a")["start"] == pytest.approx((0, 1), abs=1e-12)
    assert sk._Geometry("slot.left.a")["end"] == pytest.approx((10, 1), abs=1e-12)
    assert sk._Geometry("slot.right.a")["start"] == pytest.approx((0, -1), abs=1e-12)
    assert sk._Geometry("slot.startcap")["center"] == sk.Location("a.start")
    assert sk._Geometry("slot.endcap")["center"] == sk.Location("a.end")
    assert (sk.dof, sk.redundant) == (dof, [])
    sk.Constrain("a", px.Distance, 10, id="l")
    sk.Update({"l": 12, "slot.d": 2})
    assert area(sk, "slot") == pytest.approx(48 + 4 * math.pi, rel=REL)


@pytest.mark.constraints
def test_an_open_chain_gets_both_sides_by_default():
    sk = chain_L()
    sk.Offset("L", 1, id="b")
    assert sk.wires["b.left"] == ["b.left.Line.1", "b.left.Line.2"]
    assert sk.wires["b.right"] == ["b.right.Line.1", "b.right.Line.1.corner", "b.right.Line.2"]
    assert area(sk, "b") == pytest.approx(sk.Wire("L").Offset(1).mass, rel=REL)
    assert sk.redundant == []


@pytest.mark.constraints
def test_one_side_of_an_open_chain():
    sk = chain_L()
    dof = sk.dof
    sk.Offset("L", 1, both_sides=False, id="in")
    sk.Offset("L", -1, both_sides=False, id="out")
    assert sorted(i.local for i in sk("in") if i.kind != "point") == ["in.Line.1", "in.Line.2"]
    assert sorted(i.local for i in sk("out") if i.kind != "point") == ["out.Line.1", "out.Line.1.corner", "out.Line.2"]
    assert not any("cap" in e for e in sk.elements["Arc"]) and not any(w.startswith(("in.", "out.")) for w in sk.wires)
    assert sk._Geometry("in.Line.1")["start"] == pytest.approx((0, 1), abs=1e-12)
    assert sk._Geometry("in.Line.2")["start"] == pytest.approx((9, 1), abs=1e-12)
    assert sk._Geometry("in.Line.2")["end"] == pytest.approx((9, 5), abs=1e-12)
    assert sk.Wire("in").mass == pytest.approx(9 + 4, rel=REL)
    assert sk.Wire("out").mass == pytest.approx(10 + 5 + math.pi / 2, rel=REL)
    assert (sk.dof, sk.redundant) == (dof, [])


@pytest.mark.constraints
def test_a_point_picks_the_side():
    sk = chain_L()
    sk.Offset("L", (1, (5, 3)), id="p")
    sk.Offset("L", (1, (5, -3)), id="q")
    assert sk._Geometry("p.Line.1")["start"] == pytest.approx((0, 1), abs=1e-12)
    assert sk._Geometry("q.Line.1")["start"] == pytest.approx((0, -1), abs=1e-12)
    sk.Move((20, 0), dir=(1, 0)).Rectangle(4, 2, id="r")
    sk.Offset("r", (0.5, (21, 1)), id="i")
    sk.Offset("r", (0.5, "Line.1.end"), id="o")
    assert sk._Geometry("i.r.Line.1")["start"] == pytest.approx((20.5, 0.5), abs=1e-12)
    assert "o.r.Line.1.corner" in sk.elements["Arc"] and "i.r.Line.1.corner" not in sk.elements.get("Arc", [])
    with pytest.raises(ValueError, match="picks no side"):
        sk.Offset("L", (1, (5, 0)))
    with pytest.raises(ValueError, match="must be positive"):
        sk.Offset("L", (-1, (5, 3)))
    with pytest.raises(ValueError, match="picks one side"):
        sk.Offset("L", (1, (5, 3)), both_sides=True)


@pytest.mark.constraints
def test_both_sides_of_a_closed_chain():
    sk = rectangle()
    dof = sk.dof
    sk.Offset("r", 1, both_sides=True, id="w")
    assert area(sk, "w.right") == pytest.approx(50 + 30 + math.pi, rel=REL)
    assert area(sk, "w.left") == pytest.approx(24, rel=REL)
    assert (sk.dof, sk.redundant) == (dof, [])
    with pytest.raises(ValueError, match="must be positive"):
        sk.Offset("r", -1, both_sides=True)


@pytest.mark.constraints
def test_the_band_of_an_arc():
    sk = px.Sketch((0, 0))
    sk.Arc(4, 120, id="bend")
    sk.Constrain("bend", px.Radius, 4, id="rb")
    dof = sk.dof
    sk.Offset("bend", 1, id="b")
    assert (sk.wires["b.left"], sk.wires["b.right"]) == (["b.left.bend"], ["b.right.bend"])
    assert sk._Geometry("b.left.bend")["r"] == pytest.approx(3, rel=REL)
    assert area(sk, "b") == pytest.approx(16 * math.pi / 3 + math.pi, rel=REL)
    assert (sk.dof, sk.redundant) == (dof, [])
    sk.Update({"rb": 5})
    assert area(sk, "b") == pytest.approx(sk.Wire("bend").Offset(1).mass, rel=REL)


@pytest.mark.constraints
def test_the_band_of_an_arc_narrower_than_the_distance():
    sk = px.Sketch((0, 0))
    sk.Arc(4, 120, id="bend")
    sk.Offset("bend", 5, id="w")
    assert "w.left" not in sk.wires and sk.wires["w.right"] == ["w.right.bend"]
    assert area(sk, "w") == pytest.approx(sk.Wire("bend").Offset(5).mass, rel=REL)
    assert sk.redundant == []
    with pytest.raises(ValueError, match="nothing is left on the left"):
        sk.Offset("bend", 5, both_sides=False)


@pytest.mark.constraints
def test_a_quicksketch_band_has_the_same_elements():
    sk, qs = chain_L(), chain_L(px.QuickSketch)
    sk.Offset("L", 1, id="b")
    qs.Offset("L", 1, id="b")
    assert [i.local for i in qs("b")] == [i.local for i in sk("b")]
    assert area(qs, "b") == pytest.approx(area(sk, "b"), rel=REL)
    assert sorted(qs.wires["b"]) == sorted(i.local for i in qs("b") if "Point" not in i.local)


@pytest.mark.constraints
def test_the_caps_take_the_properties_of_their_ends():
    sk = px.Sketch((0, 0))
    sk.Line(10, 0, name="first").Line(0, 5, name="second")
    sk.Finish(id_wire="L")
    sk.Offset("L", 1, id="b")
    assert sk.Wire("b.startcap").edges[0].name == "first"
    assert sk.Wire("b.endcap").edges[0].name == "second"
    assert sk.Wire("b.right.Line.1.corner").edges[0].name == "first"
    left = ["b.left.Line.1", "b.left.Line.2"] + [f"b.left.Point.{k}" for k in (1, 2, 3)]
    assert sorted(i.local for i in sk("b.left")) == left


@pytest.mark.constraints
def test_an_offset_of_the_chain_being_drawn_leaves_it_open():
    sk = px.Sketch((0, 0))
    sk.Line(10, 0)
    sk.Offset("Line.1", 1)
    sk.Line(0, 5, id="up")
    assert sk.Location("up.start") == sk.Location("Line.1.end")
    assert area(sk, "Offset.1") == pytest.approx(20 + math.pi, rel=REL)


def rigid_rounded():
    """the rounded rectangle, its shape held (it may still move as a whole)"""
    sk = rounded()
    sk.RigidGroup(sk.wires["rr"])
    return sk


@pytest.mark.constraints
def test_a_distance_past_an_inner_arc_raises_and_changes_nothing():
    sk = rigid_rounded()
    sk.Offset("rr", -1, id="o")
    message = (r"o.d = 2.5: Offset\('o'\): the offset of Arc.1 would vanish at d = 2.5 > r = 2; make the offset again "
               r"\(Remove\('o'\), then Offset\)")
    with pytest.raises(ValueError, match=message):
        sk["o.d"] = 2.5
    with pytest.raises(ValueError, match="its side is fixed"):
        sk["o.d"] = -1
    assert sk["o.d"] == 1 and area(sk, "o") == pytest.approx(80 + math.pi, rel=REL)


@pytest.mark.constraints
def test_arcs_narrower_than_the_distance_vanish_and_may_not_come_back():
    sk = rigid_rounded()
    sk.Offset("rr", -3, id="o")
    assert edges_of(sk, "o") == [f"o.Line.{k}" for k in range(1, 5)]
    assert area(sk, "o") == pytest.approx(px.Face([sk.Wire("rr")]).Offset(-3).mass, rel=REL)
    message = (r"Update\(o.d = 1\): Offset\('o'\): the offset of Arc.\d would come back \(r = 2 > d = 1\); "
               r"make the offset again \(Remove\('o'\), then Offset\); nothing changed")
    with pytest.raises(ValueError, match=message):
        sk.Update({"o.d": 1})
    assert sk["o.d"] == 3 and len(edges_of(sk, "o")) == 4
    sk.Remove("o")
    sk.Offset("rr", -1, id="o")
    assert len(edges_of(sk, "o")) == 8


@pytest.mark.constraints
def test_an_inner_side_that_closes_raises():
    sk = rectangle()
    sk.Constrain("Line.1", px.Distance, 10)
    sk.Constrain("Line.2", px.Distance, 5)
    sk.Offset("r", -1)
    with pytest.raises(ValueError, match=r"Offset\('Offset.1'\): nothing is left inside at d = 3; make the offset"):
        sk.Update({"Offset.1.d": 3})
    assert sk["Offset.1.d"] == 1 and area(sk, "Offset.1") == pytest.approx(24, rel=REL)


@pytest.mark.constraints
def test_a_source_that_loses_its_tangency_raises():
    sk = rounded()
    sk.Offset("rr", 1, id="o")
    before = repr(sk.constraints)
    with pytest.raises(ValueError, match=r"Offset\('o'\): .*; make the offset again"):
        sk.Constrain(px.H, "Line.1", px.Angle, 5)
    assert repr(sk.constraints) == before
    assert area(sk, "o") == pytest.approx(140 + 9 * math.pi, rel=REL)


@pytest.mark.constraints
def test_a_gap_that_closes_raises():
    sk = px.Sketch((0, 0))
    sk.Line(10, 0).Line(0, 10).Line(-3, 0).Line(0, -8).Line(-4, 0).Line(0, 8).Line(-3, 0).Close(id_wire="u")
    sk.RigidGroup(sk.wires["u"])
    sk.Offset("u", 1, id="o")
    with pytest.raises(ValueError, match=r"Offset\('o'\): the offset of Line.\d would vanish; make the offset again"):
        sk.Update({"o.d": 2.5})
    assert sk["o.d"] == 1


@pytest.mark.constraints
def test_trimming_a_source_raises():
    sk = rectangle()
    sk.Offset("r", 1, id="wall")
    sk.Move((5, -1)).Line(0, 2, id="cut", construction=True)
    with pytest.raises(ValueError, match=r"Trim\('Line.1'\): Offset\('wall'\): its source is no longer one chain"):
        sk.Trim(("Line.1", (2, 0)), "cut")
    assert sk.Location("Line.1.start") == (0, 0) and len(sk("wall")) == 16


BUMP = [(0, 0), (3, 1.5), (6, 0.5), (9, 2)]


def spline_at(points, u, k=0, closed=False):
    return px._planex._InterpolatedAt(points, {}, closed, u, k)


def chord(points, closed=False):
    return sum(math.dist(a, b) for a, b in zip(points, points[1:] + points[:1] if closed else points[1:]))


def foot_distance(points, p, closed=False):
    """the distance from p to the spline through points, from its foot on it (the sketch's own interpolation)"""
    end = chord(points, closed)
    u = min((end * i / 120 for i in range(121)), key=lambda t: math.dist(spline_at(points, t, 0, closed), p))
    for _ in range(30):
        s, d1, d2 = (spline_at(points, u, k, closed) for k in range(3))
        c = (s[0] - p[0], s[1] - p[1])
        u -= (c[0] * d1[0] + c[1] * d1[1]) / (d1[0] ** 2 + d1[1] ** 2 + c[0] * d2[0] + c[1] * d2[1])
        # a closed spline runs on over its seam
        u = u % end if closed else min(max(u, 0.0), end)
    return math.dist(spline_at(points, u, 0, closed), p)


def left_of(points, u, d):
    """the point at distance d to the left of the spline through points, at its chord length u"""
    s, t = spline_at(points, u), spline_at(points, u, 1)
    n = math.hypot(*t)
    return (s[0] - d * t[1] / n, s[1] + d * t[0] / n)


def wave(make=px.Sketch):
    """the spline s through BUMP, its points fixed"""
    sk = make((0, 0))
    sk.Move((0, 0)).Spline(BUMP[1:], px.Fix, id="s")
    return sk


@pytest.mark.constraints
@pytest.mark.occ
def test_a_spline_gets_a_band_of_offset_curves():
    sk = wave()
    sk.Offset("s", 0.4, id="b")
    assert (sk.wires["b.left"], sk.wires["b.right"]) == (["b.left.s"], ["b.right.s"])
    assert {"b.left.s", "b.right.s"} <= set(sk.elements["Spline"])
    assert {"b.startcap", "b.endcap"} <= set(sk.elements["Arc"])
    assert (sk.dof, sk.redundant) == (0, [])
    left, right = sk._Geometry("b.left.s"), sk._Geometry("b.right.s")
    assert (left["kind"], left["source"], left["points"]) == ("Spline", "s", [])
    assert left["start"] == pytest.approx(left_of(BUMP, 0, 0.4), abs=1e-12)
    assert left["end"] == pytest.approx(left_of(BUMP, chord(BUMP), 0.4), abs=1e-12)
    assert right["start"] == pytest.approx(left_of(BUMP, 0, -0.4), abs=1e-12)
    for g in (left, right):
        assert [foot_distance(BUMP, p) for p in g["path"]] == pytest.approx([0.4] * len(g["path"]), abs=1e-9)
    assert [g["kind"] for g in sk._Drawing() if g["id"] == "b.left.s"] == ["Spline"]
    assert area(sk, "b") == pytest.approx(sk.Wire("s").Offset(0.4).mass, rel=1e-5)
    edges = sk.Wire("b.left.s").edges
    assert len(edges) == 1 and isinstance(edges[0].edge, px.SplineEdge)


@pytest.mark.constraints
def test_the_offset_curves_follow_the_spline_and_the_distance():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline(BUMP[1:], id="s")
    for k in (0, 1, 3):
        sk.Constrain(f"s.p{k}", px.Fix)
    sk.Constrain(px.V, "s.p2", px.Distance, 6, id="x2")
    sk.Constrain(px.H, "s.p2", px.Distance, 0.5, id="y2")
    sk.Offset("s", 0.4, id="b")
    sk.Update({"y2": 1.2, "b.d": 0.3})
    moved = [(0, 0), (3, 1.5), (6, 1.2), (9, 2)]
    assert sk.Location("s.p2") == pytest.approx((6, 1.2), abs=1e-12)
    assert sk._Geometry("b.left.s")["start"] == pytest.approx(left_of(moved, 0, 0.3), abs=1e-12)
    path = sk._Geometry("b.right.s")["path"]
    assert [foot_distance(moved, p) for p in path] == pytest.approx([0.3] * len(path), abs=1e-9)
    assert area(sk, "b") == pytest.approx(sk.Wire("s").Offset(0.3).mass, rel=1e-5)


@pytest.mark.constraints
def test_a_spline_tangent_to_a_line_needs_no_corner():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Line(4, 0, px.Fix).Spline([(7, 1), (9, 3)], px.Tangent)
    sk.Finish(id_wire="c")
    for p in ("Spline.1.p1", "Spline.1.p2"):
        sk.Constrain(p, px.Fix)
    sk.Offset("c", 0.3, id="b")
    assert sk.wires["b.left"] == ["b.left.Line.1", "b.left.Spline.1"]
    assert sk.wires["b.right"] == ["b.right.Line.1", "b.right.Spline.1"]
    assert sk._Geometry("b.left.Line.1")["end"] == sk._Geometry("b.left.Spline.1")["start"] == pytest.approx((4, 0.3), abs=1e-12)
    assert area(sk, "b") == pytest.approx(sk.Wire("c").Offset(0.3).mass, rel=1e-5)
    assert (sk.dof, sk.redundant) == (0, [])


def profile():
    """a D: Line.1 (0, 0) to (10, 0), Line.2 up to (10, 4), Spline.1 over (5, 6) to (0, 4), Line.3 down; its points fixed"""
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Line(10, 0).Line(0, 4).Spline([(5, 6), (0, 4)]).Close(id_wire="d")
    for p in ("Line.1.start", "Line.1.end", "Line.2.end", "Spline.1.p1", "Spline.1.p2"):
        sk.Constrain(p, px.Fix)
    return sk


@pytest.mark.constraints
def test_a_spline_in_a_profile_gets_corners_outside():
    sk = profile()
    sk.Offset("d", 0.5, id="o")
    assert edges_of(sk, "o") == sorted(["o.Line.1", "o.Line.1.corner", "o.Line.2", "o.Line.2.corner", "o.Spline.1",
                                        "o.Spline.1.corner", "o.Line.3", "o.Line.3.corner"])
    assert area(sk, "o") == pytest.approx(px.Face([sk.Wire("d")]).Offset(0.5).mass, rel=1e-5)
    assert (sk.dof, sk.redundant) == (0, [])
    assert not [c for c in sk.jacobian["columns"] if c[0] == "o.Spline.1"]


@pytest.mark.constraints
def test_a_concave_corner_between_a_spline_and_a_line_is_a_crossing():
    sk = profile()
    sk.Offset("d", -0.5, id="i")
    assert edges_of(sk, "i") == ["i.Line.1", "i.Line.2", "i.Line.3", "i.Spline.1"]
    assert area(sk, "i") == pytest.approx(px.Face([sk.Wire("d")]).Offset(-0.5).mass, rel=1e-5)
    curve = sk._Geometry("i.Spline.1")
    spline = [(10, 4), (5, 6), (0, 4)]
    assert (curve["start"][0], curve["end"][0]) == pytest.approx((9.5, 0.5), abs=1e-11)
    assert [foot_distance(spline, curve[k]) for k in ("start", "end")] == pytest.approx([0.5, 0.5], abs=1e-11)
    assert {("i.Spline.1", "from"), ("i.Spline.1", "to")} <= set(sk.jacobian["columns"])
    assert (sk.dof, sk.redundant) == (0, [])
    sk.Update({"i.d": 0.8})
    assert area(sk, "i") == pytest.approx(px.Face([sk.Wire("d")]).Offset(-0.8).mass, rel=1e-5)
    assert sk._Geometry("i.Spline.1")["start"][0] == pytest.approx(9.2, abs=1e-11)


def lens():
    """a from (0, 0) over (5, 2) to (10, 0), b back over (5, -2); their points fixed"""
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline([(5, 2), (10, 0)], id="a").Spline([(5, -2), (0, 0)], id="b")
    sk.Finish(id_wire="lens")
    for p in ("a.p0", "a.p1", "a.p2", "b.p1"):
        sk.Constrain(p, px.Fix)
    return sk


@pytest.mark.constraints
def test_two_offset_curves_meet_at_a_crossing():
    sk = lens()
    sk.Offset("lens", -0.5, id="i")
    assert edges_of(sk, "i") == ["i.a", "i.b"]
    assert area(sk, "i") == pytest.approx(px.Face([sk.Wire("lens")]).Offset(-0.5).mass, rel=1e-5)
    a, b = sk._Geometry("i.a"), sk._Geometry("i.b")
    assert (a["end"], a["start"]) == (b["start"], b["end"])
    for p in (a["start"], a["end"]):
        assert foot_distance([(0, 0), (5, 2), (10, 0)], p) == pytest.approx(0.5, abs=1e-11)
        assert foot_distance([(10, 0), (5, -2), (0, 0)], p) == pytest.approx(0.5, abs=1e-11)
    assert {("i.a", "from"), ("i.a", "to"), ("i.b", "from"), ("i.b", "to")} <= set(sk.jacobian["columns"])
    sk.Offset("lens", 0.5, id="o")
    assert edges_of(sk, "o") == ["o.a", "o.a.corner", "o.b", "o.b.corner"]
    assert area(sk, "o") == pytest.approx(px.Face([sk.Wire("lens")]).Offset(0.5).mass, rel=1e-5)
    assert (sk.dof, sk.redundant) == (0, [])


@pytest.mark.constraints
def test_a_cusp_raises():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline([(1, 1), (2, 0)], px.Fix, id="s")
    cusp = r"the offset of s would have a cusp: d = 2 is not below [0-9.e-]+, the radius of its tightest bend towards that side, at \("
    with pytest.raises(ValueError, match=r"Offset\('s'\): " + cusp):
        sk.Offset("s", -2, both_sides=False)
    assert sk.elements["Spline"] == ["s"] and "Offset" not in sk.elements
    sk.Offset("s", 0.1, id="b")
    with pytest.raises(ValueError, match=r"b.d = 2: Offset\('b'\): " + cusp + r".*; make the offset again"):
        sk["b.d"] = 2
    assert sk["b.d"] == 0.1


@pytest.mark.constraints
def test_a_source_that_bends_into_a_cusp_raises_and_changes_nothing():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline([(1, 0.2), (2, 0)], id="s")
    for k in (0, 2):
        sk.Constrain(f"s.p{k}", px.Fix)
    sk.Constrain(px.V, "s.p1", px.Distance, 1, id="x1")
    sk.Constrain(px.H, "s.p1", px.Distance, 0.2, id="y1")
    sk.Offset("s", -0.5, both_sides=False, id="b")
    with pytest.raises(ValueError, match=r"Offset\('b'\): the offset of s would have a cusp: .*; make the offset again"):
        sk.Update({"y1": 1})
    assert sk.Location("s.p1") == pytest.approx((1, 0.2), abs=1e-12) and sk["y1"] == 0.2


@pytest.mark.constraints
def test_points_and_dimensions_on_an_offset_curve():
    sk = wave()
    sk.Offset("s", 0.4, id="b")
    sk.Move((4, 3)).Line(0.5, 0, id="probe")
    sk.Constrain("probe.start", "b.left.s", px.Coincident)
    sk.Constrain("probe.end", "b.left.s", px.Distance, 0.3)
    assert foot_distance(BUMP, sk.Location("probe.start")) == pytest.approx(0.4, abs=1e-9)
    assert foot_distance(BUMP, sk.Location("probe.end")) == pytest.approx(0.7, abs=1e-9)
    assert sk.redundant == []


@pytest.mark.constraints
def test_a_spline_source_stays_until_its_offset_goes():
    sk = wave()
    sk.Offset("s", 0.4, id="b", col=(1, 0, 0))
    expected = area(sk, "b")
    with pytest.raises(ValueError, match=r"Remove\('s'\): the offset 'b' is made from it; keep it as construction with "
                                         r"sk\('s'\).construction = True, or Remove\('b'\) first"):
        sk.Remove("s")
    sk("s").construction = True
    assert area(sk, "b") == pytest.approx(expected, rel=REL)
    assert sk("b.left.s").col == [1, 0, 0, 1] and len(sk("b.left", kind="spline")) == 1
    sk.Remove("b")
    sk.Remove("s")
    assert "Spline" not in sk.elements


@pytest.mark.constraints
def test_what_an_offset_curve_does_not_take_yet():
    sk = wave()
    sk.Offset("s", 0.4, id="b")
    with pytest.raises(ValueError, match="'b.left.s' is an offset curve, it follows its source; fix its source"):
        sk.Constrain("b.left.s", px.Fix)
    with pytest.raises(ValueError, match="'b.left.s' is an offset curve, it follows its source; group its source"):
        sk.RigidGroup(["b.left.s"])
    with pytest.raises(ValueError, match="'b.left.s' is an offset curve, it follows its source; copy its source"):
        sk.Mirrored(["b.left.s"], px.V)
    with pytest.raises(ValueError, match=r"an offset of an offset curve comes later"):
        sk.Offset("b.left", 0.2)
    assert sk.redundant == []


BLOB = [(0, 0), (4, -1), (6, 2), (3, 4), (-1, 2)]


@pytest.mark.constraints
@pytest.mark.occ
def test_a_closed_spline_gets_one_closed_offset_curve():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline(BLOB[1:], px.Fix, periodic=True, id="blob")
    sk.Offset("blob", 0.3, id="o")
    sk.Offset("blob", -0.3, id="i")
    assert (edges_of(sk, "o"), edges_of(sk, "i")) == (["o.blob"], ["i.blob"])
    for name in ("o.blob", "i.blob"):
        g = sk._Geometry(name)
        assert g["closed"] and g["start"] == g["end"]
        assert [foot_distance(BLOB, p, True) for p in g["path"]] == pytest.approx([0.3] * len(g["path"]), abs=1e-9)
    assert area(sk, "o") == pytest.approx(px.Face([sk.Wire("blob")]).Offset(0.3).mass, rel=1e-5)
    assert area(sk, "i") == pytest.approx(px.Face([sk.Wire("blob")]).Offset(-0.3).mass, rel=1e-5)
    # one closed edge in planex, two in OCC
    assert len(sk.Wire("o.blob").edges) == 1 and len(sk.Wire("o.blob").ToOCCWire().edges) == 2
    assert (sk.dof, sk.redundant) == (0, [])
    sk["o.d"] = 0.5
    assert area(sk, "o") == pytest.approx(px.Face([sk.Wire("blob")]).Offset(0.5).mass, rel=1e-5)
    with pytest.raises(ValueError, match=r"i.d = 5: Offset\('i'\): the offset of blob would have a cusp"):
        sk["i.d"] = 5
    with pytest.raises(ValueError, match="a closed spline is a loop of its own, offset it alone"):
        sk.Move((10, 0)).Line(1, 0, id="l").Offset(["blob", "l"], 0.3)


@pytest.mark.constraints
def test_a_trimmed_spline_offsets_between_its_cuts():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline(BUMP[1:], id="s")
    for k in range(4):
        sk.Constrain(f"s.p{k}", px.Fix)
    sk.Move((7, -1)).Line(0, 4, px.Fix, id="cut", construction=True)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Trim(("s", (8.5, 1.8)), "cut")
    sk.Offset("s", 0.3, id="b")
    lo, hi = 0.0, chord(BUMP)
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        lo, hi = (mid, hi) if spline_at(BUMP, mid)[0] < 7 else (lo, mid)
    assert sk._Geometry("b.left.s")["start"] == pytest.approx(left_of(BUMP, 0, 0.3), abs=1e-9)
    assert sk._Geometry("b.left.s")["end"] == pytest.approx(left_of(BUMP, lo, 0.3), abs=1e-9)
    assert area(sk, "b") == pytest.approx(sk.Wire("s").Offset(0.3).mass, rel=1e-5)
    assert sk.redundant == []


@pytest.mark.constraints
def test_a_piece_over_the_seam_of_a_closed_spline_raises():
    sk = px.Sketch((0, 0))
    sk.Move((0, 0)).Spline(BLOB[1:], periodic=True, id="blob")
    sk.Move((2.5, -3)).Line(0, 9, id="cut", construction=True)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Trim(("blob", (5.5, 1.5)), "cut")
    with pytest.raises(ValueError, match="'blob' runs over the seam of its closed spline, which an offset cannot take yet"):
        sk.Offset("blob", 0.3)


def test_a_quicksketch_freezes_the_offset_of_a_spline():
    sk = px.QuickSketch((0, 0))
    sk.Move((0, 0)).Spline(BUMP[1:], id="s")
    sk.Offset("s", 0.4, id="b")
    assert sk.elements.offsets == ["b"] and sk.wires["b.left"] == ["b.left.s"]
    g = sk._Geometry("b.left.s")
    assert g["kind"] == "Spline" and len(g["points"]) > 2 and "source" not in g
    assert g["start"] == pytest.approx(left_of(BUMP, 0, 0.4), abs=1e-9)
    assert [foot_distance(BUMP, p) for p in g["path"]] == pytest.approx([0.4] * len(g["path"]), abs=2e-5)
    sk.Remove("s")
    assert sk._Geometry("b.left.s")["start"] == pytest.approx(left_of(BUMP, 0, 0.4), abs=1e-9)


def test_a_quicksketch_freezes_crossings_and_closed_splines():
    sk = px.QuickSketch((0, 0))
    sk.Move((0, 0)).Line(10, 0).Line(0, 4).Spline([(5, 6), (0, 4)]).Close(id_wire="d")
    sk.Offset("d", -0.5, id="i")
    assert edges_of(sk, "i") == ["i.Line.1", "i.Line.2", "i.Line.3", "i.Spline.1"]
    curve = sk._Geometry("i.Spline.1")
    assert (curve["start"][0], curve["end"][0]) == pytest.approx((9.5, 0.5), abs=1e-6)
    q = px.QuickSketch((0, 0))
    q.Move((0, 0)).Spline(BLOB[1:], periodic=True, id="blob")
    q.Offset("blob", 0.3, id="o")
    g = q._Geometry("o.blob")
    assert g["closed"]
    assert [foot_distance(BLOB, p, True) for p in g["path"]] == pytest.approx([0.3] * len(g["path"]), abs=2e-5)


@pytest.mark.constraints
def test_offsets_plot_with_their_constraints():
    pytest.importorskip("plotly")
    pytest.importorskip("matplotlib")
    sk = rounded()
    sk.Offset("rr", 1, id="o")
    sk.Move((0, 12)).Spline([(3, 13.5), (6, 12.5), (9, 14)], id="s")
    sk.Offset("s", 0.4, id="b")
    ids = {"o.Line.1", "o.Arc.1", "b.left.s", "b.right.s", "b.startcap"}
    assert ids <= set(re.findall(r"<title>(.*?)</title>", px.ToSVG(sk, show_constraints=True)))
    fig = sk.Plot(show_constraints=True, backend="plotly")
    assert ids <= {h for t in fig.data if t.hovertext is not None for h in t.hovertext}
    px.Plot(sk)


@pytest.mark.constraints
def test_the_examples_of_the_docstring():
    sk = px.Sketch().Rectangle(40, 20, id="plate")
    sk.Offset("plate", -2, id="wall")
    sk["wall.d"] = 3
    sk.Move((0, 30)).Spline([(10, 35), (20, 30)], id="s")
    sk.Offset("s", (1.5, (10, 40)), id="bead")
    assert len(edges_of(sk, "wall")) == 4 and edges_of(sk, "bead") == ["bead.s"] and sk["wall.d"] == 3
