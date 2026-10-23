# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest
from netgen.occ import OCCGeometry

import planex as px


def lshape():
    # 2 x 2 with the upper right quarter missing: the re-entrant corner at (1, 1)
    return px.Move((0, 0)).Line((2, 0)).Line((2, 1)).Line((1, 1)).Line((1, 2)).Line((0, 2)).Close().Face()


def at(shape, p):
    [v] = [v for v in shape.vertices if (v.p.x, v.p.y) == pytest.approx(p, abs=1e-12)]
    return v


def named(shape):
    return sorted((v.name, round(v.p.x, 9) + 0.0, round(v.p.y, 9) + 0.0) for v in shape.vertices if v.name)


def occ_named(occ_shape):
    return {(v.name, round(v.p.x, 9) + 0.0, round(v.p.y, 9) + 0.0, round(v.p.z, 9) + 0.0) for v in occ_shape.vertices if v.name}


def test_vertices_are_where_edges_end_each_once():
    w = px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Wire()
    assert [(v.p.x, v.p.y) for v in w.vertices] == [(0, 0), (1, 0), (1, 1)]
    f = lshape()
    assert len(f.vertices) == 6 and {(v.p.x, v.p.y) for v in f.vertices} == {(0, 0), (2, 0), (2, 1), (1, 1), (1, 2), (0, 2)}
    with pytest.raises(IndexError):
        f.vertices[6]
    assert f.vertices[-1].p == f.vertices[5].p
    glued = px.Glue([px.Move((0, 0)).Rectangle(1, 1).Face(), px.Move((1, 0)).Rectangle(1, 1).Face()])
    assert len(glued.vertices) == 6


def test_vertex_properties_are_set_on_one_vertex_or_all():
    f = lshape()
    corner = at(f, (1, 1))
    assert (corner.name, corner.maxh, corner.hpref) == (None, None, None)
    corner.name = "reentrant"
    corner.hpref = 2
    assert (at(f, (1, 1)).name, at(f, (1, 1)).hpref) == ("reentrant", 2)
    assert f.vertices.hpref is None and f.vertices.name is None
    f.vertices.maxh = 0.1
    assert f.vertices.maxh == 0.1 and at(f, (1, 1)).name == "reentrant" and at(f, (0, 0)).maxh == 0.1
    at(f, (1, 1)).name = None
    assert named(f) == []
    copy = f.Copy()
    at(copy, (1, 1)).hpref = 3
    assert at(f, (1, 1)).hpref == 2


def test_vertex_properties_reach_the_occ_vertices_and_the_mesh():
    ngsolve = pytest.importorskip("ngsolve")
    f = lshape()
    at(f, (1, 1)).name = "reentrant"
    at(f, (1, 1)).hpref = 1
    assert occ_named(f.ToOCCShape()) == {("reentrant", 1, 1, 0)}
    assert {v.hpref for v in f.ToOCCShape().vertices if v.name} == {1}
    assert occ_named(f.Translated((0, 0, 5)).ToOCCShape()) == {("reentrant", 1, 1, 5)}
    mesh = ngsolve.Mesh(OCCGeometry(f.ToOCCShape(), dim=2).GenerateMesh(maxh=0.5))
    assert mesh.GetBBoundaries().count("reentrant") == 1

    def refined(face):
        m = ngsolve.Mesh(OCCGeometry(face.ToOCCShape(), dim=2).GenerateMesh(maxh=0.5))
        n = m.ne
        m.RefineHP(2)
        return m.ne - n

    assert refined(f) > 0 and refined(lshape()) == 0
    fine = lshape()
    at(fine, (1, 1)).maxh = 0.02

    def elements(face):
        return ngsolve.Mesh(OCCGeometry(face.ToOCCShape(), dim=2).GenerateMesh(maxh=0.5)).ne

    assert elements(fine) > 2 * elements(lshape())


def test_booleans_glue_and_cuts_keep_vertex_properties():
    plate = lshape()
    at(plate, (2, 1)).name = "seam"
    tab = px.Move((2, 0)).Rectangle(1, 1).Face()
    # the corner becomes a point on the straight top edge and stays a vertex there
    union = plate + tab
    assert named(union) == [("seam", 2, 1)] and len(union.edges) == 7
    assert len((lshape() + tab).edges) == 6
    assert named(plate - px.Move((1.5, 0.5)).Rectangle(1, 1).Face()) == []
    assert named(plate * px.Move((0, 0)).Rectangle(2.5, 1.5).Face()) == [("seam", 2, 1)]
    glued = px.Glue([plate, tab])
    assert named(glued) == [("seam", 2, 1)] and len(glued.faces) == 2
    assert occ_named(glued.ToOCCShape()) == {("seam", 2, 1, 0)}
    left, right = sorted(plate.Split(px.Line((1.5, -1), (1.5, 3))), key=lambda p: p.center[0])
    assert named(left) == [] and named(right) == [("seam", 2, 1)]
    assert at(right, (1.5, 0)).name is None
    assert named(plate.Trim(px.Line((1.5, -1), (1.5, 3)), keep_closest_to=(2, 0.5))) == [("seam", 2, 1)]


def test_vertex_properties_move_with_the_shape():
    f = lshape()
    at(f, (1, 1)).name = "reentrant"
    at(f, (1, 1)).maxh = 0.1
    assert named(f.Rotated((0, 0), 90)) == [("reentrant", -1, 1)]
    assert named(f.Mirrored((0, 0), (1, 0))) == [("reentrant", 1, -1)]
    assert at(f.Scaled((0, 0), 2, scale_maxh=True), (2, 2)).maxh == pytest.approx(0.2)
    assert at(f.Scaled((0, 0), 2), (2, 2)).maxh == 0.1


