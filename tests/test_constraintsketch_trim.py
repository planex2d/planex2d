# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

TOL = 1e-13


def post_on_base():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, px.Horizontal, id="base")
    sk.Move((1, 2)).Line(0, -3, px.Vertical, id="post")
    return sk


@pytest.mark.constraints
def test_trimming_an_overshoot_keeps_it_as_construction():
    sk = post_on_base()
    sk.Trim("post")
    assert sk._Geometry("post")["end"] == pytest.approx((1, 0), abs=TOL)
    g = sk._Geometry("post.trimmed.1")
    assert (g["kind"], g["start"], g["end"]) == ("Line", pytest.approx((1, 0), abs=TOL), pytest.approx((1, -1), abs=TOL))
    assert len(sk.Construction().edges) == 1 and len(sk.Wires()) == 2
    sk.Constrain("base.start", px.H, px.Distance, 0.5)
    assert sk._Location("post.end")[1] == pytest.approx(0.5, abs=TOL)


@pytest.mark.constraints
def test_trim_with_a_cutter_near_and_reverse():
    sk = post_on_base()
    sk.Trim(("base", (3, 0)), "post")
    assert sk._Geometry("base")["end"] == pytest.approx((1, 0), abs=TOL)
    sk2 = post_on_base()
    sk2.Trim(("base", (3, 0)), "post", reverse=True)
    assert sk2._Geometry("base")["start"] == pytest.approx((1, 0), abs=TOL)
    assert sk2._Geometry("base")["end"] == pytest.approx((4, 0), abs=TOL)


@pytest.mark.constraints
def test_a_circle_cut_by_a_chord_is_a_segment():
    sk = px.Sketch()
    sk.Circle(0, 0, 1, id="ring")
    sk.Move((-2, 0.3)).Line(4, 0, px.Horizontal, id="cut")
    sk.Trim(("ring", (0, -1)), "cut")
    sk.Trim(("cut", (-1.9, 0.3)))
    sk.Trim(("cut", (1.9, 0.3)))
    assert sorted(sk.elements["Line"]) == ["cut", "cut.trimmed.1", "cut.trimmed.2"]
    states = sk._States()
    assert "over" not in states.values()
    h = 0.3
    assert sk.Face().mass == pytest.approx(math.acos(h) - h * math.sqrt(1 - h * h), rel=1e-12)
    sk.Constrain("ring.center", px.Fix)
    sk.Constrain("cut", px.H, px.Distance, h)
    sk.Constrain("ring", px.Radius, 1.2)
    r = 1.2
    assert sk.Face().mass == pytest.approx(r * r * math.acos(h / r) - h * math.sqrt(r * r - h * h), rel=1e-12)
    assert sk._Geometry("ring.trimmed.1")["r"] == pytest.approx(1.2, abs=TOL)


@pytest.mark.constraints
def test_split_numbers_the_pieces_and_the_constraints_resolve_through_the_wire():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, px.Horizontal, id="a").Move((1, -1)).Line(0, 2, id="x1").Move((3, -1)).Line(0, 2, id="x2")
    sk.Constrain("a", px.Distance, 4, id="len")
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Split("a", ["x1", "x2"])
    assert sk.wires["a"] == ["a.1", "a.2", "a.3"] and "a" not in sk.elements["Line"]
    assert [sk._Geometry(i)["end"] for i in ("a.1", "a.2", "a.3")] == [pytest.approx((1, 0), abs=TOL),
                                                                     pytest.approx((3, 0), abs=TOL), pytest.approx((4, 0), abs=TOL)]
    assert sk.Location("a.end") == sk.Location("a.3.end") and repr(sk.constraints["Distance"]["len"]) == '"a", px.Distance, 4'
    sk["len"] = 5
    sk.Constrain("a.start", px.H, px.Distance, 1)
    assert [sk._Location(i)[1] for i in ("a.2.start", "a.3.start", "a.3.end")] == pytest.approx([1, 1, 1], abs=TOL)
    assert sk._Location("a.3.end")[0] - sk._Location("a.1.start")[0] == pytest.approx(5, abs=1e-9)


