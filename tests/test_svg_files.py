# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""SVG files at true scale (px.WriteSVG, px.FromSVG) and the filled faces of px.WriteDXF."""
import math
import re

import pytest

import planex as px


def plate():
    outer = px.Move((0, 0)).Line(40, 0).Line(0, 20).Arc(10, 90).Line(-30, 0).Close().Face()
    face = outer - px.Face([px.Circle((12, 10), 4)])
    face = px.Glue([face, px.Face([px.Ellipse((60, 10), 8, 4, angle=30)])])
    return face


def area(wires):
    return px.Shape(wires).mass


def faces_of(items):
    return [x for x in items if isinstance(x, px.Face)]


def wires_of(items):
    return [x for x in items if isinstance(x, px.Wire)]


def test_write_svg_is_true_scale_with_filled_faces_and_named_groups(tmp_path):
    w = px.FromPolyline([(0, 0), (0.04, 0), (0.04, 0.02)], name="rim")
    w.col = (1, 0, 0)
    face = px.Face([px.FromPolyline([(0, 0), (0.04, 0), (0.04, 0.02), (0, 0.02)], closed=True)])
    px.WriteSVG([face, w], tmp_path / "a.svg", unit=px.mm)
    text = (tmp_path / "a.svg").read_text()
    assert 'width="40mm" height="20mm" viewBox="0 -20 40 20"' in text
    assert 'fill="#dde6f0" fill-rule="evenodd"' in text
    assert 'inkscape:label="rim"' in text and 'stroke="#ff0000"' in text


def test_outlines_only_through_the_wires(tmp_path):
    face = px.Face([px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True)])
    px.WriteSVG(face.wires, tmp_path / "a.svg")
    assert "fill-rule" not in (tmp_path / "a.svg").read_text()


def test_lines_arcs_circles_beziers_and_ellipses_come_back_exactly(tmp_path):
    pytest.importorskip("svgelements")
    shape = plate()
    shape.WriteSVG(tmp_path / "a.svg", unit=px.mm)
    back = px.FromSVG(tmp_path / "a.svg", painted=False)
    assert sum(f.mass for f in faces_of(back)) == pytest.approx(sum(f.mass for f in shape.faces), rel=1e-12)
    kinds = {type(e.edge).__name__ for w in wires_of(back) for e in w.edges}
    assert {"LineEdge", "ArcEdge", "SplineEdge"} <= kinds


def test_a_spline_without_an_svg_form_goes_out_approximated_with_a_warning(tmp_path):
    quartic = px._planex._BSpline([(0, 0), (1, 2), (2, -1), (3, 2), (4, 0)], [0] * 5 + [1] * 5, 4, None)
    with pytest.warns(px.PlanexWarning, match="1 curve"):
        px.WriteSVG(quartic, tmp_path / "a.svg")
    assert " A " in (tmp_path / "a.svg").read_text()


def test_read_svg_applies_transforms_units_layers_and_colours(tmp_path):
    pytest.importorskip("svgelements")
    (tmp_path / "b.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'width="100mm" height="50mm" viewBox="0 0 100 50">'
        '<g inkscape:groupmode="layer" inkscape:label="holes" transform="translate(10 5)">'
        '<circle cx="20" cy="20" r="5" fill="none" stroke="#0000ff"/></g>'
        '<g inkscape:groupmode="layer" inkscape:label="outline">'
        '<path d="M 0 50 L 80 50 A 20 20 0 0 0 100 30 L 100 0 L 0 0 Z"/></g></svg>')
    items = px.FromSVG(tmp_path / "b.svg", painted=False)
    holes = [w for w in wires_of(items) if w.name == "holes"]
    assert len(holes) == 1 and holes[0].col[:3] == pytest.approx((0, 0, 1))
    assert area(holes) == pytest.approx(math.pi * 0.005 ** 2, rel=1e-12)
    outline = px.FromSVG(tmp_path / "b.svg", layers=["outline"])
    assert len(outline) == 1 and outline[0].name == "outline" and outline[0].col[:3] == pytest.approx((0, 0, 0))
    assert outline[0].mass == pytest.approx(0.1 * 0.05 - (1 - math.pi / 4) * 0.02 ** 2, rel=1e-12)


