# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings
import math

import pytest

ezdxf = pytest.importorskip("ezdxf")

import planex as px  # noqa: E402
from helpers import ends, point, polygon_area, seg, square  # noqa: E402


def faces_of(items):
    return [x for x in items if isinstance(x, px.Face)]


def wires_of(items):
    return [x for x in items if isinstance(x, px.Wire)]


def test_dxf_round_trip_keeps_lines_arcs_layers_and_colors(tmp_path):
    wp = px.Move((0, 0)).Line(2, name="bottom", col=(1, 0, 0)).Arc(1, 180, name="arc").Close(name="rest")
    p = wp.Face()
    p.WriteDXF(tmp_path / "p.dxf")
    face, back = px.FromDXF(tmp_path / "p.dxf")
    assert isinstance(face, px.Face) and face.mass == pytest.approx(p.mass, rel=1e-13)
    assert isinstance(back, px.Wire)
    assert back.IsClosed()
    assert sorted(e.name for e in back.edges) == ["arc", "bottom", "rest"]
    assert back.Face().mass == pytest.approx(p.mass)
    red = [e for e in back.edges if e.name == "bottom"][0]
    assert red.col == pytest.approx([1, 0, 0, 1])
    arc = [e for e in back.edges if e.name == "arc"][0].edge
    assert isinstance(arc, px.ArcEdge) and arc.ccw
    assert point(arc.center) == pytest.approx((2, 1))


def test_dxf_polylines_with_bulges_circles_and_layer_filter(tmp_path):
    doc = ezdxf.new()
    doc.layers.add("outline", color=1)
    doc.layers.add("holes", color=5)
    msp = doc.modelspace()
    msp.add_lwpolyline([(0, 0, 0), (4, 0, 1), (4, 2, 0), (0, 2, 0)], format="xyb", close=True,
                       dxfattribs={"layer": "outline"})
    msp.add_circle((2, 1), 0.5, dxfattribs={"layer": "holes"})
    msp.add_line((10, 10), (11, 10), dxfattribs={"layer": "other"})
    doc.saveas(tmp_path / "d.dxf")
    c = px.FromDXF(tmp_path / "d.dxf")
    assert isinstance(c, list)
    assert len(c) == 3
    outline = [w for w in c if w.edges[0].name == "outline"][0]
    kinds = sorted(type(e.edge).__name__ for e in outline.edges)
    assert kinds == ["ArcEdge", "LineEdge", "LineEdge", "LineEdge"]
    arc = [e.edge for e in outline.edges if isinstance(e.edge, px.ArcEdge)][0]
    assert point(arc.center) == pytest.approx((4, 1))
    assert arc.radius == pytest.approx(1)
    assert outline.edges[0].col == pytest.approx([1, 0, 0, 1])
    assert outline.Face().mass == pytest.approx(8 + math.pi / 2)
    holes = [w for w in c if w.edges[0].name == "holes"][0]
    assert len(holes.edges) == 1 and holes.IsClosed()
    [only] = px.FromDXF(tmp_path / "d.dxf", layers=["outline"])
    assert isinstance(only, px.Wire)
    assert px.Face(px.FromDXF(tmp_path / "d.dxf", layers=["outline"], unit=2)).mass == pytest.approx(4 * (8 + math.pi / 2))


def test_dxf_read_snaps_gaps_within_tol(tmp_path):
    doc = ezdxf.new()
    msp = doc.modelspace()
    msp.add_line((0, 0), (1, 0))
    msp.add_line((1, 0), (1, 1))
    msp.add_line((1, 1 + 4e-8), (0, 0))
    doc.saveas(tmp_path / "gap.dxf")
    [closed] = px.FromDXF(tmp_path / "gap.dxf")
    assert closed.IsClosed()
    [loose] = px.FromDXF(tmp_path / "gap.dxf", tol=1e-9)
    assert isinstance(loose, px.Wire) and not loose.IsClosed()


