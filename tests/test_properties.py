# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import netgen.occ as occ
import pytest

import planex as px


def edge_at(shape, x, y):
    return min(shape.edges, key=lambda e: math.dist((e.center.x, e.center.y), (x, y)))


def test_workplane_properties_reach_the_occ_edges():
    face = px.Move((0, 0)).Line(2, name="bottom", maxh=0.1, col=(1, 0, 0)).Line(0, 1, name="right").Close(name="rest").ToOCCFace()
    assert sorted(e.name for e in face.edges) == ["bottom", "rest", "right"]
    bottom = edge_at(face, 1, 0)
    assert bottom.maxh == pytest.approx(0.1)
    assert bottom.col == pytest.approx([1, 0, 0, 1])
    assert edge_at(face, 2, 0.5).col is None


def test_properties_of_arcs_circles_rectangles_and_splines():
    assert {e.name for e in px.QuickSketch().Circle(0, 0, 1, name="circle").ToOCCFace().edges} == {"circle"}
    assert {e.name for e in px.Move((0, 0)).Rectangle(2, 1, name="rect").ToOCCFace().edges} == {"rect"}
    arc = px.Move((0, 0)).Arc(1, 180, name="arc", maxh=0.05).Close(name="chord").ToOCCFace()
    assert edge_at(arc, 1, 1).name == "arc"
    assert edge_at(arc, 1, 1).maxh == pytest.approx(0.05)
    wp = px.Move((0, 0)).Line((4, 0)).Line((4, 1))
    wp.Spline([(3, 2), (2, 1.5), (1, 2), (0, 1)], name="curve", col=(0, 0, 1, 0.5))
    curve = [e for e in wp.Close().ToOCCFace().edges if e.name == "curve"]
    assert len(curve) == 1
    assert curve[0].col == pytest.approx([0, 0, 1, 0.5])


def test_overlap_keeps_the_first_operands_name_and_the_smaller_maxh():
    a = px.Move((0, 0)).Rectangle(1, 1, name="a", maxh=0.5).Face()
    b = px.Move((0, 0)).Rectangle(1, 0.5, name="b", maxh=0.2).Face()
    face = (a + b).ToOCCFace()
    assert {e.name for e in face.edges} == {"a"}
    assert edge_at(face, 0.5, 0).maxh == pytest.approx(0.2)
    assert edge_at(face, 0.5, 1).maxh == pytest.approx(0.5)
    assert edge_at((b + a).ToOCCFace(), 0.5, 0).name == "b"


def test_crossing_edges_keep_their_own_names():
    a = px.Move((0, 0)).Rectangle(2, 2, name="a").Face()
    b = px.Move((1, 1)).Rectangle(2, 2, name="b").Face()
    face = (a + b).ToOCCFace()
    assert edge_at(face, 0, 1).name == "a"
    assert edge_at(face, 3, 2).name == "b"


def test_offset_edges_inherit_the_drawn_properties():
    face = px.Move((0, 0)).Line(2, name="trace", maxh=0.01).Wire().Offset(0.1).ToOCCFace()
    assert {e.name for e in face.edges} == {"trace"}
    assert all(e.maxh == pytest.approx(0.01) for e in face.edges)


def test_properties_survive_the_placement_on_axes_and_wires():
    wp = px.QuickSketch(occ.Axes((0, 0, 1), occ.Z, occ.X)).Rectangle(1, 1, name="lifted")
    assert {e.name for e in wp.ToOCCFace().edges} == {"lifted"}
    assert {e.name for e in wp.Face().ToOCCWires().wires[0].edges} == {"lifted"}
    assert {e.name for e in wp.Face().ToOCCWire().edges} == {"lifted"}


def test_spline_fit_does_not_join_edges_with_different_names():
    n = 400
    wp = px.Move((1, 0))
    for k in range(1, n):
        wp.Line((math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)), name="upper" if k <= n // 2 else "lower")
    face = wp.Close(name="lower").Face().ToOCCFace(fit_splines=dict(max_edge_length=0.1))
    assert len(face.edges) < n
    assert {e.name for e in face.edges} == {"upper", "lower"}


def test_col_needs_three_or_four_values():
    with pytest.raises(ValueError):
        px.Move((0, 0)).Line(1, col=(1, 0))


def test_edge_properties_can_be_set_through_the_edge_list():
    p = px.Move((0, 0)).Rectangle(2, 1).Face()
    assert [e.name for e in p.edges] == [None] * 4
    bottom = min(p.edges, key=lambda e: e.start.y + e.end.y)
    bottom.name = "bottom"
    bottom.maxh = 0.1
    assert bottom.name == "bottom"
    named = [e for e in p.ToOCCFace().edges if e.name == "bottom"]
    assert len(named) == 1
    assert named[0].maxh == pytest.approx(0.1)
    p.edges[-1].col = (1, 0, 0)
    assert p.edges[3].col == [1, 0, 0, 1]
    p.edges[3].col = None
    assert p.edges[3].col is None
    with pytest.raises(IndexError):
        p.edges[4]