def polyline(pts, closed=False):
    s = px.Move(tuple(pts[0]))
    for p in pts[1:]:
        s.Line(p)
    return s.Close() if closed else s


def test_merging_and_fitting_stop_at_vertices_with_properties():
    def bar(mark):
        s = px.Move((0, 0)).Line((1, 0)).Line((3, 0)).Line((3, 1)).Line((0, 1)).Close()
        if mark:
            s(kind="point", near=(1, 0)).hpref = 1
        return s.Face()

    assert len(bar(False).edges) == 4 and len(bar(True).edges) == 5
    w = px.Move((0, 0)).Line((1, 0)).Line((3, 0)).Wire(unify=False)
    assert len(w.ToOCCWire(merge_collinear_closer=1e-6).edges) == 1
    w.vertices[1].name = "kink"
    assert len(w.ToOCCWire(merge_collinear_closer=1e-6).edges) == 2

    n = 40
    ring = polyline([(math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)) for k in range(n)], closed=True).Face()
    at(ring, (0, 1)).name = "top"
    ring.FitArcs(tol=1e-2, max_edge_length=0.5)
    arcs = [e.edge for e in ring.edges if isinstance(e.edge, px.ArcEdge)]
    assert len(ring.edges) == 2 and len(arcs) == 2 and named(ring) == [("top", 0, 1)]
    assert sorted((round(a.start.y, 9) + 0.0) for a in arcs) == [-1, 1]

    wave = [(k / 20, math.sin(k / 20 * 3)) for k in range(41)]
    smooth = polyline(wave).Wire()
    assert len(smooth.ToOCCWire(fit_splines=dict(max_edge_length=0.5)).edges) == 1
    at(smooth, wave[20]).name = "middle"
    fitted = smooth.ToOCCWire(fit_splines=dict(max_edge_length=0.5))
    assert len(fitted.edges) == 2 and occ_named(fitted) == {("middle", 1, round(math.sin(3), 9), 0)}


def test_sketch_set_vertex():
    s = px.Move((0, 0)).Line((2, 0)).Vertex("p").Line((2, 1)).Line((0, 1)).Close()
    s("p").name, s("p").hpref = "corner", 1
    s(kind="point", near=(0, 1)).maxh = 0.05
    s("p").maxh = 0.1
    f = s.Face()
    assert (at(f, (2, 0)).name, at(f, (2, 0)).hpref, at(f, (2, 0)).maxh) == ("corner", 1, 0.1)
    assert at(f, (0, 1)).maxh == 0.05 and named(s.Shape()) == named(f)
    with pytest.raises(ValueError, match="^no element 'q' here$"):
        s("q").hpref = 1
    # set while drawing: the edges drawn on from there keep the vertex
    t = px.Move((0, 0)).Line((1, 0))
    t(kind="point", near=(1, 0)).name = "k"
    t.Line((2, 0)).Line((2, 1)).Line((0, 1)).Close()
    assert named(t.Face()) == [("k", 1, 0)] and len(t.Face().edges) == 5


def test_sketch_trim_and_mirror_keep_vertex_properties():
    def overshoot():
        s = px.Move((3, -1)).Line((3, 3), id="b", construction=True).Move((0, 1)).Line((5, 1), id="a")
        s(kind="point", near=(5, 1)).name = "tip"
        s(kind="point", near=(0, 1)).name = "root"
        return s

    assert named(overshoot().Trim("a", "b").Wire()) == [("root", 0, 1)]
    assert named(overshoot().Trim("a", "b", reverse=True).Wire()) == [("tip", 5, 1)]
    s = px.Move((0, -1)).Line((0, 3), id="axis", construction=True)
    s.Move((0, 0)).Line((2, 0), id="bottom").Line((2, 1), id="side").Line((0, 1), id="top")
    s(kind="point", near=(2, 1)).name, s(kind="point", near=(2, 1)).hpref = "c", 1
    s(kind="point", near=(0, 1)).name = "axis top"
    f = s.Mirrored(["bottom", "side", "top"], "axis").Face()
    assert named(f) == [("axis top", 0, 1), ("c", -2, 1), ("c", 2, 1)]
    assert at(f, (-2, 1)).hpref == 1


def test_projection_keeps_occ_vertex_properties():
    from netgen import occ

    box = occ.Box((0, 0, 0), (1, 2, 3))
    for v in box.vertices:
        if v.p.x > 0.5 and v.p.y > 1:
            v.name = "far"
            v.hpref = 2
    shape = px.QuickSketch().Project(box, construction=False).Shape()
    assert named(shape) == [("far", 1, 2)] and at(shape, (1, 2)).hpref == 2


def test_edge_and_vertex_lists_take_integers_and_slices_only():
    f = lshape()
    assert [(e.start.x, e.start.y) for e in f.edges[1:3]] == [(e.start.x, e.start.y) for e in (f.edges[1], f.edges[2])]
    assert len(f.edges[::2]) == 3 and len(f.vertices[::-1]) == 6 and f.vertices[::-1][0].p == f.vertices[-1].p
    import numpy as np
    assert f.edges[np.int64(1)].start == f.edges[1].start
    for bad in ("a", None, 1.0):
        with pytest.raises(TypeError, match="integers or slices"):
            f.edges[bad]
        with pytest.raises(TypeError, match="integers or slices"):
            f.vertices[bad]
    with pytest.raises(IndexError):
        f.edges[6]
    views = px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Wire().edges[:]
    assert [v.mass for v in views] == [1, 1]
