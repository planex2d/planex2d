# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

BOTH = [px.QuickSketch, pytest.param(px.Sketch, marks=pytest.mark.constraints)]


def ell(make):
    """the open chain a, b: (0, 0) to (10, 0) to (10, 10), construction, so the offset alone bounds a face"""
    sk = make()
    sk.Move((0, 0)).Line((10, 0), id="a", construction=True).Line((10, 10), id="b", construction=True)
    return sk


def ids(sk):
    return {g["id"]: g["kind"] for g in sk._Drawing() if not g["construction"]}


def ell_wire():
    return px.Move((0, 0)).Line((10, 0)).Line((10, 10)).Wire()


@pytest.mark.parametrize("make", BOTH)
@pytest.mark.parametrize("ends", [px.Round, px.Flat, px.Square])
@pytest.mark.parametrize("corners", [px.Round, px.Sharp, px.Chamfer])
def test_a_sketch_band_has_the_wire_band_s_area(make, ends, corners):
    sk = ell(make).Offset(["a", "b"], 1, ends=ends, corners=corners, id="w", construction=False)
    assert sk.Face().mass == pytest.approx(ell_wire().Offset(1, ends=ends, corners=corners).mass, rel=1e-9)


@pytest.mark.parametrize("make", BOTH)
def test_flat_ends_and_sharp_corners_are_lines_with_the_offset_s_names(make):
    sk = ell(make).Offset(["a", "b"], 1, ends=px.Flat, corners=px.Sharp, id="w", construction=False)
    d = ids(sk)
    assert {d[i] for i in ("w.left.a", "w.left.b", "w.right.a", "w.right.b", "w.startcap", "w.endcap")} == {"Line"}
    assert not any(i.endswith(".corner") for i in d)
    assert sk.Location("w.right.a.end") == pytest.approx((11, -1), abs=1e-9)


@pytest.mark.parametrize("make", BOTH)
def test_a_chamfered_corner_is_one_line(make):
    sk = ell(make).Offset(["a", "b"], 1, corners=px.Chamfer, id="w", construction=False)
    d = ids(sk)
    assert d["w.right.a.corner"] == "Line" and d["w.startcap"] == "Arc"


def line_arc(construction):
    """a line, then an arc that leaves it at a right angle"""
    sk = px.QuickSketch() if not construction else construction()
    flag = {"construction": True} if construction else {}
    sk.Move((0, 0)).Line((4, 0), id="a", **flag).Arc((8, 4), center=(8, 0), id="c", **flag)
    return sk


@pytest.mark.parametrize("make", BOTH)
def test_a_sharp_corner_beside_an_arc_runs_on_straight(make):
    sk = line_arc(make).Offset(["a", "c"], 1, corners=px.Sharp, id="w", construction=False)
    w = line_arc(None).Wire()
    assert any(i.endswith(".before") or i.endswith(".after") for i in ids(sk))
    assert sk.Face().mass == pytest.approx(w.Offset(1, corners=px.Sharp).mass, rel=1e-9)


@pytest.mark.parametrize("make", BOTH)
@pytest.mark.parametrize("d, corners, area", [(1, px.Sharp, 36.0), (1, px.Chamfer, 34.0), (1, px.Round, 32 + math.pi), (-1, px.Sharp, 4.0)])
def test_a_closed_chain_s_loop_has_its_corner_form(make, d, corners, area):
    sk = make()
    sk.Move((0, 0)).Line((4, 0), construction=True).Line((4, 4), construction=True).Line((0, 4), construction=True).Line((0, 0), construction=True)
    sk.Offset(["Line.1", "Line.2", "Line.3", "Line.4"], d, corners=corners, id="w", construction=False)
    assert sk.Face().mass == pytest.approx(area, rel=1e-9)


@pytest.mark.constraints
@pytest.mark.parametrize("ends, corners", [(px.Flat, px.Sharp), (px.Square, px.Chamfer), (px.Square, px.Sharp)])
def test_the_forms_follow_a_new_distance(ends, corners):
    sk = ell(px.Sketch).Constrain("a", px.Fix).Constrain("b", px.Fix)
    sk.Offset(["a", "b"], 1, ends=ends, corners=corners, id="w", construction=False)
    sk["w.d"] = 2
    sk.Update()
    assert sk.Face().mass == pytest.approx(ell_wire().Offset(2, ends=ends, corners=corners).mass, rel=1e-9)


@pytest.mark.constraints
def test_spurs_follow_a_new_distance():
    sk = line_arc(px.Sketch).Constrain("a", px.Fix).Constrain("c", px.Fix)
    sk.Offset(["a", "c"], 1, ends=px.Square, corners=px.Sharp, id="w", construction=False)
    sk["w.d"] = 0.5
    sk.Update()
    w = line_arc(None).Wire()
    assert sk.Face().mass == pytest.approx(w.Offset(0.5, ends=px.Square, corners=px.Sharp).mass, rel=1e-9)


def test_one_side_has_no_caps_whatever_the_ends():
    sk = ell(px.QuickSketch).Offset(["a", "b"], 1, both_sides=False, ends=px.Square, id="w", construction=False)
    assert not any("cap" in i for i in ids(sk))


def test_wrong_forms_raise():
    with pytest.raises(ValueError, match="ends= is px.Round, px.Flat or px.Square"):
        ell(px.QuickSketch).Offset(["a", "b"], 1, ends=px.Chamfer)
