# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def square(x, name):
    f = px.QuickSketch((x, 0)).Rectangle(1, 1).Face()
    f.name = name
    return f


def two_squares():
    return px.Glue([square(0, "a"), square(1, "b")])


def plate_cut_in_two():
    return px.Glue([px.QuickSketch((0, 0)).Rectangle(2, 1).Face(), px.Line((1, 0), (1, 1))])


def centers(shape):
    return [(round(f.center[0], 12), round(f.center[1], 12)) for f in shape.faces]


def test_faces_is_a_list_view():
    s = two_squares()
    assert type(s.faces).__name__ == "FacesView" and len(s.faces) == 2
    assert [f.name for f in s.faces] == ["a", "b"] and s.faces[-1].name == "b"
    assert [f.name for f in s.faces[1:]] == ["b"]
    with pytest.raises(IndexError):
        s.faces[2]


def test_face_properties_are_the_shapes():
    s = two_squares()
    s.faces[0].maxh = 0.1
    assert [f.maxh for f in s.faces] == [0.1, None]
    s.faces.maxh = 0.2
    assert s.faces.maxh == 0.2 and s.maxh == 0.2
    assert s.faces.name is None


def test_a_face_of_a_group_changes_alone_and_keeps_its_place():
    s = plate_cut_in_two()
    before = centers(s)
    left, right = s.faces[0], s.faces[1]
    assert len(s.faces) == 2 and left.name is None and right.name is None
    left.name = "left"
    assert right.name is None and centers(s) == before
    right.name = "right"
    assert [f.name for f in s.faces] == ["left", "right"] and centers(s) == before
    assert s.mass == pytest.approx(2)


def test_views_of_other_lists_follow_a_split():
    s = plate_cut_in_two()
    first, second = s.faces[0], s.faces[1]
    first.maxh = 0.3
    second.maxh = 0.4
    assert [f.maxh for f in s.faces] == [0.3, 0.4]


def test_face_edges_are_the_shapes_edges():
    s = two_squares()
    assert len(s.edges) == 7 and [len(f.edges) for f in s.faces] == [4, 4]
    shared = [e for e in s.faces[0].edges if e.midpoint.x == 1]
    assert len(shared) == 1
    shared[0].name = "middle"
    assert [e.name for e in s.faces[1].edges].count("middle") == 1
    s.faces[0].edges.maxh = 0.05
    assert sorted(e.maxh or 0 for e in s.faces[1].edges) == [0, 0, 0, 0.05]
    assert s.faces[0].edges.maxh == 0.05 and s.faces[1].edges.maxh is None


def test_face_vertices_are_the_shapes_vertices():
    s = two_squares()
    assert len(s.vertices) == 6 and [len(f.vertices) for f in s.faces] == [4, 4]
    for v in s.faces[0].vertices:
        if v.p.x == 1 and v.p.y == 0:
            v.hpref = 2
    assert sorted(v.hpref or 0 for v in s.faces[1].vertices) == [0, 0, 0, 2]


def test_views_go_stale_when_the_edges_change_and_survive_motions():
    s = two_squares()
    f, edges = s.faces[0], s.faces[0].edges
    s.Translate((1, 0))
    assert f.center[0] == pytest.approx(1.5) and len(edges) == 4
    s.Simplify()
    with pytest.raises(RuntimeError, match="stale"):
        f.name
    with pytest.raises(RuntimeError, match="stale"):
        edges[0]
    assert [g.name for g in s.faces] == ["a", "b"]


def test_a_face_view_is_a_face_for_isinstance():
    s = two_squares()
    f = s.faces[0]
    assert isinstance(f, px.Face) and type(f) is px.FaceView and issubclass(px.FaceView, px.Face)
    assert not isinstance(f, px.Shape) and not isinstance(px.Line((0, 0), (1, 0)), px.Face)
    assert isinstance(s.edges[0], px.Edge) and isinstance(s.edges[0].edge, px.Edge) and isinstance(s.edges[0].edge, px.LineEdge)
    assert not isinstance(f, px.Edge)


def test_reading_works_as_on_a_copy():
    s = two_squares()
    f = s.faces[1]
    assert f.mass == pytest.approx(1) and len(f.inner) == 0 and len(f.outer.edges) == 4
    assert f.Contains((1.5, 0.5)) and not f.Contains((0.5, 0.5))
    moved = f.Translated((0, 2))
    assert type(moved) is px.Face and moved.name == "b" and centers(s)[1] == (1.5, 0.5)
    c = f.Copy()
    c.name = "c"
    assert type(c) is px.Face and f.name == "b"
    assert f.faces[0] is f and repr(f) == "FaceView(1, name='b')"


def test_in_place_changes_are_refused():
    s = two_squares()
    with pytest.raises(TypeError, match="Copy"):
        s.faces[0].Translate((1, 0))
    with pytest.raises(TypeError, match="Copy"):
        s.faces[0].Simplify()


def test_views_are_taken_where_faces_are():
    s = two_squares()
    a, b = s.faces[0], s.faces[1]
    assert px.Shape(a).mass == pytest.approx(1) and px.Face(b).name == "b"
    assert (a + b).mass == pytest.approx(2) and sum(s.faces).mass == pytest.approx(2)
    assert len(px.Glue([a, b]).faces) == 2 and px.Unify([a, b]).mass == pytest.approx(2)


def test_the_faces_of_a_face_are_itself():
    f = square(0, "a")
    assert f.faces[0] is f
    f.faces[0].name = "x"
    assert f.name == "x"


def test_view_class_names():
    s = two_squares()
    assert type(s.edges).__name__ == "EdgesView" and type(s.edges[0]).__name__ == "EdgeView"
    assert type(s.vertices).__name__ == "VerticesView" and type(s.vertices[0]).__name__ == "VertexView"
    assert repr(s.vertices[0]).startswith("VertexView(")
