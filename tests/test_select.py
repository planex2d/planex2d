# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px


@pytest.fixture(autouse=True)
def fresh_counters():
    px.ResetIdCounters()


def plate():
    outer = px.QuickSketch().Move((0, 0)).Rectangle(4, 2, name="outer").Face()
    holes = px.QuickSketch().Circle(1, 1, 0.3, name="hole").Circle(3, 1, 0.3, name="hole").Shape()
    return outer - holes


def centres(sel):
    return sorted((round(e.edge.center.x, 9), round(e.edge.center.y, 9)) for e in sel)


def test_kind_name_and_near():
    f = plate()
    assert len(f.edges(kind="circle")) == 2 and len(f.edges(kind="arc")) == 0 and len(f.edges(kind="line")) == 4
    assert len(f.edges(name="outer")) == 4 and len(f.edges(name="hole")) == 2
    first = f.edges(near=(1, 1))
    assert len(first) == 1 and centres(first) == [(1, 1)]
    assert centres(f.edges(kind="circle")(near=(3, 1.2))) == [(3, 1)]
    assert len(f.edges(nearh=3.9)) == 1 and f.edges(nearh=3.9)[0].midpoint.x == pytest.approx(4)


def test_conditions_and_axes():
    f = plate()
    right = f.edges[px.x > 2](kind="circle")
    assert len(right) == 1 and centres(right) == [(3, 1)]
    assert len(f.edges[(px.x > 1) & (px.v > 1.5)]) == 1
    assert len(f.edges[~(px.x > 2) | (px.h < 0.5)]) == len(f.edges[px.x <= 2])
    top = f.edges(kind="line").Max(px.V)
    assert top[0].midpoint.y == pytest.approx(2)
    assert f.edges(kind="line").Max(px.Y)[0].midpoint.y == pytest.approx(2)
    assert f.edges(kind="line").Min((1, 0))[0].midpoint.x == pytest.approx(0)
    assert [round(e.mass, 9) for e in f.edges(kind="line").Sorted("length")] == [2, 2, 4, 4]


def test_ids_objects_and_chains():
    f = plate()
    f.id = "plate"
    one = f.edges("Line.1")
    assert len(one) == 1 and one[0].id == "plate.Line.1"
    assert f.edges(f.edges[2])[0].id == f.edges[2].id
    assert len(f.edges("plate")) == len(f.edges)
    assert len(f.edges(0, 3)) == 2 and len(f.edges([0, 1, 2])) == 3
    hole = f.edges(kind="circle")(near=(1, 1))
    assert centres(f.edges(kind="circle")(near=hole)) == [(1, 1)]
    assert f("Line.2")[0].id == "plate.Line.2"
    with pytest.raises(TypeError, match="unknown keyword"):
        f.edges(nearest=(0, 0))


@pytest.mark.constraints
def test_constraint_sketch_elements_and_shape_faces():
    sk = px.Sketch(id="bracket")
    sk.Circle(1, 1, 0.3, id="hole")
    sk.Move((0, 0)).Rectangle(4, 2)
    assert sk("hole").ids == ["bracket.hole"]
    assert sk(kind="circle").ids == ["bracket.hole"]
    assert sk(near=(1, 1.1)).ids == ["bracket.hole"]
    assert sk("hole").center == pytest.approx((1, 1, 0))
    g = px.Glue([px.QuickSketch().Rectangle(1, 1).Face(), px.QuickSketch().Move((1, 0)).Rectangle(1, 1).Face()])
    right = g.faces[px.x > 1]
    assert len(right) == 1 and right[0].center[0] == pytest.approx(1.5)
    assert g(g.faces[0].id)[0].id == g.faces[0].id


def test_adjacency_faces_edges_and_outline():
    f = plate()
    assert [type(x).__name__ for x in f.edges(name="hole").faces] == ["FaceView"]
    outline = f.edges(name="hole").faces.outline
    assert len(outline) == 4 and {e.name for e in outline} == {"outer"}
    g = px.Glue([px.QuickSketch().Rectangle(1, 1).Face(), px.QuickSketch().Move((1, 0)).Rectangle(1, 1).Face()])
    shared = g.edges(near=(1, 0.5))
    assert len(shared) == 1 and len(shared.faces) == 2
    left = g.edges(near=(0, 0.5)).faces
    assert len(left) == 1 and len(left.outline) == 4 and len(left.edges) == 4
    assert len(g.faces[px.x > 1].edges) == 4


