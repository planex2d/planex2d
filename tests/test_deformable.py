# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import numpy as np
import pytest

import planex as px
from helpers import circle, rect, square


def lens(d, r):
    """area of the intersection of two discs of radius r with centers d apart"""
    return 2 * r * r * math.acos(d / (2 * r)) - 0.5 * d * math.sqrt(4 * r * r - d * d)


def two_discs(d, r):
    return 2 * math.pi * r * r - lens(d, r)


def test_construction_samples_every_edge_at_most_h_apart():
    s = px.DeformableShape(square(0, 0, 2) - circle(1, 1, 0.5), 0.1)
    assert abs(s.mass - (4 - math.pi * 0.25)) < 1e-2
    v = s.Vertices()
    assert v.shape == (len(s), 2)
    assert np.array_equal(v, s.ReferenceVertices())
    wires = s.Wires()
    assert len(wires) == 2 and all(w.IsClosed() for w in wires)
    assert sum(len(w.edges) for w in wires) == len(v)
    assert max(e.mass for w in wires for e in w.edges) <= 0.1 + 1e-12
    assert s.IsValid()


def test_polygon_keeps_lines_and_interpolates_curves():
    p = px.DeformableShape(square(0, 0, 2), 0.1).Shape()
    assert len(p.edges) == 4
    assert all(isinstance(e.edge, px.LineEdge) for e in p.edges)
    assert abs(p.mass - 4) < 1e-12
    p = px.DeformableShape(circle(0, 0, 1), 0.05).Shape()
    assert all(isinstance(e.edge, px.SplineEdge) for e in p.edges)
    assert abs(p.mass - math.pi) < 1e-4
    assert p.ToOCCFace().mass == pytest.approx(math.pi, abs=1e-4)


def test_deformation_translates_stretches_and_compresses():
    s = px.DeformableShape(square(0, 0, 1), 0.1)
    n = len(s)
    assert s.SetDeformation(np.full((n, 2), 0.5)) is False
    assert np.allclose(s.bounding_box, [[0.5, 0.5], [1.5, 1.5]])
    assert len(s) == n
    v = s.Vertices()
    s.SetDeformation(np.column_stack([v[:, 0] - 0.5, np.zeros(n)]))
    assert len(s) > n
    assert max(e.mass for w in s.Wires() for e in w.edges) <= 0.1 + 1e-12
    assert abs(s.mass - 2.0) < 1e-12
    m = len(s)
    v = s.Vertices()
    s.SetDeformation(np.column_stack([-0.75 * (v[:, 0] - 0.5), np.zeros(m)]))
    assert len(s) < m
    assert abs(s.mass - 0.5) < 1e-12
    assert len(s.Shape().edges) == 4


def test_commit_and_reference():
    s = px.DeformableShape(square(0, 0, 1), 0.25)
    cpp = px._planex.DeformableShape.SetDeformation
    cpp(s, np.full((len(s), 2), 1.0))
    assert np.allclose(s.Vertices() - s.ReferenceVertices(), 1.0)
    s.Commit()
    assert np.array_equal(s.Vertices(), s.ReferenceVertices())


def test_two_discs_merge_into_one_loop():
    calls = []
    s = px.DeformableShape(circle(0, 0, 1) + circle(3, 0, 1), 0.05, topology_changed_callback=calls.append)
    v = s.Vertices()
    d = np.zeros_like(v)
    d[v[:, 0] > 1.5, 0] = -1.5
    assert s.SetDeformation(d) is True
    assert calls == [s]
    assert len(s.Shape().wires) == 1
    assert abs(s.mass - two_discs(1.5, 1.0)) < 1e-2
    assert s.IsValid()


def test_a_dip_through_the_square_splits_it():
    s = px.DeformableShape(square(0, 0, 1), 0.1)
    v = s.Vertices()
    d = np.zeros_like(v)
    dip = (v[:, 1] > 0.5) & (v[:, 0] > 0.25) & (v[:, 0] < 0.75)
    d[dip, 1] = -1.5
    assert s.SetDeformation(d) is True
    assert len(s.Shape().wires) == 2
    # two strips left of x = 0.2667 and right of x = 0.7333 at the bottom, 0.2 and 0.8 at the top
    assert abs(s.mass - 2 * 0.5 * (0.2 + 0.2 + 1 / 15)) < 1e-9
    assert s.IsValid()


def test_a_fold_is_cut_away():
    s = px.DeformableShape(square(0, 0, 1), 0.1)
    v = s.Vertices()
    d = np.zeros_like(v)
    top = v[:, 1] > 0.5
    # the top vertices at x = 0.4, 0.5, 0.6 go up to y = 1.3 in mirrored order: a bump with an inverted loop on top
    for x, target in ((0.6, 0.4), (0.5, 0.5), (0.4, 0.6)):
        i = np.flatnonzero(top & (abs(v[:, 0] - x) < 1e-9))[0]
        d[i] = (target - x, 0.3)
    assert s.SetDeformation(d) is True
    assert len(s.Shape().wires) == 1
    # the square plus the bump up to the crossing at (0.5, 1.2), without the inverted triangle above it
    assert abs(s.mass - (1 + 0.5 * 0.4 * 0.2)) < 1e-9
    assert s.IsValid()


