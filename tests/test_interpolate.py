# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

PTS = [(0, 0), (1, 0.8), (2.3, 0.5), (3, -0.4), (4.2, 0.2)]


def at(points, u, k=0, tangents=None, closed=False):
    return px._planex._InterpolatedAt(points, tangents or {}, closed, u, k)


def test_the_curve_runs_through_its_points():
    f = px._planex._Interpolate(PTS)
    assert f["degree"] == 3 and len(f["poles"]) == len(PTS)
    assert f["params"][1] == pytest.approx(math.dist(PTS[0], PTS[1]))
    for p, u in zip(PTS, f["params"]):
        assert at(PTS, u) == pytest.approx(p, abs=1e-12)


def test_given_tangents_are_the_derivatives_there():
    tangents = {0: (1, 0), 2: (0.6, -0.8), 4: (0, 1)}
    f = px._planex._Interpolate(PTS, tangents)
    assert len(f["poles"]) == len(PTS) + 3
    for p, u in zip(PTS, f["params"]):
        assert at(PTS, u, tangents=tangents) == pytest.approx(p, abs=1e-12)
    for k, t in tangents.items():
        assert at(PTS, f["params"][k], 1, tangents) == pytest.approx(t, abs=1e-12)


def test_a_closed_curve_meets_itself_smoothly():
    f = px._planex._Interpolate(PTS, {}, True)
    end = f["params"][-1]
    assert len(f["params"]) == len(PTS) + 1
    for k in range(3):
        assert at(PTS, 0, k, closed=True) == pytest.approx(at(PTS, end, k, closed=True), abs=1e-11)
    assert at(PTS, end, closed=True) == pytest.approx(PTS[0], abs=1e-12)


def test_points_on_a_line_give_the_line():
    line = [(0, 0), (1, 0.5), (3, 1.5), (3.4, 1.7)]
    f = px._planex._Interpolate(line)
    for u in (0.3, 1.7, 2.9, 3.6):
        x, y = at(line, u)
        assert y == pytest.approx(x / 2, abs=1e-12)


def test_few_points_lower_the_degree():
    assert px._planex._Interpolate([(0, 0), (1, 1)])["degree"] == 1
    assert px._planex._Interpolate([(0, 0), (1, 1), (2, 0)])["degree"] == 2
    assert px._planex._Interpolate([(0, 0), (1, 1)], {0: (1, 0)})["degree"] == 2


def test_interpolation_errors():
    with pytest.raises(ValueError, match="two following points at one place"):
        px._planex._Interpolate([(0, 0), (0, 0), (1, 1)])
    with pytest.raises(ValueError, match="three points"):
        px._planex._Interpolate([(0, 0), (1, 1)], {}, True)
