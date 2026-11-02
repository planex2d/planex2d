# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def plate():
    return px.FromPolyline([(0, 0), (4, 0), (4, 2), (0, 2)], closed=True)


def test_a_number_is_the_arc_length_along_an_edge():
    d = px.Line((0, 0), (10, 0)).edges[0].Measure(at=2.5)
    assert d["point_hv"] == pytest.approx((2.5, 0), abs=TOL) and d["point"] == pytest.approx((2.5, 0, 0), abs=TOL)
    assert d["tangent_hv"] == pytest.approx((1, 0), abs=TOL) and d["normal_hv"] == pytest.approx((0, 1), abs=TOL)
    assert d["curvature"] == 0 and d["arc_length"] == pytest.approx(2.5) and d["distance"] == 0


def test_an_arc_has_its_curvature_signed_by_its_turn():
    d = px.Arc((2, 0), (0, 2), (0, 0)).edges[0].Measure(at=math.pi / 2)
    assert d["point_hv"] == pytest.approx((math.sqrt(2), math.sqrt(2)), abs=TOL)
    assert d["tangent_hv"] == pytest.approx((-math.sqrt(0.5), math.sqrt(0.5)), abs=TOL)
    # the normal is left of the tangent: toward the center of a left turn
    assert d["normal_hv"] == pytest.approx((-math.sqrt(0.5), -math.sqrt(0.5)), abs=TOL)
    assert d["curvature"] == pytest.approx(0.5)


def test_a_point_gives_its_nearest_place_and_the_distance():
    wire = plate()
    d = wire.Measure(at=(3, -1))
    assert d["point_hv"] == pytest.approx((3, 0), abs=TOL) and d["distance"] == pytest.approx(1)
    assert d["arc_length"] == pytest.approx(3) and d["edge"].id == wire.edges[0].id
    d = wire.Measure(at=5)
    assert d["point_hv"] == pytest.approx((4, 1), abs=TOL) and d["tangent_hv"] == pytest.approx((0, 1), abs=TOL)
    # equally near everywhere: the smallest arc length
    d = px.Circle((0, 0), 2).Measure(at=(0, 0))
    assert d["arc_length"] == 0 and d["distance"] == pytest.approx(2) and d["curvature"] == pytest.approx(0.5)


def test_a_spline_is_measured_on_its_exact_curve():
    spline = px.Spline([(0, 0), (3, 2), (6, -1), (9, 0)])
    length = spline.Measure()["length"]
    for s in (0.0, length / 3, length / 2, length):
        d = spline.Measure(at=s)
        assert d["arc_length"] == pytest.approx(s, abs=1e-9)
        back = spline.Measure(at=d["point_hv"])
        assert back["arc_length"] == pytest.approx(s, abs=1e-9) and back["distance"] == pytest.approx(0, abs=1e-9)


def test_a_reversed_wire_runs_the_other_way():
    wire = px.Wire([px.Line((0, 0), (5, 0)), px.Arc((5, 0), (5, 4), (5, 2))])
    length = wire.Measure()["length"]
    a, b = wire.Measure(at=6), wire.Reversed().Measure(at=length - 6)
    assert b["point_hv"] == pytest.approx(a["point_hv"], abs=TOL)
    assert b["tangent_hv"] == pytest.approx(tuple(-x for x in a["tangent_hv"]), abs=TOL)
    assert b["curvature"] == pytest.approx(-a["curvature"]) and a["curvature"] == pytest.approx(0.5)
    assert wire.Measure(at=0)["point_hv"] == pytest.approx((0, 0), abs=TOL)
    assert wire.Reverse() is wire and wire.Measure(at=0)["point_hv"] == pytest.approx((5, 4), abs=TOL)


def test_a_sketch_chain_and_a_selection_are_measured_along():
    sk = px.QuickSketch().Line(10).Arc(2, 180).Line(10).Arc(2, 180)
    d = sk.Measure("Chain.1", at=12.5)
    assert d["edge"] == "Arc.1" and d["arc_length"] == pytest.approx(12.5) and d["curvature"] == pytest.approx(0.5)
    assert sk.Measure("Line.2", at=1)["edge"] == "Line.2"
    face = px.Face([plate()])
    for corner in ((0, 0), (4, 0), (4, 2), (0, 2)):
        face.Fillet(corner, 0.5)
    edges = face.edges(near=(2, 0), propagate=px.Tangent)
    d = edges.Measure(at=4)
    assert d["point_hv"] == pytest.approx((4, 0.5 + 4 - 3 - math.pi / 4), abs=TOL) and any(d["edge"] is e for e in edges)


def test_at_takes_one_chain_and_an_arc_length_within_it():
    with pytest.raises(ValueError, match=r"at=99.*an arc length from 0 to the length 12"):
        plate().Measure(at=99)
    with pytest.raises(TypeError, match=r"at= measures a place along this, to= the distance to a target"):
        plate().Measure(at=1, to=(0, 0))
    two = px.QuickSketch().Move((0, 0)).Line((1, 0)).Move((2, 0)).Line((3, 0))
    with pytest.raises(ValueError, match=r"Measure\(at=\.\.\.\): the edges form 2 chains.*wire\.Wires\(\) gives the chains"):
        two.Measure(at=0.5)
