# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import point, square


def references():
    """a construction wall at x = 5 and a construction circle around (5, 0) with radius 1"""
    s = px.Move((5, -1)).Line((5, 5), id="wall", construction=True)
    return s.Circle((5, 0), 1, id="c", construction=True).Move((0, 0))


def ends(sketch):
    return [(point(e.start), point(e.end)) for w in sketch.Wires() for e in w.edges]


def test_until_goes_to_the_named_target_or_to_anything():
    assert point(references().Line(until="wall").pen.position) == (5, 0)
    assert point(references().Line(until="c").pen.position) == (4, 0)
    assert point(references().Line("c", angle=0, until=px.last).pen.position) == (6, 0)
    # the circle comes before the wall
    assert point(references().Line(until=px.first).pen.position) == (4, 0)
    assert point(references().Line(until=px.last).pen.position) == (6, 0)
    with pytest.raises(ValueError, match=r"Line\(until=px.first\): nothing ahead"):
        references().Move((10, 0)).Line(until=px.first)
    assert point(references().Move((10, 0)).Line(until=px.first, reverse=True).pen.position) == (6, 0)
    with pytest.raises(ValueError, match=r"Line\(until='first'\): until='first' names no element; until=px.first"):
        references().Line(until="first")
    with pytest.raises(TypeError, match="until is px.first, px.last or a target"):
        references().Line(until=3)


def test_until_skips_the_edge_it_starts_on():
    s = px.Move((0, 0)).Line((2, 0)).Line((2, 2)).Move((1, 2)).Direction(0, -1)
    assert point(s.Line(until=px.first).pen.position) == (1, 0)
    s = px.Move((0, 0)).Line((2, 0)).Direction(0, 1).Line(1).Direction(-1, 0)
    with pytest.raises(ValueError, match="nothing ahead"):
        s.Line(until=px.first)


def test_to_ends_on_either_side_and_from_starts_a_new_wire():
    s = references().Move((10, 0)).Line("wall", angle=0)
    assert point(s.pen.position) == (5, 0) and point(s.pen.direction) == (-1, 0)
    s = references().Line("c", "wall", angle=0)
    assert ends(s) == [((4, 0), (5, 0))]
    assert ends(references().Line("c", "wall", angle=0, reverse=True)) == [((6, 0), (5, 0))]
    # a point with from picks its end, to takes the nearest of the others
    assert ends(references().Line("c", "c", angle=0)) == [((4, 0), (6, 0))]
    assert ends(references().Line(("c", (10, 0)), "c", angle=0)) == [((6, 0), (4, 0))]


def test_targets_as_objects():
    wall = px.Line((5, -1), (5, 1))
    assert point(px.Move((0, 0)).Line(until=wall).pen.position) == (5, 0)
    assert point(px.Move((0, 0.5)).Line(until=wall.edges[0]).pen.position) == (5, 0.5)
    assert point(px.Move((-1, 0.5)).Line(until=square(0, 0)).pen.position) == (0, 0.5)
    assert point(px.Move((-1, 0.5)).Line((-1, 0.5), square(0, 0), angle=0, until=px.last).pen.position) == (1, 0.5)
    lifted = px.Line((5, -1), (5, 1)).Translated((0, 0, 1))
    with pytest.raises(ValueError, match="another plane"):
        px.Move((0, 0)).Line(until=lifted)


def test_a_single_positional_with_until_is_the_from_and_needs_something_to_run_into():
    with pytest.raises(ValueError, match=r"nothing ahead; until=px.first and px.last see the sketch's own elements, until=<target> goes "
                                         r"until a given target"):
        px.Move((-1, 0.5)).Line(square(0, 0), angle=0, until=px.last)
    s = px.Move((5, -3)).Line((5, 3), id="ref", construction=True).Move((-1, 0.5))
    end = s.Line(square(0, 0), angle=0, until="ref").pen.position
    assert point(end) == pytest.approx((5, 0.5), abs=1e-13)


