# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import copy

import pytest

import planex as px
from helpers import segments, square


def corners(polygon):
    return {(round(l.start.x, 12), round(l.start.y, 12)) for l in polygon.lines}


def make_wire():
    return px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Wire()


def make_compound():
    return px.Compound([square(0, 0), px.Line((0.5, -1), (0.5, 2))])


def make_shape():
    return square(0, 0) + square(3, 0)


@pytest.mark.parametrize("make", [lambda: square(0, 0), make_wire, make_shape, make_compound])
@pytest.mark.parametrize("copier", [lambda o: o.Copy(), copy.copy, copy.deepcopy])
def test_copy_is_independent_of_the_original(make, copier):
    original = make()
    c = copier(original)
    assert c is not original
    assert type(c) is type(original)
    assert c.bounding_box == original.bounding_box
    c.Translate((5, 0))
    assert original.bounding_box == make().bounding_box
    assert c.bounding_box[0][0] == pytest.approx(original.bounding_box[0][0] + 5)


def test_polygon_copy_has_the_same_edges():
    p = square(0, 0) + square(0.5, 0.5)
    assert segments(p.Copy()) == segments(p)
    assert p.Copy().mass == pytest.approx(p.mass)


def test_edge_properties_of_a_copy_change_alone():
    w = make_wire()
    w.name = "a"
    c = w.Copy()
    c.edges[0].name = "b"
    assert [e.name for e in w.edges] == ["a", "a"]
    assert [e.name for e in c.edges] == ["b", "a"]


def test_workplane_copy_keeps_drawing_independently():
    wp = px.Move((0, 0)).Line((1, 0))
    c = wp.Copy()
    c.Line((1, 1))
    wp.Line((0, 1))
    assert c.pen.position == (1, 1)
    assert wp.pen.position == (0, 1)
    assert len(c.Wire().edges) == 2
    assert len(wp.Wire().edges) == 2


def test_vertex_copy():
    v = px.Vertex((1, 2))
    for c in (v.Copy(), copy.copy(v), copy.deepcopy(v)):
        c.x = 5
        assert v.x == 1
        assert c.y == 2


def test_deepcopy_of_containers():
    parts = [square(0, 0), square(2, 0)]
    copied = copy.deepcopy(parts)
    copied[0].Translate((0, 3))
    assert corners(parts[0]) == {(0, 0), (1, 0), (1, 1), (0, 1)}


def test_every_public_object_copies_to_an_independent_one():
    import copy

    plane = px.Plane(origin=(0, 0, 1))
    assert plane.Copy().origin == plane.origin and copy.deepcopy(plane).origin == plane.origin
    for edge in (px.LineEdge((0, 0), (1, 0)), px.Arc((1, 0), (-1, 0), (0, 0)).edges[0].edge,
                 px.Spline([(0, 0), (1, 1), (2, 0)]).edges[0].edge):
        c = edge.Copy()
        assert type(c) is type(edge) and c.start == edge.start and copy.copy(edge).end == edge.end
    t = px.Translation(1, 2)
    assert t.Copy().vector == (1, 2)
    shape = px.Move((0, 0)).Rectangle(2, 1).Shape()
    shape.edges[0].name = "bottom"
    w = shape.edges[0].Copy()
    assert isinstance(w, px.Wire) and w.edges[0].name == "bottom" and copy.copy(shape.edges[0]).edges[0].name == "bottom"
    w.edges[0].name = "other"
    assert shape.edges[0].name == "bottom"
    v = shape.vertices[1].Copy()
    assert isinstance(v, px.Vertex) and (v.x, v.y) == (shape.vertices[1].p.x, shape.vertices[1].p.y)
    assert isinstance(copy.deepcopy(shape.faces[0]), px.Face)
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((3, 0), id="a")
    assert isinstance(sk("a")[0].Copy(), px.Wire) and len(sk("a")[0].Copy().edges) == 1
    end = sk("a.end")[0].Copy()
    assert isinstance(end, px.Vertex) and (end.x, end.y) == (3, 0)
    assert isinstance(shape("Line.1.mid")[0].Copy(), px.Vertex)


def test_a_selection_copies_everything_in_it_into_a_compound():
    import copy

    shape = px.Move((0, 0)).Rectangle(2, 1).Shape()
    shape.edges[0].name = shape.edges[2].name = "long"
    c = shape.edges(name="long").Copy()
    assert isinstance(c, px.Compound) and len(c.parts) == 2 and all(isinstance(p, px.Wire) for p in c.parts)
    assert [p.edges[0].name for p in c.parts] == ["long", "long"]
    c.parts[0].edges[0].name = "changed"
    assert shape.edges[0].name == "long"
    faces = shape.faces.Copy()
    assert isinstance(faces, px.Compound) and isinstance(faces.parts[0], px.Face) and len(shape.edges.Copy().parts) == 4
    assert isinstance(copy.deepcopy(shape.edges(name="long")), px.Compound)
    with pytest.raises(TypeError, match="a Compound holds faces, wires and shapes"):
        shape.vertices.Copy()
