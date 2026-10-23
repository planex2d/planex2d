# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px
from netgen.occ import Box, Cylinder, Pnt, Z


def _plate():
    sk = px.Sketch()
    sk.Rectangle(40, 20, id="plate")
    sk.Constrain("plate.Line.1.start", px.Fix)
    sk.Constrain("plate.Line.1", px.Horizontal)
    sk.Constrain("plate.Line.1", px.Distance, 40, id="w")
    sk.Constrain("plate.Line.2", px.Distance, 20, id="h")
    return sk


def _drawn(pen, **kw):
    sk = px.Sketch()
    sk.Move(0, 0)
    sk.Line(10, 0, id="L1")
    sk.Move(*pen)
    sk.Line(4, parallel="L1", id="L2", **kw)
    return sk.Edge("L2").edges[0]


@pytest.mark.parametrize("pen, d, y", [((5, 3), 2, 2), ((5, 3), -2, -2), ((5, -3), 2, -2), ((5, -3), -2, 2), ((5, 0), 2, 2), ((5, 0), -2, -2)])
def test_line_distance_side_from_the_pen(pen, d, y):
    e = _drawn(pen, distance=d)
    assert [e.start[1], e.end[1]] == pytest.approx([y, y], abs=1e-12)
    assert abs(e.end[0] - e.start[0]) == pytest.approx(4)


def test_line_distance_without_end_raises():
    sk = px.Sketch()
    sk.Move(0, 0)
    sk.Line(10, 0, id="L1")
    sk.Move(5, 3)
    with pytest.raises(ValueError, match="until=.*Offset|Offset.*until="):
        sk.Line(parallel="L1", distance=2)
    assert list(sk.elements["Line"]) == ["L1"]


def test_config_branch_check_min_length():
    assert px.config.branch_check_min_length == 1e-6
    with pytest.raises(AttributeError):
        px.config.branch_check_min_lenght = 1e-3


def test_expression_contradiction_is_suspended():
    sk = _plate()
    with pytest.warns(px.PlanexWarning):
        sk.Expression("w + h = 70")
    assert sk.status == "over"


def test_removing_the_partner_element_retries():
    sk = _plate()
    with pytest.warns(px.PlanexWarning):
        sk.Constrain("plate.Line.3", px.Distance, 41, id="w2")
    assert sk.status == "over"
    sk.Remove("plate.Line.1")
    assert sk.status != "over"


def test_activation_warns_redundant():
    sk = _plate()
    with pytest.warns(px.PlanexWarning):
        sk.Constrain("plate.Line.3", px.Distance, 41, id="w2")
    with pytest.warns(px.PlanexWarning, match="redundant"):
        sk.Update({"w": 41})
    assert sk.status != "over"


def test_projected_hole_is_one_circle():
    solid = Box(Pnt(0, 0, 0), Pnt(40, 20, 5)) - Cylinder(Pnt(20, 10, -1), Z, r=3, h=7)
    sk = px.Sketch()
    sk.Project(solid)
    assert len(sk.elements["Circle"]) == 1
    assert "Arc" not in sk.elements
    assert [p for p in sk.elements["Point"] if "Center" in p] == ["Projection.1.Center.1"]
    c = sk.Edge(sk.elements["Circle"][0]).circles[0]
    assert c.radius == pytest.approx(3, abs=1e-12)
    assert [c.center[0], c.center[1]] == pytest.approx([20, 10], abs=1e-12)
