# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import numpy as np
import pytest

import planex as px


def plate(plane=None):
    sk = px.QuickSketch(plane) if plane is not None else px.QuickSketch()
    sk.Move((0, 0)).Line((4, 0), id="a").Line((4, 3), id="b").Line((0, 3), id="c").Close(id="d")
    return sk


def test_a_shapes_edge_ends_are_its_vertices():
    s = plate().Shape()
    e = s.edges.Min("v")[0]
    assert isinstance(e.start, px.VertexView) and isinstance(e.end, px.VertexView)
    assert {e.start.id, e.end.id} <= {v.id for v in s.vertices}
    e.end.hpref = 1
    assert [v.hpref for v in s.vertices].count(1) == 1


def test_standalone_points_are_construction_vertices():
    line = px.LineEdge((0, 0), (4, 0))
    arc = px.ArcEdge((1, 0), (0, 1), (0, 0))
    e = plate().Shape().edges[0]
    for p in (line.start, line.end, arc.center, e.midpoint, e.PointAt(0.25)):
        assert isinstance(p, px.Vertex)


def test_a_vertex_id_can_be_set_and_the_edge_ends_name_it():
    s = plate().Shape()
    e = s.edges.Min("v")[0]
    corner = e.end if e.end.h > e.start.h else e.start
    corner.id = "corner"
    assert corner.id == s.id + ".corner"
    assert s("corner")[0].id == corner.id
    assert len(s.vertices) == 4 and sum(v.id == corner.id for v in s.vertices) == 1
    assert (4, 0) in [tuple(v) for v in s("corner")]


def test_a_vertex_id_follows_the_user_id_rule_and_is_unique():
    s = plate().Shape()
    v0, v1 = s.vertices[0], s.vertices[1]
    v0.id = "corner"
    v0.id = "corner"
    with pytest.raises(ValueError, match="'corner' is taken"):
        v1.id = "corner"
    with pytest.raises(ValueError, match="contains '.'"):
        v1.id = "a.b"
    with pytest.raises(ValueError, match="must start with a letter"):
        v1.id = "1st"


def test_a_sketch_point_id_renames_the_point_element():
    sk = plate()
    p = sk("a.end")[0]
    assert p.id.startswith(sk.id + ".Point.")
    p.id = "corner"
    assert p.id == sk.id + ".corner"
    assert "corner" in sk.elements["Point"]
    assert sk.Location("corner") == pytest.approx((4, 0))
    assert sk.Location("a.end") == pytest.approx((4, 0))
    assert sk("a.end")[0].id == sk.id + ".corner" and sk("corner")[0].id == sk.id + ".corner"
    sk("corner").hpref = 2
    assert sk("b.start")[0].hpref == 2
    with pytest.raises(ValueError, match="is taken"):
        sk("b.end")[0].id = "corner"
    with pytest.raises(ValueError, match="contains '.'"):
        sk("b.end")[0].id = "x.y"


def test_a_place_has_no_id_to_set():
    sk = plate()
    with pytest.raises(AttributeError, match="is a place"):
        sk("a.mid")[0].id = "half"


@pytest.mark.constraints
def test_a_renamed_point_takes_constraints_and_dimensions():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0.5, px.Horizontal, id="a").Line(0.2, 3, px.Vertical, id="b")
    sk("a.end")[0].id = "corner"
    sk.Constrain(sk.origin, "corner", px.Distance, 5, id="w")
    assert sk.Location("a.end") == pytest.approx((5, 0))
    sk["w"] = 6
    sk.Update()
    assert sk.Location("corner") == pytest.approx((6, 0))


@pytest.mark.parametrize("kind", ["VertexView", "px.Vertex", "sketch point", "shape place", "sketch place"])
def test_every_point_acts_as_its_coordinates(kind):
    sk = plate()
    s = sk.Shape()
    e = s.edges.Min("v")[0]
    p, hv = {
        "VertexView": lambda: (e.end if e.end.h > e.start.h else e.start, (4, 0)),
        "px.Vertex": lambda: (px.Vertex((4, 0)), (4, 0)),
        "sketch point": lambda: (sk("a.end")[0], (4, 0)),
        "sketch place": lambda: (sk("a.mid")[0], (2, 0)),
        "shape place": lambda: (s(e.id[len(s.id) + 1:] + ".mid")[0], (2, 0)),
    }[kind]()
    h, v = p
    assert (h, v) == pytest.approx(hv) and (p.h, p.v) == pytest.approx(hv)
    assert p == pytest.approx(hv) and p == hv and p != (hv[0] + 1, hv[1])
    assert np.array(p).shape == (2,) and np.array(p) == pytest.approx(hv)
    assert len(p) == 2 and p[0] == pytest.approx(hv[0]) and p[-1] == pytest.approx(hv[1])


@pytest.mark.parametrize("kind", ["VertexView", "sketch point", "shape place"])
def test_a_point_in_a_plane_has_its_3d_coordinates(kind):
    sk = plate(px.Plane(origin=(1, 2, 3), normal=(1, 0, 0)))
    s = sk.Shape()
    e = s.edges.Min("v")[0]
    p, hv, xyz = {
        "VertexView": lambda: (e.end if e.end.h > e.start.h else e.start, (4, 0), (1, 6, 3)),
        "sketch point": lambda: (sk("a.end")[0], (4, 0), (1, 6, 3)),
        "shape place": lambda: (s(e.id[len(s.id) + 1:] + ".mid")[0], (2, 0), (1, 4, 3)),
    }[kind]()
    assert tuple(p) == pytest.approx(hv)
    assert (p.x, p.y, p.z) == pytest.approx(xyz)


def test_a_point_of_another_plane_is_measured_where_it_is_in_3d():
    tilted = plate(px.Plane(origin=(1, 2, 3), normal=(1, 0, 0)))
    s = plate().Shape()
    assert s.Distance(tilted("a.end")[0]) == pytest.approx(18**0.5)
