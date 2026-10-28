# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import random

import netgen.occ as occ
import pytest

import planex as px
from helpers import (arc, area, cell_boundary, circle, ends, is_empty, line, open_vertices, polygon_area, rect,
                     square, unit_boundary)


def manual_square():
    p = px.Wire()
    for a, b in [((0, 0), (1, 0)), ((1, 0), (1, 1)), ((1, 1), (0, 1)), ((0, 1), (0, 0))]:
        p.Add(line(a, b))
    return p


def test_empty_face_and_shape():
    assert is_empty(px.Face())
    assert is_empty(px.Shape())


def test_export_of_nothing_raises():
    for export in (px.Face().ToOCCFace, px.Face().ToOCCWires, px.Face().ToOCCWire, px.Shape().ToOCCShape, px.QuickSketch().ToOCCWires):
        with pytest.raises(RuntimeError):
            export()


def test_face_of_edges_exported_as_a_wire():
    p = manual_square()
    assert len(p.lines) == 4
    assert polygon_area(p) == pytest.approx(1)


def test_wire_with_arcs():
    p = px.Wire()
    p.Add(line((-1, 0), (1, 0)))
    p.Add(px.Arc(px.Vertex((1, 0)), px.Vertex((-1, 0)), px.Vertex((0, 0))))
    assert (len(p.lines), len(p.arcs)) == (1, 1)
    assert polygon_area(p) == pytest.approx(math.pi / 2)


def test_wires_share_their_vertices():
    wires = manual_square().ToOCCWires().wires
    assert len(wires) == 1
    assert len(wires[0].edges) == 4
    assert len(set(wires[0].vertices)) == 4


def test_wires_is_the_compound_of_all_wires():
    two = square(0, 0) + square(3, 0)
    compound = two.ToOCCWires()
    assert isinstance(compound, occ.Compound)
    assert len(compound.wires) == 2
    assert len(compound.edges) == 8


def test_single_face_and_compound():
    assert isinstance(square(0, 0).ToOCCFace(), occ.Face)
    two = (square(0, 0) + square(3, 0)).ToOCCFace()
    assert len(two.faces) == 2
    assert area(two) == pytest.approx(2)


def test_remove_small_gaps_moves_the_nearby_endpoint_onto_the_open_one():
    p = px.Wire([line((0, 0), (1, 0)), line((1, 0), (1, 1)), line((1, 1), (0, 1)), line((0, 1), (0, 1e-9))])
    assert len(open_vertices(p)) == 2
    p.CloseGaps(1e-6)
    assert [ends(l)[0] for l in p.lines] == [(0, 0), (1, 0), (1, 1), (0, 1)]
    assert open_vertices(p) == []


def test_remove_small_edges_merges_both_ends_into_the_midpoint():
    p = px.Wire()
    p.Add(line((0, 0), (1, 0)))
    p.Add(line((1, 0), (1, 1e-9)))
    p.Add(line((1, 1e-9), (1, 1)))
    p.Add(line((1, 1), (0, 1)))
    p.Add(line((0, 1), (0, 0)))
    p.RemoveSmallEdges(1e-6)
    assert [ends(l)[0] for l in p.lines] == [(0, 0), (1, 5e-10), (1, 1), (0, 1)]
    assert polygon_area(p) == pytest.approx(1)


def test_remove_small_edges_merges_a_chain_into_one_point():
    p = px.Wire()
    p.Add(line((0, 0), (1, 0)))
    p.Add(line((1, 0), (1, 1e-8)))
    p.Add(line((1, 1e-8), (1, 2e-8)))
    p.Add(line((1, 2e-8), (1, 1)))
    p.Add(line((1, 1), (0, 1)))
    p.Add(line((0, 1), (0, 0)))
    p.RemoveSmallEdges(1e-7)
    y = (1e-8 / 2 + 2e-8) / 2
    assert [ends(l) for l in p.lines] == [((0, 0), (1, y)), ((1, y), (1, 1)), ((1, 1), (0, 1)), ((0, 1), (0, 0))]


