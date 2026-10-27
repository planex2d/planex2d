# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_trim_of_a_chain_trims_the_run_nearest_the_point(make):
    sk = make().Circle(10, id="bore")
    sk.Move((-3, 0)).Line((3, 0)).Line((3, 12.8)).Line((-3, 12.8)).Close()
    sk.Trim(("Chain.-1", (0, 0)), "bore")
    inside = math.sqrt(100 - 9)
    assert sk.Construction().mass == pytest.approx(6 + 2 * inside, abs=1e-9)
    assert sum(w.mass for w in sk.Wires()) == pytest.approx(6 + 2 * (12.8 - inside) + 2 * math.pi * 10, abs=1e-9)


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_trim_of_a_rectangle_trims_across_its_lines(make):
    sk = make().Circle(10, id="bore")
    sk.Move((-3, 0)).Rectangle(6, 12.8, id="keyway")
    sk.Trim(("keyway", (0, 0)), "bore")
    inside = math.sqrt(100 - 9)
    assert sk.Construction().mass == pytest.approx(6 + 2 * inside, abs=1e-9)


def test_trim_of_a_chain_the_cutter_misses_raises_and_changes_nothing():
    sk = px.QuickSketch().Circle(1, id="bore")
    sk.Move((-3, 5)).Line((3, 5)).Line((3, 8)).Line((-3, 8)).Close()
    with pytest.raises(ValueError, match="the cutters do not meet it"):
        sk.Trim(("Chain.-1", (0, 5)), "bore")
    assert len(sk.Construction().edges) == 0


def test_an_unknown_id_still_gets_the_suggestion():
    sk = px.QuickSketch().Move((0, 0)).Line((1, 0), id="edge")
    with pytest.raises(ValueError, match=r"no element 'edgf' \(did you mean 'edge'\?\)"):
        sk.Trim("edgf")
