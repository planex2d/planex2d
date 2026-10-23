# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

TOL = 1e-9


def drawing(sk):
    return {g["id"]: (g["kind"], bool(g["construction"])) for g in sk._Drawing()}


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_centred_rectangle_sits_around_the_pen(make):
    sk = make().Move((10, 5)).RectangleC(4, 2)
    d = drawing(sk)
    assert all(d["RectangleC.1.Line.%d" % k] == ("Line", False) for k in range(1, 5))
    assert d["RectangleC.1.diagonal"] == ("Line", True) and d["RectangleC.1.center"] == ("Point", True)
    assert sk.Location("RectangleC.1.Line.1.start") == pytest.approx((8, 4), abs=TOL)
    assert sk.Location("RectangleC.1.center") == pytest.approx((10, 5), abs=TOL)
    assert sk.Face().mass == pytest.approx(8, rel=TOL)
    assert tuple(sk.pen.position) == pytest.approx((10, 5), abs=TOL)
    assert "RectangleC" in sk.elements and sk.wires["RectangleC.1"] == ["RectangleC.1.Line.%d" % k for k in range(1, 5)]
    sk.Line(1)
    assert "Line.1" in drawing(sk)


def test_a_centred_rectangle_follows_the_drawing_direction_and_takes_a_centre():
    sk = px.QuickSketch().Move((0, 0), dir=(0, 1)).RectangleC((1, 1), 4, 2, id="plate")
    assert sk.Location("plate.Line.1.start") == pytest.approx((2, -1), abs=TOL)
    assert sk.Location("plate.Line.1.end") == pytest.approx((2, 3), abs=TOL)
    with pytest.raises(TypeError, match="not an id string"):
        px.QuickSketch().RectangleC("A", 4, 2)


@pytest.mark.constraints
def test_a_centred_rectangle_stays_centred():
    sk = px.Sketch().Move((0, 0)).RectangleC(4, 2)
    sk.Constrain("RectangleC.1.center", px.Fix)
    sk.Constrain("RectangleC.1.Line.1", px.Distance, 4, id="l")
    sk.Constrain("RectangleC.1.Line.2", px.Distance, 2, id="w")
    assert sk.dof == 1
    sk["l"] = 6
    sk.Update()
    assert sk.Location("RectangleC.1.center") == pytest.approx((0, 0), abs=TOL)
    assert sk.Face().mass == pytest.approx(12, rel=TOL)
    a, c = sk.Location("RectangleC.1.Line.1.start"), sk.Location("RectangleC.1.Line.3.start")
    assert ((a[0] + c[0]) / 2, (a[1] + c[1]) / 2) == pytest.approx((0, 0), abs=TOL)
    sk.Remove("RectangleC.1")
    assert not any(i.startswith("RectangleC.1") for i in drawing(sk))


@pytest.mark.constraints
def test_a_selected_point_ties_the_centre():
    sk = px.Sketch().Move((3, 3)).Line((5, 3), px.Fix)
    sk.RectangleC(sk("Line.1.end"), 2, 2)
    assert sk.Location("RectangleC.1.center") == pytest.approx((5, 3), abs=TOL)
