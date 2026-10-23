# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def drawing(sk):
    return {g["id"]: (g["kind"], bool(g["construction"])) for g in sk._Drawing()}


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_polygon_has_its_vertices_on_a_construction_circle(make):
    sk = make().Move((0, 0)).Polygon(1, 6)
    d = drawing(sk)
    assert d["Polygon.1.circle"] == ("Circle", True)
    assert all(d["Polygon.1.Line.%d" % k] == ("Line", False) for k in range(1, 7))
    assert sk.Location("Polygon.1.Line.1.start") == pytest.approx((1, 0), abs=TOL)
    assert sk.Face().mass == pytest.approx(3 * math.sqrt(3) / 2, rel=TOL)
    assert tuple(sk.pen.position) == pytest.approx((0, 0), abs=TOL)
    assert "Polygon" in sk.elements


def test_a_polygon_takes_the_forms_of_circle_and_the_id():
    sk = px.QuickSketch().Polygon(2, 4, center=(1, 1), id="sq")
    assert "sq.circle" in drawing(sk)
    assert sk.Face().mass == pytest.approx(8, rel=TOL)
    with pytest.raises(ValueError, match="Polygon"):
        px.QuickSketch().Move((0, 0)).Polygon(1, 2)


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_hexagon_is_measured_across_its_flats(make):
    sk = make().Move((0, 0)).Hexagon(13)
    assert sk.Face().mass == pytest.approx(math.sqrt(3) / 2 * 13 ** 2, rel=TOL)
    flats = [k for k in range(1, 7) if sk._Geometry("Hexagon.1.Line.%d" % k)["start"][1] == pytest.approx(
        sk._Geometry("Hexagon.1.Line.%d" % k)["end"][1], abs=TOL)]
    assert len(flats) == 2
    assert sorted(abs(sk._Geometry("Hexagon.1.Line.%d" % k)["start"][1]) for k in flats) == pytest.approx([6.5, 6.5], abs=TOL)


def keyhole_area(l, w, r):
    h = w / 2
    outside = 2 * h * l - (h * math.sqrt(r * r - h * h) + r * r * math.asin(h / r))
    return math.pi * r * r + outside + math.pi * h * h / 2


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_keyhole_is_a_slot_starting_in_a_hole(make):
    sk = make().Move((0, 0)).Keyhole(10, 2, 3)
    d = drawing(sk)
    assert d["Keyhole.1.spine"] == ("Line", True) and d["Keyhole.1.hole"] == ("Arc", False)
    assert d["Keyhole.1.endcap"] == ("Arc", False) and "Keyhole.1.startcap" not in d
    assert sk.Face().mass == pytest.approx(keyhole_area(10, 2, 3), rel=TOL)
    assert tuple(sk.pen.position) == pytest.approx((10, 0), abs=TOL)
    with pytest.raises(ValueError, match="Keyhole"):
        px.QuickSketch().Move((0, 0)).Keyhole(10, 4, 1)


@pytest.mark.constraints
def test_the_dimensions_of_the_profiles_drive_them():
    hexagon = px.Sketch().Move((0, 0)).Hexagon(13)
    assert hexagon["Hexagon.1.af"] == pytest.approx(13)
    hexagon["Hexagon.1.af"] = 10
    hexagon.Update()
    assert hexagon.Face().mass == pytest.approx(math.sqrt(3) / 2 * 100, rel=TOL)
    polygon = px.Sketch().Move((0, 0)).Polygon(1, 5)
    polygon["Polygon.1.r"] = 2
    polygon.Update()
    assert polygon.Face().mass == pytest.approx(5 / 2 * 4 * math.sin(2 * math.pi / 5), rel=TOL)
    key = px.Sketch().Keyhole((0, 0), (10, 0), 2, 3, px.Fix)
    assert (key["Keyhole.1.w"], key["Keyhole.1.r"]) == pytest.approx((2, 3))
    key["Keyhole.1.r"] = 4
    key.Update()
    assert key.Face().mass == pytest.approx(keyhole_area(10, 2, 4), rel=1e-6)
    key.Remove("Keyhole.1")
    assert not any(i.startswith("Keyhole.1") for i in drawing(key))


@pytest.mark.constraints
def test_a_keyhole_refuses_sizes_that_break_it_and_changes_nothing():
    sk = px.Sketch().Keyhole((0, 0), (10, 0), 2, 3, px.Fix)
    for key, value, text in (("Keyhole.1.r", 0.9, "must exceed half the width"), ("Keyhole.1.w", 7, "must exceed half the width"),
                             ("Keyhole.1.r", 10.1, "the spine's length 10 must exceed 10.0504")):
        with pytest.raises(ValueError, match=text):
            sk[key] = value
            sk.Update()
        assert (sk["Keyhole.1.r"], sk["Keyhole.1.w"]) == pytest.approx((3, 2))
