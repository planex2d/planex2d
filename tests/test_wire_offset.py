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
