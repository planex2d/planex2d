# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest
from netgen import occ

import planex as px
from helpers import ends, point, polygon_area, seg, square


def test_wires_are_the_connected_ones_and_wire_takes_one():
    wp = px.Move((0, 0)).Rectangle(1, 1).Move((5, 5)).Rectangle(2, 2)
    with pytest.raises(ValueError, match=r"Wire\(\): the drawn edges form 2 wires; sk.Wires\(\) returns them"):
        wp.Wire()
    wires = wp.Wires()
    assert [len(w.edges) for w in wires] == [4, 4]
    assert all(isinstance(e.edge, px.LineEdge) for w in wires for e in w.edges)
    assert len(wp.ToOCCWires().wires) == 2


def test_wire_sum_cuts_crossing_edges_into_loops():
    a = px.Move((0, 0)).Rectangle(2, 2).Wire()
    b = px.Move((1, 1)).Rectangle(2, 2).Wire()
    loops = (a + b).wires
    edges = [e for w in loops for e in w.edges]
    assert sorted(len(w.edges) for w in loops) == [4, 8]
    assert len({seg(*ends(e)) for e in edges}) == 12
    assert {point(v) for e in edges for v in (e.start, e.end)} >= {(1, 2), (2, 1)}
    assert polygon_area(a.Face() + b.Face()) == pytest.approx(7)


def test_wire_sum_keeps_a_shared_stretch_once():
    a = px.Move((0, 0)).Rectangle(1, 1).Wire()
    b = px.Move((1, 0)).Rectangle(1, 1).Wire()
    loops = (a + b).wires
    edges = [e for w in loops for e in w.edges]
    # the outline and the shared side (open chains between the two vertices of three edges first)
    assert sorted(len(w.edges) for w in loops) == [1, 6]
    assert len({seg(*ends(e)) for e in edges}) == 7


def test_a_chain_crossing_itself_is_two_wires_and_its_region_two_faces():
    wp = px.Move((0, 0)).Line((2, 2)).Line((2, 0)).Line((0, 2)).Close()
    wires = wp.Wires()
    assert [len(w.edges) for w in wires] == [3, 3]
    with pytest.raises(ValueError, match="2 regions"):
        wp.Face()
    p = px.Shape(wires)
    assert len(p.lines) == 6 and len(p.faces) == 2
    assert polygon_area(p) == pytest.approx(2)


def test_shape_wires_are_its_loops():
    p = square(0, 0) + square(3, 0)
    loops = p.wires
    assert [len(w.edges) for w in loops] == [4, 4]
    assert len(p.edges) == 8
    assert sum(polygon_area(w.Face()) for w in loops) == pytest.approx(2)
    with pytest.raises(ValueError, match=r"\.wires gives them"):
        p.Wire()
    assert len(square(0, 0).Wire().edges) == 4


def test_wire_exports_take_the_cleanup_options():
    wp = px.Move((0, 0)).Rectangle(1, 1).Move((3, 3)).Line((4, 4))
    assert len(wp.ToOCCWires().wires) == 2
    assert len(wp.ToOCCWires(remove_free_edges=True).wires) == 1
    assert len(wp.ToOCCFace().edges) == 4
    w = max(wp.Wires(), key=lambda w: len(w.edges))
    assert len(w.edges) == 4
    assert isinstance(w.ToOCCWire(), occ.Wire)


def test_wires_and_polygons_keep_their_axes():
    axes = occ.Axes((0, 0, 1), occ.Z, occ.X)
    wp = px.QuickSketch(axes).Rectangle(1, 1)
    for shape in (wp.Wire().ToOCCWire(), wp.Face().Wire().ToOCCWire(), wp.Face().ToOCCFace(), wp.Wire().Offset(0.1).ToOCCFace()):
        assert all(v.p.z == pytest.approx(1) for v in shape.vertices)
    with pytest.raises(ValueError, match="different planes"):
        wp.Wire() + px.Move((0, 0)).Rectangle(1, 1).Wire()


def test_occgeometry_of_polygon_workplane_and_wire():
    mesh = px.OCCGeometry(square(0, 0, 2)).GenerateMesh(maxh=0.5)
    assert mesh.dim == 2
    assert len(mesh.Elements2D()) > 0
    assert isinstance(px.OCCGeometry(px.Move((0, 0)).Rectangle(1, 1)), occ.OCCGeometry)
    assert isinstance(px.OCCGeometry(px.Move((0, 0)).Rectangle(1, 1).Wire()), occ.OCCGeometry)


