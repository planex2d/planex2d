# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import netgen.occ as occ
import numpy as np
import pytest

import planex as px
from planex._drawing import _Lettering

pytest.importorskip("matplotlib")


def block():
    """a 60 x 30 x 20 block with a vertical hole of radius 5 through it at (20, 15)"""
    return occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(60, 30, 20)) - occ.Cylinder(occ.Pnt(20, 15, -1), occ.Z, r=5, h=22)


def extent(runs):
    pts = np.concatenate(runs)
    return pts.min(axis=0), pts.max(axis=0)


def test_a_view_shows_the_visible_edges_wide_and_the_hidden_ones_dashed():
    front = px.Drawing(block(), px.Front, unit=px.mm)
    assert front.plane.normal == pytest.approx((0, -1, 0))
    lo, hi = extent(front._outline())
    assert lo == pytest.approx((0, 0)) and hi == pytest.approx((60, 20))
    hidden = [p for p, dashed in front._drawn_lines() if dashed]
    # the hole's two sides seen from the front
    assert sorted({round(float(p[0][0]), 9) for p in hidden}) == [15, 25]
    assert not [p for p, dashed in px.Drawing(block(), px.Front, unit=px.mm, hidden=False)._drawn_lines() if dashed]



def test_hidden_edges_on_one_line_are_one_dashed_line():
    """a pocket 10 deep from below: its rim and its ceiling fall together in the top view"""
    pocket = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(60, 30, 20)) - occ.Box(occ.Pnt(10, 10, -1), occ.Pnt(50, 20, 10))
    hidden = [p for p, dashed in px.Drawing(pocket, px.Top, unit=px.mm)._drawn_lines() if dashed]
    assert len(hidden) == 4
    lo, hi = extent(hidden)
    assert lo == pytest.approx((10, 10)) and hi == pytest.approx((50, 20))


def test_the_standard_views_take_their_places_in_first_angle_projection():
    views = [px.Drawing(block(), plane, unit=px.mm) for plane in (px.Front, px.Top, px.Left)]
    places = px.Sheet(*views)._arrange().places
    assert places[1][1] < places[0][1] and places[1][0] == pytest.approx(places[0][0])
    assert places[2][0] > places[0][0] and places[2][1] == pytest.approx(places[0][1])


def test_a_section_hatches_the_faces_the_plane_cuts_and_draws_no_hidden_edges():
    plane = px.Plane(origin=(0, 15, 0), normal=(0, -1, 0), axis=(1, 0, 0))
    cut = px.Drawing(block(), plane, unit=px.mm, section=True)
    assert cut._solid.region.Measure()["area"] == pytest.approx(60 * 20 - 10 * 20)
    assert not [p for p, dashed in cut._drawn_lines() if dashed]
    hatch = [s for s in cut._strokes(1.0, _Lettering()) if s.id == "solid"][0]
    assert all(abs(abs(q[0] - p[0]) - abs(q[1] - p[1])) < 1e-9 for p, q, _ in hatch.lines)


def test_views_of_solids_name_themselves():
    names = [px.Drawing(block(), plane)._view_name for plane in (px.Front, px.Top, px.Left, px.Right, px.Rear, px.Bottom)]
    assert names == ["Front view", "Top view", "Left view", "Right view", "Rear view", "Bottom view"]
    assert px.Drawing(block(), px.Isometric)._view_name == "Isometric view"
    assert px.Drawing(block(), px.Plane(normal=(1, -1, 0), axis=(1, 1, 0)))._view_name == "Auxiliary view"
    assert px.Drawing(block(), px.Front, section=True)._view_name == "Section view"
    assert px.Drawing(block(), px.Front, view_name="A")._view_name == "A"
    assert px.Drawing(block(), px.Front, view_name="")._view_name == ""