def test_an_svg_without_a_length_unit_warns(tmp_path):
    pytest.importorskip("svgelements")
    (tmp_path / "c.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">'
                                    '<path d="M 0 0 L 9.6 0" fill="none" stroke="black"/></svg>')
    with pytest.warns(px.PlanexWarning, match="names no length unit"):
        wires = px.FromSVG(tmp_path / "c.svg", painted=False)
    assert wires[0].edges[0].mass == pytest.approx(9.6 * 0.0254 / 96, rel=1e-12)
    assert px.FromSVG(tmp_path / "c.svg", unit=px.mm, painted=False)[0].edges[0].mass == pytest.approx(0.0096, rel=1e-12)


def test_write_dxf_fills_each_face_with_a_hatch_of_its_exact_loops(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    from ezdxf import path as dxfpath
    shape = plate()
    shape.WriteDXF(tmp_path / "a.dxf", unit=px.mm)
    hatches = ezdxf.readfile(str(tmp_path / "a.dxf")).modelspace().query("HATCH")
    assert len(hatches) == len(shape.faces)

    def filled(h):
        total = 0.0
        for p in dxfpath.from_hatch(h):
            pts = [(v.x, v.y) for v in p.flattening(distance=1e-6)]
            total += abs(sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))) / 2
        return total

    outer_and_holes = sum(filled(h) for h in hatches)
    holes = 2 * math.pi * 4 ** 2
    # ezdxf flattens arcs and splines through Bezier approximations, to about 3e-4
    assert (outer_and_holes - holes * 1e6) == pytest.approx(sum(f.mass for f in shape.faces) * 1e6, rel=1e-3)
    back = px.FromDXF(tmp_path / "a.dxf")
    assert sum(f.mass for f in faces_of(back)) == pytest.approx(sum(f.mass for f in shape.faces), rel=1e-12)
    assert area(wires_of(back)) == pytest.approx(sum(f.mass for f in shape.faces), rel=1e-12)


def test_write_dxf_takes_a_list_and_wires_are_not_filled(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    face = px.Face([px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True)])
    px.WriteDXF(face.wires, tmp_path / "a.dxf")
    assert len(ezdxf.readfile(str(tmp_path / "a.dxf")).modelspace().query("HATCH")) == 0


@pytest.mark.parametrize("r", [11, 2.25])
def test_a_circle_reads_back_exactly_whatever_its_radius_s_last_digit(tmp_path, r):
    pytest.importorskip("svgelements")
    c = px.Circle((38, 0), r).Face().Scaled((0, 0), px.mm)
    px.WriteSVG(c.wires, tmp_path / "c.svg", unit=px.mm)
    f = px.Face(px.FromSVG(tmp_path / "c.svg", painted=False))
    assert f.mass / c.mass - 1 == pytest.approx(0, abs=1e-12)
    centers = [(e.edge.center.x, e.edge.center.y) for e in f.edges]
    for x, y in centers:
        assert (x, y) == pytest.approx(centers[0], abs=1e-15)


def test_an_arc_near_a_half_turn_reads_back_exactly(tmp_path):
    pytest.importorskip("svgelements")
    w = px.Move((0, 0)).Line(0.02, 0).Arc(0.011, 179).Wire().Scaled((0, 0), 1.0000000000000002)
    px.WriteSVG(w, tmp_path / "a.svg", unit=px.mm)
    back = px.FromSVG(tmp_path / "a.svg", painted=False)
    assert sum(b.mass for b in back) == pytest.approx(w.mass, rel=1e-12)


def test_a_cubic_bspline_goes_out_as_bezier_pieces_and_comes_back_exactly(tmp_path):
    pytest.importorskip("svgelements")
    lens = px.Face(px.ClosedOpenWires(px.BSpline([(10, 10), (20, 30), (40, 30), (60, 20), (70, 10)])))
    holed = px.Move((0, 0)).Rectangle(80, 40).Face() - lens
    for obj in (lens, holed):
        obj.WriteSVG(tmp_path / "a.svg", unit=px.mm)
        assert (tmp_path / "a.svg").read_text().count(" C ") == 4
        assert sum(f.mass for f in faces_of(px.FromSVG(tmp_path / "a.svg", painted=False))) == pytest.approx(obj.mass, rel=1e-12)


def test_nothing_to_draw_is_refused(tmp_path):
    with pytest.raises(ValueError, match="WriteSVG: there is nothing to draw"):
        px.WriteSVG([], tmp_path / "a.svg")


MM2 = 1e-6


def svg(tmp_path, body, name="a"):
    path = tmp_path / f"{name}.svg"
    path.write_text('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="100mm" '
                    'height="100mm" viewBox="0 0 100 100">' + body + "</svg>")
    return path