def test_edge_factories_return_one_edge_wires():
    l = px.Line((0, 0), (1, 0))
    assert isinstance(l, px.Wire)
    assert isinstance(l.edges[0].edge, px.LineEdge)
    assert ends(l.edges[0].edge) == ((0, 0), (1, 0))
    a = px.Arc((1, 0), (-1, 0), (0, 0), plane=occ.Axes((0, 0, 2), occ.Z, occ.X))
    assert isinstance(a.edges[0].edge, px.ArcEdge)
    assert all(v.p.z == pytest.approx(2) for v in a.ToOCCWire().vertices)
    s = px.Spline([(0, 0), (1, 1), (2, 0)])
    assert len(s.edges) == 1 and isinstance(s.edges[0].edge, px.SplineEdge)
    assert len(px.Spline([(0, 0), (1, 1), (2, 0), (1, -1)], periodic=True).edges) == 2
    assert px.LineEdge((0, 0), (1, 0)).start == px.Vertex((0, 0))


def test_edge_view_gives_a_movable_copy():
    f = square(0, 0, 2)
    w = f.edges[0].Wire()
    assert isinstance(w, px.Wire)
    assert ends(w.edges[0].edge) == ends(f.edges[0].edge)
    w.name = "copy"
    assert f.edges[0].name is None
    moved = f.edges[0].Translated((0, -1))
    assert ends(moved.edges[0].edge) == tuple((x, y - 1) for x, y in ends(f.edges[0].edge))
    assert ends(f.edges[0].edge) == ends(w.edges[0].edge)
    turned = f.edges[0].Rotated((0, 0), 90)
    assert point(turned.edges[0].edge.start) == pytest.approx((0, 0)) or point(turned.edges[0].edge.end) == pytest.approx((0, 0))
    lifted = f.edges[0].Translated((0, 0, 3))
    assert all(v.p.z == pytest.approx(3) for v in lifted.ToOCCWire().vertices)


def test_edges_that_bound_nothing_are_dropped_with_a_warning():
    """a U whose ends lie on the square's top side (T-junctions): inside and outside are ambiguous there, the region
    is the square and the U's edges go, said by a warning"""
    sq = px.QuickSketch().Move((0, 0)).Line((10, 0)).Line((10, 10)).Line((0, 10)).Close().Wire()
    u = px.QuickSketch().Move((4, 10)).Line((4, 8)).Line((6, 8)).Line((6, 10)).Wire()
    with pytest.warns(px.PlanexWarning, match=r"3 edges separate no inside from outside and are dropped, e.g. at \([0-9., ]+\): "
                                              r"a chain ending on other edges \(a T-junction\) or running across the "
                                              r"region; consider px.ClosedOpenWires\(wires\)"):
        f = px.Shape([sq, u]).Face()
    assert f.mass == pytest.approx(100)
    with pytest.warns(px.PlanexWarning, match="1 edge separates no inside from outside and is dropped"):
        px.Shape([sq, px.Line((0, 5), (10, 5))])


def test_closed_open_wires_joins_open_ends():
    u = px.QuickSketch().Move((4, 10)).Line((4, 8)).Line((6, 8)).Line((6, 10)).Wire()
    closed, = px.ClosedOpenWires([u])
    assert closed.IsClosed() and not u.IsClosed() and len(closed.edges) == 4
    sq = px.QuickSketch().Move((0, 0)).Line((10, 0)).Line((10, 10)).Line((0, 10)).Close().Wire()
    # the U closed along the square's side: a notch
    assert px.Face(px.ClosedOpenWires([sq, u])).mass == pytest.approx(96)
    loop, = px.ClosedOpenWires(px.FromPolyline([(0, 0), (4, 0), (4, 1), (0, 1)]))
    assert loop.IsClosed() and px.Face([loop]).mass == pytest.approx(4)
    empty, kept = px.ClosedOpenWires([px.Wire(), sq])
    assert len(empty.edges) == 0 and len(kept.edges) == 4 and kept.IsClosed()


def test_views_raise_once_the_edges_changed():
    w = px.QuickSketch().Move((0, 0)).Line((1, 0)).Line((2, 0)).Line((2, 1)).Close().Wire()
    first, corner = w.edges[0], w.vertices[2]
    p = corner.p
    w.Translate((1, 0))
    assert first.edge.start.x == pytest.approx(1)
    # a moved vertex is still the same vertex
    assert corner.p.x == pytest.approx(p.x + 1)
    w.MergeCollinearEdges(1e-9)
    for stale in (lambda: first.edge, lambda: corner.p, lambda: setattr(first, "name", "x")):
        with pytest.raises(RuntimeError, match="stale"):
            stale()
    assert w.edges[0].edge is not None
