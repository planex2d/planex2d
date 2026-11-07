# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import datetime
import math
import re

import numpy as np
import pytest

import planex as px
from planex import _dimensions as dims
from planex._drawing import _Lettering

pytest.importorskip("matplotlib")


def plate(scale=1.0, w=60):
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(w * scale, 0, px.Horizontal, id="bottom").Line(0, 40 * scale, px.Vertical, id="right") \
        .Line(-w * scale, 0, px.Horizontal, id="top").Close(px.Vertical, id="left")
    sk.Constrain("bottom", px.Distance, w * scale, id="w")
    sk.Constrain("right", px.Distance, 40 * scale, id="ht")
    sk.Circle((20 * scale, 20 * scale), 5 * scale, id="hole")
    sk.Constrain("hole", px.Diameter, 10 * scale, id="d")
    sk.Constrain("hole.center", "left", px.Distance, 20 * scale, id="hx")
    sk.Constrain("hole.center", px.H, px.Distance, 20 * scale, id="hy")
    return sk


def part():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(80, 0, px.Horizontal, id="base").Line(0, 20, px.Vertical, id="side") \
        .Line(-30, 25, id="slope").Line(-50, 0, px.Horizontal, id="top").Close(px.Vertical, id="back")
    sk.Constrain("base", px.Distance, 80, id="L")
    sk.Constrain("side", px.Distance, 20, id="h1")
    sk.Constrain("back", px.Distance, 45, id="Ht")
    sk.Constrain("top", px.Distance, 50, id="th")
    sk.Constrain("slope", "base", px.Angle, id="a", reference=True)
    sk.Circle((12, 12), 3, id="hole")
    sk.Constrain("hole", px.Diameter, 6, id="d")
    sk.Constrain("hole.center", "back", px.Distance, 12, id="x0")
    sk.Constrain("hole.center", "base", px.Distance, 12, id="y0")
    sk.LinearPattern("hole", px.H, 3, 14, id="row")
    return sk


def strokes(dw, mm=1.0):
    return {s.id: s for s in dw._strokes(mm, _Lettering())}


def texts(s):
    return [t for _, _, t in s.texts]


def dimension_line(s):
    """the dimension line of a linear dimension: its line perpendicular to the extension lines"""
    lines = [(a, b) for a, b, style in s.lines if style == "thin"]
    units = [(b - a) / np.linalg.norm(b - a) for a, b in lines]
    for k, (a, b) in enumerate(lines):
        if all(abs(units[k] @ v) < 1e-9 for j, v in enumerate(units) if j != k):
            return a, b
    raise AssertionError("no dimension line")


def test_what_the_dimensions_measure():
    found = {d.id: d for d in dims.Dimensions(plate())}
    assert sorted(found) == ["d", "ht", "hx", "hy", "w"]
    assert found["w"].kind == "linear" and found["w"].span == pytest.approx(60)
    assert found["d"].kind == "diameter" and found["d"].r == pytest.approx(5)
    assert np.allclose(found["d"].c, (20, 20))
    assert np.allclose(sorted(map(tuple, (found["hx"].p, found["hx"].q))), [(0, 20), (20, 20)])
    assert np.allclose(sorted(map(tuple, (found["hy"].p, found["hy"].q))), [(20, 0), (20, 20)])


def test_rows_fill_from_the_part_outward_shorter_first():
    s = strokes(px.Drawing(plate(), unit=px.mm))
    (a, b), (c, d) = dimension_line(s["hx"]), dimension_line(s["w"])
    assert a[1] == pytest.approx(-dims.FIRST) and b[1] == pytest.approx(-dims.FIRST)
    assert c[1] == pytest.approx(-dims.FIRST - dims.ROW)
    (e, _), (f, _) = dimension_line(s["hy"]), dimension_line(s["ht"])
    assert e[0] == pytest.approx(-dims.FIRST) and f[0] == pytest.approx(60 + dims.FIRST)
    assert texts(s["w"]) == ["60"] and texts(s["d"]) == ["⌀10"]


def test_a_placed_offset_goes_outward_from_the_part():
    # w below the part, ht right of it: both outward; hx from the left side to the hole at the middle's height takes
    # the drawing's axes
    dw = px.Drawing(plate(), unit=px.mm).Place("w", 15).Place("ht", 12).Place("hx", 25)
    s = strokes(dw)
    assert dimension_line(s["w"])[0][1] == pytest.approx(-15)
    assert dimension_line(s["ht"])[0][0] == pytest.approx(72)
    assert dimension_line(s["hx"])[0][1] == pytest.approx(45)


def test_an_inclined_dimension_is_offset_to_the_side_facing_x():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(20, 10, id="slant")
    sk.Constrain("slant", px.Distance, id="sl", reference=True)
    a, b = dimension_line(strokes(px.Drawing(sk, unit=px.mm).Place("sl", 5))["sl"])
    n = np.array([1.0, -2.0]) / math.sqrt(5)
    assert ((a + b) / 2 - np.array([10.0, 5.0])) @ n == pytest.approx(5)
    assert texts(strokes(px.Drawing(sk, unit=px.mm))["sl"]) == [f"({22.361:.3f})".replace(".", ",")]


def test_a_diameters_direction_by_angle_or_compass():
    for place, angle in (("ne", 45), ("NW", 135), (90, 90), ("s", 270)):
        s = strokes(px.Drawing(plate(), unit=px.mm).Place("d", place), mm=0.5)["d"]
        (a, b, _), = s.lines
        u = (b - a) / np.linalg.norm(b - a)
        assert abs(u @ np.array([math.cos(math.radians(angle)), math.sin(math.radians(angle))])) == pytest.approx(1)
    with pytest.raises(ValueError, match="'north' is no direction"):
        px.Drawing(plate(), unit=px.mm).Place("d", "north")


def test_what_a_dimension_writes():
    sk = plate()
    sk.Update({"w": 62.5})
    assert texts(strokes(px.Drawing(sk, unit=px.mm))["w"]) == ["62,5"]
    assert texts(strokes(px.Drawing(plate(), unit=px.mm, names=True))["w"]) == ["w = 60"]
    dw = px.Drawing(plate(), unit=px.mm).Place("d", text="M10").Place("w", suffix="EQS")
    assert texts(strokes(dw)["d"]) == ["M10"] and texts(strokes(dw)["w"]) == ["60 EQS"]
    assert texts(strokes(px.Drawing(plate(0.001)))["w"]) == ["60"]


def test_equal_features_are_dimensioned_once():
    s = strokes(px.Drawing(part(), unit=px.mm))
    assert texts(s["d"]) == ["3× ⌀6"]
    assert texts(strokes(px.Drawing(part(), unit=px.mm).Place("d", count=1))["d"]) == ["⌀6"]
    sk = plate()
    sk.Circle((45, 20), 5, id="twin")
    sk.Constrain("hole", "twin", px.Equal)
    assert texts(strokes(px.Drawing(sk, unit=px.mm))["d"]) == ["2× ⌀10"]


