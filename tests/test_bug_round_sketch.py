# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


@pytest.mark.parametrize("kind", [px.QuickSketch, px.Sketch])
@pytest.mark.parametrize("given, offset", [({"construction": False}, False), ({}, True)])
def test_an_offset_takes_the_construction_flag_given_else_its_sources(kind, given, offset):
    sk = kind().Move((0, 0)).Line(10, 0, id="src", construction=True)
    sk.Offset("src", 2, id="off", **given)
    assert [sk(i).construction for i in sk.elements["Line"]] == [True, offset, offset]


@pytest.mark.parametrize("hints, reverse, center", [
    ((("a", (10, 10)), ("b", (10, 10))), False, (8, 8)),
    (("a", "b"), False, (8, 8)),
    ((("a", (10, 10)), ("b", (10, 10))), True, (12, 12)),
])
def test_a_point_on_both_lines_picks_no_side(hints, reverse, center):
    sk = px.QuickSketch()
    sk.Move((0, 10)).Line(10, 0, id="a")
    sk.Move((10, 0)).Line(0, 10, id="b")
    sk.Arc(hints[0], hints[1], 2, tangent=("a", "b"), reverse=reverse)
    assert sk.Location("Arc.1.center") == pytest.approx(center, abs=1e-12)


def test_a_spline_closing_up_to_rounding_makes_a_face():
    def area(last):
        return px.QuickSketch().Move((1, 0)).Spline([(0, 1), (-1, 0), (0, -1), last]).Face().mass

    assert area((1, -1.7e-15)) == pytest.approx(area((1, 0)), rel=1e-12) and area((1, 0)) > 3


def test_the_edges_of_a_sketch_selection_are_its_elements():
    sk = px.QuickSketch().Move((0, 0)).Line(5, 0, name="flank").Line(0, 5).Line(-5, 0, name="flank")
    assert len(sk(name="flank").edges) == 2
    with pytest.raises(TypeError, match=r"is a point, it has no edges"):
        sk("Point.1").edges


def test_two_quarter_turns_of_the_pen_are_exact():
    sk = px.QuickSketch().Move((0, 0)).Line(10, 0)
    sk.Turn(90)
    sk.Turn(90)
    sk.Line(10)
    assert sk.Location("Line.2.end") == (0.0, 0.0) and sk.pen.direction == (-1.0, 0.0)


def head_to_tail(wire):
    es = list(wire.edges)
    return all(tuple(a.end) == tuple(b.start) for a, b in zip(es, es[1:] + es[:1]))


@pytest.mark.parametrize("drawn", ["cw", "ccw"])
def test_the_outer_loop_runs_head_to_tail_with_a_spline_in_it(drawn):
    sk = px.QuickSketch().Move((0, 0))
    if drawn == "cw":
        sk.Line(0, 5).Spline([(5, 6), (8, 4), (10, 0)]).Close()
    else:
        sk.Line(10, 0).Spline([(8, 4), (5, 6), (0, 5)]).Close()
    f = sk.Face()
    outer = f.outer
    assert head_to_tail(outer)
    assert [(tuple(e.start), tuple(e.end)) for e in outer.edges] == [((0, 0), (10, 0)), ((10, 0), (0, 5)), ((0, 5), (0, 0))]
    spline = [e for e in f.edges if isinstance(e.edge, px.SplineEdge)][0]
    turned = [e for e in outer.edges if isinstance(e.edge, px.SplineEdge)][0]
    assert tuple(spline.edge.MidPoint()) == tuple(turned.edge.MidPoint())


@pytest.mark.parametrize("hole", [
    lambda: px.QuickSketch().Move((2, 2)).Line(1, 4).Spline([(5, 5), (4, 3)]).Close().Face(),
    lambda: px.QuickSketch().Move((2, 2)).Spline([(4, 3), (5, 5), (3, 6)]).Close().Face(),
    lambda: px.QuickSketch().Circle(8, 3, 1).Face(),
])
def test_holes_run_head_to_tail(hole):
    f = (px.QuickSketch().Move((-1, -1)).Rectangle(12, 9).Face() - hole()).faces[0]
    assert head_to_tail(f.inner[0])


def test_a_clockwise_spline_face_writes_to_gmsh(tmp_path):
    blob = px.Move((0, 0)).Spline([(1, 0.5), (2, 0), (1, -1)]).Close().Face()
    blob.WriteGeo(tmp_path / "s.geo")
    assert sum(line.startswith("BSpline") for line in (tmp_path / "s.geo").read_text().splitlines()) == 1
