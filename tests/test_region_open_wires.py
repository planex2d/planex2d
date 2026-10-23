# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px


def rect_lines(x=0.0):
    return [px.Line((x, 0), (x + 4, 0)), px.Line((x + 4, 0), (x + 4, 2)), px.Line((x + 4, 2), (x, 2)), px.Line((x, 2), (x, 0))]


def rect_wire(x=0.0):
    w = px.Wire()
    for l in rect_lines(x):
        w.Add(l)
    return w


def quiet(make):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        return make()


def areas(s):
    return [pytest.approx(f.mass, abs=1e-12) for f in s.faces]


@pytest.mark.parametrize("line", [((4, 1), (4, 2)), ((4, 1), (4, 3)), ((4, 0.5), (4, 1.5)), ((4, 0), (4, 2)),
                                  ((4, -1), (4, 3)), ((1, 2), (6, 2))])
def test_an_open_line_on_an_outline_edge_keeps_the_region(line):
    assert areas(quiet(lambda: px.Shape([rect_wire(), px.Line(*line)]))) == [8]


@pytest.mark.parametrize("cls", [px.QuickSketch, pytest.param(px.Sketch, marks=pytest.mark.constraints, id="Sketch")])
def test_the_line_on_the_outline_in_a_sketch(cls):
    sk = cls()
    sk.Move((4, 1)).Line(0, 2)
    sk.Move((0, 0)).Line(4, 0).Line(0, 2).Line(-4, 0).Close()
    with pytest.warns(px.PlanexWarning, match="open wire"):
        assert sk.Face().mass == pytest.approx(8, abs=1e-12)
    assert areas(quiet(sk.Shape)) == [8]


def test_the_line_on_the_outline_in_shape_wires_and_a_closed_wire_takes_nothing_more():
    with pytest.warns(px.PlanexWarning, match="open wire"):
        assert areas(px.Shape([rect_wire(), px.Line((4, 1), (4, 3))])) == [8]
    with pytest.raises(ValueError, match="closed"):
        rect_wire().Add(px.Line((4, 1), (4, 3)))


def test_a_line_exactly_on_a_side_is_left_out_silently_and_the_side_keeps_its_properties():
    outline = rect_wire()
    outline.name = "outline"
    line = px.Line((4, 0), (4, 2))
    line.name, line.maxh = "cut", 0.1
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Shape([outline, line])
    assert areas(s) == [8]
    assert {(e.name, e.maxh) for e in s.edges} == {("outline", None)}


def test_a_partly_overlapping_line_leaves_the_side_as_it_was():
    outline = rect_wire()
    outline.name = "outline"
    line = px.Line((4, 1), (4, 3))
    line.name = "cut"
    s = quiet(lambda: px.Shape([outline, line]))
    assert areas(s) == [8]
    assert {e.name for e in s.edges} == {"outline"} and len(s.edges) == 4


def test_four_separate_lines_and_a_duplicate_side_keep_the_rectangle():
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Shape(rect_lines() + [px.Line((4, 0), (4, 2))])
    assert areas(s) == [8]


def test_two_closed_rectangles_sharing_a_side_still_cancel_it():
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Shape([rect_wire(), rect_wire(4)])
    assert areas(s) == [16]
    assert not [e for e in s.edges if e.start.x == 4 and e.end.x == 4]


def test_a_stretch_of_closed_loops_with_an_open_line_on_it_still_cancels():
    below = rect_wire().Translated((0, -2))
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Shape([rect_wire(), below, px.Line((0, 0), (4, 0))])
    assert areas(s) == [16]
    assert not [e for e in s.edges if e.start.y == 0 and e.end.y == 0]


def test_a_stretch_drawn_three_times_by_closed_loops_goes_completely():
    below = rect_wire().Translated((0, -2))
    with pytest.warns(px.PlanexWarning, match="open wire"):
        s = px.Shape([rect_wire(), rect_wire(), below])
    assert len(s.faces) == 0


def test_a_wire_closed_within_rounding_counts_as_closed():
    w = px.Line((0, 0), (4, 0))
    for p, q in (((4, 0), (4, 2)), ((4, 2), (0, 2)), ((0, 2), (0, 1e-15))):
        w.Add(px.Line(p, q))
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Shape([w, rect_wire(4)])
    assert areas(s) == [16]


def test_a_pen_that_retraces_an_edge_keeps_the_region():
    s = px.QuickSketch().Move((0, 0)).Line(4, 0).Line(0, 2).Line(-4, 0).Line(0, -2).Line(4, 0)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert areas(s.Shape()) == [8]