def test_setting_a_property_changes_only_that_edge():
    off = px.Move((0, 0)).Line(2, name="trace").Wire().Offset(0.1)
    off.edges[0].name = "cap"
    assert sorted(e.name for e in off.edges) == ["cap", "trace", "trace", "trace"]
    assert isinstance(off.Wire().edges[0].edge, (px.LineEdge, px.ArcEdge))


def occ_point(e, t):
    s0, s1 = e.parameter_interval
    p = e.Value(s0 + t * (s1 - s0))
    return (p.x, p.y)


def test_new_edge_properties_from_the_drawing_calls_and_the_edges():
    w = px.Move((0, 0)).Line((2, 0), minh=0.01, hpref=2, layer=3, partition=[0.75, 0.25]).Line((2, 1)).Wire()
    e = w.edges[0]
    assert (e.minh, e.hpref, e.layer, e.partition) == (0.01, 2, 3, [0.25, 0.75])
    assert (w.edges[1].minh, w.edges[1].partition) == (None, None)
    assert w.layer is None
    w.layer = 5
    assert w.layer == 5 and [x.layer for x in w.edges] == [5, 5]
    w.edges[1].partition = [0.5]
    assert w.edges[1].partition == [0.5]
    for bad in ([0.0], [1.0], [0.5, 0.5], [1.5]):
        with pytest.raises(ValueError):
            w.edges[0].partition = bad


def test_new_edge_properties_reach_the_occ_edges():
    face = px.Move((0, 0)).Line((2, 0), hpref=2, layer=3, maxh=0.1).Line((2, 1)).Close().ToOCCFace()
    bottom = edge_at(face, 1, 0)
    assert (bottom.hpref, bottom.layer, bottom.maxh) == (2, 3, pytest.approx(0.1))
    other = edge_at(face, 2, 0.5)
    assert (other.hpref, other.layer) == (0, 1)


@pytest.mark.parametrize("make", [
    lambda: px.Line((0, 0), (4, 0)),
    lambda: px.Arc((1, 0), (-1, 0), (0, 0)),
    lambda: px.Arc((1, 0), (-1, 0), (0, 0), reverse=True),
    lambda: px.Spline([(0, 0), (1, 1), (2, 0.5), (3, 2)]),
])
def test_partition_reaches_netgen_at_the_same_points(make):
    w = make()
    w.edges[0].partition = [0.2, 0.5, 0.9]
    e = w.ToOCCWire().edges[0]
    assert len(e.partition) == 3
    planex = [point_at_length(w.edges[0], f) for f in (0.2, 0.5, 0.9)]
    for t, p in zip(e.partition, planex):
        # a spline exports its curve, which is within the approximation tolerance of the planex edge
        assert occ_point(e, t) == pytest.approx(p, abs=1e-5)


def point_at_length(edge, f):
    # sample the edge densely and walk the length
    import planex._geom as g
    pts = g.points(edge.edge, 1e-9)
    lengths = [0.0]
    for a, b in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + math.dist(a, b))
    target = f * lengths[-1]
    for (a, b), l0, l1 in zip(zip(pts, pts[1:]), lengths, lengths[1:]):
        if l1 >= target:
            s = (target - l0) / (l1 - l0)
            return (a[0] + s * (b[0] - a[0]), a[1] + s * (b[1] - a[1]))
    return pts[-1]


def test_partition_is_remapped_on_the_pieces_of_a_cut():
    a = px.Line((0, 0), (4, 0))
    a.edges[0].partition = [0.25, 0.75]
    cut = [e for w in (a + px.Line((2, -1), (2, 1))).wires for e in w.edges]
    pieces = sorted((e for e in cut if e.bounding_box[0][1] == 0 and e.bounding_box[1][1] == 0),
                    key=lambda e: e.bounding_box[0][0])
    assert [e.partition for e in pieces] == [[0.5], [0.5]]
    assert all(e.partition is None for e in cut if e.bounding_box[0][0] == e.bounding_box[1][0] == 2)


def bottom_nodes(p):
    # the x of every partition node on the edges along y = 0; fractions follow each edge's own direction
    nodes = []
    for e in p.edges:
        if e.bounding_box[1][1] == 0 and e.partition:
            nodes += [round(e.start.x + f * (e.end.x - e.start.x), 12) for f in e.partition]
    return sorted(nodes)