def test_remove_small_edges_removes_a_ring_of_small_edges():
    p = square(0, 0) - px.Move((0.5, 0.5)).Line((0.501, 0.5)).Line((0.5005, 0.501)).Close().Face()
    assert len(p.lines) == 7
    with pytest.warns(px.PlanexWarning, match="collapsed"):
        p.RemoveSmallEdges(2e-3)
    assert len(p.lines) == 4
    assert polygon_area(p) == pytest.approx(1)


def test_remove_small_edges_removes_an_edge_whose_ends_collapse():
    p = px.Wire()
    p.Add(line((0, 0), (6.8e-3, 0)))
    p.Add(line((6.8e-3, 0), (3.4e-3, 0.9e-3)))
    p.Add(line((3.4e-3, 0.9e-3), (0, 0)))
    p.RemoveSmallEdges(5e-3)
    assert is_empty(p)


def test_export_removes_free_edges():
    """a wire is one chain: the only free edge a wire can have is its single edge"""
    p = square(0, 0).Wire()
    assert len(p.ToOCCWires(remove_free_edges=True).wires) == 1
    assert area(p.ToOCCFace()) == pytest.approx(1)
    free = px.Line((3, 3), (4, 4))
    assert len(free.ToOCCWires().wires) == 1
    with pytest.raises(RuntimeError, match="no edges"):
        free.ToOCCWires(remove_free_edges=True)


def test_steps_below_the_occ_resolution_export_after_removing_small_edges():
    p = rect(0, 0, 1, 1) + rect(1, 5e-8, 1, 1)
    p.RemoveSmallEdges(1e-7)
    assert open_vertices(p) == []
    assert polygon_area(p) == pytest.approx(2)


def test_export_cleans_a_copy_and_leaves_the_polygon_as_it_is():
    p = rect(0, 0, 1, 1) + rect(1, 5e-8, 1, 1)
    n = len(p.lines)
    assert area(p.ToOCCFace()) == pytest.approx(2)
    assert len(p.lines) == n


def test_export_without_cleanup_fails_on_steps_below_the_occ_resolution():
    p = rect(0, 0, 1, 1) + rect(1, 5e-8, 1, 1)
    with pytest.raises(RuntimeError):
        p.ToOCCFace(remove_edges_smaller=0, close_gaps_smaller=0)


def test_a_gap_in_a_wire_is_joined_when_it_is_built():
    """a gap above rounding stays open unless tol= joins it when the wire is built"""
    edges = [line((0, 0), (1, 0)), line((1, 1e-9), (1, 1)), line((1, 1), (0, 1)), line((0, 1), (0, 0))]
    gap = px.Wire(edges)
    assert len(gap.edges) == 4 and not gap.IsClosed()
    p = px.Wire(edges, tol=1e-6)
    assert p.IsClosed()
    assert area(p.ToOCCFace()) == pytest.approx(1)
    assert open_vertices(p) == []


def test_fit_splines_replaces_chains_of_small_edges():
    n = 400
    wp = px.Move((1, 0))
    for k in range(1, n):
        wp.Line((math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)))
    p = wp.Close().Face()
    plain, fitted = p.ToOCCFace(), p.ToOCCFace(fit_splines=dict(max_edge_length=0.1))
    assert len(plain.edges) == n
    assert len(fitted.edges) < n
    assert area(fitted) == pytest.approx(area(plain), rel=1e-4)


def test_fit_splines_runs_before_small_edges_are_removed():
    n = 400
    wp = px.Move((1, 0))
    for k in range(1, n):
        wp.Line((math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)))
    p = wp.Close().Face()
    fitted = p.ToOCCFace(remove_edges_smaller=0.02, fit_splines=dict(max_edge_length=0.1))
    assert len(fitted.edges) == 2
    assert area(fitted) == pytest.approx(area(p.ToOCCFace()), rel=1e-4)


def unit_tangent_at(edge, p):
    t = edge.start_tangent if math.dist(edge.start, p) < 1e-9 else edge.end_tangent
    n = math.hypot(t.x, t.y)
    return (t.x / n, t.y / n)


