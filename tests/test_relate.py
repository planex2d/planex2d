# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""DE-9IM: the matrices of the standard configurations (rows the first operand, columns the second) and the named
relations on them"""
import pytest

import planex as px


def square(x, y, s=2.0):
    return px.Move((x, y)).Rectangle(s, s).Face()


def transposed(m):
    return "".join(m[3 * j + i] for i in range(3) for j in range(3))


@pytest.mark.parametrize("b, matrix", [
    (square(1, 1), "212101212"),            # overlapping
    (square(5, 0), "FF2FF1212"),            # disjoint
    (square(2, 0), "FF2F11212"),            # sharing an edge
    (square(2, 2), "FF2F01212"),            # touching at a corner
    (square(0, 0), "2FFF1FFF2"),            # equal
    (square(0.5, 0.5, 1), "212FF1FF2"),     # containing, no contact
    (square(0, 0, 1), "212F11FF2"),         # containing, touching inside along two edges
])
def test_two_faces(b, matrix):
    a = square(0, 0)
    assert a.Relate(b) == matrix
    assert b.Relate(a) == transposed(matrix)


@pytest.mark.parametrize("line, matrix", [
    (px.Line((-1, 1), (3, 1)), "1F20F1102"),   # through the face
    (px.Line((1, 1), (2, 1)), "102F01FF2"),    # inside, one end on the boundary
    (px.Line((0, 0), (2, 0)), "FF2101FF2"),    # along an edge
    (px.Line((3, 0), (4, 1)), "FF2FF1102"),    # apart
])
def test_a_face_and_a_line(line, matrix):
    a = square(0, 0)
    assert a.Relate(line) == matrix
    assert line.Relate(a) == transposed(matrix)


@pytest.mark.parametrize("b, matrix", [
    (px.Line((0, 2), (2, 0)), "0F1FF0102"),    # crossing
    (px.Line((2, 2), (3, 3)), "FF1F00102"),    # meeting at an end, collinear
    (px.Line((1, 1), (3, 3)), "1010F0102"),    # overlapping
    (px.Line((0, 0), (2, 2)), "1FFF0FFF2"),    # equal
])
def test_two_lines(b, matrix):
    a = px.Line((0, 0), (2, 2))
    assert a.Relate(b) == matrix
    assert b.Relate(a) == transposed(matrix)


@pytest.mark.parametrize("point, matrix", [((1, 1), "0F2FF1FF2"), ((2, 1), "FF20F1FF2"), ((3, 1), "FF2FF10F2")])
def test_a_face_and_a_point(point, matrix):
    assert square(0, 0).Relate(point) == matrix


def test_a_line_a_ring_and_points():
    line = px.Line((0, 0), (2, 0))
    assert line.Relate((1, 0)) == "0F1FF0FF2"
    assert line.Relate((0, 0)) == "FF10F0FF2"
    assert line.Relate((1, 1)) == "FF1FF00F2"
    ring = square(0, 0).outer
    assert ring.Relate((1, 1)) == "FF1FFF0F2"
    assert ring.Relate((2, 1)) == "0F1FFFFF2"


def test_a_disc_and_a_square():
    disc = px.Circle((2, 1), 1).Face()
    assert square(0, 0).Relate(disc) == "212101212"
    assert square(-1, -1, 4).Relate(px.Circle((1, 1), 0.5).Face()) == "212FF1FF2"


def test_the_named_relations():
    a, inner = square(0, 0), square(0.5, 0.5, 1)
    assert a.Intersects(square(1, 1)) and a.Overlaps(square(1, 1)) and not a.Touches(square(1, 1))
    assert a.Touches(square(2, 0)) and a.IsDisjoint(square(5, 0)) and not a.Intersects(square(5, 0))
    assert a.Contains(inner) and inner.IsWithin(a) and a.Covers(inner) and inner.IsCoveredBy(a)
    assert a.Covers(square(0, 0, 1)) and not a.Contains(square(5, 0))
    assert a.Equals(square(0, 0)) and not a.Equals(inner)
    line = px.Line((-1, 1), (3, 1))
    assert line.Crosses(a) and a.Crosses(line) and not a.Overlaps(line)
    assert px.Line((0, 0), (2, 2)).Crosses(px.Line((0, 2), (2, 0)))
    assert px.Line((0, 0), (2, 0)).Overlaps(px.Line((1, 0), (3, 0)))
    assert px.Line((0, 0), (2, 0)).Contains((1, 0)) and not px.Line((0, 0), (2, 0)).Contains((0, 0))


def test_shapes_patterns_and_points():
    s = px.Shape(square(0, 0))
    assert s.Relate(square(1, 1)) == "212101212"
    assert s.Relate(square(1, 1), "T*T***T**")
    assert not s.Relate(square(1, 1), "FF*FF****")
    assert s.Contains((1, 1)) and not s.Contains((2, 1), boundary=False)
    assert square(0, 0).Contains([(1, 1), (3, 3)]).tolist() == [True, False]


def test_what_has_no_relation():
    a = square(0, 0)
    with pytest.raises(ValueError, match="a pattern is nine characters"):
        a.Relate(a, "T*F")
    with pytest.raises(TypeError, match="a Compound has no relations"):
        a.Relate(px.Compound([a]))
    with pytest.raises(ValueError, match="a shape of faces and wires has no relation"):
        a.Relate(px.Glue([square(1, 1), px.Line((5, 5), (6, 6))]))
    with pytest.raises(ValueError, match="different planes"):
        a.Relate(px.QuickSketch(px.XZ).Move((0, 0)).Rectangle(1, 1).Face())
