# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px

occ = pytest.importorskip("netgen.occ")


def at_height(z):
    return occ.Axes(p=(0, 0, z), n=occ.Z, h=occ.X)


def segment(p, q, name):
    e = occ.Segment(occ.Pnt(*p), occ.Pnt(*q))
    e.name = name
    return e


def path():
    """a 3d polyline crossing z = 0 at (0.5, 1) and (2, 2)"""
    return occ.Wire([segment((0, 0, -1), (1, 2, 1), "path"), segment((1, 2, 1), (3, 2, -1), "path")])


def cs_points(sk):
    return [c for p in sorted(tuple(sk._Location(p)) for p in sk.elements["Point"] if p != "origin") for c in p]


def length(shape):
    return sum(e.mass for e in shape.edges)


@pytest.mark.constraints
def test_a_wire_crossing_the_plane_gives_points_to_sketches_and_a_warning_otherwise():
    with pytest.warns(px.PlanexWarning, match="2 point"):
        assert len(px.Intersect3d(at_height(0), path()).edges) == 0
    s = px.QuickSketch(at_height(0)).Intersect3d(path())
    assert cs_points(s) == pytest.approx([0.5, 1, 2, 2], abs=1e-9)
    sk = px.Sketch(at_height(0))
    sk.Intersect3d(path())
    assert cs_points(sk) == pytest.approx([0.5, 1, 2, 2], abs=1e-9)
    assert cs_points(px.QuickSketch(at_height(5)).Intersect3d(path())) == []


def test_a_curve_crossing_the_plane_gives_its_points():
    circle = occ.Circle(occ.Pnt(0, 0, 0), occ.Y, 1)
    assert cs_points(px.QuickSketch(at_height(0)).Intersect3d(circle)) == pytest.approx([-1, 0, 1, 0], abs=1e-9)
    tilted = occ.Circle(occ.Pnt(0, 0, 0.5), occ.Y, 1)
    s = px.QuickSketch(at_height(0)).Intersect3d(tilted)
    assert cs_points(s) == pytest.approx([-0.75 ** 0.5, 0, 0.75 ** 0.5, 0], abs=1e-9)


def test_a_wire_touching_the_plane_gives_the_touch_point():
    touching = occ.Circle(occ.Pnt(0, 0, 1), occ.Y, 1)
    assert cs_points(px.QuickSketch(at_height(0)).Intersect3d(touching)) == pytest.approx([0, 0], abs=1e-7)


def test_a_wire_in_the_plane_is_a_wire_named_after_its_edges():
    rail = occ.Wire([segment((0, 0, 0), (2, 0, 0), "rail"), segment((2, 0, 0), (2, 1, 0), "rail")])
    # occ.Wire keeps the name of its first edge only
    rail.edges.name = "rail"
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Intersect3d(at_height(0), rail)
    assert len(s.faces) == 0 and len(s.edges) == 2 and {e.name for e in s.edges} == {"rail"}
    assert length(s) == pytest.approx(3, abs=1e-12)
    assert px.QuickSketch(at_height(0)).Intersect3d(rail).points == {}


def test_a_wire_leaving_the_plane_keeps_its_part_in_it_and_no_extra_point():
    bent = occ.Wire([segment((0, 0, 0), (2, 0, 0), "bent"), segment((2, 0, 0), (2, 0, 3), "bent")])
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        s = px.Intersect3d(at_height(0), bent)
    assert len(s.edges) == 1 and length(s) == pytest.approx(2, abs=1e-12)
    assert px.QuickSketch(at_height(0)).Intersect3d(bent).points == {}


def test_a_free_vertex_in_the_plane_is_a_point():
    s = px.QuickSketch(at_height(0)).Intersect3d(occ.Vertex(occ.Pnt(3, 4, 0)))
    assert cs_points(s) == [3, 4]
    with pytest.warns(px.PlanexWarning, match="misses the shape"):
        assert len(px.Intersect3d(at_height(0), occ.Vertex(occ.Pnt(3, 4, 1))).edges) == 0


def test_a_solid_and_a_wire_together():
    box = occ.Box(occ.Pnt(0, 0, -1), occ.Pnt(2, 3, 1))
    both = occ.Compound([box, occ.Segment(occ.Pnt(5, 5, -1), occ.Pnt(5, 5, 1))])
    with pytest.warns(px.PlanexWarning, match="1 point"):
        s = px.Intersect3d(at_height(0), both)
    assert len(s.faces) == 1 and s.mass == pytest.approx(6, abs=1e-12)
    sk = px.QuickSketch(at_height(0)).Intersect3d(both)
    assert cs_points(sk) == pytest.approx([0, 0, 0, 3, 2, 0, 2, 3, 5, 5], abs=1e-9)
    assert len(sk.Construction().edges) == 4


def named_points(sk, name):
    return [c for p in sorted(tuple(sk._Location(e.local)) for e in sk(name=name)) for c in p]


@pytest.mark.constraints
def test_a_constraint_sketch_selects_the_points_of_a_wire_by_its_name():
    named = path()
    named.edges.name = "guide"
    sk = px.Sketch(at_height(0))
    sk.Intersect3d(named)
    assert named_points(sk, "guide") == pytest.approx([0.5, 1, 2, 2], abs=1e-9)
    # the name on the wire only
    rail = occ.Wire([occ.Segment(occ.Pnt(5, 0, -1), occ.Pnt(6, 2, 1)), occ.Segment(occ.Pnt(6, 2, 1), occ.Pnt(8, 2, -1))])
    rail.name = "rail"
    sk.Intersect3d(rail)
    assert named_points(sk, "rail") == pytest.approx([5.5, 1, 7, 2], abs=1e-9)
    given = px.Sketch(at_height(0))
    given.Intersect3d(named, name="cut")
    assert named_points(given, "cut") == pytest.approx([0.5, 1, 2, 2], abs=1e-9) and len(given(name="guide")) == 0