def test_fit_splines_ends_tangent_to_the_neighbouring_edges():
    n = 10
    corner = [(0.9 + 0.1 * math.cos(math.pi / 2 * k / n), 0.9 + 0.1 * math.sin(math.pi / 2 * k / n)) for k in range(n + 1)]
    pts = [(0, 0), (1, 0)] + corner + [(0, 1)]
    p = px.Wire()
    for a, b in zip(pts, pts[1:] + pts[:1]):
        p.Add(line(a, b))
    face = p.ToOCCFace(fit_splines=dict(max_edge_length=0.1))
    assert len(face.edges) == 5
    [spline] = [e for e in face.edges if abs(e.mass - math.dist(e.start, e.end)) > 1e-6]
    assert abs(unit_tangent_at(spline, (1, 0.9, 0))[0]) < 1e-6
    assert abs(unit_tangent_at(spline, (0.9, 1, 0))[1]) < 1e-6
    assert area(face) == pytest.approx(area(p.ToOCCFace()), rel=1e-4)


def test_fit_splines_joins_the_halves_of_a_closed_chain_tangentially():
    n = 400
    wp = px.Move((1, 0))
    for k in range(1, n):
        wp.Line((math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)))
    a, b = wp.Close().Face().ToOCCFace(fit_splines=dict(max_edge_length=0.1)).edges
    for p in (a.start, a.end):
        ta, tb = unit_tangent_at(a, p), unit_tangent_at(b, p)
        assert abs(ta[0] * tb[1] - ta[1] * tb[0]) < 1e-6


def test_fit_splines_ignores_tiny_edges_in_the_angle_test():
    n = 400
    pts = [(math.cos(2 * math.pi * k / n), math.sin(2 * math.pi * k / n)) for k in range(n)]
    pts.insert(2, (pts[1][0] * (1 + 1e-6), pts[1][1] * (1 + 1e-6)))
    p = px.Wire()
    for a, b in zip(pts, pts[1:] + pts[:1]):
        p.Add(line(a, b))
    fitted = p.ToOCCFace(merge_collinear_closer=0, fit_splines=dict(max_edge_length=0.1))
    assert len(fitted.edges) == 2
    assert min(e.mass for e in fitted.edges) > 1
    assert area(fitted) == pytest.approx(area(p.ToOCCFace()), rel=1e-4)
    broken = p.ToOCCFace(merge_collinear_closer=0, fit_splines=dict(max_edge_length=0.1, ignore_edges_smaller=1e-7))
    assert min(e.mass for e in broken.edges) < 1e-5


def test_fit_splines_does_not_interpolate_through_tiny_kinked_edges():
    pts = [(0, 0), (0.029583, 0.002774), (0.029586, 0.002775), (0.029589, 0.002775), (0.5, 0.002775), (0.5, 0.5), (0, 0.5)]
    p = px.Wire()
    for a, b in zip(pts, pts[1:] + pts[:1]):
        p.Add(line(a, b))
    plain = p.ToOCCFace()
    fitted = p.ToOCCFace(remove_edges_smaller=5e-3, fit_splines=dict(max_edge_length=0.1, tol=2e-2))
    assert all(e.mass < 1 for e in fitted.edges)
    assert area(fitted) == pytest.approx(area(plain), rel=1e-6)


def polyline(points):
    p = px.Wire()
    for a, b in zip(points, points[1:] + points[:1]):
        p.Add(line(a, b))
    return p


def test_merge_collinear_edges_joins_the_pieces_of_a_straight_run():
    p = polyline([(0, 0), (0.2, 0), (0.4, 0), (1, 0), (1, 1), (0, 1)])
    p.MergeCollinearEdges()
    assert len(p.lines) == 4
    assert ((0, 0), (1, 0)) in [ends(l) for l in p.lines]
    assert polygon_area(p) == pytest.approx(1)


def test_merge_collinear_edges_keeps_a_kink_above_the_tolerance():
    p = polyline([(0, 0), (0.5, 1e-4), (1, 0), (1, 1), (0, 1)])
    p.MergeCollinearEdges(1e-5)
    assert len(p.lines) == 5
    p.MergeCollinearEdges(1e-3)
    assert len(p.lines) == 4


