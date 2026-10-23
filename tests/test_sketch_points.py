# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import numpy as np
import pytest

import planex as px

TOL = 1e-12


def bend():
    """Line.1 (0, 0) to (4, 0), the arc 'bend' (centre (4, 1), Center.1) on to (5, 1)"""
    sk = px.Sketch((0, 0))
    sk.Line(4, 0).Arc(1, 90, id="bend")
    return sk


def centres(sk):
    return [p for p in sk.elements["Point"] if "Center" in p]


@pytest.mark.constraints
def test_arcs_and_circles_get_centre_points_and_no_point_id_changes():
    sk = bend()
    sk.Circle(10, 0, 2, id="hole")
    assert [p for p in sk.elements["Point"] if p.startswith("Point.")] == ["Point.1", "Point.2", "Point.3"]
    assert centres(sk) == ["Center.1", "Center.2"]
    assert [i.local for i in sk("bend.center")] == ["Center.1"] and [i.local for i in sk("hole.center")] == ["Center.2"]
    assert sk.Location("Center.2") == sk.Location("hole.center") == (10, 0)
    assert sk.dof == 10


@pytest.mark.constraints
def test_a_shared_centre_has_one_point():
    sk = px.Sketch()
    sk.Circle(0, 0, 2, id="c")
    sk.Move((-3, 0)).Line(6, 0, id="cut")
    sk.Split("c", "cut")
    assert sorted(sk.elements["Arc"]) == ["c.1", "c.2"] and sk._Geometry("c.1")["start"] == pytest.approx((2, 0), abs=1e-12)
    assert centres(sk) == ["Center.1"] and sk("c.center")[0].local == sk("c.1.center")[0].local == "Center.1"


@pytest.mark.parametrize("make", [pytest.param(px.Sketch, marks=pytest.mark.constraints, id="Sketch"), px.QuickSketch])
def test_a_centre_goes_with_its_last_element(make):
    sk = make()
    sk.Circle(0, 0, 1, id="c")
    with pytest.raises(ValueError, match=r"Remove\('Center.1'\): c has its centre there, remove it first"):
        sk.Remove("Center.1")
    sk.Remove("c")
    assert centres(sk) == []
    if make is px.Sketch:
        sk.Circle(0, 0, 1, id="d")
        sk.Constrain("Center.2", px.Fix)
        sk.Remove("d")
        assert centres(sk) == ["Center.2"]


@pytest.mark.constraints
def test_copies_get_one_centre_each():
    sk = px.Sketch()
    sk.Circle(2, 0, 0.3, id="bolt")
    sk.CircularPattern("bolt", sk.origin, 4)
    assert len(centres(sk)) == 4 and "CircularPattern.1.1.Center.1" in centres(sk)
    sk["CircularPattern.1.count"] = 6
    assert len(sk.Construction().edges) == 0 and len(centres(sk)) == 6
    sk["CircularPattern.1.count"] = 3
    assert len(sk.Construction().edges) == 0 and len(centres(sk)) == 3
    for order in (["Center.1", "bolt"], ["bolt", "Center.1"]):
        before = len(centres(sk))
        sk.Mirrored(order, px.V)
        assert len(centres(sk)) == before + 1
    before = len(centres(sk))
    sk.Mirrored(sk.elements["Circle"][-1], px.H)
    assert len(centres(sk)) == before + 1


@pytest.mark.constraints
def test_offsets_share_the_centres_and_own_a_free_one():
    sk, qs = bend(), px.QuickSketch((0, 0))
    qs.Line(4, 0).Arc(1, 90, id="bend")
    sk.Offset("bend", 0.5, id="b")
    qs.Offset("bend", 0.5, id="b")
    assert sk("b.right.bend.center")[0].local == qs("b.right.bend.center")[0].local == "Center.1"
    assert "b.Center.1" in [i.local for i in sk("b")]
    sk.Remove("b")
    assert centres(sk) == ["Center.1"]


