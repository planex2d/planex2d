# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-13


def flange():
    sk = px.Sketch()
    sk.Circle(0, 0, 3, px.Fix, id="rim")
    sk.Circle(2, 0, 0.3, id="bolt")
    sk.Constrain("bolt.center", px.H, px.Coincident)
    sk.Constrain(sk.origin, "bolt.center", px.Distance, 2, id="pcd")
    sk.Constrain("bolt", px.Diameter, 0.6, id="d")
    return sk


def centers(sk, n):
    return [sk._Geometry(f"CircularPattern.1.{k}.bolt")["center"] for k in range(1, n + 1)]


def test_a_circular_pattern_of_bolt_holes():
    sk = flange()
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.CircularPattern("bolt", sk.origin, 6)
    assert sk.elements["Circle"] == ["rim", "bolt"] + [f"CircularPattern.1.{k}.bolt" for k in range(1, 6)]
    assert sk.elements["CircularPattern"] == ["CircularPattern.1"] and "Pattern" not in sk.elements
    assert (sk.status, sk.dof) == ("full", 0)
    expected = [(2 * math.cos(math.radians(60 * k)), 2 * math.sin(math.radians(60 * k))) for k in range(1, 6)]
    assert [v for c in centers(sk, 5) for v in c] == pytest.approx([v for c in expected for v in c], abs=TOL)
    assert repr(sk.constraints["Angle"]["CircularPattern.1.angle"]) == '"CircularPattern.1", px.Angle, 60'
    assert "Copy" not in sk.constraints


def test_the_copies_follow_the_original_and_the_step():
    sk = flange()
    sk.CircularPattern("bolt", sk.origin, 6)
    sk.Update({"CircularPattern.1.angle": 30, "pcd": 2.5, "d": 0.8})
    expected = [(2.5 * math.cos(math.radians(30 * k)), 2.5 * math.sin(math.radians(30 * k))) for k in range(1, 6)]
    assert [v for c in centers(sk, 5) for v in c] == pytest.approx([v for c in expected for v in c], abs=TOL)
    assert [sk._Geometry(f"CircularPattern.1.{k}.bolt")["r"] for k in range(1, 6)] == pytest.approx([0.4] * 5, abs=TOL)
    assert sk.Shape().faces[0].mass == pytest.approx(9 * math.pi - 6 * 0.16 * math.pi, rel=1e-12)


def test_a_linear_pattern_moves_only_the_copies_of_a_fixed_original():
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(1, 0.5)
    # the rectangle keeps its right angles: its bottom fixed and its height hold it
    sk.Constrain("Rectangle.1.Line.1", px.Fix)
    sk.Constrain("Rectangle.1.Line.2", px.Distance, 0.5)
    sk.LinearPattern("Rectangle.1", px.H, 3, 2)
    sk["LinearPattern.1.spacing"] = 3
    assert [sk._Geometry(f"LinearPattern.1.{k}.Rectangle.1.Line.1")["start"] for k in (1, 2)] == [pytest.approx((3, 0), abs=TOL), pytest.approx((6, 0), abs=TOL)]
    assert sk.status == "full"


def test_without_keep_constraints_the_copies_are_free():
    sk = flange()
    sk.CircularPattern("bolt", sk.origin, 4, auto_constraint=False)
    assert "Pattern" not in sk.elements and "Equal" not in sk.constraints
    sk["pcd"] = 2.5
    assert sk._Geometry("CircularPattern.1.1.bolt")["center"] == pytest.approx((0, 2), abs=TOL)
    assert sk.status == "under"


def test_removing_the_pattern_or_its_step_frees_the_copies():
    sk = flange()
    sk.CircularPattern("bolt", sk.origin, 4)
    sk.Remove("CircularPattern.1.angle")
    assert sk.dof == 1
    sk.Remove("CircularPattern.1")
    assert "Pattern" not in sk.elements and "CircularPattern.1.3.bolt" in sk.elements["Circle"]
    assert sk._Geometry("CircularPattern.1.1.bolt")["center"] == pytest.approx((0, 2), abs=TOL)
    assert sk.dof == 3 * 3 - 3


def test_a_pattern_of_a_chain_with_an_arc():
    sk = px.Sketch()
    sk.Move((1, 0)).Line(1, 0).Arc(0.5, 90).Line(0, 0.5)
    sk.CircularPattern("Chain.1", sk.origin, 3)
    arcs = [sk._Geometry(i) for i in sk.elements["Arc"]]
    assert all(a["ccw"] for a in arcs) and [a["r"] for a in arcs] == pytest.approx([0.5] * 3, abs=TOL)
    c0 = arcs[0]["center"]
    t = math.radians(120)
    assert arcs[1]["center"] == pytest.approx((c0[0] * math.cos(t) - c0[1] * math.sin(t), c0[0] * math.sin(t) + c0[1] * math.cos(t)), abs=TOL)


def test_pattern_errors():
    sk = flange()
    with pytest.raises(ValueError, match="at least 2"):
        sk.CircularPattern("bolt", sk.origin, 1)
    with pytest.raises(ValueError, match="'rim' is no point"):
        sk.CircularPattern("bolt", "rim", 3)
    with pytest.raises(ValueError, match="'origin' is no line"):
        sk.LinearPattern("bolt", sk.origin, 3, 1)
    assert "Pattern" not in sk.elements


def row(n=3):
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(1, 0.5)
    sk.Constrain("Rectangle.1.Line.1", px.Fix)
    sk.Constrain("Rectangle.1.Line.2", px.Distance, 0.5)
    sk.LinearPattern("Rectangle.1", px.H, n, 2)
    return sk