def test_a_wire_has_no_fold_backs_or_junctions_to_merge():
    """a fold-back overlaps, a junction branches: neither is a wire (px.Wires cuts such edges into clean wires)"""
    p = px.Wire([line((0, 0), (1, 0))])
    with pytest.raises(ValueError, match="overlap"):
        p.Add(line((1, 0), (0.5, 0)))
    assert len(p.lines) == 1
    q = polyline([(0, 0), (0.5, 0), (1, 0), (1, 1), (0, 1)])
    with pytest.raises(ValueError, match="closed"):
        q.Add(line((0.5, 0), (0.5, -1)))
    with pytest.raises(ValueError, match="3 edge ends meet at"):
        px.Wire(list(q.edges) + [line((0.5, 0), (0.5, -1))])
    q.MergeCollinearEdges()
    assert len(q.lines) == 4


def test_merge_collinear_edges_ignores_the_edges_of_a_spline():
    trace = px.Move((0, 0)).Spline([(1, 0), (2, 0)]).Wire().Offset(0.1)
    assert len(trace.splines) == 2
    trace.MergeCollinearEdges()
    assert len(trace.splines) == 2


def test_export_merges_collinear_edges_on_a_copy():
    p = polyline([(0, 0), (0.2, 0), (0.4, 0), (1, 0), (1, 1), (0, 1)])
    assert len(p.ToOCCFace().edges) == 4
    assert len(p.ToOCCFace(merge_collinear_closer=0).edges) == 6
    assert len(p.lines) == 6


def test_remove_small_edges_measures_the_arc_length():
    p = px.Wire()
    p.Add(arc((1, 0), (1, 1e-8), (0, 0), ccw=False))
    p.RemoveSmallEdges(1e-6)
    assert len(p.arcs) == 1


def test_face_with_a_hole():
    p = square(-2, -2, 4) - circle(0, 0, 1)
    face = p.ToOCCFace()
    assert len(face.faces) == 1
    assert len(face.wires) == 2
    assert area(face) == pytest.approx(16 - math.pi)


@pytest.mark.parametrize("radii, faces", [([5, 4, 1], 2), ([5, 4, 3, 2, 1], 3), ([7, 6, 5, 4, 3, 2, 1], 4)])
def test_nested_rings_with_a_dot(radii, faces):
    p = circle(0, 0, radii[0])
    for k, r in enumerate(radii[1:]):
        p = p - circle(0, 0, r) if k % 2 == 0 else p + circle(0, 0, r)
    face = p.ToOCCFace()
    assert len(face.faces) == faces
    assert sorted(len(f.wires) for f in face.faces) == [1] + [2] * (faces - 1)
    assert all(f.mass > 0 for f in face.faces)
    assert area(face) == pytest.approx(math.pi * sum((-1) ** k * r * r for k, r in enumerate(radii)))


def test_nested_square_rings_with_a_dot():
    p = rect(-5, -5, 10, 10) - rect(-4, -4, 8, 8) + rect(-3, -3, 6, 6) - rect(-2, -2, 4, 4) + rect(-1, -1, 2, 2)
    face = p.ToOCCFace()
    assert len(face.faces) == 3
    assert all(f.mass > 0 for f in face.faces)
    assert area(face) == pytest.approx(60)


def test_face_of_regions_touching_at_corners():
    rng = random.Random(31)
    p, cells = px.Shape(), set()
    for i in range(8):
        for j in range(8):
            if rng.random() < 0.6:
                w, h = rng.choice([1, 2]), rng.choice([1, 2])
                p = p + rect(i, j, w, h)
                cells |= {(i + a, j + b) for a in range(w) for b in range(h)}
    assert unit_boundary(p) == cell_boundary(cells)
    assert polygon_area(p) == pytest.approx(len(cells))


def _normal_flux(region, boundary):
    import ngsolve as ngs
    mesh = ngs.Mesh(px.OCCGeometry(region).GenerateMesh(maxh=0.3))
    n = ngs.specialcf.normal(2)
    return ngs.Integrate(ngs.x * n[0], mesh, ngs.BND, definedon=mesh.Boundaries(boundary))