@pytest.mark.parametrize("rule, count, mm2", [("nonzero", 1, 700), ("evenodd", 2, 600)])
def test_overlapping_subpaths_fill_by_the_fill_rule(tmp_path, rule, count, mm2):
    pytest.importorskip("svgelements")
    d = "M 10 10 L 30 10 L 30 30 L 10 30 Z M 20 20 L 40 20 L 40 40 L 20 40 Z"
    read = px.FromSVG(svg(tmp_path, f'<path d="{d}" fill-rule="{rule}"/>'))
    assert len(read) == count and sum(f.mass for f in read) / MM2 == pytest.approx(mm2, rel=1e-12)


def test_a_pentagram_is_filled_whole_by_nonzero_and_without_its_middle_by_evenodd(tmp_path):
    pytest.importorskip("svgelements")
    tips = [(50 + 40 * math.cos(math.radians(-90 + 144 * k)), 50 + 40 * math.sin(math.radians(-90 + 144 * k))) for k in range(5)]
    d = "M " + " L ".join(f"{x!r} {y!r}" for x, y in tips) + " Z"
    whole = px.FromSVG(svg(tmp_path, f'<path d="{d}"/>', "a"))
    rim = px.FromSVG(svg(tmp_path, f'<path d="{d}" fill-rule="evenodd"/>', "b"))
    r = 40 * math.cos(math.radians(72)) / math.cos(math.radians(36))
    assert len(whole) == 1 and len(rim) == 5
    assert (whole[0].mass - sum(f.mass for f in rim)) / MM2 == pytest.approx(2.5 * r * r * math.sin(math.radians(72)), rel=1e-9)


@pytest.mark.parametrize("inner, rule, mm2, holes", [("M 20 20 L 20 40 L 40 40 L 40 20 Z", "nonzero", 1200, 1),
                                                     ("M 20 20 L 40 20 L 40 40 L 20 40 Z", "nonzero", 1600, 0),
                                                     ("M 20 20 L 40 20 L 40 40 L 20 40 Z", "evenodd", 1200, 1)])
def test_an_inner_subpath_is_a_hole_by_its_direction_or_by_parity(tmp_path, inner, rule, mm2, holes):
    pytest.importorskip("svgelements")
    read = px.FromSVG(svg(tmp_path, f'<path d="M 10 10 L 50 10 L 50 50 L 10 50 Z {inner}" fill-rule="{rule}"/>'))
    assert len(read) == 1 and len(read[0].inner) == holes and read[0].mass / MM2 == pytest.approx(mm2, rel=1e-12)


def test_an_open_subpath_is_filled_as_closed_by_a_straight_line_and_a_bare_line_paints_nothing(tmp_path):
    pytest.importorskip("svgelements")
    assert px.FromSVG(svg(tmp_path, '<path d="M 0 0 L 20 0 L 20 20"/>'))[0].mass / MM2 == pytest.approx(200, rel=1e-12)
    assert px.FromSVG(svg(tmp_path, '<path d="M 0 0 L 20 0"/>', "b")) == []


@pytest.mark.parametrize("cap, join, mm2", [("butt", "miter", 140), ("round", "round", 140 + math.pi - (1 - math.pi / 4)),
                                            ("square", "bevel", 143.5)])
def test_a_stroke_is_the_band_of_its_width_caps_and_joins(tmp_path, cap, join, mm2):
    pytest.importorskip("svgelements")
    read = px.FromSVG(svg(tmp_path, f'<path d="M 10 10 L 50 10 L 50 40" fill="none" stroke="black" stroke-width="2" '
                                    f'stroke-linecap="{cap}" stroke-linejoin="{join}"/>'))
    assert len(read) == 1 and read[0].mass / MM2 == pytest.approx(mm2, rel=1e-9)


def test_later_paint_covers_earlier_paint(tmp_path):
    pytest.importorskip("svgelements")
    body = ('<rect x="10" y="10" width="40" height="40" fill="black"/><circle cx="50" cy="50" r="10" fill="red"/>'
            '<rect x="45" y="45" width="2" height="2" fill="black"/>')
    read = px.FromSVG(svg(tmp_path, body))
    black = [f for f in read if f.col[:3] == pytest.approx((0, 0, 0))]
    red = [f for f in read if f.col[:3] == pytest.approx((1, 0, 0))]
    assert len(black) == 2 and len(red) == 1 and len(red[0].inner) == 1
    assert sum(f.mass for f in black) / MM2 == pytest.approx(1600 - 25 * math.pi + 4, rel=1e-9)
    assert red[0].mass / MM2 == pytest.approx(100 * math.pi - 4, rel=1e-9)