def test_a_small_hole_goes_on_a_leader_that_crosses_no_other_dimension():
    s = strokes(px.Drawing(part(), unit=px.mm))
    leader = s["d"]
    assert len(leader.lines) == 2 and leader.texts[0][1] == 0
    (shoulder,), (line,) = ([(a, b) for a, b, _ in leader.lines if (abs(a[1] - b[1]) < 1e-9) == flat] for flat in (True, False))
    # DIN: the dimension line through the centre, both arrows on the circle from outside, bent close to the circle
    c = np.array([12.0, 12.0])
    u = (line[1] - line[0]) / np.linalg.norm(line[1] - line[0])
    assert abs((c - line[0]) @ np.array([-u[1], u[0]])) < 1e-9
    assert sorted(np.linalg.norm(tip - c) for tip, _ in leader.arrows) == pytest.approx([3, 3])
    assert np.linalg.norm(line[1] - c) == pytest.approx(3 + dims.LEADER)
    outside = [(c + u * 3, line[1]), (line[0], c - u * 3), shoulder]
    others = [(a, b) for i, t in s.items() if i not in ("d", "center lines") for a, b, _ in t.lines]
    P, Q = dims._ends(others)
    assert sum(dims._hits(a, b, P, Q) for a, b in outside) == 0


def test_a_hole_with_room_has_its_value_inside():
    (a, b, _), = strokes(px.Drawing(plate(), unit=px.mm), mm=0.5)["d"].lines
    assert np.linalg.norm(b - a) == pytest.approx(10)


def test_angles():
    s = strokes(px.Drawing(part(), unit=px.mm))["a"]
    assert texts(s) == [f"({math.degrees(math.atan2(25, 30)):.3f}°)".replace(".", ",")]
    assert texts(strokes(px.Drawing(part(), unit=px.mm).Place("a", supplement=True))["a"]) == \
        [f"({180 - math.degrees(math.atan2(25, 30)):.3f}°)".replace(".", ",")]
    # a taper: the vertex far off the part, the arc past the edges' ends, the arrows from outside
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(70, 0, px.Horizontal, id="base").Line(0, 10, px.Vertical, id="end") \
        .Line(-70, -6, id="taper").Close()
    sk.Constrain("taper", "base", px.Angle, id="tp", reference=True)
    t = strokes(px.Drawing(sk, unit=px.mm))["tp"]
    c = np.array([-70 * 4 / 6, 0.0])
    arc, = t.arcs
    assert np.linalg.norm(arc[0] - c) > math.hypot(70 - c[0], 10)
    swept = math.degrees(abs(math.atan2(*(arc[-1] - c)[::-1]) - math.atan2(*(arc[0] - c)[::-1])))
    assert swept > math.degrees(math.atan2(6, 70)) + 5
    assert len(t.lines) == 2


def test_an_angles_arc_beyond_a_short_leg_gets_an_extension_line():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(60, 0, px.Horizontal).Line(0, 30, px.Vertical) \
        .Line(-10, 10, id="chamfer").Line(-50, 0, px.Horizontal, id="top").Close(px.Vertical)
    sk.Constrain("chamfer", "top", px.Angle, id="c45", reference=True)
    s = strokes(px.Drawing(sk, unit=px.mm))["c45"]
    assert texts(s) == ["(45°)"]
    assert any(np.allclose(a, (60, 30)) for a, _, _ in s.lines)


def test_angles_at_one_vertex_stack_a_row_apart_smaller_inside_from_three_rows_out():
    sk = px.Sketch()
    for deg in (0, 80, 15, 40):
        sk.Move((0, 0), px.Fix).Line(60 * math.cos(math.radians(deg)), 60 * math.sin(math.radians(deg)), id=f"l{deg}")
    for deg in (80, 15, 40):
        sk.Constrain("l0", f"l{deg}", px.Angle, id=f"a{deg}", reference=True)
    s = strokes(px.Drawing(sk, unit=px.mm))
    radii = {deg: float(np.linalg.norm(s[f"a{deg}"].arcs[0][0])) for deg in (15, 40, 80)}
    assert radii == pytest.approx({15: 3 * dims.ROW, 40: 4 * dims.ROW, 80: 5 * dims.ROW})


def test_an_angle_to_an_axis_gets_an_auxiliary_line_along_the_axis_from_the_line_s_start():
    from planex import _svg
    sk = px.Sketch()
    sk.Move((5, 5), px.Fix).Line(20 * math.cos(math.radians(20)), -20 * math.sin(math.radians(20)), id="tilted")
    sk.Constrain("tilted", px.Distance, 20, id="l")
    sk.Constrain(px.H, "tilted", px.Angle, -20, id="tilt")
    sk.Move((5, 5), px.Fix).Line(10 * math.sin(math.radians(30)), 10 * math.cos(math.radians(30)), id="leaning")
    sk.Constrain("leaning", px.Distance, 10, id="m")
    sk.Constrain("leaning", px.V, px.Angle, -30, id="lean")
    sk.Move((5, 5)).Line(15, 0, id="flat")
    sk.Constrain(px.H, "flat", px.Angle, 0, id="level")
    s = strokes(px.Drawing(sk, unit=px.mm))
    for name, value, axis in (("tilt", "20°", (1, 0)), ("lean", "30°", (0, 1))):
        assert texts(s[name]) == [value]
        radius = s[name].entity["radius"]
        assert any(np.allclose(a, (5, 5)) and np.allclose(np.abs(b - a), np.multiply(axis, radius + dims.OVER))
                   for a, b, style in s[name].lines)
        arc, = s[name].arcs
        assert np.linalg.norm(arc - (5, 5), axis=1) == pytest.approx(np.full(len(arc), radius))
    # along the axis: nothing to draft, the value keeps its label
    assert "level" not in s
    drafted, _ = _svg._drafting(sk, {g["id"]: g for g in sk._Drawing()}, 4.0, 12)
    assert {"tilt", "lean"} <= set(drafted) and "level" not in drafted


def test_center_lines_and_axes():
    sk = plate()
    sk.SymmetryLine((30, -5), (30, 45), id="sym")
    s = strokes(px.Drawing(sk, unit=px.mm))["center lines"]
    centre = [(a, b) for a, b, style in s.lines if np.allclose((a + b) / 2, (20, 20))]
    assert len(centre) == 2 and all(np.linalg.norm(b - a) == pytest.approx(2 * (5 + dims.CENTER_OVER))
                                    for a, b in centre)
    assert any(abs(a[1] - 20) < 1e-9 and abs(b[1] - 20) < 1e-9 and min(a[0], b[0]) < 0 and max(a[0], b[0]) > 60
               for a, b, _ in s.lines)
    off = strokes(px.Drawing(plate(), unit=px.mm).Center("hole", off=True))["center lines"]
    assert off.lines == []
    axis = strokes(px.Drawing(plate(), unit=px.mm).Center("hole", off=True).Axis((-5, 30), (65, 30)))["center lines"]
    assert len(axis.lines) == 2 and all(style == "center" for _, _, style in axis.lines)


