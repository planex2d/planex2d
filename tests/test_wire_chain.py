# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""a px.Wire is one clean chain; px.Wires builds clean wires from loose edges"""
import math

import pytest
from netgen import occ

import planex as px
from planex import _planex
from helpers import ends, line


def chain_points(w):
    """the vertices along the chain: each edge keeps its own direction, so take the end away from the previous edge"""
    segs = [ends(v.edge) for v in w.edges]
    a, b = segs[0]
    pts = [a, b] if b in segs[1] else [b, a]
    for s, e in segs[1:]:
        assert pts[-1] in (s, e)
        pts.append(e if pts[-1] == s else s)
    return pts


SQUARE = [((0, 0), (1, 0)), ((1, 0), (1, 1)), ((1, 1), (0, 1)), ((0, 1), (0, 0))]


def test_wire_of_edges_in_any_order_and_direction_is_one_chain():
    edges = [line((1, 1), (1, 0)), line((0, 1), (0, 0)), line((0, 0), (1, 0)), line((0, 1), (1, 1))]
    w = px.Wire(edges)
    assert len(w.edges) == 4 and w.IsClosed()
    pts = chain_points(w)
    assert pts[0] == pts[-1] and sorted(set(pts)) == [(0, 0), (0, 1), (1, 0), (1, 1)]
    assert sorted(ends(e.edge) for e in w.edges) == sorted(ends(e) for e in edges)


def test_wire_of_an_open_chain_starts_at_a_free_start():
    w = px.Wire([line((2, 0), (3, 0)), line((1, 0), (2, 0)), line((0, 0), (1, 0))])
    assert [ends(e.edge) for e in w.edges] == [((0, 0), (1, 0)), ((1, 0), (2, 0)), ((2, 0), (3, 0))]
    assert not w.IsClosed()


def test_wire_takes_edges_views_and_wires_and_an_empty_list():
    sq = px.FromPolyline([(0, 0), (1, 0), (1, 1), (0, 1)], closed=True)
    assert len(px.Wire(list(sq.edges)).edges) == 4
    assert len(px.Wire([px.Line((0, 0), (1, 0)), px.FromPolyline([(1, 0), (2, 0), (2, 1)])]).edges) == 3
    assert len(px.Wire(px.Line((0, 0), (1, 0))).edges) == 1
    assert len(px.Wire([]).edges) == 0 and len(px.Wire().edges) == 0
    with pytest.raises(TypeError, match=r"Wire\(edges\): takes edges"):
        px.Wire([(0, 0), (1, 0)])


def test_wire_raises_for_several_chains_naming_wires():
    with pytest.raises(ValueError, match=r"Wire\(edges\): the edges form 2 chains \(the closest open ends are 1 apart\).*"
                                         r"px.Wires\(edges\)"):
        px.Wire([line((0, 0), (1, 0)), line((2, 0), (3, 0))])


def test_wire_raises_for_a_branch_and_an_eight():
    with pytest.raises(ValueError, match=r"3 edge ends meet at \(1, 0\).*an 8 is two wires"):
        px.Wire([line((0, 0), (1, 0)), line((1, 0), (2, 0)), line((1, 0), (1, 1))])
    eight = [line(a, b) for a, b in SQUARE] + [line((1, 1), (2, 1)), line((2, 1), (2, 2)), line((2, 2), (1, 2)),
                                                line((1, 2), (1, 1))]
    with pytest.raises(ValueError, match=r"4 edge ends meet at \(1, 1\)"):
        px.Wire(eight)
    assert len(px.Wires(eight)) == 2


def test_wire_raises_for_a_crossing_or_an_overlap_and_never_cuts():
    bowtie = [line((0, 0), (2, 2)), line((2, 2), (2, 0)), line((2, 0), (0, 2)), line((0, 2), (0, 0))]
    with pytest.raises(ValueError, match=r"cross.*px.Wires\(edges\) cuts the edges"):
        px.Wire(bowtie)
    with pytest.raises(ValueError, match="overlap"):
        px.Wire([line((0, 0), (2, 0)), line((2, 0), (1, 0))])
    with pytest.raises(ValueError, match="touch"):
        px.Wire([line((0, 0), (2, 0)), line((2, 0), (2, 1)), line((2, 1), (1, 1)), line((1, 1), (1, 0))])


def test_wire_joins_ends_within_tol_bit_exactly():
    edges = [line((0, 0), (1, 0)), line((1, 1e-9), (1, 1))]
    with pytest.raises(ValueError, match="2 chains.*tol= joins"):
        px.Wire(edges)
    w = px.Wire(edges, tol=1e-6)
    assert ends(w.edges[1].edge)[0] == (1, 0)
    assert w.OpenEnds() == [(0, False), (1, True)]
    rounding = px.Wire([line((0, 0), (1, 0)), line((1, 1e-16), (1, 1))])
    assert ends(rounding.edges[1].edge)[0] == (1, 0)
    for tol in (-1.0, float("nan")):
        with pytest.raises(ValueError, match="tol is a length >= 0"):
            px.Wire(edges, tol=tol)


