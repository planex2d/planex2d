# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest
from netgen import occ

import planex as px
from helpers import circle, rect, square


def areas(shapes):
    return sorted(round(s.mass, 9) for s in shapes)


def named(obj, name):
    obj.name = name
    return obj


def test_a_line_across_splits_a_face_in_two():
    f = named(square(0, 0, 2), "plate")
    cut = named(px.Line((1, -1), (1, 3)), "cut")
    pieces = f.Split(cut)
    assert len(pieces) == 2 and all(isinstance(p, px.Shape) and len(p.faces) == 1 for p in pieces)
    assert areas(pieces) == [2, 2]
    assert all(p.name == "plate" for p in pieces)
    assert all({e.name for e in p.edges} == {None, "cut"} for p in pieces)
    assert all(sum(e.name == "cut" for e in p.edges) == 1 for p in pieces)


def test_cuts_that_separate_nothing_return_the_original():
    f = square(0, 0, 2)
    for cutter in (px.Line((3, 0), (3, 2)), px.Line((1, -1), (1, 1)), px.Line((0.5, 0.5), (1.5, 1.5))):
        [same] = f.Split(cutter)
        assert same.mass == pytest.approx(4) and len(same.edges) == 4


def test_closed_cutters_and_holes():
    pieces = square(0, 0, 4).Split(px.QuickSketch().Circle((2, 2), 1).Wire())
    assert areas(pieces) == [round(math.pi, 9), round(16 - math.pi, 9)]
    ring = square(0, 0, 4) - square(1.5, 1.5, 1)
    pieces = ring.Split(px.Line((2, -1), (2, 5)))
    assert areas(pieces) == [7.5, 7.5] and all(len(p.faces) == 1 for p in pieces)
    zigzag = px.Move((-1, 1)).Line((5, 1)).Line((5, 3)).Line((-1, 3)).Wire()
    assert areas(square(0, 0, 4).Split(zigzag)) == [4, 4, 8]


def test_trim_by_distance_size_and_side():
    f = square(0, 0, 3)
    cutter = px.Move((1, -1)).Line((1, 4)).Move((2, 4)).Line((2, -1))
    assert f.Trim(cutter, keep_closest_to=(0.2, 1)).mass == pytest.approx(3)
    middle_gone = f.Trim(cutter, remove_closest_to=(1.5, 1.5))
    assert middle_gone.mass == pytest.approx(6) and len(middle_gone.faces) == 2
    end_gone = f.Trim(cutter, remove_closest_to=(2.5, 1.5))
    assert end_gone.mass == pytest.approx(6) and len(end_gone.faces) == 1 and len(end_gone.edges) == 4
    assert square(0, 0, 4).Trim(px.Line((1, -1), (1, 5)), keep_largest=True).mass == pytest.approx(12)
    assert square(0, 0, 4).Trim(px.Line((1, -1), (1, 5)), keep_smallest=True).mass == pytest.approx(4)
    up = px.Line((1, -1), (1, 5))
    left = square(0, 0, 4).Trim(up, keep_left=True)
    assert left.mass == pytest.approx(4) and left.center[0] == pytest.approx(0.5)
    assert square(0, 0, 4).Trim(up, keep_right=True).center[0] == pytest.approx(2.5)
    ccw = px.QuickSketch().Circle((2, 2), 1).Wire()
    assert square(0, 0, 4).Trim(ccw, keep_left=True).mass == pytest.approx(math.pi)
    assert square(0, 0, 4).Trim(ccw, keep_right=True).mass == pytest.approx(16 - math.pi)
    with pytest.raises(ValueError, match="exactly one"):
        f.Trim(cutter)
    with pytest.raises(ValueError, match="exactly one"):
        f.Trim(cutter, keep_largest=True, keep_smallest=True)
    assert f.Trim(px.Line((5, 0), (5, 1)), keep_largest=True).mass == pytest.approx(9)


def test_wires_split_at_the_cutter():
    w = named(px.Line((0, 0), (4, 0)), "rail")
    cutter = px.Move((1, -1)).Line((1, 1)).Move((3, -1)).Line((3, 1))
    runs = w.Split(cutter)
    assert sorted(r.wires[0].mass for r in runs) == pytest.approx([1, 1, 2])
    assert all(e.name == "rail" for r in runs for e in r.edges)
    assert w.Trim(cutter, keep_closest_to=(3.5, 0.2)).wires[0].mass == pytest.approx(1)
    assert sum(x.mass for x in w.Trim(cutter, remove_closest_to=(2, 0)).wires) == pytest.approx(2)
    assert w.Trim(cutter, keep_largest=True).wires[0].mass == pytest.approx(2)
    with pytest.raises(ValueError, match="faces"):
        w.Trim(cutter, keep_left=True)
    [same] = w.Split(px.Line((5, -1), (5, 1)))
    assert same.wires[0].mass == pytest.approx(4)


def test_shapes_are_cut_face_by_face_and_cutters_are_projected():
    s = named(square(0, 0, 2), "a") + named(square(3, 0, 2), "b")
    pieces = s.Split(px.Line((-1, 1), (6, 1)))
    assert areas(pieces) == [2, 2, 2, 2] and sorted(p.name for p in pieces) == ["a", "a", "b", "b"]
    trimmed = s.Trim(px.Line((-1, 1), (6, 1)), keep_closest_to=(2.5, 0))
    assert trimmed.mass == pytest.approx(4) and len(trimmed.faces) == 2
    above = occ.Segment(occ.Pnt(1, -1, 5), occ.Pnt(1, 3, 5))
    assert areas(square(0, 0, 2).Split(above)) == [2, 2]
    lifted = px.Line((1, -1), (1, 3)).Translated((0, 0, 7))
    assert areas(square(0, 0, 2).Split(lifted)) == [2, 2]
    assert areas(square(0, 0, 2).Split(circle(2, 2, 1))) == sorted([round(4 - math.pi / 4, 9), round(math.pi / 4, 9)])


def test_a_wire_is_cut_where_it_crosses_a_spline_cutter():
    blob = px.QuickSketch((0, -3)).Spline([(4, -2), (5, 0), (4, 2), (0, 3)]).Close().Face()
    spline = px.Spline([(0, -3), (4, -2), (5, 0), (4, 2), (0, 3)])
    circle = px.Circle((0, 0), 4.5)
    assert len(circle.Split(blob)) == 2
    assert len(circle.Split(spline)) == 2
    assert len(px.Line((-4.5, 0.5), (9, 0.5)).Split(blob)) == 3
    assert len(px.Line((-1, 0.5), (9, 0.5)).Split(spline)) == 2
    assert len(spline.Split(circle)) == 3 and len(spline.Split(px.Line((-4.5, 0.5), (9, 0.5)))) == 2
    pieces = circle.Split(blob)
    assert sum(e.mass for p in pieces for e in p.edges) == pytest.approx(circle.mass, rel=1e-12)


def test_a_circle_cut_at_a_spline_cutter_s_ends_only():
    blob = px.QuickSketch((0, -3)).Spline([(4, -2), (5, 0), (4, 2), (0, 3)]).Close().Face()
    assert len(px.Circle((0, 0), 3).Split(blob)) == 2