def test_a_white_page_stays_a_face_with_a_hole_where_the_drawing_is(tmp_path):
    pytest.importorskip("svgelements")
    body = '<rect x="0" y="0" width="100" height="100" fill="#ffffff"/><rect x="10" y="10" width="20" height="20"/>'
    page, square = px.FromSVG(svg(tmp_path, body))
    assert page.col[:3] == pytest.approx((1, 1, 1)) and len(page.inner) == 1
    assert page.mass / MM2 == pytest.approx(9600, rel=1e-12) and square.mass / MM2 == pytest.approx(400, rel=1e-12)


def test_paths_as_drawn_keep_each_path_whole_and_join_nothing_across_paths(tmp_path):
    pytest.importorskip("svgelements")
    sides = (("10 60", "60 60"), ("60 60", "60 10"), ("60 10", "10 10"), ("10 10", "10 60"), ("60 60", "72 60"),
             ("60 10", "72 10"), ("68 60", "68 10"))
    lines = "".join(f'<path d="M {a} L {b}" fill="none" stroke="black"/>' for a, b in sides)
    arrow = '<path d="M 68 10 L 67.3 15 L 68.7 15 Z"/>'
    triangle = '<path d="M 80 80 L 90 80 L 90 90 Z" fill="none" stroke="black"/>'
    read = px.FromSVG(svg(tmp_path, arrow + lines + triangle), painted=False)
    assert isinstance(read[0], px.Face) and read[0].mass / MM2 == pytest.approx(3.5, rel=1e-12)
    wires = wires_of(read)
    assert [len(w.edges) for w in wires] == [1] * 7 + [3]
    assert [w.IsClosed() for w in wires] == [False] * 7 + [True]


def test_a_glyph_placed_with_use_is_a_face_with_its_hole(tmp_path):
    pytest.importorskip("svgelements")
    body = ('<defs><path id="o" d="M 0 0 L 10 0 L 10 10 L 0 10 Z M 3 3 L 3 7 L 7 7 L 7 3 Z"/></defs>'
            '<use xlink:href="#o" transform="translate(20 20) rotate(90)"/>')
    read = px.FromSVG(svg(tmp_path, body))
    assert len(read) == 1 and len(read[0].inner) == 1 and read[0].mass / MM2 == pytest.approx(84, rel=1e-12)


def test_text_without_outlines_is_left_out_with_a_warning(tmp_path):
    pytest.importorskip("svgelements")
    with pytest.warns(px.PlanexWarning, match="1 text element"):
        read = px.FromSVG(svg(tmp_path, '<text x="10" y="10">Hi</text><rect x="0" y="0" width="10" height="10"/>'))
    assert len(read) == 1


def test_a_gradient_fill_is_a_face_without_a_colour_and_hidden_paint_is_left_out(tmp_path):
    pytest.importorskip("svgelements")
    body = ('<defs><linearGradient id="g"><stop offset="0" stop-color="red"/></linearGradient></defs>'
            '<rect x="0" y="0" width="10" height="10" fill="url(#g)"/>'
            '<rect x="20" y="0" width="10" height="10" visibility="hidden"/>'
            '<rect x="40" y="0" width="10" height="10" fill-opacity="0"/>')
    read = px.FromSVG(svg(tmp_path, body))
    assert len(read) == 1 and read[0].col is None and read[0].mass / MM2 == pytest.approx(100, rel=1e-12)


def test_write_svg_reads_back_exactly_as_paths_and_with_its_drawn_edges_as_painted(tmp_path):
    pytest.importorskip("svgelements")
    px.Move((0, 0)).Rectangle(0.04, 0.02).Face().WriteSVG(tmp_path / "a.svg")
    assert faces_of(px.FromSVG(tmp_path / "a.svg", painted=False))[0].mass == pytest.approx(0.04 * 0.02, rel=1e-12)
    painted = px.FromSVG(tmp_path / "a.svg")
    inside = [f for f in painted if f.col[:3] != pytest.approx((0, 0, 0))]
    assert len(inside) == 1 and inside[0].mass == pytest.approx(0.0399 * 0.0199, rel=1e-9)
    assert sum(f.mass for f in painted) == pytest.approx(0.04 * 0.02 + 2 * (0.04 + 0.02) * 0.05e-3, rel=1e-9)