def test_add_appends_prepends_in_either_direction_and_closes():
    w = px.Wire()
    assert w.Add(px.Line((1, 0), (2, 0))) is w
    w.Add(px.Line((3, 0), (2, 0)))
    w.Add(px.Line((1, 0), (0, 0)))
    assert [ends(e.edge) for e in w.edges] == [((1, 0), (0, 0)), ((1, 0), (2, 0)), ((3, 0), (2, 0))]
    w.Add(px.FromPolyline([(3, 0), (3, 1), (0, 1)]))
    assert not w.IsClosed()
    w.Add(px.Line((0, 0), (0, 1)))
    assert w.IsClosed() and px.Face([w]).mass == pytest.approx(3)


def test_add_moves_the_added_end_onto_the_wire_within_tol():
    w = px.Line((0, 0), (1, 0))
    with pytest.raises(ValueError, match=r"Add: the added edges' nearest end is 1e-09 from the wire's free ends.*tol="):
        w.Add(px.Line((1, 1e-9), (1, 1)))
    assert len(w.edges) == 1
    w.Add(px.Line((1, 1e-9), (1, 1)), tol=1e-6)
    assert ends(w.edges[0].edge) == ((0, 0), (1, 0)) and ends(w.edges[1].edge) == ((1, 0), (1, 1))


def test_add_raises_and_changes_nothing():
    sq = px.FromPolyline([(0, 0), (1, 0), (1, 1), (0, 1)], closed=True)
    with pytest.raises(ValueError, match="the wire is closed.*px.Wires"):
        sq.Add(px.Line((1, 1), (2, 2)))
    w = px.FromPolyline([(0, 0), (2, 0), (2, 2), (1, 2)])
    with pytest.raises(ValueError, match="cross"):
        w.Add(px.Line((1, 2), (1, -1)))
    with pytest.raises(ValueError, match="overlap"):
        w.Add(px.Line((1, 2), (1.5, 2)))
    with pytest.raises(ValueError, match="3 edge ends meet at \\(2, 2\\)"):
        w.Add(px.Line((1, 2), (2, 2)))
    with pytest.raises(ValueError, match="nearest end is"):
        w.Add(px.Line((5, 5), (6, 6)))
    with pytest.raises(ValueError, match="edge ends meet"):
        w.Add(px.Line((1, 2), (2, 0)))
    assert [ends(e.edge) for e in w.edges] == [((0, 0), (2, 0)), ((2, 0), (2, 2)), ((2, 2), (1, 2))]
    with pytest.raises(ValueError, match="another plane"):
        w.Add(px.Line((1, 2), (1, 3), plane=occ.Axes((0, 0, 1), occ.Z, occ.X)))


def test_wires_cut_crossing_edges_into_a_list():
    ws = px.Wires([px.Line((0, 0), (2, 0)), px.Line((1, -1), (1, 1))])
    assert isinstance(ws, list) and len(ws) == 2
    assert sorted(len(w.edges) for w in ws) == [2, 2]
    assert sum(w.mass for w in ws) == pytest.approx(4)
    assert px.Wires([]) == []
    [one] = px.Wires(px.Line((0, 0), (1, 0)))
    assert len(one.edges) == 1


def test_wires_keep_an_overlapping_stretch_once_and_merge_nothing():
    [w] = px.Wires([line((0, 0), (2, 0)), line((1, 0), (3, 0))])
    assert sorted(ends(e.edge) for e in w.edges) == [((0, 0), (1, 0)), ((1, 0), (2, 0)), ((2, 0), (3, 0))]
    [straight] = px.Wires([line((0, 0), (1, 0)), line((1, 0), (2, 0))])
    assert len(straight.edges) == 2


def test_wires_of_an_outline_with_a_rib_ending_on_it():
    outline = [line(a, b) for a, b in [((0, 0), (4, 0)), ((4, 0), (4, 2)), ((4, 2), (0, 2)), ((0, 2), (0, 0))]]
    ws = px.Wires(outline + [line((2, 0), (2, 1))])
    assert len(ws) == 2
    closed = [w for w in ws if w.IsClosed()]
    rib = [w for w in ws if not w.IsClosed()]
    assert len(closed) == 1 and len(closed[0].edges) == 5 and closed[0].mass == pytest.approx(12)
    assert len(rib) == 1 and [ends(e.edge) for e in rib[0].edges] == [((2, 0), (2, 1))]


def test_wires_join_ends_within_tol_and_leave_larger_gaps_open():
    edges = [line((0, 0), (1, 0)), line((1, 1e-9), (1, 1))]
    assert len(px.Wires(edges)) == 2
    [w] = px.Wires(edges, tol=1e-6)
    assert len(w.edges) == 2 and w.OpenEnds() == [(0, False), (1, True)]


def test_remove_small_gaps_closes_a_chain_but_never_branches_it():
    w = px.FromPolyline([(0, 0), (1, 0), (1, 1), (0, 1), (0, 1e-9)])
    assert not w.IsClosed()
    w.CloseGaps(1e-6)
    assert w.IsClosed()
    hook = px.FromPolyline([(0, 0), (1, 0), (2, 0), (2, 2), (1, 2), (1, 1e-9)])
    with pytest.raises(ValueError, match=r"CloseGaps: 3 edge ends meet"):
        hook.CloseGaps(1e-6)
    assert ends(hook.edges[4].edge)[1] == (1, 1e-9) and ends(hook.edges[0].edge)[1] == (1, 0)


