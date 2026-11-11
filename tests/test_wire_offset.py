# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

TOL = 1e-9


def ends(w):
    return [(tuple(round(c, 9) for c in e.start), tuple(round(c, 9) for c in e.end)) for e in w.edges]


def test_the_offset_of_an_open_wire_is_its_band_s_boundary_by_default():
    band = px.Line((0, 0), (10, 0)).Offset(1)
    assert isinstance(band, px.Wire) and band.IsClosed() and len(band.edges) == 4
    assert band.Face().Measure()["area"] == pytest.approx(20 + math.pi)


def test_one_side_of_an_open_wire_runs_along_it_without_ends():
    line = px.Line((0, 0), (10, 0))
    assert ends(line.Offset(1, both_sides=False)) == [((0, 1), (10, 1))]
    assert ends(line.Offset(-1, both_sides=False)) == [((0, -1), (10, -1))]
    ell = px.FromPolyline([(0, 0), (10, 0), (10, 10)])
    # inside the corner the two offsets meet, outside it they go round
    assert ends(ell.Offset(1, both_sides=False)) == [((0, 1), (9, 1)), ((9, 1), (9, 10))]
    right = ell.Offset(-1, both_sides=False)
    assert ends(right) == [((0, -1), (10, -1)), ((10, -1), (11, 0)), ((11, 0), (11, 10))] and len(right.arcs) == 1
    assert ends(ell.Offset(-1, both_sides=False, corners=px.Sharp)) == [((0, -1), (11, -1)), ((11, -1), (11, 10))]
    spline = px.Spline([(0, 0), (3, 2), (6, -1), (9, 0)]).Offset(0.5, both_sides=False)
    assert len(spline.edges) == 1 and not spline.IsClosed()


def test_a_closed_wire_takes_one_side_by_default():
    rect = px.FromPolyline([(0, 0), (4, 0), (4, 2), (0, 2)], closed=True)
    assert rect.Offset(1).Face().Measure()["area"] == pytest.approx(8 + 12 + math.pi)
    assert rect.Offset(-0.5).Face().Measure()["area"] == pytest.approx(3)
    ring = rect.Offset(0.5, both_sides=True)
    assert px.Shape([ring]).Measure()["area"] == pytest.approx(8 + 6 + math.pi / 4 - 3)
    circle = px.Circle((0, 0), 2)
    assert circle.Offset(1).Measure()["length"] == pytest.approx(6 * math.pi)
    assert len(circle.Offset(-2.5).edges) == 0


def test_the_offset_takes_a_distance_and_a_side_switch():
    with pytest.raises(ValueError, match="nonzero and finite"):
        px.Line((0, 0), (1, 0)).Offset(0)
    with pytest.raises(TypeError, match=r"Offset\(both_sides=1\): True or False"):
        px.Line((0, 0), (1, 0)).Offset(1, both_sides=1)


def test_the_offset_of_several_wires_is_the_boundary_of_the_region_their_bands_cover():
    cross = px.Offset([px.Line((0, 0), (10, 0)), px.Line((5, -5), (5, 5))], 1.0)
    assert all(isinstance(w, px.Wire) for w in cross)
    assert px.Shape(cross).mass == pytest.approx(2 * (20 + math.pi) - 4)
    assert px.Face(cross).mass == pytest.approx(2 * (20 + math.pi) - 4)
    sk = px.Sketch(auto_constraint=False)
    sk.Move((0, 0)).Line((0, 30)).Line((12, 30)).Arc((12, 15)).Line((0, 15)).Line((14, 0))
    stroke = px.Face(px.Offset(sk.Wires(), 1.0))
    assert stroke.mass == pytest.approx(px.Unify([w.Offset(1.0, both_sides=True).Face() for w in sk.Wires()]).mass)
    assert stroke.mass == pytest.approx(193.24921, rel=1e-8)
    assert len(stroke.wires) == 2


def test_the_offset_of_several_wires_takes_both_sides_of_closed_ones_too():
    ring = px.Offset([px.Circle((0, 0), 2)], 0.5)
    assert px.Shape(ring).mass == pytest.approx(math.pi * (2.5 ** 2 - 1.5 ** 2))
    rails = px.Offset([px.Line((0, 0), (10, 0)), px.Circle((20, 0), 2)], 1.0, both_sides=False)
    assert sorted(round(sum(e.mass for e in w.edges), 9) for w in rails) == [10.0, round(2 * math.pi * 3, 9)]


def test_the_offset_of_several_faces_is_their_offsets_united():
    a, b = px.Circle((0, 0), 1).Face(), px.Circle((1.5, 0), 1).Face()
    grown = px.Offset([a, b], 0.5)
    assert isinstance(grown, px.Shape)
    assert grown.mass == pytest.approx(px.Unify([a.Offset(0.5, inplace=False), b.Offset(0.5, inplace=False)]).mass)
    with pytest.raises(ValueError, match="one side"):
        px.Offset([a, b], 0.5, both_sides=True)
    with pytest.raises(TypeError, match="wires and faces"):
        px.Offset([a, px.Line((0, 0), (1, 0))], 0.5)
    with pytest.raises(ValueError, match="nonzero and finite"):
        px.Offset([px.Line((0, 0), (1, 0))], 0)
    assert px.Offset([], 1.0) == []