def test_two_sketches_of_a_view_keep_their_equal_ids_apart():
    a, b = px.Sketch(), px.Sketch()
    for sk, x in ((a, 20), (b, 70)):
        sk.Circle((x, 20), 5)
        sk.Constrain("Circle.1", px.Diameter, 10)
    dw = px.Drawing(a, unit=px.mm).Add(b)

    def centers():
        return {round(float(p[0] + q[0]) / 2) for p, q, _ in strokes(dw)["center lines"].lines}

    assert centers() == {20, 70}
    with pytest.raises(ValueError, match=rf"'Diameter.1' is in 2 of the drawing's sketches; name one by its full id "
                                         rf"\('{a.id}.Diameter.1', '{b.id}.Diameter.1'\) or selected"):
        dw.Place("Diameter.1", "ne")
    dw.Place(f"{b.id}.Diameter.1", "ne").Center(b("Circle.1"), off=True)
    assert centers() == {20}
    dw.Hide(f"{a.id}.Diameter.1")
    assert [d.id for d in dw._shown()] == [f"{b.id}.Diameter.1"]


def test_measures_and_extra_lines():
    dw = px.Drawing(plate(), unit=px.mm).Measure((0, 40), (0, 65)).Measure((60, 0), (60, 40), value="⌀40", offset=20)
    s = [t for t in dw._strokes(1.0, _Lettering()) if t.id == "Measure"]
    assert [texts(t) for t in s] == [["25"], ["⌀40"]]
    dw.Line([(0, 40), (0, 65)]).Line([(5, 0), (5, 40)], hidden=True)
    assert len(dw._outline()) == 5 + 1


def test_update_reads_the_sketch_anew():
    sk = plate()
    dw = px.Drawing(sk, unit=px.mm).Place("w", 15)
    sk.Update({"w": 80})
    dw.Update()
    s = strokes(dw)
    assert texts(s["w"]) == ["80"] and dimension_line(s["w"])[0][1] == pytest.approx(-15)


def test_the_scale_is_the_largest_that_fits():
    assert px.Sheet(px.Drawing(plate(), unit=px.mm))._figure().planex_scale == 2
    assert px.Sheet(px.Drawing(plate(1 / 60)))._figure().planex_scale == pytest.approx(0.1)
    assert px.Sheet(px.Drawing(plate(), unit=px.mm), scale=0.5)._figure().planex_scale == 0.5


def test_first_angle_placement():
    front, top, right = (px.Sketch(plane) for plane in (px.XZ, px.XY, px.YZ))
    for sk in (front, top, right):
        sk.Move((0, 0), px.Fix).Line(40, 0, px.Horizontal).Line(0, 20, px.Vertical).Line(-40, 0, px.Horizontal) \
            .Close(px.Vertical)
    sh = px.Sheet(*(px.Drawing(sk, unit=px.mm) for sk in (front, top, right)))
    _, _, places, _ = sh._layout(1.0, _Lettering())
    assert places[1][0] == 0 and places[1][1] < -20
    assert places[2][1] == 0 and places[2][0] < -40


def test_asme_places_the_views_in_third_angle_projection():
    front, top, right = (px.Sketch(plane) for plane in (px.XZ, px.XY, px.YZ))
    for sk in (front, top, right):
        sk.Move((0, 0), px.Fix).Line(40, 0, px.Horizontal).Line(0, 20, px.Vertical).Line(-40, 0, px.Horizontal) \
            .Close(px.Vertical)
    sh = px.Sheet(*(px.Drawing(sk, unit=px.mm) for sk in (front, top, right)), standard="ASME")
    _, _, places, _ = sh._layout(1.0, _Lettering())
    assert places[1][0] == 0 and places[1][1] > 20
    assert places[2][1] == 0 and places[2][0] > 40


def test_the_drawing_standard_comes_from_the_config():
    assert px.config.drawing_standard == "DIN" and px.Sheet().standard == "DIN"
    try:
        px.config.drawing_standard = "ASME"
        assert px.Sheet().standard == "ASME" and px.Sheet(standard="DIN").standard == "DIN"
        for wrong in ("ISO", None, 1):
            with pytest.raises(ValueError, match="it is 'DIN' or 'ASME'"):
                px.config.drawing_standard = wrong
    finally:
        px.config.drawing_standard = "DIN"
    with pytest.raises(ValueError, match="Sheet: standard 'ISO'; it is 'DIN' or 'ASME'"):
        px.Sheet(standard="ISO")
    with pytest.raises(ValueError, match="Drawing: standard 'ansi'; it is 'DIN' or 'ASME'"):
        px.Drawing(plate(), standard="ansi")


def test_asme_writes_a_decimal_point_and_the_third_angle_symbol(tmp_path):
    from planex._drawing import _Ink

    class Record(_Ink):
        def __init__(self):
            self.lines = []

        def polyline(self, pts, width, style="-"):
            self.lines.append(np.asarray(pts, float))

    for third in (False, True):
        r = Record()
        r.projection_symbol(100.0, 10.0, third)
        cone, circle = r.lines[0], r.lines[1]
        assert (circle[:, 0].mean() < cone[:, 0].mean()) == third
        assert cone[0, 0] < cone[1, 0] and np.ptp(cone[[0, 3], 1]) < np.ptp(cone[[1, 2], 1])
    ezdxf = pytest.importorskip("ezdxf")
    asme = px.Drawing(part(), unit=px.mm, standard="ASME")
    din = px.Drawing(part(), unit=px.mm)
    for sheet, standard in ((px.Sheet(din, scale=0.5, standard="ASME"), "ASME"), (px.Sheet(din, scale=0.5), "DIN"),
                            (px.Sheet(asme, scale=0.5), "DIN")):
        sheet.WriteDXF(tmp_path / "part.dxf")
        doc = ezdxf.readfile(tmp_path / "part.dxf")
        sign = "." if standard == "ASME" else ","
        assert chr(doc.dimstyles.get(standard).dxf.dimdsep) == sign
        assert {d.dxf.dimstyle for d in doc.modelspace().query("DIMENSION")} == {standard}
        shown = [e.text for d in doc.modelspace().query("DIMENSION") for e in doc.blocks.get(d.dxf.geometry)
                 if e.dxftype() == "MTEXT"]
        assert f"(39{sign}806°)" in shown
    asme.WriteDXF(tmp_path / "alone.dxf")
    assert ezdxf.readfile(tmp_path / "alone.dxf").dimstyles.get("ASME") is not None