def test_parallel_through_the_current_position_or_to_a_target():
    s = px.Move((0, 0)).Line((4, 4), id="diag").Move((0, 1)).Line(math.sqrt(2), parallel="diag")
    assert point(s.pen.position) == pytest.approx((1, 2))
    s.Direction(-1, 0).Line(math.sqrt(2), parallel="diag")
    assert point(s.pen.position) == pytest.approx((0, 1))
    base = px.Move((0, 0)).Line((4, 0), id="base")
    with pytest.raises(ValueError, match="Offset"):
        base.Copy().Line(parallel="base", distance=1)
    s = references().Line((4, 0), id="base").Line("wall", parallel="base", distance=1)
    (start, end) = ends(s)[-1]
    assert start == (4, 1) and end == pytest.approx((5, 1))


def test_perpendicular_and_angle():
    s = references().Line((4, 0), id="base").Move((2, 0)).Line(3, perpendicular="base")
    assert point(s.pen.position) == pytest.approx((2, 3)) or point(s.pen.position) == pytest.approx((2, -3))
    s = px.Move((0, 0)).Line((4, 0), id="base").Move((1, 0)).Direction(0, -1).Line(2, perpendicular="base")
    assert point(s.pen.position) == pytest.approx((1, -2))
    assert point(px.Move((0, 0)).Line(2, angle=90).pen.position) == pytest.approx((0, 2))
    s = px.Move((0, 0)).Line((4, 4), id="diag").Line(2, angle=(-45, "diag"))
    assert point(s.pen.position) == pytest.approx((6, 4))


def test_congruent_extends_an_edge():
    s = references().Line((2, 0), id="a")
    s.Move(s.Wire("a").edges[0].end).Line("wall", coincident="a", id="ext")
    assert ends(s) == [((0, 0), (5, 0))]
    e = s.Wire("ext").edges[0]
    assert (point(e.start), point(e.end)) == ((2, 0), (5, 0))
    s = references().Line((2, 0), id="a").Move((3, 2)).Line("wall", coincident="a")
    assert ends(s)[-1] == ((3, 0), (5, 0))


def test_tangent_to_a_circle():
    r = math.sqrt(24) / 5
    assert point(references().Line(tangent="c").pen.position) == pytest.approx((4.8, r))
    assert point(references().Line(tangent=("c", (5, -1))).pen.position) == pytest.approx((4.8, -r))
    s = references().Line(tangent="c")
    t = s.pen.position
    assert (t[0] - 5) * t[0] + t[1] * t[1] == pytest.approx(0, abs=1e-12)
    s = references().Move((5, 1)).Line(2, tangent="c")
    assert point(s.pen.position) == pytest.approx((7, 1))
    with pytest.raises(ValueError, match="no tangent"):
        references().Move((5, 0.5)).Line(tangent="c")
    with pytest.raises(ValueError, match="a tangent starts at the pen or at a point given as from"):
        references().Line("wall", "c", tangent="c")


def test_tangent_to_a_spline():
    s = px.Move((0, 2)).Spline([(1, 1), (2, 0.8), (3, 1.2), (4, 2)], id="curve", construction=True).Move((2, -1))
    t = s.Line(tangent=("curve", (1.5, 0.8))).pen.position
    curve = s.Wire("curve").edges[0].edge
    d = s.pen.direction
    # the line touches the spline's approximation: its tangent there, not the exact curve's
    tangent = next(x for x in (q.Tangent(t) for q in curve.approximation) if (x.x, x.y) != (0, 0))
    assert abs(d[0] * tangent.y - d[1] * tangent.x) < 1e-5


