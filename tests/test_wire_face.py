# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px


def square(x=0.0, y=0.0, s=1.0):
    return px.QuickSketch((x, y)).Rectangle(s, s).Face()


def lengths(shape):
    return sorted(round(w.mass, 12) for w in shape.wires)


def test_a_line_across_a_square():
    line = px.Line((-1, 0.5), (2, 0.5))
    line.name = "cut"
    inside, outside = line * square(), line - square()
    assert type(inside) is px.Shape and len(inside.faces) == 0
    assert lengths(inside) == [1] and lengths(outside) == [1, 1]
    assert {e.name for e in inside.edges} == {"cut"} and {e.name for e in outside.edges} == {"cut"}
    assert lengths(square() * line) == [1]


def test_the_boundary_counts_as_inside():
    along = px.Line((-1, 0), (3, 0))
    assert lengths(along * square()) == [1] and lengths(along - square()) == [1, 2]
    assert len((px.Line((0, 0), (1, 0)) - square()).edges) == 0


def test_an_arc_against_a_disc():
    disc = px.QuickSketch().Circle((0, 0), 1).Face()
    assert len((px.Arc((-2, 0), (2, 0), (0, 0)) * disc).edges) == 0
    line = px.Line((-2, 0.5), (2, 0.5))
    assert sum(lengths(line * disc)) == pytest.approx(2 * math.sqrt(0.75), rel=1e-12)
    assert sum(lengths(line - disc)) == pytest.approx(4 - 2 * math.sqrt(0.75), rel=1e-12)
    arc = px.Arc((-2, 0.5), (2, 0.5), (0, -3.5), reverse=True)
    inside, outside = sum(lengths(arc * disc)), sum(lengths(arc - disc))
    assert inside > 0 and inside + outside == pytest.approx(arc.mass, rel=1e-12)


def test_several_faces_at_once():
    line = px.Line((-1, 0.5), (4, 0.5))
    a, b = square(0, 0, 2), square(1, 0, 2)
    assert lengths(px.Intersect([line, a, b])) == [1]
    assert lengths(px.Subtract([line, a, b])) == [1, 1]
    assert lengths((line - a) - b) == [1, 1]


def test_a_face_minus_a_wire_is_not_defined():
    with pytest.raises(TypeError, match="face - wire"):
        square() - px.Line((0, 0), (1, 1))
    with pytest.raises(ValueError, match="only faces"):
        px.Intersect([px.Line((0, 0), (1, 1)), px.Line((0, 1), (1, 0))])


@pytest.mark.constraints
def test_sketches_edge_views_and_wire_unions():
    sk = px.QuickSketch().Move((-1, 0.5)).Line((2, 0.5))
    assert lengths(sk * square()) == [1]
    cs = px.Sketch()
    cs.Move((-1, 0.5)).Line(3, 0)
    assert lengths(cs * square()) == [1] and lengths(cs - square()) == [1, 1]
    plate = square(0, 0, 3)
    assert lengths(plate.edges[0] * square(1, -1, 1)) == [1]
    union = px.Line((-1, 0.25), (2, 0.25)) + px.Line((-1, 0.75), (2, 0.75))
    assert lengths(union * square()) == [1, 1]
