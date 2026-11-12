# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The less used forms of selections: copies and texts of what a selection holds, targets of several elements,
sorting keys, coordinate conditions, indexing, and the messages when a call cannot take what is selected."""
import copy

import pytest

import planex as px
from helpers import circle, square

TOL = 1e-9


def two_squares():
    return px.Glue([square(0, 0), square(3, 0)])


def sketch():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((4, 0)).Line((4, 2)).Line((0, 2)).Close()
    sk.Circle((1, 1), 0.5)
    return sk


def test_a_distance_to_several_faces_is_to_their_union():
    s = two_squares()
    both = s.faces[[0, 1]]
    assert len(both) == 2
    assert circle(10, 0.5, 0.5).Distance(both) == pytest.approx(5.5, abs=TOL)


def test_a_distance_to_several_edges_is_to_their_wire():
    s = two_squares()
    right = s.edges[px.x > 3.9]
    assert len(right) == 1
    lines = s.edges[px.x >= 3]
    assert len(lines) == 4
    assert circle(10, 0.5, 0.5).Distance(lines) == pytest.approx(5.5, abs=TOL)
    assert s.faces[0].Distance(right) == pytest.approx(3, abs=TOL)


def test_edges_sort_by_radius_and_refuse_a_key_they_lack():
    s = px.Glue([circle(0, 0, 1), circle(5, 0, 2)])
    assert s.edges.Max("radius")[0].edge.radius == pytest.approx(2)
    assert s.edges.Min("radius")[0].edge.radius == pytest.approx(1)
    with pytest.raises(ValueError, match="no 'pitch' to sort by"):
        s.edges.Sorted("pitch")


def test_near_takes_one_element_a_point_or_an_object():
    s = two_squares()
    with pytest.raises(ValueError, match="a selection of 2 elements has no single center"):
        s.edges(near=s.faces[[0, 1]])
    assert len(s.faces(near=px.Vertex((3.6, 0.4)))) == 1
    assert s.faces(near=square(3, 0))[0].mass == pytest.approx(1)


def test_coordinate_conditions_read_as_written():
    condition = (px.x >= 1) & ~(px.y < 0) | (px.h <= 2)
    assert repr(px.x) == "px.x"
    assert repr(condition) == "Predicate(((x >= 1) & (~(y < 0))) | (h <= 2))"
    assert len(two_squares().vertices[px.x >= 3]) == 4


def test_a_selection_is_true_when_it_holds_something_and_lists_its_ids():
    s = two_squares()
    s.id = "pair"
    chosen = s.edges[px.x > 3.9]
    assert chosen and not s.edges[px.x > 100]
    assert repr(chosen) == "Selection([%s])" % chosen[0].id
    assert [e.id for e in s.edges[[2, 0]]] == [s.edges[2].id, s.edges[0].id]


def test_copies_of_selected_sketch_elements_and_points_are_their_geometry():
    sk = sketch()
    line = sk("Line.1")[0]
    point = sk("Line.1.end")[0]
    assert repr(line) == "<line %s>" % line.id
    assert repr(point).startswith("<point %s (4, 0)" % point.id)
    assert copy.copy(line).mass == pytest.approx(4) and copy.deepcopy(line).mass == pytest.approx(4)
    assert (copy.copy(point).x, copy.deepcopy(point).y) == pytest.approx((4, 0))
    assert line.Measure()["length"] == pytest.approx(4)


def test_a_place_of_a_shape_is_no_id_of_a_sketch():
    face = square(0, 0, 2)
    mid = face("Line.1.mid")[0]
    assert (mid.h, mid.v) == pytest.approx((1, 0))
    assert mid.Distance((1, 3)) == pytest.approx(3)
    assert repr(mid).startswith("<point")
    assert (copy.copy(mid).x, copy.deepcopy(mid).x) == pytest.approx((1, 1))
    sk = sketch()
    with pytest.raises(ValueError, match="is a place of its own, not an id of this sketch"):
        sk.Fillet(mid, 0.1)


def test_a_count_from_the_end_past_the_first_names_what_there_is():
    sk = sketch()
    assert sk("Line.-1")[0].local == "Line.4"
    with pytest.raises(ValueError, match="there are 4 numbers under 'Line' \\(-1 is the last\\)"):
        sk("Line.-5")


def test_one_element_is_asked_for_where_a_call_takes_one():
    s = two_squares()
    with pytest.raises(ValueError, match="the selection has 2 elements, select one"):
        s.faces[[0, 1]].Measure()
    with pytest.raises(TypeError, match="is a point, it has no edges"):
        s.vertices[[0, 1]].Wire()
    assert not s.edges[px.x > 100].Sorted("length")


def test_a_property_set_on_a_selection_needs_it_on_every_element():
    s = two_squares()
    with pytest.raises(AttributeError, match="has no col"):
        s(kind="face|vertex").col = (1, 0, 0)
    with pytest.raises(ValueError, match="the selection selects nothing"):
        s.edges[px.x > 100].name = "none"
    s.edges[px.x > 3.9].name = "right"
    assert s.edges(name="right")[0].start.x == pytest.approx(4)