@pytest.mark.constraints
def test_references_act_as_live_coordinates():
    sk = px.Sketch(px.YZ)
    sk.Move((0, 0), px.Fix).Line(4, 0).Arc(1, 90, id="bend")
    end = sk("Line.1.end")[0]
    assert (end.h, end.v) == (4, 0) and tuple(end) == (4, 0) and end == (4, 0)
    assert (end.x, end.y, end.z) == pytest.approx(tuple(sk.ToXYZ((4, 0))), abs=TOL)
    h, v = end
    assert (h, v) == (4, 0) and end[1] == v and len(end) == 2
    assert np.array(end) == pytest.approx(np.array((4, 0)), abs=TOL)
    assert end.local.startswith("Point.") and repr(end) == "<point %s.%s (4, 0)>" % (sk.id, end.local)
    mid = sk("Line.1.mid")[0]
    sk.Constrain("Line.1", px.Distance, 4, id="l")
    sk.Update({"l": 6})
    start, there = sk.Location("Line.1.start"), sk.Location("Line.1.end")
    assert (end.h, end.v) == there and math.dist(start, there) == pytest.approx(6, abs=TOL)
    assert (mid.h, mid.v) == pytest.approx(((start[0] + there[0]) / 2, (start[1] + there[1]) / 2), abs=TOL)
    end.name, end.maxh = "tip", 0.1
    assert sk(name="tip")[0].local == end.local and sk("Line.1.end").maxh == 0.1
    sk("bend.center").name = "axis"
    assert sk("Center.1").name == "axis"
    assert mid.name is None
    with pytest.raises(AttributeError, match="is a place"):
        mid.name = "m"
    with pytest.raises(AttributeError, match="is a place"):
        sk("Line.1.mid").name = "m"


def test_a_shape_point_is_read_only():
    f = px.Move((0, 0)).Rectangle(2, 1).Face()
    p = f(f.edges[0].id + ".mid")[0]
    assert (p.h, p.v) == (p.p.x, p.p.y) and p.z == 0
    with pytest.raises(AttributeError, match="is a place"):
        p.name = "x"


@pytest.mark.constraints
def test_references_are_taken_where_points_and_ids_are():
    sk = bend()
    end, centre = sk("Line.1.end"), sk("bend.center")
    sk.Move(end).Line(0, -2, id="down")
    assert sk.Location("down.start") == (4, 0) and sk("down.start")[0].local != end[0].local
    sk.Move(centre[0]).Line(1, 0, id="from centre")
    assert sk.Location("from centre.start") == pytest.approx((4, 1), abs=TOL)
    sk.Move((0, 3)).Line(2, parallel="Line.1", distance=(1, centre), id="par")
    assert sk.Location("par.start")[1] == pytest.approx(1, abs=TOL)
    sk.Constrain(centre, "down", px.Distance, 0.5, id="dc")
    assert '"bend.center"' in repr(sk.constraints["Distance"]["dc"])
    assert sk.Location(end) == sk.Location("Line.1.end") and sk.Location(centre[0]) == sk.Location("Center.1")
    assert sk(kind="point", near=centre)[0].local == "Center.1"
    assert end.Measure()["center"] == pytest.approx(tuple(sk.ToXYZ(sk.Location("Line.1.end"))), abs=TOL)
    assert px.Vertex(end) == px.Vertex(sk.Location("Line.1.end"))
    assert px.Vertex(centre[0]) == px.Vertex(sk.Location("Center.1"))
    other = bend()
    with pytest.raises(ValueError, match="belongs to another sketch"):
        other.Constrain(end, "Line.1", px.Distance, 1)
    with pytest.raises(TypeError):
        px.Vertex((1, 2, 3))


@pytest.mark.constraints
def test_dimensions_from_centre_points():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0, px.Horizontal, px.Fix, id="base")
    sk.Circle(2, 3, 1, id="hole")
    sk.Circle(6, 2, 0.5, id="pin")
    sk.Constrain("Center.1", "base", px.Distance, 3, id="a")
    sk.Constrain("pin.center", "base", px.Distance, 2, id="b")
    sk.Expression("b = a/2")
    assert sk["b"] == pytest.approx(1.5, abs=TOL)
    assert sk.Location("pin.center")[1] == pytest.approx(1.5, abs=TOL)
    sk.Constrain("pin", px.Radius, 0.5, id="rp")
    sk.Expression("rp = Center.1.v / 6")
    sk.Update({"a": 4.2})
    assert (sk["rp"], sk["b"]) == (pytest.approx(0.7, abs=TOL), pytest.approx(2.1, abs=TOL))


@pytest.mark.constraints
def test_centres_are_no_lone_points_in_plots():
    sk = bend()
    sk.Circle(10, 0, 2)
    drawing = sk._Drawing()
    ends = px._svg._ends(drawing)
    assert all(tuple(g["point"]) in ends for g in drawing if g["kind"] == "Point" and g["id"] != "origin")
