# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""a circle trimmed where a tangent line ends on it: its new end is the line's end, so it stays there after an update
(a point held on the line could slide along it at the double root of the touch)"""
import math
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def bell_crank():
    crank = px.Sketch(auto_constraint=True)
    crank.Circle(16, id="hub")
    crank.Circle(10, center=(60, 0), id="boss_1")
    crank.Circle(9, center=(-12, 43), id="boss_2")
    crank.Line("hub.center", "boss_1.center", construction=True, id="axis_1")
    crank.Line("hub.center", "boss_2.center", construction=True, id="axis_2")
    crank.Line(tangent=("hub", "boss_1"), id="flank_1a")
    crank.Line(tangent=("hub", "boss_1"), reverse=True, id="flank_1b")
    crank.Line(tangent=("hub", "boss_2"), id="flank_2a")
    crank.Line(tangent=("hub", "boss_2"), reverse=True, id="flank_2b")
    crank.Constrain("hub.center", "origin", px.Coincident)
    crank.Constrain("axis_1", px.Horizontal)
    crank.Constrain("axis_1", px.Distance, 60, id="arm_1")
    crank.Constrain("axis_2", px.Distance, 45, id="arm_2")
    crank.Constrain("axis_1", "axis_2", px.Angle, 105, id="arm_angle")
    for c, r in (("hub", 16), ("boss_1", 10), ("boss_2", 9)):
        crank.Constrain(c, px.Radius, r)
    crank.Trim(("flank_1a", (2, 15.9)), ["flank_2a"])
    crank.Trim(("flank_2a", (14.6, 6.5)), ["flank_1a"])
    crank.Fillet("flank_1a.start", 8, id="notch")
    crank.Trim(("hub", (16, 0)), ["flank_1b", "flank_2b"])
    crank.Trim(("boss_1", (50, 0)), ["flank_1a", "flank_1b"])
    crank.Trim(("boss_2", (-9, 34)), ["flank_2a", "flank_2b"])
    return crank


@pytest.mark.parametrize("change", [{"arm_angle": 95}, {"arm_2": 50}])
def test_trimmed_arcs_keep_the_tangent_lines_ends_after_an_update(change):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        crank = bell_crank()
        crank.Update(change)
    ends = [f + x for f in ("flank_1a", "flank_1b", "flank_2a", "flank_2b") for x in (".start", ".end")]
    for arc in ("hub", "boss_1", "boss_2"):
        for end in (".start", ".end"):
            p = crank.Location(arc + end)
            assert min(math.dist(p, crank.Location(e)) for e in ends) == 0.0, arc + end
    face = crank.Face()
    assert len(face.wires) == 1 and crank.status == "full"