def test_svg_and_pdf_are_the_sheet_at_true_scale(tmp_path):
    dw = px.Drawing(plate(), unit=px.mm)
    svg = dw.ToSVG()
    width, height = (float(v) for v in re.search(r'<svg[^>]*width="([\d.]+)pt" height="([\d.]+)pt"', svg).groups())
    assert (width, height) == pytest.approx((210 / 25.4 * 72, 297 / 25.4 * 72))
    assert svg == dw.ToSVG() and svg == dw._repr_svg_()
    dw.WriteSVG(tmp_path / "plate.svg")
    assert (tmp_path / "plate.svg").read_text(encoding="utf-8") == svg
    px.Sheet(dw, size="A3", frame=True).WritePDF(tmp_path / "plate.pdf")
    data = (tmp_path / "plate.pdf").read_bytes()
    box = [float(v) for v in re.search(rb"/MediaBox \[ 0 0 ([\d.]+) ([\d.]+) \]", data).groups()]
    assert box == pytest.approx([420 / 25.4 * 72, 297 / 25.4 * 72])
    assert b"/FontFile2" in data and re.search(rb"/BaseFont /\w+\+osifont", data)


def test_wrong_input_names_what_is_wrong():
    with pytest.raises(TypeError, match="a px.Sketch"):
        px.Drawing(px.QuickSketch())
    with pytest.raises(ValueError, match="unit is a positive length"):
        px.Drawing(plate(), unit=0)
    with pytest.raises(ValueError, match="'nope' is no dimension of the drawing's sketches; they have d, ht, hx, hy, w"):
        px.Drawing(plate()).Place("nope", 3)
    with pytest.raises(ValueError, match="size 'B4' is unknown"):
        px.Sheet(size="B4")
    with pytest.raises(ValueError, match="scale is a positive number"):
        px.Sheet(scale=0)
    with pytest.raises(ValueError, match="no views"):
        px.Sheet().ToSVG()
    with pytest.raises(TypeError, match="a px.Drawing"):
        px.Sheet().Add(plate())