def test_errors_name_the_call():
    s = references()
    with pytest.raises(ValueError, match=r"Line\(2.0, until='wall'\): give the end once: a length, a positional to or until="):
        s.Line(2, until="wall")
    with pytest.raises(TypeError, match=r"Line\(to=...\): the ends are positional: Line\(start, end, ...\), or Line\(to, ...\)"):
        s.Line(to="wall")
    with pytest.raises(TypeError, match="Line: Line"):
        s.Line("wall", 2)
    with pytest.raises(ValueError, match=r"Line\('wall'\): an element as an end needs what fixes the line: through=, "
                                         r"parallel=, perpendicular=, angle=, coincident= or tangent=; until=<target>"):
        s.Line("wall")
    with pytest.raises(ValueError, match="until= picks among the intersections with an element as the end, a point has none; "
                                         "through="):
        s.Line((0, 0), (5, 3), until=px.first)
    with pytest.raises(ValueError, match="give the end once"):
        s.Line("wall", "c", angle=0, until="c")
    with pytest.raises(ValueError, match="give at most one of parallel"):
        s.Line(5, parallel="wall", angle=3)
    with pytest.raises(ValueError, match="parallel needs a line"):
        s.Line(1, parallel="c")
    with pytest.raises(ValueError, match="distance needs parallel"):
        s.Line(1, distance=2)
    with pytest.raises(ValueError, match="give the end: a length, a positional to or until="):
        s.Line(perpendicular="wall")
    with pytest.raises(ValueError, match=r"Line\('nothing'\): no element 'nothing'"):
        s.Line("nothing")
    with pytest.raises(ValueError, match="no element 'p'"):
        s.Line(1, through="p")


def test_through_a_named_point_and_construction_lines():
    s = references().Vertex("p", (0, 3)).Line("wall", through="p", angle=0, construction=True)
    # construction leaves the drawing pen where it was
    assert point(s.pen.position) == (0, 0)
    assert s.Construction().edges[-1].end.x == 5
    assert s.Wires() == []
    assert len(s.Construction().edges) == 3


def test_lines_ending_on_a_spline_make_a_face():
    s = px.Move((0, 0)).Line((4, 0)).Line((4, 1))
    s.Spline([(3, 2), (2, 1.5), (1, 2), (0, 1)], id="curve").Close()
    s.Move((2, 0)).Direction(0, 1).Line(until="curve", name="split")
    face = s.Face()
    assert sum(e.name == "split" for e in face.edges) == 0
    assert face.mass == pytest.approx(px.Move((0, 0)).Line((4, 0)).Line((4, 1)).Spline([(3, 2), (2, 1.5), (1, 2), (0, 1)]).Close().Face().mass)


def test_from_and_to_take_points_the_line_must_pass_through():
    s = px.QuickSketch().Vertex("A", (1, 1))
    s.Line("A", (4, 5))
    assert ends(s)[-1] == ((1, 1), (4, 5))
    # a tangent from A to the circle around (5, 0) with radius 1, ending at a point B on it
    s = references().Vertex("A", (0, 0))
    touch = (5 - 1 / 5, 2 * math.sqrt(6) / 5)
    b = (2 * touch[0], 2 * touch[1])
    s.Line("A", b, tangent=("c", (5, 1)))
    start, end = ends(s)[-1]
    assert start == (0, 0) and end == pytest.approx(b, abs=1e-12)
    with pytest.raises(ValueError, match=r"to \(9.6, 2\) is not on the line"):
        references().Line((0, 0), (9.6, 2.0), tangent=("c", (5, 1)))
    with pytest.raises(ValueError, match=r"from \(1, 1\) is not on the line"):
        references().Line((1, 1), (0, 5), parallel="wall", through=(0, 0))
    with pytest.raises(ValueError, match="no element 'nowhere'"):
        references().Line("nowhere", (0, 1))


def test_an_arc_from_and_to_points_on_its_circle():
    s = px.QuickSketch().Arc((1, 0), (0, 1), center=(0, 0))
    assert ends(s)[-1] == ((1, 0), (0, 1))
    with pytest.raises(ValueError, match=r"to \(0, 2\) is not on the arc's circle"):
        px.QuickSketch().Arc((1, 0), (0, 2), center=(0, 0))
    # two ends and a radius: counterclockwise the short way
    s = px.QuickSketch().Arc((2, 0), (1, 1), 1)
    assert ends(s)[-1] == ((2, 0), (1, 1))
