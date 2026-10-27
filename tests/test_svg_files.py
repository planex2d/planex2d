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
    back = px.FromSVG(tmp_path / "a.svg")
    assert area(back) == pytest.approx(sum(f.mass for f in shape.faces), rel=1e-12)
    kinds = {type(e.edge).__name__ for w in back for e in w.edges}
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
        '<circle cx="20" cy="20" r="5" stroke="#0000ff"/></g>'
        '<g inkscape:groupmode="layer" inkscape:label="outline">'
        '<path d="M 0 50 L 80 50 A 20 20 0 0 0 100 30 L 100 0 L 0 0 Z"/></g></svg>')
    wires = px.FromSVG(tmp_path / "b.svg")
    holes = [w for w in wires if w.name == "holes"]
    assert sum(len(w.edges) for w in holes) >= 1 and holes[0].col[:3] == pytest.approx((0, 0, 1))
    assert area([w for w in wires if w.name == "holes"]) == pytest.approx(math.pi * 0.005 ** 2, rel=1e-12)
    outline = px.FromSVG(tmp_path / "b.svg", layers=["outline"])
    assert all(w.name == "outline" for w in outline)
    assert area(outline) == pytest.approx(0.1 * 0.05 - (1 - math.pi / 4) * 0.02 ** 2, rel=1e-12)


def test_an_svg_without_a_length_unit_warns(tmp_path):
    pytest.importorskip("svgelements")
    (tmp_path / "c.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">'
                                    '<path d="M 0 0 L 9.6 0"/></svg>')
    with pytest.warns(px.PlanexWarning, match="names no length unit"):
        wires = px.FromSVG(tmp_path / "c.svg")
    assert wires[0].edges[0].mass == pytest.approx(9.6 * 0.0254 / 96, rel=1e-12)
    assert px.FromSVG(tmp_path / "c.svg", unit=px.mm)[0].edges[0].mass == pytest.approx(0.0096, rel=1e-12)


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
    assert area(px.FromDXF(tmp_path / "a.dxf")) == pytest.approx(sum(f.mass for f in shape.faces), rel=1e-12)


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
    f = px.Face(px.FromSVG(tmp_path / "c.svg"))
    assert f.mass / c.mass - 1 == pytest.approx(0, abs=1e-12)
    centers = [(e.edge.center.x, e.edge.center.y) for e in f.edges]
    for x, y in centers:
        assert (x, y) == pytest.approx(centers[0], abs=1e-15)


def test_an_arc_near_a_half_turn_reads_back_exactly(tmp_path):
    pytest.importorskip("svgelements")
    w = px.Move((0, 0)).Line(0.02, 0).Arc(0.011, 179).Wire().Scaled((0, 0), 1.0000000000000002)
    px.WriteSVG(w, tmp_path / "a.svg", unit=px.mm)
    back = px.FromSVG(tmp_path / "a.svg")
    assert sum(b.mass for b in back) == pytest.approx(w.mass, rel=1e-12)