def test_two_rings_give_a_squished_zero():
    ring = lambda x: circle(x, 0, 1) - circle(x, 0, 0.3)  # noqa: E731
    s = px.DeformableShape(ring(0) + ring(3), 0.05)
    v = s.Vertices()
    d = np.zeros_like(v)
    d[v[:, 0] > 1.5, 0] = -2.5
    assert s.SetDeformation(d) is True
    assert len(s.Shape().wires) == 2
    expected = two_discs(0.5, 1.0) - lens(0.5, 0.3)
    assert abs(s.mass - expected) < 2e-2


def test_a_hole_cutting_the_outline_becomes_an_opening():
    s = px.DeformableShape(circle(0, 0, 1) - circle(0, 0, 0.5), 0.05)
    v = s.Vertices()
    d = np.zeros_like(v)
    d[np.hypot(v[:, 0], v[:, 1]) < 0.75, 0] = 0.7
    assert s.SetDeformation(d) is True
    assert len(s.Shape().wires) == 1
    r1, r2, dist = 1.0, 0.5, 0.7
    overlap = (r1**2 * math.acos((dist**2 + r1**2 - r2**2) / (2 * dist * r1))
               + r2**2 * math.acos((dist**2 + r2**2 - r1**2) / (2 * dist * r2))
               - 0.5 * math.sqrt((-dist + r1 + r2) * (dist + r1 - r2) * (dist - r1 + r2) * (dist + r1 + r2)))
    assert abs(s.mass - (math.pi - overlap)) < 2e-2


def test_clean_construction_keeps_right_angles():
    l_shape = square(0, 0, 2) - square(1, 1, 1)
    s = px.DeformableShape(l_shape, 0.1, clean=True)
    assert abs(s.mass - 3) < 1e-12
    assert len(s.Shape().edges) == 6
    assert len(s) == len(px.DeformableShape(l_shape, 0.1))
    assert s.min_distance == 0.1


def test_a_neck_thinner_than_min_distance_splits():
    s = px.DeformableShape(square(0, 0, 1) + rect(1, 0.475, 0.5, 0.05) + square(1.5, 0, 1), 0.1, clean=True)
    assert len(s.Shape().wires) == 2
    # the connections run along the squares' sides, so nothing but the bar goes
    assert abs(s.mass - 2) < 1e-9
    assert s.IsValid()


def test_a_slit_thinner_than_min_distance_is_filled():
    s = px.DeformableShape(square(0, 0, 1) - rect(0.475, 0.5, 0.05, 0.6), 0.1, clean=True)
    assert len(s.Shape().wires) == 1
    assert abs(s.mass - 1) < 1e-9
    assert s.IsValid()


def test_a_thin_part_of_the_given_shape_stays_unless_it_gets_thinner():
    dumbbell = square(0, 0, 1) + rect(1, 0.475, 0.5, 0.05) + square(1.5, 0, 1)
    s = px.DeformableShape(dumbbell, 0.1)
    n = len(s)
    assert s.SetDeformation(np.full((n, 2), 0.3)) is False
    assert len(s.Shape().wires) == 1
    assert abs(s.mass - dumbbell.mass) < 1e-9
    v = s.Vertices()
    d = np.zeros_like(v)
    # the bar's top side (the shape sits at (0.3, 0.3) now) moves down onto its bottom side
    d[(v[:, 0] > 1.3) & (v[:, 0] < 1.8) & (v[:, 1] > 0.8), 1] = -0.03
    assert s.SetDeformation(d) is True
    assert len(s.Shape().wires) == 2
    assert abs(s.mass - 2) < 1e-9


def test_a_hole_near_the_outline_opens_before_touching():
    calls = []
    s = px.DeformableShape(square(0, 0, 1) - circle(0.5, 0.5, 0.2), 0.05, min_distance=0.1,
                           topology_changed_callback=calls.append)
    v = s.Vertices()
    d = np.zeros_like(v)
    d[np.hypot(v[:, 0] - 0.5, v[:, 1] - 0.5) < 0.3, 1] = 0.25
    assert s.SetDeformation(d) is True
    assert calls == [s]
    assert len(s.Shape().wires) == 1
    # the hole top at y = 0.95 is 0.05 below the outline: the bridge of material above the hole goes, nothing else
    assert 1 - math.pi * 0.04 - 0.04 < s.mass < 1 - math.pi * 0.04
    assert np.allclose(s.bounding_box, [[0, 0], [1, 1]])
    assert s.IsValid()


def test_gridfunction_deformation():
    ngs = pytest.importorskip("ngsolve")
    from netgen.occ import OCCGeometry
    s = px.DeformableShape(square(0, 0, 1), 0.1)
    mesh = ngs.Mesh(OCCGeometry(s.Shape().ToOCCFace(), dim=2).GenerateMesh(maxh=0.2))
    gf = ngs.GridFunction(ngs.VectorH1(mesh, order=1))
    gf.Set((0.1 * ngs.x, 0.2 * ngs.x))
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert s.SetDeformation(gf) is False
    # the stretch and shear are linear, so the vertices move exactly
    assert abs(s.mass - 1.1) < 1e-9
    v = s.Vertices()
    assert np.allclose(v[:, 1] - v[:, 0] * 0.2 / 1.1, np.clip(v[:, 1] - v[:, 0] * 0.2 / 1.1, 0, 1))
    assert np.allclose(s.bounding_box[1][0], 1.1)
