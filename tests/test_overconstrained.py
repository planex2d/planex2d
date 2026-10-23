# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px


def Plate():
    sk = px.Sketch()
    sk.Rectangle(40, 20, id="plate")
    sk.Constrain("plate.Line.1.start", px.Fix)
    sk.Constrain("plate.Line.1", px.Horizontal)
    sk.Constrain("plate.Line.1", px.Distance, 40, id="length")
    sk.Constrain("plate.Line.2", px.Distance, 20, id="width")
    return sk


def Overcome(sk):
    with pytest.warns(px.PlanexWarning) as record:
        sk.Constrain("plate.Line.3", px.Distance, 41, id="top")
    return [str(w.message) for w in record]


def States(sk):
    return dict(sk._States())


def Corners(sk):
    return [sk.Location("plate.Line.%d.start" % k) for k in (1, 2, 3, 4)]


def AssertPlate(sk, length, width):
    x0, y0 = sk.Location("plate.Line.1.start")
    expected = [(x0, y0), (x0 + length, y0), (x0 + length, y0 + width), (x0, y0 + width)]
    for got, want in zip(Corners(sk), expected):
        assert math.isclose(got[0], want[0], abs_tol=1e-9) and math.isclose(got[1], want[1], abs_tol=1e-9)


def test_plate_is_full():
    assert Plate().status == "full"


def test_contradiction_is_kept_and_over():
    sk = Plate()
    messages = Overcome(sk)
    assert any("contradicts" in m and "length" in m for m in messages)
    assert sk.status == "over"
    assert "top" in [c["id"] for c in sk._Constraints()]
    AssertPlate(sk, 40, 20)
    states = States(sk)
    assert states["plate.Line.1"] == "over" and states["plate.Line.3"] == "over"
    infos = {c["id"]: c["state"] for c in sk._Constraints()}
    assert infos["top"] == "over" and infos["length"] == "over"


def test_consistent_value_is_still_redundant():
    sk = Plate()
    with pytest.warns(px.PlanexWarning):
        sk.Constrain("plate.Line.3", px.Distance, 40, id="top")
    assert sk.status == "full"
    assert "top" in sk.redundant


def test_removing_the_suspended_constraint_clears_over():
    sk = Plate()
    Overcome(sk)
    sk.Remove("top")
    assert sk.status == "full"
    assert "over" not in States(sk).values()
    assert all(c["state"] != "over" for c in sk._Constraints())


def test_removing_one_of_its_set_clears_over():
    sk = Plate()
    Overcome(sk)
    sk.Remove("length")
    assert sk.status != "over"


def test_changing_the_other_dimension_activates_it():
    sk = Plate()
    Overcome(sk)
    sk.Update({"length": 41})
    assert sk.status == "full"
    assert "over" not in States(sk).values()
    AssertPlate(sk, 41, 20)


def test_face_warns_and_keeps_last_solution():
    sk = Plate()
    Overcome(sk)
    with pytest.warns(px.PlanexWarning, match="top"):
        face = sk.Face()
    assert face.mass == 800.0


def test_domain_error_still_raises():
    sk = px.Sketch()
    sk.Line((0, 0), (10, 0), id="p")
    sk.Line((0, 5), (10, 5), id="q")
    sk.Constrain("p", px.Distance, 10, id="a")
    sk.Constrain("q", px.Distance, 10, id="b")
    with pytest.raises(ValueError):
        sk.Expression("b = sqrt(a - 20)")
    assert "a" in sk.dimensions and sk.expressions == {}