def test_dxf_has_real_dimension_entities(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    px.Sheet(px.Drawing(part(), unit=px.mm), scale=0.5).WriteDXF(tmp_path / "part.dxf")
    doc = ezdxf.readfile(tmp_path / "part.dxf")
    msp = doc.modelspace()
    assert doc.units == 4 and doc.header["$LTSCALE"] == 2 and doc.dimstyles.get("DIN").dxf.dimscale == 2
    shown = sorted(e.text for d in msp.query("DIMENSION") for e in doc.blocks.get(d.dxf.geometry) if e.dxftype() == "MTEXT")
    assert shown == sorted(["12", "12", "20", "45", "50", "80", "(39,806°)", "3× %%c6", "2× 14 (= 28)"])
    assert sorted(d.dxf.text for d in msp.query("DIMENSION"))[:3] == ["(<>)", "2× <> (= 28)", "3× <>"]
    assert len(msp.query("CIRCLE")) == 3 and len(msp.query("LINE")) == 5
    px.Drawing(plate(0.001)).WriteDXF(tmp_path / "plate.dxf")
    plate_msp = ezdxf.readfile(tmp_path / "plate.dxf").modelspace()
    assert max(abs(e.dxf.end.x - e.dxf.start.x) for e in plate_msp.query("LINE")) == pytest.approx(60)


def test_a_title_block_is_filled_in(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    sh = px.Sheet(px.Drawing(plate(), unit=px.mm), frame=True).Title(title="Plate", Number="P-1", revision=2)
    sh.WriteDXF(tmp_path / "plate.dxf")
    ref, = ezdxf.readfile(tmp_path / "plate.dxf").modelspace().query("INSERT")
    assert ref.dxf.xscale == pytest.approx(0.5)
    assert {a.dxf.tag: a.dxf.text for a in ref.attribs if a.dxf.text} == {
        "TITLE": "Plate", "NUMBER": "P-1", "REVISION": "2", "SCALE": "2:1", "SIZE": "A4",
        "DATE": datetime.date.today().isoformat(), "SHEET": "1/1"}
    assert sh._footer()[1] > px._drawing.BORDER + 32
    sh.WritePDF(tmp_path / "plate.pdf")
    sh.Title(sheet="2/3").WriteDXF(tmp_path / "again.dxf")
    ref, = ezdxf.readfile(tmp_path / "again.dxf").modelspace().query("INSERT")
    assert {a.dxf.tag: a.dxf.text for a in ref.attribs}["SHEET"] == "2/3"
    with pytest.raises(ValueError, match="colour is no field of the template; its fields are department, reference"):
        sh.Title(colour="red")


def test_a_title_block_from_a_template_of_ones_own(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    doc = ezdxf.new()
    block = doc.blocks.new("COMPANY", base_point=(0, 0))
    block.add_lwpolyline([(-60, 0), (0, 0), (0, 12), (-60, 12)], close=True)
    block.add_text("Part", height=1.8).set_placement((-59, 9))
    block.add_attdef("Part_No", (-59, 2), height=3.5)
    doc.saveas(tmp_path / "company.dxf")
    sh = px.Sheet(px.Drawing(plate(), unit=px.mm)).Title(tmp_path / "company.dxf", part_no="X7")
    sh._figure()
    sh.WriteDXF(tmp_path / "plate.dxf")
    ref, = ezdxf.readfile(tmp_path / "plate.dxf").modelspace().query("INSERT")
    assert [(a.dxf.tag, a.dxf.text) for a in ref.attribs] == [("Part_No", "X7")]
    empty = ezdxf.new()
    empty.modelspace().add_line((0, 0), (1, 0))
    empty.saveas(tmp_path / "empty.dxf")
    with pytest.raises(ValueError, match="has no attribute definitions"):
        sh.Title(tmp_path / "empty.dxf")
    (tmp_path / "note.dxf").write_text("not a drawing")
    with pytest.raises(ValueError, match="is no DXF file planex can read"):
        sh.Title(tmp_path / "note.dxf")


def test_landscape_sheets():
    def page(sh):
        svg = sh.ToSVG()
        return tuple(round(float(v) / 72 * 25.4, 3) for v in
                     re.search(r'<svg[^>]*width="([\d.]+)pt" height="([\d.]+)pt"', svg).groups())

    dw = px.Drawing(plate(w=200), unit=px.mm)
    assert page(px.Sheet(dw)) == (210, 297) and page(px.Sheet(dw, landscape=True)) == (297, 210)
    assert page(px.Sheet(dw, size="A3")) == (420, 297) and page(px.Sheet(dw, size="A3", landscape=False)) == (297, 420)
    assert px.Sheet(dw)._figure().planex_scale == 0.5
    assert px.Sheet(dw, landscape=True)._figure().planex_scale == 1


def test_view_names_and_callouts(tmp_path):
    dw = px.Drawing(plate(), unit=px.mm, view_name="A").Callout((30, 50), (0, -1), "B")
    s = strokes(dw)
    (anchor, angle, text), = s["view name"].texts
    assert text == "A" and angle == 0 and anchor[0] == pytest.approx(30)
    others = px._drawing._points(dw._outline(), [t for i, t in s.items() if i != "view name"], 1.0, _Lettering())
    assert anchor[1] == pytest.approx(others[:, 1].max() + 4)
    callout = s["callouts"]
    (a, b, style), = callout.lines
    assert style == "wide" and np.allclose(a, (30, 62)) and np.allclose(b, (30, 55))
    assert np.allclose(callout.fills[0][0], (30, 50)) and callout.texts[0][2] == "B" and callout.height == 5
    assert "view name" not in strokes(px.Drawing(plate(), unit=px.mm).ViewName("A-A").ViewName(None))
    assert strokes(px.Drawing(plate(), unit=px.mm).ViewName("A-A"))["view name"].texts[0][2] == "A-A"
    with pytest.raises(ValueError, match="direction has no length"):
        dw.Callout((0, 0), (0, 0), "C")
    ezdxf = pytest.importorskip("ezdxf")
    px.Sheet(dw, scale=1).WriteDXF(tmp_path / "plate.dxf")
    texts_ = {e.dxf.text: e.dxf.height for e in ezdxf.readfile(tmp_path / "plate.dxf").modelspace().query("TEXT")}
    assert texts_ == {"A": 5, "B": 5}


def test_a_point_to_line_distance_is_square_to_the_line():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(50, 0, px.Horizontal, id="bottom").Line(0, 30, px.Vertical, id="right").Line(-50, 0) \
        .Close()
    sk.Move((0, 40)).Line(10, 0, px.Horizontal, id="stub")
    sk.Constrain("bottom.start", "right", px.Distance, 50, id="w")
    sk.Constrain("stub", "bottom.end", px.Distance, 40, id="up")
    found = {d.id: d for d in dims.Dimensions(sk)}
    assert abs(found["w"].u[1]) < 1e-12 and found["w"].span == pytest.approx(50)
    assert abs(found["up"].u[0]) < 1e-12 and found["up"].span == pytest.approx(40)


def test_concentric_radii_take_different_directions():
    sk = px.Sketch()
    sk.Circle((0, 0), 15, id="boss")
    sk.Circle((0, 0), 6, id="hole")
    sk.Circle((60, 0), 10, id="eye")
    sk.Constrain("boss", px.Radius, 15, id="r1")
    sk.Constrain("hole", px.Radius, 6, id="r2")
    sk.Constrain("eye", px.Radius, 10, id="r3")
    s = strokes(px.Drawing(sk, unit=px.mm))
    lines = {i: [(a, b) for a, b, _ in s[i].lines] for i in ("r1", "r2")}
    P, Q = dims._ends(lines["r1"])
    assert sum(dims._hits(a, b, P, Q) for a, b in lines["r2"]) == 0


def test_the_frame_has_grid_reference_zones():
    assert px._drawing._fields(20, 200, 105) == [20, 55, 105, 155, 200]
    assert px._drawing._fields(10, 287, 148.5) == [10, 48.5, 98.5, 148.5, 198.5, 248.5, 287]
    def labels(**kw):
        ax = px.Sheet(px.Drawing(plate(), unit=px.mm), frame=True, **kw)._figure().axes[0]
        return sorted(t.get_text() for t in ax.texts if len(t.get_text()) == 1)

    # on both sides on every size (the user's choice; ISO 5457 4.4 has A4's on the top and right only)
    assert labels() == sorted(list("1234ABCDEF") * 2) and labels(landscape=True) == sorted(list("123456ABCD") * 2)
    assert labels(size="A3") == sorted(list("12345678ABCDEF") * 2)
    lines = [l.get_xydata() for l in px.Sheet(px.Drawing(plate(), unit=px.mm), frame=True)._figure().axes[0].lines
             if l.get_linewidth() == pytest.approx(0.7 * 72 / 25.4)]
    centring = [p for p in lines if len(p) == 2 and abs(p[0][1] - 297 / 2) < 1e-9 and abs(p[1][1] - 297 / 2) < 1e-9]
    assert sorted(round(abs(p[1][0] - p[0][0]), 9) for p in centring) == [15, 15]


def test_a_negative_offset_goes_towards_the_part():
    s = strokes(px.Drawing(plate(), unit=px.mm).Place("w", -15).Place("ht", -12))
    assert dimension_line(s["w"])[0][1] == pytest.approx(15)
    assert dimension_line(s["ht"])[0][0] == pytest.approx(48)


def test_hide_leaves_dimensions_out_and_keeps_the_sketch():
    sk = plate()
    dw = px.Drawing(sk, unit=px.mm).Hide("hx", "hy")
    assert {"hx", "hy"}.isdisjoint(strokes(dw)) and {"w", "ht", "d"} <= set(strokes(dw))
    assert {"hx", "hy"} <= set(sk.dimensions)
    dw.Update()
    assert "hx" not in strokes(dw)
    assert {"w", "ht", "hx", "hy", "d"}.isdisjoint(strokes(px.Drawing(sk, unit=px.mm).Hide("*")))
    with pytest.raises(ValueError, match="'nope' is no dimension"):
        dw.Hide("nope")


def bolt_plate():
    sk = px.Sketch(auto_constraint=True)
    sk.Circle(50, id="outside")
    sk.Constrain("outside.center", "origin", px.Coincident)
    sk.Constrain("outside", px.Diameter, 100, id="D")
    sk.Circle(35, id="bolt_circle", construction=True)
    sk.Constrain("bolt_circle", "outside", px.Concentric)
    sk.Constrain("bolt_circle", px.Diameter, 70, id="k")
    return sk


def test_a_construction_element_with_a_dimension_is_drawn_as_a_center_line():
    centers = strokes(px.Drawing(bolt_plate(), unit=px.mm))["center lines"]
    radii = [np.linalg.norm(np.asarray(a), axis=1) for a in centers.arcs]
    assert any(np.allclose(r, 35) for r in radii)
    hidden = strokes(px.Drawing(bolt_plate(), unit=px.mm).Hide("k"))["center lines"]
    assert not any(np.allclose(np.linalg.norm(np.asarray(a), axis=1), 35) for a in hidden.arcs)


def test_a_length_to_a_filleted_corner_draws_the_corner_lines():
    sk = plate()
    sk.Fillet("bottom.end", 5)
    s = strokes(px.Drawing(sk, unit=px.mm))
    corner = s["theoretical corners"]
    assert len(corner.lines) >= 2
    tips = [b for a, b, style in corner.lines]
    assert all(style == "thin" for _, _, style in corner.lines)
    assert any(np.allclose(b, (62, 0)) for b in tips) and any(np.allclose(b, (60, -2)) for b in tips)


def flange():
    sk = px.Sketch(auto_constraint=True)
    sk.Circle(50, id="outside")
    sk.Constrain("outside.center", "origin", px.Coincident)
    sk.Constrain("outside", px.Diameter, 100, id="D")
    sk.Circle(15, id="bore")
    sk.Constrain("bore", "outside", px.Concentric)
    sk.Constrain("bore", px.Diameter, 30, id="d")
    sk.Circle(35, id="bolt_circle", construction=True)
    sk.Constrain("bolt_circle", "outside", px.Concentric)
    sk.Constrain("bolt_circle", px.Diameter, 70, id="k")
    return sk


def test_concentric_diameters_take_different_directions():
    s = strokes(px.Drawing(flange(), unit=px.mm))
    angles = [math.radians(s[i].entity["angle"]) for i in ("D", "d", "k")]
    for j, a in enumerate(angles):
        for b in angles[j + 1:]:
            assert abs(math.sin(a - b)) > 0.17


def test_a_small_angle_s_arc_stays_near_the_shorter_leg():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(40, 0, px.Horizontal, id="ground", construction=True)
    sk.Move((0, 0)).Line(10 * math.cos(math.radians(20)), 10 * math.sin(math.radians(20)), id="crank")
    sk.Constrain("crank", px.Distance, 10, id="r")
    sk.Constrain("ground", "crank", px.Angle, 20, id="phi")
    radius = strokes(px.Drawing(sk, unit=px.mm))["phi"].entity["radius"]
    assert radius <= 10 + 10.0 + 1e-9


def test_an_angle_between_legs_short_on_the_paper_stays_at_their_ends():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(0, -3, px.Vertical, id="back", construction=True)
    sk.Move((0, 0)).Line(3 * math.sin(math.radians(15)), -3 * math.cos(math.radians(15)), id="arm")
    sk.Constrain("arm", px.Distance, 3, id="a")
    sk.Constrain("back", "arm", px.Angle, 15, id="steer")
    radius = strokes(px.Drawing(sk, unit=px.mm))["steer"].entity["radius"]
    # at the legs' ends its value would lie on the arm and on the arm's length's extension line: a row further out
    assert radius <= 3 + 2.0 + dims.ROW + 1e-9


def test_a_box_check_finds_a_segment_through_it():
    from planex._svg import _meets_box
    segments = np.array([[[0.0, 0.0], [10.0, 0.0]], [[20.0, -5.0], [20.0, 5.0]]])
    assert _meets_box(segments, (5.0, 1.0), (2.0, 2.0))
    assert not _meets_box(segments, (5.0, 3.0), (2.0, 2.0))
    assert _meets_box(segments, (19.0, 0.0), (1.5, 1.0))
    assert not _meets_box(np.zeros((0, 2, 2)), (0.0, 0.0), (1.0, 1.0))


def test_a_sketch_on_a_turned_or_flipped_plane_is_dimensioned_in_the_view():
    turned = px.Sketch(px.Plane(origin=(100, 0, 5), normal=(0, 0, 1), axis=(0, 1, 0)))
    turned.Line((0, 0), (30, 0), id="rail")
    turned.Constrain("rail", px.Distance, 30, id="rail_length")
    turned.Circle((10, 5), 2, id="pin")
    turned.Constrain("pin", px.Diameter, 4, id="pin_d")
    flipped = px.Sketch(px.Plane(origin=(0, 100, 0), normal=(0, 0, -1), axis=(1, 0, 0)))
    flipped.Line((0, 0), (20, 0), id="arm_a")
    flipped.Line((0, 0), (20, 20), id="arm_b")
    flipped.Constrain("arm_a", "arm_b", px.Angle, 45, id="spread")
    view = px.Drawing(plate(), unit=px.mm).Add(turned).Add(flipped)
    rail, spread = (next(d for d in view._dims if d.id == i) for i in ("rail_length", "spread"))
    # the turned sketch's h runs along the view's y, its v along -x
    assert rail.p == pytest.approx((100, 0)) and rail.q == pytest.approx((100, 30)) and rail.u == pytest.approx((0, 1))
    # seen from behind, the angle still opens counterclockwise from its first side to its second
    a, b = spread.a, spread.b
    assert math.degrees(math.atan2(a[0] * b[1] - a[1] * b[0], a @ b)) == pytest.approx(45)
    assert spread.c == pytest.approx((0, 100))
    centers = strokes(view)["center lines"].lines
    assert any(np.allclose((p + q) / 2, (95, 10)) for p, q, _ in centers)
    px.Sheet(view)._figure()


def test_a_slots_width_is_drawn_across_its_sides():
    sk = px.Sketch()
    sk.Slot(20, 6.6, id="slot")
    (d,) = [d for d in px.Drawing(sk, unit=px.mm)._dims if d.id == "slot.w"]
    assert d.kind == "linear" and d.value == pytest.approx(6.6)
    assert d.p == pytest.approx((10, 3.3)) and d.q == pytest.approx((10, -3.3))


def arms(s, c, reach):
    """the lengths end to end of the center lines through c, horizontal and vertical, within reach of it (a gap at a
    value kept)"""
    c, out = np.asarray(c, float), []
    for u in (np.array([1.0, 0.0]), np.array([0.0, 1.0])):
        ts = [u @ (p - c) for a, b, _ in s.lines if all(abs(u[0] * (p - c)[1] - u[1] * (p - c)[0]) < 1e-9
                                                         and np.linalg.norm(p - c) <= reach + 1e-9 for p in (a, b))
              for p in (a, b)]
        if ts:
            out.append(max(ts) - min(ts))
    return out


def test_an_arc_dimensioned_by_its_diameter_gets_its_center_mark():
    sk = px.Sketch()
    sk.Move((10, 0)).Arc((-10, 0), center=(0, 0), id="throat")
    sk.Constrain("throat", px.Diameter, 20)
    s = strokes(px.Drawing(sk, unit=px.mm))["center lines"]
    assert arms(s, (0, 0), 10) == pytest.approx([2 * dims.CROSS] * 2)


def test_center_marks_the_center_of_a_construction_circle_as_asked():
    # a construction circle a dimension measures: drawn as a center line; Center adds lines across it or a cross
    sk = plate()
    sk.Circle((40, 20), 8, construction=True, id="throat")
    sk.Constrain("throat", px.Diameter, 16)

    def at_throat(dw):
        return arms(strokes(dw)["center lines"], (40, 20), 8 + dims.CENTER_OVER)

    assert at_throat(px.Drawing(sk, unit=px.mm)) == []
    assert at_throat(px.Drawing(sk, unit=px.mm).Center("throat")) == pytest.approx([2 * (8 + dims.CENTER_OVER)] * 2)
    assert at_throat(px.Drawing(sk, unit=px.mm).Center("throat", cross=True)) == pytest.approx([2 * dims.CROSS] * 2)
    assert len(at_throat(px.Drawing(sk, unit=px.mm).Center("throat", arms="h"))) == 1
    assert at_throat(px.Drawing(sk, unit=px.mm).Center("throat", pitch=True)) == []



def test_a_vertical_line_a_hair_off_reads_from_the_right():
    assert dims._upright((1e-15, -1.0)) == pytest.approx(90)
    assert dims._upright((-1e-15, 1.0)) == pytest.approx(90)
    assert dims._upright((0.0, -1.0)) == pytest.approx(90)


def test_segments_on_one_line_cross_only_where_they_overlap():
    a, b = np.array([0.0, 0.0]), np.array([1.0, 0.0])
    assert dims._hits(a, b, np.array([[2.0, 0.0]]), np.array([[3.0, 0.0]])) == 0
    assert dims._hits(a, b, np.array([[0.5, 0.0]]), np.array([[2.0, 0.0]])) == 1
    assert dims._hits(a, b, np.array([[1.0, 0.0]]), np.array([[2.0, 0.0]])) == 1
    assert dims._hits(a, b, np.array([[0.5, -1.0]]), np.array([[0.5, 1.0]])) == 1


def value_box(s, k=0):
    anchor, angle, text = s.texts[k]
    return dims._box(anchor, angle, _Lettering().width(text, s.height), s.height)


def test_a_value_wider_than_its_extension_lines_goes_beyond_the_arrow():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(3.5, 0, px.Horizontal, id="tab").Line(0, 20, px.Vertical).Line(-3.5, 0, px.Horizontal) \
        .Close(px.Vertical)
    sk.Constrain("tab", px.Distance, 3.5, id="w")
    s = strokes(px.Drawing(sk, unit=px.mm))["w"]
    (anchor, angle, text), = s.texts
    assert text == "3,5" and angle == pytest.approx(0)
    assert anchor[0] - _Lettering().width(text) / 2 >= 3.5 + dims.ARROW + 1 - 1e-9
    extensions = [(a, b) for a, b, _ in s.lines if abs(a[0] - b[0]) < 1e-9]
    assert len(extensions) == 2 and dims._crossed(value_box(s), dims._ends(extensions)) == 0


def test_a_long_radius_or_diameter_value_ends_before_the_arrow():
    pen = dims.Pen(1.0, [], lambda t: 2.0 * len(t))
    pen.out = dims.Strokes("r")
    dims.Radius(pen, np.zeros(2), 40.0, np.array([1.0, 0.0]), "R40 (long)")
    (anchor, _, text), = pen.out.texts
    assert anchor[0] + pen.width(text) / 2 <= 40 - dims.ARROW - 1 + 1e-9
    pen.out = dims.Strokes("d")
    dims.Diameter(pen, np.zeros(2), 12.0, np.array([1.0, 0.0]), "⌀24 h7")
    (anchor, _, text), = pen.out.texts
    assert anchor[0] + pen.width(text) / 2 <= 12 - dims.ARROW - 1 + 1e-9


def test_an_angle_s_value_moves_off_a_value_drawn_where_it_would_go():
    def angle():
        b = np.array([math.cos(math.radians(40)), math.sin(math.radians(40))])
        d = dims.Dimension("a", "angle", 40.0, c=np.zeros(2), a=np.array([1.0, 0.0]), b=b, legs=((0.0, 60.0), (0.0, 60.0)))
        return d

    free = dims.Layout([], 1.0, lambda t: 2.0 * len(t))
    alone = free.Draw(angle())
    blocked = dims.Layout([], 1.0, lambda t: 2.0 * len(t))
    blocker = dims.Strokes("blocker")
    blocker.texts.append((alone.texts[0][0], alone.texts[0][1], "XXXXXX"))
    blocked.drawn.append(blocker)
    moved = blocked.Draw(angle())
    box = lambda s: dims._box(s.texts[0][0], s.texts[0][1], 2.0 * len(s.texts[0][2]), dims.HEIGHT)
    assert dims._crossed(box(moved), dims._ends(box(blocker))) == 0
    assert texts(moved) == ["40°"]


def test_a_center_line_gets_a_gap_where_a_value_lies_on_it():
    layout = dims.Layout([], 1.0, lambda t: 2.0 * len(t))
    centres = dims.Strokes("center lines")
    centres.lines.append((np.array([-20.0, 2.0]), np.array([20.0, 2.0]), "center"))
    t = np.linspace(0, 2 * math.pi, 241)
    centres.arcs.append(np.c_[3 * np.cos(t), 2 + 3 * np.sin(t)])
    value = dims.Strokes("v")
    value.texts.append((np.zeros(2), 0.0, "R10"))
    layout.drawn += [centres, value]
    layout.Interrupt(centres)
    # the value's box x -3..3, y 0..3.5, half a millimeter clear
    lines = np.array([(a, b) for a, b, _ in centres.lines])
    assert lines.shape == (2, 2, 2) and np.allclose(lines, [((-20, 2), (-3.5, 2)), ((3.5, 2), (20, 2))])
    assert len(centres.arcs) == 2
    for arc in centres.arcs:
        inside = (np.abs(arc[:, 0]) < 3.5 - 1e-9) & (arc[:, 1] > -0.5 + 1e-9) & (arc[:, 1] < 4.0 - 1e-9)
        assert not inside.any()


def crossed_by(s, blocker):
    """how often the values of strokes s cross the lines of the strokes blocker"""
    boxes = [seg for anchor, angle, text in s.texts for seg in dims._box(anchor, angle, 2.0 * len(text), dims.HEIGHT)]
    return dims._crossed(boxes, dims._ends([(a, b) for a, b, _ in blocker.lines]))


def test_an_arc_s_radius_goes_on_a_leader_where_every_place_on_the_arc_crosses_a_line():
    layout = dims.Layout([], 1.0, lambda t: 2.0 * len(t))
    blocker = dims.Strokes("blocker")
    blocker.lines += [(np.array([-30.0, y]), np.array([30.0, y]), "thin") for y in (5.0, 15.0)]
    layout.drawn.append(blocker)
    d = dims.Dimension("r", "radius", 10.0, c=np.zeros(2), r=10.0, a=np.array([0.0, 1.0]), sweep=math.radians(60))
    s = layout.Draw(d)
    (anchor, angle, text), = s.texts
    assert text == "R10" and angle == 0 and anchor[1] > 15
    assert crossed_by(s, blocker) == 0 and s.entity["location"] is not None
    tip, _ = s.arrows[0]
    assert np.linalg.norm(tip) == pytest.approx(10)


def test_a_circle_s_diameter_goes_on_a_leader_where_every_place_inside_crosses_a_line():
    layout = dims.Layout([], 1.0, lambda t: 2.0 * len(t))
    blocker = dims.Strokes("blocker")
    for k in range(12):
        u = np.array([math.cos(k * math.pi / 12), math.sin(k * math.pi / 12)])
        blocker.lines.append((-9.5 * u, 9.5 * u, "thin"))
    layout.drawn.append(blocker)
    s = layout.Draw(dims.Dimension("d", "diameter", 20.0, c=np.zeros(2), r=10.0))
    (anchor, _, text), = s.texts
    assert text == "⌀20" and np.linalg.norm(anchor) > 10
    assert crossed_by(s, blocker) == 0
    assert sorted(np.linalg.norm(tip) for tip, _ in s.arrows) == pytest.approx([10, 10])


def box(plane, w, h, dimensioned=False):
    """a w x h rectangle on plane, its corner at the origin; with its two sizes dimensioned"""
    sk = px.Sketch(plane)
    sk.Move((0, 0), px.Fix).Line(w, 0, px.Horizontal, id="b").Line(0, h, px.Vertical, id="r").Line(-w, 0, px.Horizontal) \
        .Close(px.Vertical)
    if dimensioned:
        sk.Constrain("b", px.Distance, w, id="width")
        sk.Constrain("r", px.Distance, h, id="height")
    return px.Drawing(sk, unit=px.mm)


def paper_middle(sh, k):
    """view k's middle on the sheet in mm, its dimensions included"""
    from planex._drawing import _points

    a = sh._arrange()
    d = sh._views[k][0]
    pts = _points(d._outline(), a.drawn[k], 1 / a.paper, a.lettering)
    return ((pts.min(axis=0) + pts.max(axis=0)) / 2 + a.places[k]) * a.paper + a.shift


def test_a_view_placed_in_a_zone_has_its_middle_at_the_zone_s_middle():
    # A4 upright: columns 20, 55, 105, 155, 200 mm from the left, rows 287, 248.5, 198.5, ... from the top
    sh = px.Sheet(box(px.XZ, 40, 20, True)).Add(box(px.XY, 30, 10, True), zone="B2")
    assert paper_middle(sh, 1) == pytest.approx((80, 223.5))
    span = px.Sheet(box(px.XZ, 40, 20, True)).Add(box(px.XY, 30, 10, True), zone="b2:C3")
    assert paper_middle(span, 1) == pytest.approx((105, 198.5))
    framed = px.Sheet(box(px.XZ, 40, 20, True), frame=True).Add(box(px.XY, 30, 10, True), zone="B2")
    assert paper_middle(framed, 1) == pytest.approx((80, 223.5))
    framed.ToSVG()


def test_a_top_view_placed_in_a_zone_leaves_the_line_up_and_a_front_view_takes_the_others_along():
    sh = px.Sheet(box(px.XZ, 40, 20, True)).Add(box(px.XY, 40, 20, True), zone="E4")
    a = sh._arrange()
    assert a.places[1][0] != pytest.approx(0) and paper_middle(sh, 1)[0] == pytest.approx(177.5)
    front = px.Sheet().Add(box(px.XZ, 40, 20, True), zone="B3").Add(box(px.XY, 40, 20, True))
    a = front._arrange()
    assert np.allclose(a.places[0], 0) and a.places[1][0] == pytest.approx(0) and a.places[1][1] < 0
    assert paper_middle(front, 0) == pytest.approx((130, 223.5))


def test_views_placed_in_zones_count_in_the_scale_and_one_running_over_warns():
    # 200 mm wide: 1:2 on A4 by projection, but in column 1 (35 mm wide) only from 1:10
    assert px.Sheet(box(px.XZ, 200, 50), box(px.XY, 200, 50))._arrange().ratio == pytest.approx(0.5)
    assert px.Sheet(box(px.XZ, 200, 50)).Add(box(px.XY, 200, 50), zone="C1")._arrange().ratio == pytest.approx(0.1)
    with pytest.warns(px.PlanexWarning) as caught:
        px.Sheet(box(px.XZ, 20, 5), scale=1).Add(box(px.XY, 200, 50), zone="C1")._arrange()
    assert any(re.search(r"view 2 in zone C1 runs over the drawing area .* at 1:1", str(w.message)) for w in caught)


def test_dimension_rows_lie_8_mm_from_the_part_and_7_mm_apart():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(40, 0, px.Horizontal, id="b").Line(0, 20, px.Vertical).Line(-40, 0, px.Horizontal) \
        .Close(px.Vertical)
    sk.Circle((10, 10), 2, id="hole")
    sk.Constrain("b", px.Distance, 40, id="width")
    sk.Constrain("hole.center", px.V, px.Distance, 10, id="x")
    strokes = {s.id: s for s in px.Drawing(sk, unit=px.mm)._strokes(1.0, _Lettering())}

    def level(i):
        return {round(float(a[1]), 6) for a, b, _ in strokes[i].lines if abs(a[1] - b[1]) < 1e-9}

    # below the part, the shorter one nearer
    assert level("x") == {-8.0} and level("width") == {-15.0}


def test_a_view_placed_in_a_zone_that_fits_at_no_scale_leaves_the_scale_to_the_others():
    # its name, 5 mm high, is wider than column 1 (35 mm) at every scale; the front view alone fits at 1:2
    named = box(px.XY, 10, 10).ViewName("Flat pattern of the cover")
    sh = px.Sheet(box(px.XZ, 200, 50)).Add(named, zone="C1")
    with pytest.warns(px.PlanexWarning) as caught:
        assert sh._arrange().ratio == pytest.approx(0.5)
    assert any(re.search(r"'Flat pattern of the cover' in zone C1 runs over the drawing area .* at 1:2", str(w.message))
               for w in caught)
    with pytest.warns(px.PlanexWarning):
        assert paper_middle(sh, 1) == pytest.approx((37.5, 173.5))


def test_a_view_in_a_zone_on_another_warns_of_the_overlap():
    with pytest.warns(px.PlanexWarning, match="view 1 and the view 2 in zone C2 overlap"):
        px.Sheet(box(px.XZ, 100, 50), scale=1).Add(box(px.XY, 20, 10), zone="C2")._arrange()


def test_a_zone_the_sheet_has_not_names_its_zones():
    sh = px.Sheet()
    for zone in ("G1", "A5", "I1", "B", "A1:B2:C3"):
        with pytest.raises(ValueError, match=r"is not on this A4 sheet; its zones are A1 to F4"):
            sh.Add(box(px.XZ, 10, 10), zone=zone)
    with pytest.raises(ValueError, match="at or zone, not both"):
        sh.Add(box(px.XZ, 10, 10), at=(0, 0), zone="A1")
