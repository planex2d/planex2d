# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import arc, ends, line, point, seg

Untouched = px._planex.CutResult.Untouched
Crossing = px._planex.CutResult.Crossing
Overlap = px._planex.CutResult.Overlap
Congruent = px._planex.CutResult.Congruent


def test_crossing_lines_are_split_at_one_shared_point():
    a, b = line((0, 0), (2, 2)), line((0, 2), (2, 0))
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert ends(a) == ((0, 0), (1, 1))
    assert ends(b) == ((0, 2), (1, 1))
    assert [ends(p) for p in pieces] == [((1, 1), (2, 2)), ((1, 1), (2, 0))]


def test_t_junction_splits_at_the_exact_endpoint():
    a, b = line((0, 0), (3, 0)), line((1 / 3, 0), (1 / 3, 1))
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert a.end == b.start
    assert pieces[0].start == b.start
    assert ends(b) == ((1 / 3, 0), (1 / 3, 1))


@pytest.mark.parametrize("b", [
    line((1, 0), (1, 1)),
    line((0, 1), (1, 2)),
    line((0, 1), (1, 1)),
    line((1, 0), (2, 0)),
    line((2, 0), (3, 0)),
])
def test_lines_that_do_not_cut(b):
    a = line((0, 0), (1, 0))
    result, pieces = a.Cut(b)
    assert result == Untouched
    assert pieces == []
    assert ends(a) == ((0, 0), (1, 0))


def test_partial_overlap_keeps_the_overlap_once():
    a, b = line((0, 0), (2, 0)), line((1, 0), (3, 0))
    result, pieces = a.Cut(b)
    assert result == Overlap
    assert ends(a) == ((0, 0), (1, 0))
    assert [ends(p) for p in pieces] == [((1, 0), (2, 0))]
    assert ends(b) == ((2, 0), (3, 0))


def test_partial_overlap_in_opposite_directions():
    a, b = line((0, 0), (2, 0)), line((3, 0), (1, 0))
    result, pieces = a.Cut(b)
    assert result == Overlap
    assert ends(a) == ((0, 0), (1, 0))
    assert [ends(p) for p in pieces] == [((1, 0), (2, 0))]
    assert ends(b) == ((3, 0), (2, 0))


def test_contained_edge_stays_and_the_longer_one_is_split_around_it():
    a, b = line((0, 0), (3, 0)), line((1, 0), (2, 0))
    result, pieces = a.Cut(b)
    assert result == Overlap
    assert ends(b) == ((1, 0), (2, 0))
    assert ends(a) == ((0, 0), (1, 0))
    assert [ends(p) for p in pieces] == [((2, 0), (3, 0))]


def test_contained_edge_with_a_shared_endpoint():
    a, b = line((0, 0), (2, 0)), line((0, 0), (1, 0))
    result, pieces = a.Cut(b)
    assert result == Overlap
    assert ends(b) == ((0, 0), (1, 0))
    assert ends(a) == ((1, 0), (2, 0))
    assert pieces == []


def test_congruent_lines():
    a, b = line((0, 0), (1, 0)), line((1, 0), (0, 0))
    result, pieces = a.Cut(b)
    assert result == Congruent
    assert pieces == []
    assert ends(a) == ((0, 0), (1, 0))
    assert ends(b) == ((1, 0), (0, 0))


def test_line_through_an_arc_splits_both_twice():
    a, b = line((-2, 0.5), (2, 0.5)), arc((1, 0), (-1, 0), (0, 0))
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert len(pieces) == 4
    x = math.sqrt(0.75)
    assert point(a.end) == pytest.approx((-x, 0.5))
    assert point(b.end) == pytest.approx((x, 0.5))


def test_line_ending_on_an_arc_splits_the_arc_at_its_endpoint():
    a, b = line((0, 0), (0, 1)), arc((1, 0), (-1, 0), (0, 0))
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert b.end == a.end
    assert ends(a) == ((0, 0), (0, 1))
    assert len(pieces) == 1


def test_tangent_line_touches_the_arc_in_one_point():
    a, b = line((-2, 1), (2, 1)), arc((1, 0), (-1, 0), (0, 0))
    result, pieces = b.Cut(a)
    assert result == Crossing
    assert point(a.end) == point(b.end) == (0, 1)
    assert len(pieces) == 2


def test_crossing_arcs():
    a, b = arc((1, 0), (-1, 0), (0, 0)), arc((2, 0), (0, 0), (1, 0))
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert point(a.end) == pytest.approx((0.5, math.sqrt(0.75)))
    assert a.end == b.end
    assert len(pieces) == 2


def test_externally_tangent_arcs_touch_in_one_point():
    a = arc((0, 1), (0, -1), (0, 0), ccw=False)
    b = arc((2, 1), (2, -1), (2, 0))
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert point(a.end) == point(b.end) == (1, 0)
    assert len(pieces) == 2


def test_internally_tangent_arcs_touch_in_one_point():
    a = arc((0, 2), (0, -2), (0, 0), ccw=False)
    b = arc((1, 1), (1, -1), (1, 0), ccw=False)
    result, pieces = a.Cut(b)
    assert result == Crossing
    assert point(a.end) == point(b.end) == (2, 0)
    assert len(pieces) == 2


def test_arcs_on_one_circle_overlap():
    a, b = arc((1, 0), (-1, 0), (0, 0)), arc((0, 1), (0, -1), (0, 0))
    result, pieces = a.Cut(b)
    assert result == Overlap
    assert ends(a) == ((1, 0), (0, 1))
    assert [ends(p) for p in pieces] == [((0, 1), (-1, 0))]
    assert ends(b) == ((-1, 0), (0, -1))


def test_arcs_on_one_circle_can_overlap_twice():
    a, b = arc((1, 0), (0, -1), (0, 0)), arc((-1, 0), (0, 1), (0, 0))
    result, pieces = a.Cut(b)
    assert result == Overlap
    assert ends(a) == ((1, 0), (0, 1))
    assert [ends(p) for p in pieces] == [((0, 1), (-1, 0)), ((-1, 0), (0, -1))]
    assert ends(b) == ((0, -1), (1, 0))


def test_congruent_arcs_in_opposite_directions():
    a, b = arc((1, 0), (-1, 0), (0, 0)), arc((-1, 0), (1, 0), (0, 0), ccw=False)
    result, pieces = a.Cut(b)
    assert result == Congruent
    assert pieces == []


def test_complementary_half_circles_do_not_cut():
    a, b = arc((1, 0), (-1, 0), (0, 0)), arc((-1, 0), (1, 0), (0, 0))
    result, pieces = a.Cut(b)
    assert result == Untouched
    assert pieces == []


def test_lines_at_a_tiny_angle_share_the_stretch_between_their_endpoints():
    a = line((65.1159010791497, 61.93992268170584), (64.97355107914969, 61.408662681705835))
    b = line((65.13531100630003, 62.012362409824895), (65.11590100630004, 61.939922409824895))
    result, pieces = a.Cut(b)
    assert result == Overlap
    stretch = seg((65.1159010791497, 61.93992268170584), (65.11590100630004, 61.939922409824895))
    assert [seg(*ends(e)) for e in [a, b] + pieces].count(stretch) == 1
