# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""px.Fix and rigid groups take any number of points: the solver evaluates their rows point by point, each with only
its own unknowns (and the group's place)"""
import math

import pytest

import planex as px


def wave(n):
    return [(10 * k / n, math.sin(6 * k / n)) for k in range(1, n + 1)]


@pytest.mark.parametrize("n", [30, 90, 400])
def test_a_fix_holds_a_spline_of_any_number_of_points(n):
    sk = px.Sketch()
    sk.Move((0, 0)).Spline(wave(n), id="s")
    sk.Constrain("s", px.Fix)
    assert (sk.status, sk.dof) == ("full", 0)


def test_a_rigid_group_of_a_long_spline_turns_as_one():
    sk = px.Sketch()
    sk.Move((0, 0)).Spline(wave(150), id="s")
    sk.RigidGroup("s", id="body")
    assert (sk.status, sk.dof) == ("under", 3)
    chord = math.dist(sk.Location("s.start"), sk.Location("s.end"))
    sk.Constrain("s.start", px.Fix)
    sk.Constrain("s.end", px.H, px.Distance, 2, id="lift")
    assert (sk.status, sk.dof) == ("full", 0)
    assert abs(sk.Location("s.end")[1]) == pytest.approx(2, abs=1e-10)
    assert math.dist(sk.Location("s.start"), sk.Location("s.end")) == pytest.approx(chord, abs=1e-10)
