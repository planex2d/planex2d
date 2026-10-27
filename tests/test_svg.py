# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import xml.etree.ElementTree as ET

import pytest

import planex as px
from helpers import square

NS = "{http://www.w3.org/2000/svg}"


def paths(svg):
    root = ET.fromstring(svg)
    return root, root.findall(NS + "path")


def test_face_with_hole_is_filled_evenodd_and_stroked():
    root, (fill, stroke) = paths((square(0, 0, 3) - square(1, 1, 1))._repr_svg_())
    assert (root.get("width"), root.get("height")) == ("300", "300")
    assert fill.get("fill-rule") == "evenodd"
    assert fill.get("d").count("Z") == 2
    assert stroke.get("fill") == "none"
    assert stroke.get("d").count("M") == 8


def test_viewbox_mirrors_y_and_keeps_the_aspect_ratio():
    root, _ = paths(px.Move((0, 0)).Rectangle(10, 1).Wire().ToSVG())
    assert tuple(map(float, root.get("viewBox").split())) == pytest.approx((-0.5, -1.5, 11, 2))
    assert (float(root.get("width")), float(root.get("height"))) == pytest.approx((300, 300 * 2 / 11))


def test_arcs_and_splines():
    _, (stroke,) = paths(px.Arc((1, 0), (0, 1), (0, 0)).ToSVG())
    assert stroke.get("d") == "M 1 0 A 1 1 0 0 0 0 -1"
    _, (stroke,) = paths(px.Arc((1, 0), (0, 1), (0, 0), reverse=True).ToSVG())
    assert stroke.get("d").count("A") == 2
    assert " 0 0 1 " in stroke.get("d")
    _, (stroke,) = paths(px.Spline([(0, 0), (1, 0.3), (2, 0)]).ToSVG())
    assert stroke.get("d").count("M") == 1
    assert stroke.get("d").endswith("2 0")


def test_workplane_strokes_without_fill_and_a_compound_fills_its_faces():
    _, (fill, stroke) = paths(px.Compound([square(0, 0), px.Line((5, 5), (6, 6))]).ToSVG())
    assert fill.get("d").count("Z") == 1
    assert stroke.get("d").count("M") == 5


def test_the_preview_fills_faces_in_the_planex_face_colour():
    svg = square(0, 0)._repr_svg_()
    assert 'fill="#dde6f0"' in svg and "fill-opacity" not in svg


def test_max_preview_edges_is_a_setting():
    assert px.config.max_preview_edges == 5000


def test_large_or_empty_objects_fall_back_to_the_text_repr(monkeypatch):
    monkeypatch.setattr(px.config, "max_preview_edges", 3)
    assert square(0, 0)._repr_svg_() is None
    assert square(0, 0).ToSVG() is not None
    assert px.Compound()._repr_svg_() is None


def test_overlapping_parts_of_a_compound_are_filled_not_holed():
    discs = px.Compound([px.QuickSketch().Circle((0, 0), 1).Face(), px.QuickSketch().Circle((1, 0), 1).Face()])
    _, ps = paths(discs.ToSVG())
    fills = [p for p in ps if p.get("fill") != "none"]
    assert len(fills) == 2 and all(p.get("d").count("Z") == 1 for p in fills)
    holed = px.Compound([square(0, 0, 3) - square(1, 1, 1), square(1, 1, 1)])
    fills = [p for p in paths(holed.ToSVG())[1] if p.get("fill") != "none"]
    assert sorted(p.get("d").count("Z") for p in fills) == [1, 2]
