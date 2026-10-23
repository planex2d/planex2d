# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import copy
import math
import warnings

import pytest
from netgen import occ

import planex as px
from helpers import area, circle, ends, point, polygon_area, rect, seg, square


def segs(wire):
    return {seg(*(tuple(round(c, 9) for c in p) for p in ends(e.edge))) for e in wire.edges}


def named(face, name):
    face.name = name
    return face


def test_booleans_always_return_a_shape():
    for result in (square(0, 0) + square(1, 0), square(0, 0, 2) - square(1, 1), square(0, 0, 2) * square(1, 1)):
        assert isinstance(result, px.Shape)
        assert len(result.faces) == 1 and isinstance(result.Face(), px.Face)
    two = square(0, 0) + square(3, 0)
    assert len(two.faces) == 2 and two.mass == pytest.approx(2)
    with pytest.raises(ValueError, match="2 faces"):
        two.Face()
    empty = square(0, 0) * square(3, 0)
    assert isinstance(empty, px.Shape) and len(empty.faces) == 0 and empty.mass == 0


def test_touching_faces_unite_into_one_rectangle_and_corners_stay_apart():
    joined = square(0, 0) + square(1, 0)
    assert len(joined.faces) == 1 and joined.mass == pytest.approx(2)
    corners = square(0, 0) + square(1, 1)
    assert len(corners.faces) == 2


def test_a_face_is_one_region():
    outer = px.Move((0, 0)).Rectangle(3, 3).Wire()
    inner = px.Move((1, 1)).Rectangle(1, 1).Wire()
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        ring = px.Face([outer, inner])
        assert ring.mass == pytest.approx(8) and len(ring.wires) == 2
        island = px.Move((1.25, 1.25)).Rectangle(0.5, 0.5).Wire()
        with pytest.raises(ValueError, match="2 regions; Shape"):
            px.Face([outer, inner, island])
        shape = px.Shape([outer, inner, island])
        assert len(shape.faces) == 2 and shape.mass == pytest.approx(8.25)
        sketch = px.Move((0, 0)).Rectangle(1, 1).Move((5, 0)).Rectangle(1, 1)
        with pytest.raises(ValueError, match="Shape"):
            sketch.Face()
        assert len(sketch.Shape().faces) == 2
        assert isinstance(px.QuickSketch().Move((0, 0)).Rectangle(1, 1).Face(), px.Face)
    with pytest.warns(px.PlanexWarning, match="open wire"):
        assert px.Face([px.Move((0, 0)).Line((1, 0)).Wire()]).mass == 0
    with pytest.raises(ValueError, match="different planes"):
        px.Face([outer, px.Line((0, 0), (1, 1), plane=occ.Axes((0, 0, 1), occ.Z, occ.X))])


def test_union_regions_take_the_properties_of_the_faces_they_hold_in_list_order():
    a, b = named(square(0, 0, 2), "a"), named(square(1, 0, 2), "b")
    assert (a + b).name == "a" and (b + a).name == "b"
    far = named(square(10, 0), "far")
    u = px.Unify([a, far, b])
    assert sorted(f.name for f in u.faces) == ["a", "far"]
    small, big = named(square(1, 1, 0.5), "small"), named(square(0, 0, 4), "big")
    small.maxh, big.maxh = 0.1, 0.5
    held = px.Unify([small, big])
    assert (held.name, held.maxh) == ("small", 0.1)
    assert px.Unify([big, small]).name == "big"
    discs = named(circle(0, 0, 1), "left") + named(circle(3, 0, 1), "right")
    assert sorted(f.name for f in discs.faces) == ["left", "right"]


def test_difference_and_intersection_keep_the_faces_of_the_first_operand():
    pair = named(square(0, 0), "a") + named(square(3, 0), "b")
    assert sorted(f.name for f in pair.faces) == ["a", "b"]
    cut = pair - rect(-1, 0.25, 6, 0.5)
    assert len(cut.faces) == 4
    assert sorted(f.name for f in cut.faces) == ["a", "a", "b", "b"]
    assert cut.mass == pytest.approx(1)
    strip = named(rect(-1, 0.25, 6, 0.5), "strip")
    strip.maxh = 0.05
    common = pair * strip
    assert sorted((f.name, f.maxh) for f in common.faces) == [("a", 0.05), ("b", 0.05)]
    assert sorted(f.name for f in (strip * pair).faces) == ["strip", "strip"]