def test_a_section_is_marked_on_the_view_it_is_taken_from():
    top = px.Drawing(block(), px.Top, unit=px.mm)
    cut = px.Drawing(block(), px.Plane(origin=(0, 15, 0), normal=(0, -1, 0), axis=(1, 0, 0)), unit=px.mm, section=True,
                     view_name="Section view B-B")
    strokes = {s.id: s for s in top.Section(cut)._strokes(1.0, _Lettering())}
    line = [(p, q) for p, q, style in strokes["cutting lines"].lines if style == "center"][0]
    # across the part at y = 15, 5 mm past it on both sides
    assert sorted([line[0][0], line[1][0]]) == pytest.approx([-5, 65]) and line[0][1] == pytest.approx(15)
    assert sum(style == "wide" for _, _, style in strokes["cutting lines"].lines) == 2
    letters = [text for _, _, text in strokes["callouts"].texts]
    shafts = [q - p for p, q, style in strokes["callouts"].lines if style == "wide"]
    # seen from the front: the arrows point back, +y in the top view
    assert letters == ["B", "B"] and len(shafts) == 2 and all(s / np.linalg.norm(s) == pytest.approx((0, 1)) for s in shafts)
    with pytest.raises(ValueError, match="no section"):
        top.Section(px.Drawing(block(), px.Front))
    with pytest.raises(ValueError, match="has no letters"):
        top.Section(px.Drawing(block(), px.Front, section=True))
    with pytest.raises(ValueError, match="runs along this view's"):
        top.Section(px.Drawing(block(), px.Plane(origin=(0, 0, 10), normal=(0, 0, 1), axis=(1, 0, 0)), section=True,
                               view_name="C-C"))


def test_smooth_edges_are_narrow_and_stop_short_of_the_outline():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(40, 30, 20))
    rounded = box.MakeFillet([e for e in box.edges if e.center.z > 10], 4)
    top = px.Drawing(rounded, px.Top, unit=px.mm)
    smooth = [s for s in top._strokes(1.0, _Lettering()) if s.id == "solid"][0].lines
    assert smooth and all(style == "thin" for _, _, style in smooth)
    xs = [x for p, q, _ in smooth for x in (p[0], q[0])]
    assert min(xs) > 0 and max(xs) < 40


def test_a_pictorial_is_shaded_and_shows_no_hidden_edges():
    iso = px.Drawing(block(), px.Isometric, unit=px.mm)
    triangles, light = iso._solid.shading
    assert len(triangles) == len(light) > 0 and all(0 <= x <= 1 for x in light)
    assert not iso._solid.hidden and iso.plane.normal == pytest.approx(np.array([1, -1, 1]) / math.sqrt(3))
    assert repr(px.Isometric) == "px.Isometric"


def test_a_pictorial_shows_the_nearest_face_at_every_point():
    n = np.array([1, -1, 1]) / math.sqrt(3)
    x, y = np.array([1, 1, 0]) / math.sqrt(2), np.array([-1, 1, 2]) / math.sqrt(6)
    to_light = n + 0.5 * (y - x)
    to_light /= np.linalg.norm(to_light)
    cube = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(10, 10, 10))
    tall = occ.Box(occ.Pnt(2, 2, -20), occ.Pnt(8, 8, 30)).Rotate(occ.Axis(occ.Pnt(5, 5, 0), occ.Z), 40).Move(occ.Vec(*(-25 * n)))
    pixel = 0.05
    light, low = px._planex._ShadedImage(cube + tall, px.Isometric.plane, 0.01, pixel)
    for center, normal in (((10, 5, 5), (1, 0, 0)), ((5, 0, 5), (0, -1, 0)), ((5, 5, 10), (0, 0, 1))):
        h, v = np.dot(center, x), np.dot(center, y)
        row, col = light.shape[0] - 1 - int((v - low[1]) / pixel), int((h - low[0]) / pixel)
        assert light[row, col] == pytest.approx(np.dot(normal, to_light), abs=1e-12)
    assert np.isnan(light[0, 0]) and np.nanmax(light) <= 1