def test_dxf_splines_are_transferred_exactly(tmp_path):
    s = px.Spline([(0, 0), (1, 1), (2, 0), (3, 1)])
    s.name = "curve"
    s.WriteDXF(tmp_path / "s.dxf")
    doc = ezdxf.readfile(tmp_path / "s.dxf")
    spline = list(doc.modelspace())[0]
    assert spline.dxftype() == "SPLINE" and spline.dxf.degree == 5
    [back] = px.FromDXF(tmp_path / "s.dxf")
    e = back.edges[0].edge
    assert isinstance(e, px.SplineEdge)
    assert point(e.start) == pytest.approx((0, 0)) and point(e.end) == pytest.approx((3, 1))
    written, read = s.edges[0].edge.bspline, e.bspline
    assert read["degree"] == written["degree"] and read["knots"] == pytest.approx(written["knots"], abs=1e-9)
    assert [c for p in read["poles"] for c in p] == pytest.approx([c for p in written["poles"] for c in p], abs=1e-9)
    assert back.edges[0].name == "curve"


def test_dxf_bezier_chains_and_degenerate_splines(tmp_path):
    doc = ezdxf.new()
    msp = doc.modelspace()
    # two cubic Bezier pieces as one spline, interior knot of multiplicity 4, as SVG conversions write them
    poles = [(0, 0), (1, 1), (2, 1), (3, 0), (3, 0), (4, -1), (5, -1), (6, 0)]
    msp.add_open_spline(poles, 3, [0, 0, 0, 0, 0.5, 0.5, 0.5, 0.5, 1, 1, 1, 1])
    # a straight piece with coincident end poles: zero derivative at both ends
    msp.add_open_spline([(10, 0), (10, 0), (12, 0), (12, 0)], 3, [0, 0, 0, 0, 1, 1, 1, 1])
    doc.saveas(tmp_path / "bezier.dxf")
    c = px.FromDXF(tmp_path / "bezier.dxf")
    chain = [w for w in c if len(w.edges) == 2][0]
    assert all(isinstance(e.edge, px.SplineEdge) for e in chain.edges)
    assert point(chain.edges[0].edge.start) == pytest.approx((0, 0))
    assert point(chain.edges[1].edge.end) == pytest.approx((6, 0))
    assert chain.mass == pytest.approx(2 * 3.5, rel=0.1)
    straight = [w for w in c if len(w.edges) == 1][0]
    assert straight.mass == pytest.approx(2)
    assert all(math.isfinite(v) for e in straight.edges for v in (e.start.x, e.end.x, e.mass))
    with pytest.raises(ValueError, match="degenerate spline"):
        px.BSpline([(0, 0), (0, 0), (0, 0), (0, 0)], [0, 0, 0, 0, 1, 1, 1, 1], 3)


def test_dxf_written_in_a_unit_is_scaled_and_says_so(tmp_path):
    plate = px.QuickSketch().Move((0, 0)).Line((40 * px.mm, 0)).Arc(10 * px.mm, 180).Close().Face()
    plate.WriteDXF(tmp_path / "mm.dxf", unit=px.mm)
    doc = ezdxf.readfile(tmp_path / "mm.dxf")
    assert doc.header["$INSUNITS"] == 4
    lines = [e for e in doc.modelspace() if e.dxftype() == "LINE"]
    assert max(max(abs(e.dxf.start.x), abs(e.dxf.end.x)) for e in lines) == pytest.approx(40)
    arc = [e for e in doc.modelspace() if e.dxftype() == "ARC"][0]
    assert arc.dxf.radius == pytest.approx(10)
    plate.WriteDXF(tmp_path / "m.dxf")
    assert ezdxf.readfile(tmp_path / "m.dxf").header["$INSUNITS"] == 6
    plate.WriteDXF(tmp_path / "in.dxf", unit=px.inch)
    assert ezdxf.readfile(tmp_path / "in.dxf").header["$INSUNITS"] == 1
    with pytest.raises(TypeError):
        plate.WriteDXF(tmp_path / "old.dxf", units=ezdxf.units.MM)
    with pytest.raises(ValueError, match="a unit is a positive length"):
        plate.WriteDXF(tmp_path / "x.dxf", unit=0)