def test_faces_and_wires_do_not_mix_in_booleans():
    f, w = square(0, 0), px.Line((0, 0), (1, 1))
    for op in (lambda: f + w, lambda: w + f, lambda: f - w):
        with pytest.raises(TypeError, match="not supported"):
            op()
    assert sum(e.mass for e in (f * w).edges) == pytest.approx(2 ** 0.5) and len((w - f).edges) == 0
    with pytest.raises(ValueError, match="faces and wires"):
        px.Unify([f, w])
    with pytest.raises(ValueError, match="different planes"):
        square(0, 0) + square(0, 0).Translated((0, 0, 1))


def test_wire_union_cuts_crossings_and_keeps_overlaps_once():
    u = px.Line((0, 1), (2, 1)) + px.Line((1, 0), (1, 2))
    assert isinstance(u, px.Shape) and len(u.faces) == 0
    assert sum(len(w.edges) for w in u.wires) == 4
    overlap = px.Line((0, 0), (2, 0)) + px.Line((1, 0), (3, 0))
    assert [segs(w) for w in overlap.wires] == [{seg((0, 0), (3, 0))}]
    two = px.Move((0, 0)).Rectangle(1, 1).Wire() + px.Move((5, 5)).Rectangle(1, 1).Wire()
    assert len(two.wires) == 2 and two.mass == 0
    assert len((two + px.Line((5, 7), (6, 7))).wires) == 3


def test_shape_properties_set_every_face():
    s = square(0, 0) + square(3, 0)
    s.name, s.maxh = "copper", 0.2
    assert [(f.name, f.maxh) for f in s.faces] == [("copper", 0.2)] * 2
    assert {f.name for f in s.ToOCCShape().faces} == {"copper"}
    s.faces[0].name = "first"
    assert s.name is None and [f.name for f in s.faces] == ["first", "copper"]


def test_shape_export_and_mesh():
    one = square(0, 0, 2) - square(0.5, 0.5, 1)
    assert isinstance(one.ToOCCShape(), occ.Face)
    two = square(0, 0) + square(3, 0)
    shape = two.ToOCCShape()
    assert isinstance(shape, occ.Compound) and len(shape.faces) == 2
    assert area(two.ToOCCFace()) == pytest.approx(2)
    assert len(two.ToOCCWires().wires) == 2
    mesh = px.OCCGeometry(two).GenerateMesh(maxh=0.5)
    assert len(mesh.Elements2D()) > 0 and mesh.dim == 2
    wires = px.Move((0, 0)).Line((1, 0)).Wire() + px.Line((0, 1), (1, 1))
    assert len(wires.ToOCCShape().wires) == 2


def test_shape_simplify_keeps_its_faces():
    s = rect(0, 0, 1, 1) + rect(1, 5e-8, 1, 1) + square(5, 0)
    assert len(s.faces) == 2
    s.Simplify()
    assert len(s.faces) == 2 and s.mass == pytest.approx(3)


def test_shape_motion_copy_and_plot():
    s = square(0, 0, 2) + square(5, 0)
    lifted = s.Translated((0, 0, 1))
    assert lifted.Normal() == (0, 0, 1)
    assert all(v.p.z == pytest.approx(1) for v in lifted.ToOCCShape().vertices)
    turned = s.Rotated((0, 0), 90)
    (x0, y0), (x1, y1) = turned.bounding_box
    assert (x0, y0, x1, y1) == pytest.approx((-2, 0, 0, 6))
    c = copy.deepcopy(s)
    c.Translate((1, 0))
    assert s.bounding_box == ((0, 0), (6, 2))
    pytest.importorskip("matplotlib").use("Agg")
    ax = s.Plot(face_col="C0")
    assert len(ax.patches) == 1


def test_compound_is_a_plain_list():
    f, w = square(0, 0, 2), px.Line((-1, 1), (3, 1))
    c = px.Compound([f, w, square(1, 1)])
    assert len(c) == 3 and c[1] is not w and c[1].bounding_box == w.bounding_box
    for op in (lambda: c + f, lambda: f + c, lambda: c - f):
        with pytest.raises(TypeError):
            op()
    exported = c.ToOCCShape()
    assert len(exported.faces) == 2 and len(exported.edges) == 9
    assert c.bounding_box == ((-1, 0), (3, 2))
    moved = c.Translated((10, 0))
    assert moved.bounding_box == ((9, 0), (13, 2)) and c.bounding_box == ((-1, 0), (3, 2))
    faces = px.Compound([square(0, 0), px.Compound([square(0.5, 0)])])
    u = px.Unify([faces])
    assert isinstance(u, px.Shape) and u.mass == pytest.approx(1.5)
    assert px.Compound().bounding_box is None