def test_a_pictorial_shades_an_open_shell_seen_from_inside_as_from_outside():
    n = np.array([1, -1, 1]) / math.sqrt(3)
    x, y = np.array([1, 1, 0]) / math.sqrt(2), np.array([-1, 1, 2]) / math.sqrt(6)
    to_light = n + 0.5 * (y - x)
    to_light /= np.linalg.norm(to_light)
    tube = occ.Wire([occ.Segment(occ.Pnt(10, 0, 0), occ.Pnt(10, 0, 20))]).Revolve(occ.Axis((0, 0, 0), occ.Z), 360)
    # through the middle of the top opening the back wall's inside at (-1, 1, 0) / sqrt(2) * 10, its normal towards the
    # axis (1, -1, 0) / sqrt(2) as the front wall's outside at (1, -1, 0) / sqrt(2) * 10
    expected = np.dot(np.array([1, -1, 0]) / math.sqrt(2), to_light)
    pixel = 0.05
    for shell in (tube, tube.Reversed()):
        light, low = px._planex._ShadedImage(shell, px.Isometric.plane, 1e-3, pixel)
        for point in ((0, 0, 20), (10 / math.sqrt(2), -10 / math.sqrt(2), 10)):
            h, v = np.dot(point, x), np.dot(point, y)
            row, col = light.shape[0] - 1 - int((v - low[1]) / pixel), int((h - low[0]) / pixel)
            assert light[row, col] == pytest.approx(expected, abs=0.02)


def test_fold_lines_on_a_free_form_surface_lie_on_its_exact_fold():
    # a b-spline profile through points of a circle of radius 5 about (10, 0), turned about z: its outline seen from the
    # front is the profile, widest at x = 15 (the point (15, 0) on it, symmetric about it)
    pts = [occ.Pnt(10 + 5 * math.cos(t), 0, 5 * math.sin(t)) for t in np.linspace(-math.pi / 2, math.pi / 2, 9)]
    profile = occ.SplineInterpolation(pts)
    face = occ.Face(occ.Wire([occ.Edge(profile), occ.Segment(pts[-1], pts[0])]))
    solid = face.Revolve(occ.Axis((0, 0, 0), occ.Z), 360)
    front = px.Drawing(solid, px.Front)
    gap = min(e.Measure(at=(100, 0))["distance"] for w in front._solid.wires["visible"] for e in w.edges)
    # the refined fold, fitted through points on the exact fold: within about 1e-8 of the size
    assert gap == pytest.approx(85, abs=1e-6)


def test_a_sheet_of_views_writes_pdf_svg_and_dxf(tmp_path):
    pytest.importorskip("ezdxf")
    sheet = px.Sheet(px.Drawing(block(), px.Front, unit=px.mm, view_name="A"), px.Drawing(block(), px.Isometric, unit=px.mm))
    sheet.WritePDF(tmp_path / "views.pdf")
    sheet.WriteDXF(tmp_path / "views.dxf")
    assert (tmp_path / "views.pdf").stat().st_size > 0 and "<svg" in sheet.ToSVG()


def test_a_sketch_on_a_face_of_the_solid_puts_its_dimensions_on_the_view():
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(60, 40, 20))
    top = box.faces.Max(occ.Z)
    sk = px.Sketch(top)
    sk.Circle((12, 10), 4.5, id="hole")
    sk.Constrain("hole", px.Diameter, 9, id="d")
    sk.Project(top.edges.Min(occ.X), id="left_edge")
    sk.Constrain("hole.center", "left_edge.Edge.0", px.Distance, 12, id="x")
    view = px.Drawing(box, px.Top, unit=px.mm).Add(sk)
    d, x = (next(m for m in view._dims if m.id == i) for i in ("d", "x"))
    assert d.c == pytest.approx((12, 10))
    assert sorted(map(tuple, (x.p, x.q))) == [pytest.approx((0, 10)), pytest.approx((12, 10))]
    px.Sheet(view)._figure()


def test_wrong_views_name_what_is_wrong():
    with pytest.raises(TypeError, match="the plane it is seen along"):
        px.Drawing(block())
    sk = px.Sketch()
    sk.Move((0, 0)).Line((10, 0)).Line((10, 5)).Close()
    with pytest.raises(TypeError, match="a sketch is seen on its own plane"):
        px.Drawing(sk, px.Front)
    with pytest.raises(ValueError, match="a section is a view along a plane"):
        px.Drawing(block(), px.Isometric, section=True)
    with pytest.raises(TypeError, match="or an OCC solid"):
        px.Drawing(42)
