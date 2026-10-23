# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import numpy as np
import pytest

import planex as px
from helpers import point


def test_move_to_gives_a_sketch_without_the_netgen_names():
    s = px.Move((1, 2))
    assert type(s) is px.QuickSketch
    assert not hasattr(s, "LineTo") and not hasattr(s, "ArcTo")


def test_line_overloads():
    s = px.Move((0, 0))
    assert point(s.Line(2).pen.position) == (2, 0)
    assert point(s.Line(1, 2).pen.position) == (3, 2)
    assert point(s.Line((5, 5)).pen.position) == (5, 5)
    assert point(s.Line([6, 5]).pen.position) == (6, 5)
    assert point(s.Line(np.array((6, 7))).pen.position) == (6, 7)
    # the pen goes on along the line drawn last (2026-10-11), also after a line to a point
    assert point(s.Line(1.5).pen.position) == (6, 8.5)
    assert len(s.Wire(unify=False).edges) == 6


def test_arc_overloads():
    a = px.Move((0, 0)).Arc(1, 90)
    assert point(a.pen.position) == pytest.approx((1, 1))
    b = px.Move((0, 0)).Arc((1, 1), tangent=(1, 0), name="quarter")
    assert point(b.pen.position) == (1, 1)
    assert point(b.pen.direction) == pytest.approx((0, 1))
    e = b.Wire().edges[0]
    assert e.name == "quarter"
    assert e.edge.radius == pytest.approx(1)


@pytest.mark.skip(reason="px.WorkPlane is switched off (later list)")
def test_workplane_is_a_sketch_with_netgen_names():
    wp = px.WorkPlane()
    assert isinstance(wp, px.Sketch)
    assert wp.MoveTo(0, 0) is wp
    assert wp.LineTo(2, 0, name="bottom") is wp
    wp.ArcTo(2, 2, (1, 0), maxh=0.1).Close()
    edges = wp.Wire().edges
    assert [e.name for e in edges] == ["bottom", None, None]
    assert edges[1].maxh == 0.1
    assert point(edges[1].edge.center) == pytest.approx((2, 1))


def test_copies_of_a_sketch_stay_sketches():
    s = px.Move((0, 0)).Line((1, 0))
    assert type(s.Copy()) is px.QuickSketch
    assert len(s.Copy().Wire().edges) == 1


def test_both_export_and_mesh_the_same():
    s = px.Move((0, 0)).Rectangle(2, 1)
    wp = px.QuickSketch().Move((0, 0)).Rectangle(2, 1)
    assert s.Face().mass == wp.Face().mass == pytest.approx(2)
    assert px.OCCGeometry(s).shape.mass == pytest.approx(px.OCCGeometry(wp).shape.mass)


def test_points_and_tangents_on_edges():
    line = px.Line((0, 0), (4, 0)).edges[0]
    assert (point(line.midpoint), point(line.PointAt(0.25)), point(line.TangentAt(0.7))) == ((2, 0), (1, 0), (1, 0))
    for ccw, turn in [(True, 1), (False, -1)]:
        arc = px.Arc((1, 0), (-1, 0), (0, 0), reverse=not ccw).edges[0]
        assert point(arc.center) == (0, 0)
        assert point(arc.midpoint) == pytest.approx((0, turn))
        assert point(arc.PointAt(0)) == (1, 0)
        assert point(arc.TangentAt(0)) == pytest.approx((0, turn))
        assert point(arc.TangentAt(0.5)) == pytest.approx((-1, 0))
    spline = px.Spline([(0, 0), (1, 1), (2, 0.5), (3, 2)]).edges[0]
    assert point(spline.PointAt(0)) == (0, 0)
    assert point(spline.PointAt(1)) == pytest.approx((3, 2))
    t = spline.TangentAt(0.5)
    assert t.x ** 2 + t.y ** 2 == pytest.approx(1)
    with pytest.raises(ValueError, match="only arcs"):
        line.center
    with pytest.raises(ValueError, match=r"PointAt\(1.5"):
        line.PointAt(1.5)


def test_construction_elements_stay_out_of_wires_faces_and_exports():
    s = px.Move((0, 0)).Line((4, 0), name="base", construction=True)
    assert len(s.Construction().edges) == 1
    assert point(s.pen.position) == (0, 0)
    s.Line((4, 0)).Line((4, 3)).Close()
    assert s.Face().mass == pytest.approx(6)
    assert len(s.Face().edges) == 3
    assert len(s.ToOCCFace().edges) == 3
    assert [len(w.edges) for w in s.Wires()] == [3]
    s.Move((10, 10)).Rectangle(2, 1, construction=True)
    assert len(s.Construction().edges) == 5
    assert s.Face().mass == pytest.approx(6)


def test_named_points():
    s = px.Move((1, 2)).Vertex("a").Vertex("b", (3, 4))
    assert {k: point(v) for k, v in s.points.items()} == {"a": (1, 2), "b": (3, 4)}
    s.Move("b")
    assert point(s.pen.position) == (3, 4) and point(s.pen.direction) == (1, 0)
    s.Move("a", dir=(0, 2))
    assert point(s.pen.position) == (1, 2) and point(s.pen.direction) == (0, 1)
    s.Move([5, 5])
    assert point(s.pen.position) == (5, 5) and point(s.pen.direction) == (0, 1)
    with pytest.raises(ValueError, match="id 'a' is taken"):
        s.Vertex("a")
    with pytest.raises(ValueError, match="no element 'c'"):
        s.Move("c")
    with pytest.raises(ValueError, match="the direction has no length"):
        s.Move((0, 0), dir=(0, 0))
    assert "a" not in px.Move((0, 0)).points
    t = px.Move((1, 2), dir=(0, -1))
    assert (point(t.pen.position), point(t.pen.direction)) == ((1, 2), (0, -1))


def test_an_end_tangent_does_not_make_the_spline_overshoot():
    """a Sears-Haack nose through points spaced quadratically: with its end tangent the curve ends at the last point
    (OCC's tangent scaling by the point spacing let it run to x = 230)"""
    import math
    R, L = 20, 120

    def haack(x):
        t = math.acos(1 - 2 * x / L)
        return R / math.sqrt(math.pi) * math.sqrt(t - math.sin(2 * t) / 2)

    points = [(L * (k / 40) ** 2, haack(L * (k / 40) ** 2)) for k in range(1, 41)]
    s = px.QuickSketch().Move((0, 0)).Spline(points, tangents={40: (1, 0)})
    assert s.Wire().ToOCCWire().bounding_box[1][0] == pytest.approx(L, abs=1e-6)
    s.Line((L, 0)).Close().Face()