@pytest.mark.constraints
def test_constraints_on_the_cut_off_end_follow_it():
    sk = post_on_base()
    sk.Move((1, -1)).Line(1, 0, id="foot")
    sk.Constrain("post.end", "foot.start", px.Coincident)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Trim(("post", (1, -0.5)), "base")
    assert repr(sk.constraints["Coincident"]["Coincident.1"]) == '"post.trimmed.1.end", "foot.start", px.Coincident'


@pytest.mark.constraints
def test_trim_and_split_errors_change_nothing():
    sk = post_on_base()
    sk.Move((10, 10)).Line(1, 0, id="far")
    before = repr(sk.constraints), sk.elements
    with pytest.raises(ValueError, match="does not meet the cutter"):
        sk.Trim("far", "base")
    with pytest.raises(ValueError, match="it meets nothing"):
        sk.Split("far")
    assert (repr(sk.constraints), sk.elements) == before
    sk.Trim("far")
    assert sk._Drawing()[-1]["construction"] is True


@pytest.mark.constraints
def test_split_takes_no_point_every_piece_is_numbered():
    sk = post_on_base()
    with pytest.raises(ValueError, match=r"every piece is numbered \('Line.3.1', 'Line.3.2', ...\), none keeps a's id"):
        sk.Split(("base", (3, 0)), "post")
    sk.Split("base", "post")
    assert sk._Geometry("base.1")["end"] == pytest.approx((1, 0), abs=TOL)
    assert sk._Geometry("base.2")["end"] == pytest.approx((4, 0), abs=TOL)


@pytest.mark.constraints
def test_a_point_id_picks_the_piece_too():
    sk = post_on_base()
    sk.Vertex("right", (3.5, 0.1))
    sk.Trim(("base", "right"), "post")
    assert sk._Geometry("base")["end"] == pytest.approx((1, 0), abs=TOL)


@pytest.mark.constraints
def test_near_is_gone_and_says_where_the_point_goes():
    sk = post_on_base()
    with pytest.raises(TypeError, match=r"Trim\(near=...\): a Sketch has no near=; .*Trim\(\(a, point\), b\)"):
        sk.Trim("base", "post", near=(3, 0))
    with pytest.raises(TypeError, match=r"Split\(near=...\).*every piece is numbered"):
        sk.Split("base", "post", near=(3, 0))
    with pytest.raises(TypeError, match=r"SymmetryLine\(near=...\).*reverse=True"):
        sk.SymmetryLine("base", "post", near=(3, 3))
    with pytest.raises(TypeError, match=r"Line\(near=...\).*distance=\(d, point\)"):
        sk.Line(parallel="base", distance=1, near=(0, 1))
    with pytest.raises(TypeError, match=r"Arc\(near=...\).*tangent=\(target, point\)"):
        sk.Arc("base", "post", 0.5, tangent=("base", "post"), near=(2, 1))
    with pytest.raises(TypeError, match=r"Circle\(near=...\).*tangent=\(target, point\)"):
        sk.Circle(0.5, tangent="base", near=(2, 1))
    with pytest.raises(TypeError, match="unexpected keyword argument 'nearest'"):
        sk.Trim("base", "post", nearest=(3, 0))


@pytest.mark.parametrize("sketch", [px.QuickSketch, pytest.param(px.Sketch, marks=pytest.mark.constraints, id="Sketch")])
def test_parallel_distance_with_a_point_but_no_end_raises(sketch):
    sk = sketch()
    sk.Move((0, 0)).Line(4, 0, id="base")
    for distance in [(2, (1, 5)), (-2, (1, 5)), (2, (7, 0))]:
        with pytest.raises(ValueError):
            sk.Line(parallel="base", distance=distance, id="copy")
