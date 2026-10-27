# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import pytest

import planex as px
from planex import _svg

pytestmark = pytest.mark.constraints


def chain_plate():
    """the chain link plate exercise: two eyes, two concave flanks tangent to both"""
    plate = px.Sketch(auto_constraint=True)
    plate.Circle(15, id="left_eye")
    plate.Circle(15, center=(50, 0), id="right_eye")
    plate.Circle(40, id="upper_flank", tangent=("left_eye", "right_eye"))
    plate.Circle(40, id="lower_flank", tangent=("left_eye", "right_eye"), reverse=True)
    for element, point in (("left_eye", (20, 0)), ("right_eye", (30, 0)), ("upper_flank", (25, 100)), ("lower_flank", (25, -100))):
        plate.Trim((element, point))
    plate.Constrain("left_eye.center", "origin", px.Coincident)
    plate.Constrain("right_eye.center", "H", px.Coincident)
    plate.Constrain("left_eye.center", "right_eye.center", px.Distance, 50, id="pitch")
    plate.Constrain("left_eye", px.Radius, 15, id="eye_r")
    plate.Constrain("upper_flank", px.Radius, 40, id="flank_r")
    plate.Constrain("lower_flank", "upper_flank", px.Equal)
    return plate


def test_a_concave_arcs_radius_stays_on_its_centres_side():
    plate = chain_plate()
    everything = {g["id"]: g for g in plate._Drawing()}
    drafted, _ = _svg._drafting(plate, everything, 4.0, 12)
    (anchor, _, text), = drafted["flank_r"].texts
    # the flank's arc is the plate's upper edge at v = 10, its centre at (25, 50): outside it would run through the part
    assert "R40" in text and anchor[1] > 10
    (eye, _, _), = drafted["eye_r"].texts
    assert eye[0] < -15