def test_read_takes_the_unit_the_file_names(tmp_path):
    plate = px.QuickSketch().Move((0, 0)).Line((40 * px.mm, 0)).Arc(10 * px.mm, 180).Close().Face()
    for unit in (px.mm, px.inch, px.m, px.mil):
        plate.WriteDXF(tmp_path / "p.dxf", unit=unit)
        [face] = faces_of(px.FromDXF(tmp_path / "p.dxf"))
        assert face.mass == pytest.approx(plate.mass, rel=1e-12)
        assert px.Face(wires_of(px.FromDXF(tmp_path / "p.dxf"))).mass == pytest.approx(plate.mass, rel=1e-12)
    plate.WriteDXF(tmp_path / "mm.dxf", unit=px.mm)
    assert px.Face(wires_of(px.FromDXF(tmp_path / "mm.dxf", unit=px.m))).mass == pytest.approx(plate.mass * 1e6, rel=1e-12)


def test_read_of_a_file_without_a_unit_warns_and_takes_meters(tmp_path):
    doc = ezdxf.new()
    doc.units = 0
    doc.modelspace().add_circle((0, 0), 2)
    doc.saveas(tmp_path / "plain.dxf")
    with pytest.warns(px.PlanexWarning, match="names no length unit"):
        assert px.FromDXF(tmp_path / "plain.dxf")[0].mass == pytest.approx(4 * math.pi)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert px.FromDXF(tmp_path / "plain.dxf", unit=px.cm)[0].mass == pytest.approx(0.04 * math.pi)
    with pytest.raises(ValueError, match="positive length"):
        px.FromDXF(tmp_path / "plain.dxf", unit=-1)


def test_a_mirrored_slot_from_cad_keeps_its_place_and_turning_sense(tmp_path):
    doc = ezdxf.new()
    msp = doc.modelspace()
    mirrored = {"extrusion": (0, 0, -1)}
    msp.add_arc((-1, 0), 1, -90, 90, dxfattribs=mirrored)
    msp.add_arc((-3, 0), 1, 90, 270, dxfattribs=mirrored)
    msp.add_line((1, 1), (3, 1))
    msp.add_line((1, -1), (3, -1))
    doc.saveas(tmp_path / "slot.dxf")
    face = px.Face(px.FromDXF(tmp_path / "slot.dxf"))
    assert face.mass == pytest.approx(4 + math.pi, rel=1e-13)
    assert face.bounding_box[0][0] == pytest.approx(0) and face.bounding_box[1][0] == pytest.approx(4)


def test_a_tilted_circle_is_skipped_with_a_warning(tmp_path):
    doc = ezdxf.new()
    doc.modelspace().add_circle((0, 0), 1, dxfattribs={"extrusion": (0, 1, 1)})
    doc.modelspace().add_line((0, 0), (1, 0))
    doc.saveas(tmp_path / "t.dxf")
    with pytest.warns(px.PlanexWarning, match="tilted"):
        [w] = px.FromDXF(tmp_path / "t.dxf")
    assert len(w.edges) == 1


def test_names_that_are_no_layer_names_are_written_with_underscores(tmp_path):
    sq = px.QuickSketch().Rectangle(1, 1, name="in/out").Face()
    with pytest.warns(px.PlanexWarning, match="'in/out' -> 'in_out'"):
        px.WriteDXF(sq, tmp_path / "n.dxf")
    assert {e.name for w in wires_of(px.FromDXF(tmp_path / "n.dxf")) for e in w.edges} == {"in_out"}


def entities_in(tmp_path, name, add, order=1):
    """a DXF file of the entities add(msp) draws, in their order (order=1) or reversed (order=-1)"""
    doc = ezdxf.new()
    add(doc.modelspace())
    if order == -1:
        msp = doc.modelspace()
        ents = list(msp)
        for e in ents:
            msp.unlink_entity(e)
        for e in reversed(ents):
            msp.add_entity(e)
    doc.saveas(tmp_path / name)
    return tmp_path / name


def drawn(wires):
    """the wires as sets of their closedness, edge count and box, rounded: what a reading gives, whatever its order"""
    return sorted((w.IsClosed(), len(w.edges), tuple(round(c, 9) for p in w.bounding_box for c in p)) for w in wires)