@pytest.mark.constraints
def test_points_by_id_ends_midpoints_and_centres():
    import planex.measurements as pm

    f = plate()
    f.id = "plate_points"
    line = f.edges(kind="line").Min(px.Y)[0]
    lid = line.id[len("plate_points."):]
    arc = f.edges(kind="circle")(near=(1, 1.2))[0]
    aid = arc.id[len("plate_points."):]
    start, end = f(lid + ".start"), f(lid + ".end")
    assert len(start) == 1 and isinstance(start[0], px.VertexView) and end[0].id.startswith("plate_points.Vertex.")
    assert (end[0].p.x, end[0].p.y) == (line.edge.end.x, line.edge.end.y)
    assert f.vertices(lid + ".end")[0].id == f.edges(lid + ".end")[0].id == end[0].id
    assert px.Find(line.id + ".end")[0].id == end[0].id
    mid = f(lid + ".mid")[0]
    assert mid.id == line.id + ".mid" and mid == pytest.approx((2, 0)) and f(lid + ".midpoint")[0] == mid
    centre = f(aid + ".center")
    assert centre[0] == pytest.approx((1, 1)) and centres(f.edges(kind="circle")(near=centre)) == [(1, 1)]
    with pytest.raises(ValueError, match="only arcs"):
        f(lid + ".center")
    assert line.Distance(centre) == pytest.approx(1) and end[0].Distance(centre) == pytest.approx(math.hypot(end[0].p.x - 1, end[0].p.y - 1))
    assert f.Measure(to=centre)["distance"] == pytest.approx(0.3)
    assert f.Measure(pm.Clearance(lid + ".mid", aid + ".center")) == pytest.approx(math.sqrt(2))
    assert f.Measure(pm.Hausdorff(aid, aid + ".center")) == pytest.approx(0.3)
    g = px.Glue([px.QuickSketch().Rectangle(1, 1).Face(), px.QuickSketch().Move((1, 0)).Rectangle(1, 1).Face()])
    assert g(g.faces[px.x > 1][0].id + ".center")[0] == pytest.approx((1.5, 0.5))
    sk = px.Sketch(id="bracket")
    sk.Circle(1, 1, 0.3, id="hole")
    assert sk("hole.center")[0] == pytest.approx((1, 1)) and sk("hole.center")[0].id == "bracket.Center.1"


@pytest.mark.constraints
def test_constraint_sketch_elements_and_construction_points_by_name():
    cs = px.Sketch()
    cs.Move((0, 0)).Line(4, 0, px.Horizontal, id="bottom", name="floor").Line(0, 2, px.Perpendicular) \
        .Line(-4, 0, px.Perpendicular).Close(px.Perpendicular)
    cs.Vertex("pin", (2, 1))
    assert cs(name="floor").ids == [cs.id + ".bottom"]
    assert cs(name="pin").ids == []
    cs("pin").name = "anchor"
    assert cs(name="anchor").ids == [cs.id + ".pin"]
    assert cs("pin", name="anchor").ids == [cs.id + ".pin"] and cs(name="nothing").ids == []


def test_kinds_and_names_joined_by_a_bar():
    f = plate()
    assert len(f.edges(kind="arc|circle")) == 2 and len(f.edges(kind="Line | circle")) == 6
    assert len(f.edges(name="outer|hole")) == 6 and len(f.edges(name="hole | nothing")) == 2


def test_a_name_has_no_bar():
    f = plate()
    with pytest.raises(ValueError, match=r"a name has no '\|', it joins names in a selection"):
        f.edges[0].name = "a|b"
    with pytest.raises(ValueError, match=r"a name has no '\|'"):
        px.FromPolyline([(0, 0), (1, 0)], name="a|b")
    with pytest.raises(ValueError, match=r"a name has no '\|'"):
        px.QuickSketch().Circle(0, 0, 1, name="a|b")


def test_a_name_read_in_gets_an_underscore_for_the_bar():
    from netgen import occ

    plate = occ.WorkPlane().Rectangle(2, 1).Face()
    plate.edges[0].name = "wall|left"
    with pytest.warns(px.PlanexWarning, match=r"name 'wall\|left' read as 'wall_left': '\|' joins names in a selection"):
        f = px.FromOCC(plate)
    assert "wall_left" in {e.name for e in f.edges}


def test_a_wire_to_a_polyline_and_back():
    import numpy as np

    w = px.QuickSketch().Circle(0, 0, 1).Wire()
    points = w.ToPolyline(1e-3)
    assert isinstance(points, np.ndarray) and points.shape[1] == 2 and (points[0] == points[-1]).all()
    assert np.allclose(np.hypot(points[:, 0], points[:, 1]), 1)
    back = px.FromPolyline(points, closed=True)
    assert back.Face().mass == pytest.approx(np.pi, rel=1e-2)
    assert (w.Polylines(1e-3)[0] == points).all()
