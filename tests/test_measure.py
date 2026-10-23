# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest
from netgen import occ

import planex as px
from helpers import circle, ends, point, rect, square


def gp_mat(m):
    return [[m[i, j] for j in range(3)] for i in range(3)]


def shoelace(wire):
    # the vertices where each edge meets the next, in the order of the edges
    es = list(wire.edges)
    pts = []
    for e, nxt in zip(es, es[1:] + es[:1]):
        pts.append(point(e.end) if point(e.end) in (point(nxt.start), point(nxt.end)) else point(e.start))
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))


def test_outer_loop_and_holes():
    ring = (square(0, 0, 4) - square(1, 1, 1) - square(2.5, 2.5, 1)).Face()
    assert len(ring.outer.edges) == 4 and sorted(len(h.edges) for h in ring.inner) == [4, 4]
    assert len(ring.wires) == 3
    assert shoelace(ring.outer) > 0 and all(shoelace(h) < 0 for h in ring.inner)
    assert ring.outer.mass == pytest.approx(16)
    assert square(0, 0).inner == []


def test_center_and_inertia_match_occ():
    f = (square(0, 0, 3) - circle(2, 1, 0.5)).Face()
    c = f.ToOCCFace().center
    assert f.center == pytest.approx((c.x, c.y, c.z), rel=1e-12)
    assert sum(gp_mat(f.ToOCCFace().inertia), []) == pytest.approx(sum(map(list, f.inertia), []), rel=1e-12)
    lifted = f.Rotated((0, 0, 0), (1, 1, 0), 40).Translated((1, 2, 3))
    assert sum(gp_mat(lifted.ToOCCFace().inertia), []) == pytest.approx(sum(map(list, lifted.inertia), []), rel=1e-12)
    c = lifted.ToOCCFace().center
    assert lifted.center == pytest.approx((c.x, c.y, c.z), rel=1e-12)
    w = px.Move((0, 0)).Line((2, 0)).Arc((3, 1), tangent=(1, 0)).Line((3, 3)).Wire()
    assert sum(gp_mat(w.ToOCCWire().inertia), []) == pytest.approx(sum(map(list, w.inertia), []), rel=1e-12)
    assert px.Shape().center is None and px.Shape().inertia is None
    two = square(0, 0) + square(4, 0)
    assert two.center == pytest.approx((2.5, 0.5, 0))


def test_section_values():
    m = rect(0, 0, 4, 2).Measure()
    assert (m["Ih"], m["Iv"], m["Ihv"]) == pytest.approx((4 * 8 / 12, 2 * 64 / 12, 0), abs=1e-12)
    assert (m["I1"], m["I2"]) == pytest.approx((2 * 64 / 12, 4 * 8 / 12))
    assert m["principal_angle"] == pytest.approx(90)
    turned = rect(0, 0, 4, 2).Rotated((0, 0), 30).Measure()
    assert (turned["I1"], turned["I2"]) == pytest.approx((m["I1"], m["I2"]))
    assert turned["principal_angle"] % 180 == pytest.approx(120)
    disc = circle(0, 0, 2).Measure()
    assert disc["Ih"] == pytest.approx(math.pi * 16 / 4) and disc["Ihv"] == pytest.approx(0, abs=1e-12)


def test_distance_to_points_and_components():
    s = square(0, 0, 2)
    assert s.Distance((5, 1)) == pytest.approx(3) and s.Distance((1, 1)) == 0
    assert s.Distance((5, 6)) == pytest.approx(5)
    assert s.Distance((5, 1), components="h") == pytest.approx(3)
    assert s.Distance((5, 1), components="xyz") == pytest.approx((3, 0, 0))
    assert s.Distance((1, 1, 3)) == pytest.approx(3) and s.Distance((1, 1, 3), components="z") == pytest.approx(3)
    with pytest.raises(ValueError, match="hvxyz"):
        s.Distance((5, 1), components="w")
    assert s.ClosestPoint((5, 1)) == pytest.approx((2, 1)) and s.ClosestPoint((1, 1)) == (1, 1)
    assert s.ClosestPoint((5, 1, 7)) == pytest.approx((2, 1, 0))
    assert circle(0, 0, 1).Distance((3, 4)) == pytest.approx(4)
    w = px.Move((0, 0)).Arc(1, 90).Wire()
    assert w.Distance((0, 1)) == pytest.approx(1)


def test_distance_between_shapes():
    a, b = square(0, 0), square(3, 0.5)
    assert a.Distance(b) == pytest.approx(2) and a.Distance(b, components="hv") == pytest.approx((2, 0))
    assert a.Distance(square(0.5, 0.5)) == 0
    assert a.Distance(square(-1, -1, 4)) == 0
    assert circle(0, 0, 1).Distance(circle(5, 0, 2)) == pytest.approx(2)
    assert circle(0, 0, 5).Distance(circle(1, 0, 1)) == 0
    arc = px.Move((1, 0)).Arc(1, 180).Wire()
    line = px.Line((-2, 3), (2, 3))
    assert arc.Distance(line) == pytest.approx(1)
    assert line.Distance(arc, components="v") == pytest.approx(-1)
    assert a.Distance(px.Line((-1, 3), (5, 3))) == pytest.approx(2)


def test_distance_in_3d_and_to_occ():
    a = square(0, 0)
    lifted = square(0, 0).Translated((0, 0, 2))
    assert a.Distance(lifted) == pytest.approx(2) and a.Distance(lifted, components="z") == pytest.approx(2)
    assert a.Distance(lifted, project=True) == 0
    box = occ.Box(occ.Pnt(3, 0, -1), occ.Pnt(4, 1, 1))
    assert a.Distance(box) == pytest.approx(2)
    above = occ.Segment(occ.Pnt(0, 3, 5), occ.Pnt(1, 3, 5))
    assert a.Distance(above) == pytest.approx(math.hypot(2, 5))
    assert a.Distance(above, project=True) == pytest.approx(2)