def test_polyline_and_spline_raise_when_they_cross_themselves():
    with pytest.raises(ValueError, match=r"FromPolyline: .*cross"):
        px.FromPolyline([(0, 0), (2, 2), (2, 0), (0, 2)], closed=True)
    with pytest.raises(ValueError, match=r"FromPolyline: 4 edge ends meet at \(1, 1\)"):
        px.FromPolyline([(0, 0), (1, 1), (2, 0), (2, 2), (1, 1), (0, 2)])
    with pytest.raises(ValueError, match=r"Spline: .*cross"):
        px.Spline([(0, 0), (2, 1), (1, 2), (1, -1)])


def test_closed_open_wires_closes_each_chain():
    [loop] = px.ClosedOpenWires(px.FromPolyline([(0, 0), (1, 0), (1, 1)]))
    assert loop.IsClosed() and len(loop.edges) == 3
    with pytest.raises(ValueError, match="ClosedOpenWires: the straight line from"):
        px.ClosedOpenWires(px.Line((0, 0), (1, 0)))


def test_from_dxf_cuts_the_outline_at_a_rib_ending_on_it(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    doc = ezdxf.new()
    msp = doc.modelspace()
    msp.add_lwpolyline([(0, 0), (4, 0), (4, 2), (0, 2)], close=True, dxfattribs={"layer": "outline"})
    msp.add_line((2, 0), (2, 1), dxfattribs={"layer": "rib"})
    doc.saveas(tmp_path / "rib.dxf")
    ws = px.FromDXF(tmp_path / "rib.dxf", unit=px.m)
    assert isinstance(ws, list) and len(ws) == 2
    [outline] = [w for w in ws if w.IsClosed()]
    [rib] = [w for w in ws if not w.IsClosed()]
    assert len(outline.edges) == 5 and {e.name for e in outline.edges} == {"outline"}
    assert len(rib.edges) == 1 and rib.edges[0].name == "rib"
    assert px.FromDXF(tmp_path / "rib.dxf", unit=px.m, layers=["none"]) == []


def test_project_returns_a_list_of_wires():
    occ = pytest.importorskip("netgen.occ")
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 3))
    [w] = px.Project(box)
    assert w.IsClosed() and len(w.edges) == 4


def test_selection_wire_is_one_chain_or_raises():
    f = px.QuickSketch().Rectangle(2, 1).Face()
    w = px.Selection(list(f.edges)).Wire()
    assert w.IsClosed() and len(w.edges) == 4
    with pytest.raises(ValueError, match="2 chains"):
        px.Selection([f.edges[0], f.edges[2]]).Wire()


LOOP = [(0, 0), (2, 1), (1, 2), (1, -1)]


def test_wires_split_a_spline_that_crosses_itself_into_clean_spline_wires():
    with pytest.raises(ValueError, match=r"Spline: the edge from .* crosses or touches itself.*px.Wires\(edges\)"):
        px.Spline(LOOP)
    s = _planex._Spline(LOOP)
    ws = px.Wires([s])
    assert len(ws) >= 2
    assert all(isinstance(e.edge, px.SplineEdge) for w in ws for e in w.edges)
    assert sum(w.mass for w in ws) == pytest.approx(s.mass, rel=1e-9)
    for w in ws:
        assert len(px.Wire(list(w.edges)).edges) == len(w.edges)


def test_from_dxf_splits_a_spline_that_crosses_itself(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    doc = ezdxf.new()
    doc.modelspace().add_spline(fit_points=[(x, y, 0) for x, y in LOOP])
    doc.saveas(tmp_path / "loop.dxf")
    ws = px.FromDXF(tmp_path / "loop.dxf")
    assert isinstance(ws, list) and len(ws) >= 2
    assert all(isinstance(e.edge, px.SplineEdge) for w in ws for e in w.edges)


@pytest.mark.constraints
def test_sketch_edge_of_a_pattern_id_raises():
    sk = px.Sketch((0, 0))
    sk.Line(1, 0, id="a")
    sk.LinearPattern("a", px.H, 3, 2.0, id="p")
    with pytest.raises(ValueError, match=r"Wire\('p'\): 'p' is no line, arc, circle or spline"):
        sk.Wire("p")


@pytest.mark.constraints
def test_sketch_wire_of_an_offset_on_both_sides_raises_and_its_loops_are_wires():
    sk = px.Sketch((0, 0))
    sk.Rectangle(10, 5, id="r")
    sk.Offset("r", 1, both_sides=True, id="w")
    with pytest.raises(ValueError, match=r"Wire\('w'\): the edges form 2 chains.*sk.Wires\(\) gives the chains"):
        sk.Wire("w")
    outer = sk.Wire("w.right")
    assert outer.IsClosed() and px.Face([outer]).mass == pytest.approx(12 * 7 - 4 + math.pi, rel=1e-12)