def test_faces_and_wires_hold_all_parts_as_in_netgen():
    ring = square(0, 0, 3) - square(1, 1)
    face = ring.Face()
    assert len(face.faces) == 1 and face.faces[0].mass == pytest.approx(8)
    assert sorted(len(w.edges) for w in face.wires) == [4, 4]
    two = ring + square(5, 0)
    assert len(two.faces) == 2 and len(two.wires) == 3
    w = px.Move((0, 0)).Line((1, 0)).Wire()
    assert w.faces == [] and len(w.wires) == 1


def test_pieces_on_one_carrier_join_unless_something_else_tells_them_apart():
    joined = rect(0, 0, 1, 1) + rect(1, 0, 1, 1) + rect(2, 0, 1, 1)
    assert sorted(segs(w) for w in joined.wires) == [{seg((0, 0), (3, 0)), seg((3, 0), (3, 1)), seg((3, 1), (0, 1)),
                                                       seg((0, 1), (0, 0))}]
    a, b = named(rect(0, 0, 1, 1), "a"), named(rect(1, 0, 1, 1), "b")
    a.edges.name, b.edges.name = "a", "b"
    assert len((a + b).edges) == 6
    c, d = rect(0, 0, 1, 1), rect(1, 0, 1, 1)
    for f in (c, d):
        for e in f.edges:
            if e.start.y == 0 and e.end.y == 0:
                e.partition = [0.5]
    u = c + d
    assert len(u.edges) == 5 and len([e for e in u.edges if e.partition is not None]) == 2
    t = rect(0, 0, 2, 1) + rect(1, 1, 1, 1)
    assert len(t.edges) == 6


def test_arcs_on_one_circle_join_but_never_close_it():
    disc = circle(0, 0, 1)
    assert len(disc.edges) == 1 and len(disc.circles) == 1
    stick = disc + rect(-2, -0.25, 1.5, 0.5)
    arcs = stick.arcs
    assert len(arcs) == 1 and math.radians(abs(arcs[0].sweep)) > math.pi
    assert stick.mass == pytest.approx(area(stick.ToOCCFace()), rel=1e-12)
    assert len((disc + circle(3, 0, 1)).circles) == 2


def test_glue_keeps_the_wires_and_shares_their_common_stretch():
    a, b = px.Line((0, 0), (2, 0)), px.Line((1, 0), (3, 0))
    a.name, b.name = "a", "b"
    g = px.Glue([a, b])
    assert isinstance(g, px.Shape) and len(g.faces) == 0
    assert len(g.edges) == 3
    assert sorted(segs(w) for w in g.wires) == sorted([{seg((0, 0), (1, 0)), seg((1, 0), (2, 0))},
                                                        {seg((1, 0), (2, 0)), seg((2, 0), (3, 0))}])
    assert {seg(*ends(e.edge)): e.name for e in g.edges} == {seg((0, 0), (1, 0)): "a", seg((1, 0), (2, 0)): "a",
                                                            seg((2, 0), (3, 0)): "b"}
    exported = g.ToOCCShape()
    used = [e for w in exported.wires for e in w.edges]
    assert len(exported.wires) == 2 and len(used) == 4 and len(set(used)) == 3
    assert len((a + b).wires) == 1


def test_glue_splits_crossing_wires_and_keeps_each_whole():
    g = px.Glue([px.Line((0, 1), (2, 1)), px.Line((1, 0), (1, 2))])
    assert len(g.edges) == 4 and sorted(len(w.edges) for w in g.wires) == [2, 2]
    loop = px.Move((0, 0)).Rectangle(2, 2).Wire()
    g = px.Glue([loop, px.Compound([px.Line((-1, 1), (3, 1))])])
    assert sorted(len(w.edges) for w in g.wires) == [3, 6]
    assert len(px.Glue([g]).wires) == 2


def test_the_common_part_keeps_the_first_ones_properties_and_the_smaller_sizes():
    a = px.QuickSketch((-1, -1)).Rectangle(2, 2).Face()
    a.name, a.maxh = "a", 0.5
    b = px.QuickSketch((0, -1)).Rectangle(2, 2).Face()
    b.name, b.maxh, b.minh, b.col = "b", 0.1, 0.01, (1, 0, 0)
    common = a * b
    assert [(f.name, f.maxh, f.minh, f.col) for f in common.faces] == [("a", 0.1, 0.01, None)]
    plain = px.QuickSketch((-1, -1)).Rectangle(2, 2).Face()
    assert [(f.name, f.maxh) for f in px.Intersect([plain, b]).faces] == [(None, 0.1)]