def test_mesh_normals_point_out_of_the_material_however_the_edges_were_drawn():
    """netgen's 2d mesh takes a boundary's direction from the curve: the export runs every edge with the face on its
    left, so the integral of x n_x over a boundary is + its area outside, - the area of a hole"""
    pytest.importorskip("ngsolve")
    cw = px.QuickSketch().Move((0, 0)).Line((0, 1), name="b").Line((2, 1), name="b").Line((2, 0), name="b").Close(name="b").Face()
    assert _normal_flux(cw, "b") == pytest.approx(2, abs=1e-9)
    box = px.QuickSketch((-5, -2)).Rectangle(10, 4).Face()
    naca = px.FromNACA("2412", chord=3).Translated((-1.5, 0))
    for hole in (px.QuickSketch().Circle((0, 0), 1).Face(), naca, naca.Mirrored((0, 0), (1, 0)),
                 px.QuickSketch().Move((-1, -0.5)).Line((1, -0.3)).Line((0.2, 0.8)).Close().Face().Mirrored((0, 0), (1, 0))):
        for e in hole.edges:
            e.name = "hole"
        region = px.Subtract([box, hole])
        assert _normal_flux(region, "hole") == pytest.approx(-hole.mass, rel=0.02)
        # the single face's own export
        assert _normal_flux(region.faces[0], "hole") == pytest.approx(-hole.mass, rel=0.02)


def test_occgeometry_meshes_a_face_off_the_xy_plane_as_a_surface():
    ngs = pytest.importorskip("ngsolve")
    planes = [px.XY, px.Plane(origin=(2, 3, 0), normal=(0, 0, 1), axis=(1, 1, 0)), px.Plane(origin=(0, 0, 1), normal=(0, 0, 1)),
              px.Plane(origin=(0, 0, 0), normal=(0, 1, 1)), px.YZ]
    for plane, dim in zip(planes, (2, 2, 3, 3, 3)):
        sketch = px.QuickSketch(plane).Rectangle(2, 1)
        for obj in (sketch, sketch.Face(), px.Glue([sketch.Face()])):
            mesh = ngs.Mesh(px.OCCGeometry(obj).GenerateMesh(maxh=0.5))
            assert mesh.dim == dim
            assert ngs.Integrate(1, mesh, ngs.VOL if dim == 2 else ngs.BND) == pytest.approx(2, rel=1e-12)


@pytest.mark.constraints
def test_occgeometry_of_a_sketch_follows_its_plane():
    ngs = pytest.importorskip("ngsolve")
    tilted = px.Plane(origin=(0, 0, 0), normal=(0, 1, 1))
    for make in (px.QuickSketch, px.Sketch):
        for plane, dim in ((px.XY, 2), (tilted, 3), (px.YZ, 3)):
            mesh = ngs.Mesh(px.OCCGeometry(make(plane).Rectangle(2, 1)).GenerateMesh(maxh=0.5))
            assert mesh.dim == dim
            assert ngs.Integrate(1, mesh, ngs.VOL if dim == 2 else ngs.BND) == pytest.approx(2, rel=1e-12)


def test_a_face_has_an_outer_and_inner_loops():
    ring = (square(0, 0, 4) - square(1, 1, 2)).faces[0]
    assert len(ring.outer.edges) == 4 and len(ring.inner) == 1 and len(ring.inner[0].edges) == 4
    assert not hasattr(ring, "holes")


def test_fit_options_are_one_dict_per_fit():
    p = px.Move((0, 0)).Spline([(1, 1), (2, 0)]).Line((0, 0)).Face()
    assert len(p.ToOCCFace(fit_splines=dict(max_edge_length=0.1)).edges) == len(p.ToOCCFace(fit_splines=True).edges)
    with pytest.raises(TypeError, match="no option 'tolerance'"):
        p.ToOCCFace(fit_splines=dict(tolerance=1e-3))
    with pytest.raises(TypeError):
        p.ToOCCFace(max_spline_edge_length=0.1)