def starts(sk):
    bottoms = [sk._Geometry(i) for i in sk.elements["Line"]]
    return sorted(round(g["start"][0], 12) + 0.0 for g in bottoms if abs(g["start"][1]) < 1e-12 and abs(g["end"][1]) < 1e-12)


def test_the_count_of_a_linear_pattern_changes_with_update():
    sk = row()
    assert sk["LinearPattern.1.count"] == 3 and isinstance(sk["LinearPattern.1.count"], int)
    assert sk.dimensions["LinearPattern.1.count"] == 3
    sk["LinearPattern.1.count"] = 5
    sk.Update()
    assert starts(sk) == [0, 2, 4, 6, 8] and len(sk.elements["Line"]) == 20
    assert (sk.status, sk.dof) == ("full", 0)
    sk.Update({"LinearPattern.1.count": 2, "LinearPattern.1.spacing": 3})
    assert starts(sk) == [0, 3] and len(sk.elements["Line"]) == 8 and len(sk.elements["Point"]) == 9
    assert sk["LinearPattern.1.count"] == 2 and sk.status == "full"
    sk.Update({"LinearPattern.1.count": 4})
    assert starts(sk) == [0, 3, 6, 9]
    sk["LinearPattern.1.spacing"] = 1.5
    assert starts(sk) == [0, 1.5, 3, 4.5]
    assert sk.Shape().mass == pytest.approx(4 * 0.5, rel=1e-12)


def test_a_full_turn_pattern_spreads_its_new_count_and_a_given_angle_stays():
    sk = flange()
    sk.CircularPattern("bolt", sk.origin, 6)
    sk["CircularPattern.1.count"] = 8
    sk.Update()
    assert sk["CircularPattern.1.angle"] == pytest.approx(45, abs=1e-12)
    expected = [(2 * math.cos(math.radians(45 * k)), 2 * math.sin(math.radians(45 * k))) for k in range(1, 8)]
    assert [sk._Geometry(i)["center"] for i in sk.elements["Circle"][2:]] == [pytest.approx(c, abs=TOL) for c in expected]
    sk.Update({"CircularPattern.1.count": 3})
    assert sk["CircularPattern.1.angle"] == pytest.approx(120, abs=1e-12) and len(sk.elements["Circle"]) == 4
    assert (sk.status, sk.dof) == ("full", 0)
    given = flange()
    given.CircularPattern("bolt", given.origin, 3, angle=30)
    given.Update({"CircularPattern.1.count": 5})
    assert given["CircularPattern.1.angle"] == 30
    assert given._Geometry(given.elements["Circle"][-1])["center"] == pytest.approx((-1, math.sqrt(3)), abs=TOL)


def test_fewer_copies_take_the_constraints_on_them_and_a_failed_update_changes_nothing():
    sk = row(4)
    with pytest.warns(px.PlanexWarning, match="redundant"):
        sk.Constrain(sk.elements["Line"][-4], px.Distance, 1, id="last bottom")
    with pytest.warns(px.PlanexWarning, match="removed last bottom with the copies they held"):
        sk.Update({"LinearPattern.1.count": 2})
    assert "last bottom" not in sk.dimensions and len(sk.elements["Line"]) == 8
    sk.Update({"LinearPattern.1.count": 4})
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("LinearPattern.1.1.Rectangle.1.Line.1", px.Fix)
    with pytest.raises(ValueError, match="no solution, nothing changed"):
        sk.Update({"LinearPattern.1.count": 3, "LinearPattern.1.spacing": 3})
    assert sk["LinearPattern.1.count"] == 4 and sk["LinearPattern.1.spacing"] == 2
    assert len(sk.elements["Line"]) == 16 and starts(sk) == [0, 2, 4, 6]
    for bad in (0, 2.5, -1):
        with pytest.raises(ValueError, match="whole number"):
            sk["LinearPattern.1.count"] = bad
    sk.Remove("LinearPattern.1")
    with pytest.raises(ValueError, match="no dimension"):
        sk["LinearPattern.1.count"] = 3


def all_ids(sk):
    return sorted(i for ids in sk.elements.values() for i in ids) + sorted(i for c in sk.constraints.values() for i in c)


def test_copies_have_ids_under_their_pattern_and_a_new_count_shifts_no_other_id():
    sk = row(3)
    assert [i for i in sk.elements["Line"] if i.startswith("LinearPattern.1.")] == [
        f"LinearPattern.1.{k}.Rectangle.1.Line.{j}" for k in (1, 2) for j in range(1, 5)]
    sk.Move((0, 2)).Line(1, 0)
    assert "Line.1" in sk.elements["Line"]
    before = all_ids(sk)
    sk.Update({"LinearPattern.1.count": 5})
    assert "Line.1" in sk.elements["Line"] and "LinearPattern.1.4.Rectangle.1.Line.4" in sk.elements["Line"]
    sk.Update({"LinearPattern.1.count": 2})
    sk.Update({"LinearPattern.1.count": 3})
    assert all_ids(sk) == before
    assert len(sk("Rectangle.1.Line.1")) == 1 and len(sk("LinearPattern.1.1.Rectangle.1.Line.1")) == 1
    assert sk("Rectangle.1.Line.1")[0].id != sk("LinearPattern.1.1.Rectangle.1.Line.1")[0].id
    assert sorted(i.local for i in sk("LinearPattern.1.*.Rectangle.1", kind="line")) == sorted(
        f"LinearPattern.1.{k}.Rectangle.1.Line.{j}" for k in (1, 2) for j in range(1, 5))
