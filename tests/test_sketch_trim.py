# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def length(sketch, name):
    return sketch.Measure(name)["length"]


def overshoot():
    # a wall b, then a drawn past it: the pen stays at the end of the overshoot
    return px.Move((3, -1)).Line((3, 3), id="b", construction=True).Move((0, 1)).Line((5, 1), id="a")


def test_trim_removes_the_piece_nearest_the_pen():
    assert length(overshoot().Trim("a", "b"), "a") == pytest.approx(3)
    assert length(overshoot().Trim("a", "b", reverse=True), "a") == pytest.approx(2)
    assert length(overshoot().Trim(("a", (0.5, 1)), "b"), "a") == pytest.approx(2)
    s = overshoot().Trim("a")
    assert length(s, "a") == pytest.approx(3) and s.Measure("a")["bounding_box"] == ((0, 1), (3, 1))
    assert length(s, "b") == pytest.approx(4)


def test_quick_trim_deletes_an_edge_nothing_cuts():
    s = px.Move((0, 0)).Line((1, 0)).Move((5, 5)).Line((6, 5), id="lone")
    s.Trim("lone")
    assert len(s.Construction().edges) == 1 and len(s.Wires()) == 1
    with pytest.raises(ValueError, match="it does not meet the cutter"):
        px.Move((0, 0)).Line((1, 0), id="a").Move((5, 0)).Line((5, 1), id="b").Trim("a", "b")


def test_trimming_inside_wires():
    s = px.Move((0, 0)).Line((2, 0)).Line((2, 2), id="side").Line((0, 2)).Close()
    s.Move((1, 1)).Line((3, 1), id="cut")
    whole = px.Move((0, 0)).Line((2, 0)).Line((2, 2), id="side").Line((0, 2)).Close().Trim("side")
    assert len(whole.Construction().edges) == 1 and len(whole.Wires()) == 1
    assert s.Copy().Trim(("cut", (2.5, 1)), "side").Measure("cut")["length"] == pytest.approx(1)
    opened = s.Copy().Trim(("side", (2, 1.5)), "cut")
    assert opened.Measure("side")["bounding_box"] == ((2, 0), (2, 1))
    line = px.Move((0, 0)).Line((4, 0), id="a").Move((1, -1)).Line((1, 1), id="b1").Move((3, -1)).Line((3, 1), id="b2")
    middle = line.Trim(("a", (2, 0)), ["b1", "b2"])
    assert middle.wires["a"] == ["a.1", "a.trimmed.1", "a.2"] and middle.Measure("a")["length"] == pytest.approx(4)
    assert middle.Measure("a.1")["length"] == middle.Measure("a.2")["length"] == pytest.approx(1)
    assert len(middle.Construction().edges) == 1


def test_split_keeps_every_piece_and_returns_the_sketch():
    s = overshoot()
    assert s.Split("a", "b") is s
    assert s.Measure()["edges"] == 2 and s.Measure()["length"] == pytest.approx(5)
    c = px.QuickSketch().Circle((0, 0), 1, id="c").Move((-2, 0)).Line((2, 0), id="d", construction=True)
    c.Trim(("c", (0, -1)), "d")
    m = c.Measure("c")
    assert m["type"] == "arc" and m["angle"] == pytest.approx(180) and m["bounding_box"][0][1] == pytest.approx(0)
    with pytest.raises(ValueError, match="no element 'missing'"):
        c.Trim("missing")