def test_partition_of_boolean_pieces_and_of_a_shared_piece():
    # a's bottom edge has nodes at x = 0.5 and 2.5, c's top edge at x = 1
    a = px.Move((0, 0)).Line((4, 0), partition=[0.125, 0.625]).Line((4, 1)).Line((0, 1)).Close().Face()
    b = px.Move((2, -1)).Rectangle(4, 3).Face()
    assert bottom_nodes(a - b) == [0.5]
    assert [e.partition for e in (a - b).edges if e.bounding_box[1][1] == 0] == [[0.25]]
    c = px.Move((0, 0)).Line((0, -1)).Line((2, -1)).Line((2, 0)).Line((0, 0), partition=[0.5]).Face()
    # the shared stretch takes the first operand's partition
    assert bottom_nodes(a - c) == [0.5, 2.5]
    assert bottom_nodes(c - a) == [1]


def test_cleanup_keeps_edges_with_a_partition():
    w = px.Move((0, 0)).Line((1, 0)).Line((2, 0), partition=[0.5]).Line((2, 1)).Wire()
    assert len(w.Simplified(merge_collinear_closer=1e-3).edges) == 3
    w.edges[1].partition = None
    assert len(w.Simplified(merge_collinear_closer=1e-3).edges) == 2


def test_offsets_drop_the_partition():
    w = px.Move((0, 0)).Line((2, 0), name="trace", partition=[0.5]).Wire()
    o = w.Offset(0.1)
    assert {e.name for e in o.edges} == {"trace"}
    assert all(e.partition is None for e in o.edges)


def test_face_properties():
    p = px.Move((0, 0)).Rectangle(2, 1).Face()
    assert (p.name, p.maxh, p.quad_dominated) == (None, None, None)
    p.name, p.maxh, p.minh, p.hpref, p.layer, p.col, p.quad_dominated = "iron", 0.5, 0.01, 1, 2, (1, 0, 0), True
    assert all(e.name is None for e in p.edges)
    f = p.ToOCCFace()
    assert (f.name, f.maxh, f.hpref, f.layer, f.quad_dominated) == ("iron", 0.5, 1, 2, True)
    assert f.col == pytest.approx([1, 0, 0, 1])
    c = p.Copy()
    c.Mirror((0, 0), (0, 1))
    assert c.name == "iron"


def test_face_properties_in_booleans():
    a = px.Move((0, 0)).Rectangle(2, 1).Face()
    a.name, a.maxh = "a", 0.5
    b = px.Move((1, 0)).Rectangle(2, 1).Face()
    b.name, b.maxh, b.hpref = "b", 0.2, 2
    assert ((a + b).name, (a + b).maxh, (a + b).hpref) == ("a", 0.2, 2)
    assert ((b * a).name, (b * a).maxh) == ("b", 0.2)
    d = a - b
    assert (d.name, d.maxh, d.hpref) == ("a", 0.5, None)
    a.Translate((10, 0))
    assert sorted(f.name for f in (a + b).faces) == ["a", "b"]
    a += b
    assert isinstance(a, px.Shape) and sorted(f.name for f in a.faces) == ["a", "b"]


def test_list_booleans_merge_face_properties_in_list_order():
    far = px.Move((0, 0)).Rectangle(2, 2).Face()
    near = px.Move((1, 0)).Rectangle(2, 2).Face()
    mid = px.Move((0.5, 0.5)).Rectangle(2, 2).Face()
    far.name, near.name, mid.maxh = "far", "near", 0.1
    u = px.Unify([far, near, mid])
    assert (u.name, u.maxh) == ("far", 0.1)
    apart = px.Unify([far, near.Translated((10, 0))])
    assert sorted(f.name for f in apart.faces) == ["far", "near"]
    assert px.Unify([near, far, mid]).name == "near"
    assert px.Subtract([far, near, mid]).name == "far"
    assert px.Intersect([near, near.Copy()]).name == "near"


def test_compound_faces_keep_their_own_properties():
    air = px.Move((0, 0)).Rectangle(4, 4).Face()
    iron = px.Move((1, 1)).Rectangle(2, 2).Face()
    air, iron = air - iron, iron
    air.name, iron.name = "air", "iron"
    c = px.Compound([air, iron])
    assert sorted(f.name for f in c.ToOCCShape().faces) == ["air", "iron"]
    assert air.Simplified().name == "air"


def test_an_intersection_keeps_the_smaller_maxh():
    for ma, mb, expected in ((0.5, 0.1, 0.1), (0.1, 0.5, 0.1), (None, 0.2, 0.2)):
        a = px.QuickSketch((-2, -2)).Rectangle(4, 4).Face()
        b = px.QuickSketch().Move((-1, -1)).Rectangle(4, 4).Face()
        a.maxh, b.maxh = ma, mb
        for r in (a * b, b * a, px.Intersect([a, b])):
            assert [f.maxh for f in r.faces] == [expected]
