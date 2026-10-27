# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""An exported wire runs as the wire does: from its first edge, in that edge's direction (a loft pairs the sections'
wires by their order). The rounded rectangle at z 82.5 of the guided-loft page started at its last arc."""
import math

import pytest

import planex as px


def first_edge_ends(occ_wire):
    e = occ_wire.edges[0]
    t0, t1 = e.parameter_interval
    a, b = e.Value(t0), e.Value(t1)
    return (a.x, a.y), (b.x, b.y)


def rounded_rectangle(w, h, r, z):
    sk = px.Sketch(px.Plane(px.XY, z))
    sk.Move((-w / 2 + r, -h / 2)).Line(w - 2 * r, 0, px.Horizontal, id="bottom")
    sk.Arc(r, 90, px.Tangent, id="a1")
    sk.Line(0, h - 2 * r, px.Vertical, px.Tangent, id="right")
    sk.Arc(r, 90, px.Tangent, id="a2")
    sk.Line(-(w - 2 * r), 0, px.Horizontal, px.Tangent, id="top")
    sk.Arc(r, 90, px.Tangent, id="a3")
    sk.Line(0, -(h - 2 * r), px.Vertical, px.Tangent, id="left")
    sk.Arc(r, 90, px.Tangent, id="a4")
    sk.Constrain("a4.end", "bottom.start", px.Coincident)
    sk.Constrain("a4", "bottom", px.Tangent)
    sk.Constrain("a1", px.Radius, r, id="r")
    for arc in ("a2", "a3", "a4"):
        sk.Constrain("a1", arc, px.Equal)
    sk.Constrain("left", "right", px.Distance, w, id="w")
    sk.Constrain("bottom", "top", px.Distance, h, id="ht")
    sk.Constrain("bottom.mid", px.V, px.Coincident)
    sk.Constrain("left.mid", px.H, px.Coincident)
    return sk


@pytest.mark.parametrize("w, h, r, z", [(108.06536948209212, 8.230298412909026, 2.6396956370157594, 82.5),
                                        (108.06536948209211, 8.230298412909026, 2.6396956370157545, 82.5),
                                        (40.0, 40.0, 14.0, 0.0)])
def test_a_sketch_loop_exports_from_its_first_element(w, h, r, z):
    sk = rounded_rectangle(w, h, r, z)
    first = sk.Wire().edges[0].edge
    assert math.dist(tuple(first.start), sk.Location("bottom.start")) < 1e-9
    start, end = first_edge_ends(sk.ToOCCWire())
    assert math.dist(start, sk.Location("bottom.start")) < 1e-9
    assert math.dist(end, sk.Location("bottom.end")) < 1e-9


@pytest.mark.parametrize("shift", range(4))
@pytest.mark.parametrize("reverse", [False, True])
def test_a_closed_wire_exports_from_its_first_edge(shift, reverse):
    corners = [(0, 0), (3, 0), (3, 2), (0, 2)]
    if reverse:
        corners.reverse()
    corners = corners[shift:] + corners[:shift]
    w = px.Wire([px.LineEdge(corners[k], corners[(k + 1) % 4]) for k in range(4)])
    start, end = first_edge_ends(w.ToOCCWire())
    assert math.dist(start, corners[0]) < 1e-12 and math.dist(end, corners[1]) < 1e-12
