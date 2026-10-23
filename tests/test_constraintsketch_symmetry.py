# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-13


def half():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(3.8, 0.2, px.Horizontal, id="bottom").Line(0.1, 2.9, px.Perpendicular, id="side") \
        .Line(-1.4, 0.1, px.Perpendicular, id="top").Line(-0.1, -0.9, px.Perpendicular).Line(-2.4, 0.1, px.Perpendicular, id="notch")
    sk.Constrain("notch.end", px.V, px.Coincident)
    return sk


def dimension(sk):
    sk.Constrain("bottom", px.Distance, 4, id="w")
    sk.Constrain("bottom", "top", px.Distance, 3, id="h")
    sk.Constrain("notch", "top", px.Distance, 1)
    sk.Constrain("top", px.Distance, 1.5)


def test_a_chain_is_a_wire_that_the_copy_calls_take():
    sk = half()
    assert sk.wires == {"Chain.1": ["bottom", "side", "top", "Line.1", "notch"]}
    drawn = lambda: sum(len(ids) for kind, ids in sk.elements.items() if kind in ("Point", "Line"))
    before = drawn()
    sk.Mirrored("Chain.1", px.V)
    assert drawn() - before == 11 and sk.elements.mirrors == ["Mirrored.1"]
    assert sk.wires["Mirrored.1.Chain.1"] == [f"Mirrored.1.{e}" for e in sk.wires["Chain.1"]]
    sk.Move((9, 9)).Line(1, 0)
    assert sk.wires["Chain.2"] == ["Line.2"]


def test_a_mirrored_half_is_one_symmetric_face_that_follows():
    sk = half()
    sk.Mirrored("Chain.1", px.V)
    dimension(sk)
    assert (sk.status, sk.dof) == ("full", 0)
    assert sk.Face().mass == pytest.approx(8 * 3 - 5 * 1, rel=1e-13)
    sk.Update({"w": 5})
    assert sk.Face().mass == pytest.approx(10 * 3 - 7 * 1, rel=1e-13)
    for k in range(1, 7):
        (x, y), (mx, my) = sk._Location(f"Point.{k}"), sk._Location(f"Mirrored.1.Point.{k}")
        assert (mx, my) == pytest.approx((-x, y), abs=TOL)


def test_mirrored_copies_keep_the_properties_and_get_new_ids():
    sk = px.Sketch()
    sk.Move((1, 0)).Line(2, 1, id="edge", name="wall", maxh=0.1)
    sk.Mirrored("edge", px.V)
    copy = sk.elements["Line"][-1]
    assert copy == "Mirrored.1.edge"
    assert [e.name for e in sk.Wires()[1].edges] == ["wall"]
    assert sk._Geometry(copy)["start"] == pytest.approx((-1, 0), abs=TOL)


def test_without_symmetry_the_copies_are_free():
    sk = half()
    sk.Mirrored("Chain.1", px.V, auto_constraint=False)
    assert "Symmetric" not in sk.constraints
    dimension(sk)
    sk.Update({"w": 5})
    assert sk._Location("Mirrored.1.Point.2") == pytest.approx((-3.8, 0.2), abs=0.5)
    assert sk.status == "under"


def test_mirrored_arcs_turn_the_other_way_and_circles_keep_their_radius():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(3, 0, px.Horizontal, id="bottom").Arc(1, 90, px.Tangent, id="fillet") \
        .Line(0, 2, px.Vertical, px.Tangent, id="side").Line(-4, 0, px.Horizontal, id="top")
    sk.Constrain("top.end", px.V, px.Coincident)
    profile = ["Chain.1"]
    sk.Circle(1.5, 1.5, 0.4, id="hole")
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Mirrored(profile + ["hole"], px.V)
    assert (sk.status, sk._Geometry("Mirrored.1.fillet")["ccw"]) == ("under", False)
    assert sk.Face().mass == pytest.approx(8 * 3 - 2 * (1 - math.pi / 4) - 2 * math.pi * 0.4 ** 2, rel=1e-12)
    sk.Constrain("hole", px.Radius, 0.5)
    assert sk._Geometry("Mirrored.1.hole")["r"] == pytest.approx(0.5, abs=TOL)
    assert sk._Geometry("Mirrored.1.hole")["center"] == pytest.approx((-1.5, 1.5), abs=TOL)