def test_distance_to_the_carrier():
    e = px.Line((0, 0), (1, 0)).edges[0]
    assert e.Distance((5, 2)) == pytest.approx(math.hypot(4, 2))
    assert e.Distance((5, 2), on_carrier=True) == pytest.approx(2)
    q = px.Move((1, 0)).Arc(1, 90).Wire().edges[0]
    assert q.Distance((0, -3)) == pytest.approx(math.hypot(1, 3))
    assert q.Distance((0, -3), on_carrier=True) == pytest.approx(math.sqrt(17) - 1)
    assert e.ClosestPoint((0.5, 4)) == pytest.approx((0.5, 0))


def test_contains():
    s = square(0, 0, 2) - square(0.5, 0.5, 1)
    assert s.Contains((0.25, 0.25)) and not s.Contains((1, 1)) and not s.Contains((3, 3))
    assert s.Contains((0, 1)) and not s.Contains((0, 1), boundary=False)
    assert s.Contains((0.25, 0.25, 0)) and not s.Contains((0.25, 0.25, 1))


def test_measure_named_parts():
    sk = px.Move((0, 0)).Line((4, 0), name="bottom").Line((4, 2)).Line((0, 2), name="top").Close()
    sk.Circle(2, 1, 0.5, name="hole")
    sk.Vertex("p", (1, 1))
    bottom = sk.Measure("bottom")
    assert bottom["type"] == "line" and bottom["length"] == pytest.approx(4)
    assert bottom["center"] == pytest.approx((2, 0, 0)) and bottom["bounding_box"] == ((0, 0), (4, 0))
    hole = sk.Measure("hole")
    assert hole["type"] == "circle" and hole["radius"] == pytest.approx(0.5) and hole["angle"] == pytest.approx(360)
    assert hole["center"] == pytest.approx((2, 1, 0))
    assert sk.Measure("p")["type"] == "point" and sk.Measure("p")["center"] == (1, 1, 0)
    with pytest.raises(ValueError, match="no element, point or edge name 'nothing'"):
        sk.Measure("nothing")
    between = sk.Measure("bottom", to="top")
    assert between["distance"] == pytest.approx(2) and between["dv"] == pytest.approx(2) and between["angle"] == pytest.approx(0)
    assert sk.Measure("p", to="hole")["distance"] == pytest.approx(0.5)
    whole = sk.Measure()
    # the circle inside the rectangle is a hole
    assert whole["type"] == "face" and whole["area"] == pytest.approx(8 - math.pi / 4)
    q = px.Move((0, 0)).Arc(2, 90, name="q").Line(1, name="r")
    m = q.Measure("q")
    assert m["type"] == "arc" and m["center"] == pytest.approx((0, 2, 0)) and m["angle"] == pytest.approx(90)


def test_measure_angle_between_lines_and_faces_by_name():
    a = px.Line((0, 0), (1, 0))
    b = px.Line((0, 1), (1, 1 + math.tan(math.radians(30))))
    assert a.Measure(to=b)["angle"] == pytest.approx(30)
    assert a.Measure(to=px.Line((0, 1), (-1, 1 + math.tan(math.radians(30)))))["angle"] == pytest.approx(30)
    iron, air = square(5, 0), square(0, 0, 3)
    iron.name, air.name = "iron", "air"
    s = air + iron
    assert s.Measure("iron")["type"] == "face" and s.Measure("iron")["area"] == pytest.approx(1)
    assert s.Measure("air")["area"] == pytest.approx(9) and s.Measure()["type"] == "faces"
    assert s.Measure("iron", to="air")["distance"] == pytest.approx(2)
    assert s.Measure("iron", to="air")["du"] == pytest.approx(-2)


def test_center_and_inertia_of_faces_with_partial_arcs():
    # a rectangle L x 2r with a semicircular end, moved away from the origin: lines and a partial arc in one loop
    L, r, off = 3.0, 1.0, (5.0, -2.0)
    s = px.Move((0, 0)).Line((L, 0)).Arc((L, 2 * r), tangent=(1, 0)).Line((0, 2 * r)).Close()
    f = s.Face().Translated(off)
    rect, half = 2 * r * L, math.pi * r * r / 2
    xc = (rect * L / 2 + half * (L + 4 * r / (3 * math.pi))) / (rect + half)
    assert f.mass == pytest.approx(rect + half, rel=1e-14)
    assert f.center[:2] == pytest.approx((off[0] + xc, off[1] + r), rel=1e-14)
    assert f.inertia[0][0] == pytest.approx(L * (2 * r) ** 3 / 12 + math.pi * r**4 / 8, rel=1e-13)


def test_area_center_and_inertia_far_from_the_origin():
    L, r = 3.0, 1.0
    base = px.Move((0, 0)).Line((L, 0)).Arc((L, 2 * r), tangent=(1, 0)).Line((0, 2 * r)).Close().Face()
    for off in ((1e6, -2e6), (1e8, 1e8)):
        f = base.Translated(off)
        assert f.mass == pytest.approx(base.mass, rel=1e-14)
        assert f.Measure()["area"] == pytest.approx(base.mass, rel=1e-14)
        assert [c - o for c, o in zip(f.center[:2], off)] == pytest.approx(base.center[:2], abs=1e-6)
        assert f.inertia[0][0] == pytest.approx(base.inertia[0][0], rel=1e-12)
