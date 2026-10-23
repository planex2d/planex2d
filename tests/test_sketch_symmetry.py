# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from helpers import point


def on_axis(sketch, id, *points):
    g = sketch._Geometry(id)
    t = math.radians(g["normal_angle"])
    return all(abs(math.cos(t) * x + math.sin(t) * y - g["d"]) < 1e-12 for x, y in points)


def test_symmetry_line_of_points_lines_and_circles():
    s = px.Move((0, 0))
    s.Vertex("p", (0, 0)).Vertex("q", (2, 0))
    s.SymmetryLine("p", "q", id="s")
    assert on_axis(s, "s", (1, -1), (1, 1))
    assert s.Wires() == [] and point(s.pen.position) == (0, 0)
    par = px.Move((0, 0)).Line((4, 0), id="a").Move((1, 2)).Line((5, 2), id="b").SymmetryLine("a", "b", id="m")
    assert on_axis(par, "m", (0, 1), (5, 1))
    v = px.Move((0, 0)).Line((2, 2), id="a").Move((0, 0)).Line((2, -2), id="b")
    assert on_axis(v.Copy().SymmetryLine("a", "b", id="m"), "m", (0, 0), (2, 0))
    assert on_axis(v.Copy().SymmetryLine("a", "b", id="m", reverse=True), "m", (0, -2), (0, 2))
    c = px.QuickSketch().Circle(0, 0, 1, id="c1").Circle(4, 0, 1, id="c2").SymmetryLine("c1", "c2", id="m")
    assert on_axis(c, "m", (2, -1), (2, 1))


def test_symmetry_line_errors():
    s = px.QuickSketch().Circle(0, 0, 1, id="c1").Circle(4, 0, 2, id="c2").Move((0, 0)).Line((1, 0), id="l")
    with pytest.raises(ValueError, match="two points, two lines"):
        s.SymmetryLine("l", "c1")
    with pytest.warns(px.PlanexWarning, match="only one solution"):
        px.Move((0, 0)).Line((4, 0), id="a").Move((0, 2)).Line((4, 2), id="b").SymmetryLine("a", "b", reverse=True)


def half():
    s = px.Move((0, -1)).Line((0, 3), id="axis", construction=True)
    return s.Move((0, 0)).Line((2, 0), id="bottom").Line((2, 1), id="side").Line((0, 1), id="top")


def test_add_mirrored_completes_a_symmetric_face():
    s = half().Mirrored(["bottom", "side", "top"], "axis")
    assert s.Face().mass == pytest.approx(4)
    assert point(s.pen.position) == (0, 1)