def test_a_bore_with_its_centre_lines_reads_as_three_wires_in_any_order(tmp_path):
    def bore(msp):
        msp.add_circle((0, 0), 10)
        msp.add_line((-14, 0), (14, 0), dxfattribs={"layer": "center"})
        msp.add_line((0, -14), (0, 14), dxfattribs={"layer": "center"})

    for order in (1, -1):
        ws = px.FromDXF(entities_in(tmp_path, "bore.dxf", bore, order), unit=px.m)
        assert drawn(ws) == [(False, 1, (-14, 0, 14, 0)), (False, 1, (0, -14, 0, 14)), (True, 1, (-10, -10, 10, 10))]
        [circle] = [w for w in ws if w.IsClosed()]
        assert circle.Face().mass == pytest.approx(math.pi * 100, rel=1e-14)


def test_a_closed_polyline_stays_whole_where_a_line_crosses_it_or_a_rib_ends_on_it(tmp_path):
    def plate(msp):
        msp.add_lwpolyline([(0, 0), (100, 0), (100, 60), (0, 60)], close=True)
        msp.add_line((-5, 30), (105, 30))
        msp.add_line((50, 0), (50, 40))

    ws = px.FromDXF(entities_in(tmp_path, "plate.dxf", plate), unit=px.m)
    assert drawn(ws) == [(False, 1, (-5, 30, 105, 30)), (False, 1, (50, 0, 50, 40)), (True, 4, (0, 0, 100, 60))]
    assert px.Face([w for w in ws if w.IsClosed()]).mass == pytest.approx(6000, rel=1e-14)


def test_loose_lines_join_where_two_ends_meet_and_end_where_three_meet_in_any_order(tmp_path):
    def square_with_a_dimension(msp):
        for a, b in [((10, -60), (60, -60)), ((60, -60), (60, -10)), ((60, -10), (10, -10)), ((10, -10), (10, -60)),
                     ((60, -60), (72, -60)), ((60, -10), (72, -10)), ((68, -60), (68, -10))]:
            msp.add_line(a, b)
        msp.add_lwpolyline([(68, -10), (67.3, -15), (68.7, -15)], close=True)

    readings = [drawn(px.FromDXF(entities_in(tmp_path, "sq.dxf", square_with_a_dimension, order), unit=px.m))
                for order in (1, -1)]
    assert readings[0] == readings[1]
    assert readings[0] == [(False, 1, (60, -60, 60, -10)), (False, 1, (60, -60, 72, -60)), (False, 1, (60, -10, 72, -10)),
                           (False, 1, (68, -60, 68, -10)), (False, 3, (10, -60, 60, -10)),
                           (True, 3, (67.3, -15, 68.7, -10))]


def test_outlines_of_two_materials_that_touch_stay_apart(tmp_path):
    def heat_sink(msp):
        for a, b in [((0, 0), (30, 0)), ((30, 0), (30, 4)), ((30, 4), (0, 4)), ((0, 4), (0, 0))]:
            msp.add_line(a, b, dxfattribs={"layer": "aluminium"})
        for a, b in [((10, 4), (20, 4)), ((20, 4), (20, 6)), ((20, 6), (10, 6)), ((10, 6), (10, 4))]:
            msp.add_line(a, b, dxfattribs={"layer": "copper"})

    ws = px.FromDXF(entities_in(tmp_path, "sink.dxf", heat_sink), unit=px.m)
    assert all(w.IsClosed() for w in ws) and len(ws) == 2
    glued = px.Glue([px.Face([w]) for w in ws])
    assert sorted(f.mass for f in glued.faces) == pytest.approx([20, 120], rel=1e-14)
    assert {w.edges[0].name for w in ws} == {"aluminium", "copper"}


def test_write_dxf_reads_back_as_its_faces_and_its_edges(tmp_path):
    plate = px.QuickSketch().Rectangle(40, 20).Circle((10, 10), 4).Face()
    plate.name, plate.col = "steel", (0.2, 0.4, 0.6)
    plate.WriteDXF(tmp_path / "p.dxf", unit=px.m)
    items = px.FromDXF(tmp_path / "p.dxf", unit=px.m)
    [face] = faces_of(items)
    assert isinstance(items[0], px.Face) and face.mass == pytest.approx(plate.mass, rel=1e-13)
    assert len(face.inner) == 1 and face.name == "steel" and face.col == pytest.approx([0.2, 0.4, 0.6, 1], abs=3e-3)
    assert sorted(len(w.edges) for w in wires_of(items)) == [1, 4] and all(w.IsClosed() for w in wires_of(items))
    assert px.Face(wires_of(items)).mass == pytest.approx(plate.mass, rel=1e-13)