def test_a_symmetry_line_of_two_crossing_lines_stays_their_bisector():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, px.Fix, id="a").Move((0, 0)).Line(3, 3, id="b")
    sk.SymmetryLine("a", "b", id="axis")
    assert sk._Geometry("axis")["normal_angle"] % 180 == pytest.approx(112.5, abs=1e-12)
    assert repr(sk.constraints["SymmetricCarriers"]["SymmetricCarriers.1"]) == '"a", "b", "axis", px.SymmetricCarriers'
    sk.Constrain("b", "a", px.Angle, 60)
    g = sk._Geometry("axis")
    t = math.radians(g["normal_angle"])
    n = (math.cos(t), math.sin(t))
    b = sk._Geometry("b")
    u = (b["end"][0] - b["start"][0], b["end"][1] - b["start"][1])
    for p in [(0, 0), (4, 0)]:
        f = n[0] * p[0] + n[1] * p[1] - g["d"]
        m = (p[0] - 2 * f * n[0], p[1] - 2 * f * n[1])
        assert abs(u[0] * (m[1] - b["start"][1]) - u[1] * (m[0] - b["start"][0])) / math.hypot(*u) < 1e-12


def test_a_symmetry_line_of_points_and_of_circles():
    sk = px.Sketch()
    sk.Move((1, 0)).Line(1, 1, id="l")
    sk.SymmetryLine("l.start", "l.end", id="bisector")
    sk.Circle(5, 0, 1, id="c1")
    sk.Circle(8, 2, 1, id="c2")
    sk.SymmetryLine("c1", "c2", id="between")
    assert repr(sk.constraints["Symmetric"]["Symmetric.2"]) == '"c1.center", "c2.center", "between", px.Symmetric'
    assert repr(sk.constraints["Equal"]["Equal.1"]) == '"c1", "c2", px.Equal'
    sk.Constrain("l.end", px.Fix)
    sk.Constrain("l.start", "l.end", px.Distance, 3)
    (x0, y0), (x1, y1) = sk._Location("l.start"), sk._Location("l.end")
    g = sk._Geometry("bisector")
    t = math.radians(g["normal_angle"])
    assert math.cos(t) * (x0 + x1) / 2 + math.sin(t) * (y0 + y1) / 2 == pytest.approx(g["d"], abs=TOL)
    with pytest.raises(ValueError, match="two points, two lines or two circles"):
        sk.SymmetryLine("l", "c1")
    assert "SymmetryLine.1" not in sk.elements["Axis"]


def test_symmetry_lines_in_the_plots():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, px.Fix, id="a").Move((0, 0)).Line(3, 3, id="b")
    sk.SymmetryLine("a", "b", id="axis")
    svg = px.ToSVG(sk, show_constraints=True)
    assert 'stroke-dasharray="9 3 2 3"' in svg and "<title>axis</title>" in svg
    fig = sk.Plot(backend="plotly")
    assert [t.meta for t in fig.data].count("unbounded") == 1
    assert fig.layout.xaxis.range[1] - fig.layout.xaxis.range[0] < 10


def test_two_arcs_made_symmetric_are_not_over_constrained():
    sk = px.Sketch()
    sk.Circle(0, 0, 1, px.Fix, id="hub")
    sk.Move((1, 0.5)).Arc(0.8, 100, id="a")
    sk.Move((-1.1, 0.4)).Arc(0.7, -95, id="b")
    sk.Constrain("a", px.Fix)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Constrain("a", "b", px.V, px.Symmetric)
    assert sk.dof == 0
    assert sk._Geometry("b")["center"] == pytest.approx((-sk._Geometry("a")["center"][0], sk._Geometry("a")["center"][1]), abs=TOL)


def test_a_symmetry_line_of_arcs_mirrors_their_circles():
    sk = px.Sketch()
    sk.Move((1, 0)).Arc(1, 60, id="a")
    sk.Move((-1.2, 0.1)).Arc(0.8, -70, id="b")
    sk.SymmetryLine("a", "b", id="axis")
    sk.Constrain("a", px.Fix)
    ga, gb, g = sk.Geometry("a"), sk.Geometry("b"), sk._Geometry("axis")
    assert gb["r"] == pytest.approx(ga["r"], abs=TOL)
    t = math.radians(g["normal_angle"])
    n = (math.cos(t), math.sin(t))
    f = n[0] * ga["center"][0] + n[1] * ga["center"][1] - g["d"]
    assert gb["center"] == pytest.approx((ga["center"][0] - 2 * f * n[0], ga["center"][1] - 2 * f * n[1]), abs=TOL)
    # the ends are free, each slides along its circle; the axis turns about a's centre with b's
    assert sk.dof == 4 and gb["sweep"] < 0