@pytest.mark.parametrize("style, area", [(0, 100 - 36 + 4), (1, 100 - 36), (2, 100)])
def test_a_hatch_fills_by_its_island_style(tmp_path, style, area):
    from ezdxf.lldxf import const

    doc = ezdxf.new()
    hatch = doc.modelspace().add_hatch(dxfattribs={"layer": "fill", "hatch_style": style})
    hatch.paths.add_polyline_path([(0, 0), (10, 0), (10, 10), (0, 10)], flags=const.BOUNDARY_PATH_EXTERNAL)
    hole = hatch.paths.add_edge_path(flags=const.BOUNDARY_PATH_OUTERMOST)
    for a, b in [((2, 2), (2, 8)), ((2, 8), (8, 8)), ((8, 8), (8, 2)), ((8, 2), (2, 2))]:
        hole.add_line(a, b)
    hatch.paths.add_polyline_path([(4, 4), (6, 4), (6, 6), (4, 6)], flags=const.BOUNDARY_PATH_DEFAULT)
    doc.saveas(tmp_path / "h.dxf")
    faces = px.FromDXF(tmp_path / "h.dxf", unit=px.m)
    assert all(isinstance(f, px.Face) and f.name == "fill" for f in faces)
    assert sum(f.mass for f in faces) == pytest.approx(area, rel=1e-14)


def test_hatch_boundaries_of_arcs_ellipses_splines_and_bulges_are_exact(tmp_path):
    doc = ezdxf.new()
    msp = doc.modelspace()
    disc = msp.add_hatch()
    disc.paths.add_edge_path().add_arc((0, 0), 2, 0, 360)
    slot = msp.add_hatch()
    slot.paths.add_polyline_path([(10, 0, 1), (12, 0, 0), (12, 2, 1), (10, 2, 0)])
    oval = msp.add_hatch()
    oval.paths.add_edge_path().add_ellipse((20, 0), major_axis=(3, 0), ratio=0.5, start_angle=0, end_angle=360)
    lens = msp.add_hatch()
    path = lens.paths.add_edge_path()
    path.add_line((30, 0), (34, 0))
    path.add_spline(control_points=[(34, 0), (33, 2), (31, 2), (30, 0)], knot_values=[0, 0, 0, 0, 1, 1, 1, 1], degree=3)
    doc.saveas(tmp_path / "arcs.dxf")
    disc_f, slot_f, oval_f, lens_f = px.FromDXF(tmp_path / "arcs.dxf", unit=px.m)
    assert disc_f.mass == pytest.approx(4 * math.pi, rel=1e-13)
    assert slot_f.mass == pytest.approx(4 + math.pi, rel=1e-13)
    assert oval_f.mass == pytest.approx(math.pi * 3 * 1.5, rel=1e-13)
    # the area between the Bezier with poles (4, 0), (3, 2), (1, 2), (0, 0) and its chord
    assert lens_f.mass == pytest.approx(21 / 5, rel=1e-13)


def test_a_solid_and_a_wide_polyline_are_faces(tmp_path):
    doc = ezdxf.new()
    msp = doc.modelspace()
    msp.add_solid([(0, 0), (1, 0), (0, 1), (1, 1)])
    msp.add_solid([(5, 0), (7, 0), (5, 3)])
    msp.add_lwpolyline([(10, 0), (20, 0), (20, 5)], dxfattribs={"const_width": 1})
    msp.add_lwpolyline([(30, 0, 1, 0), (40, 0, 0, 0)], format="xyse")
    doc.saveas(tmp_path / "s.dxf")
    with pytest.warns(px.PlanexWarning, match="whose width changes along it is read as its line"):
        square, triangle, band, line = px.FromDXF(tmp_path / "s.dxf", unit=px.m)
    assert square.mass == pytest.approx(1, rel=1e-14) and triangle.mass == pytest.approx(3, rel=1e-14)
    assert isinstance(band, px.Face) and band.mass == pytest.approx(15, rel=1e-13)
    assert isinstance(line, px.Wire) and line.mass == pytest.approx(10, rel=1e-14)
