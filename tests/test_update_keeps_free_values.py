# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px


@pytest.mark.xfail(strict=False, reason="the slot turns by 1.4e-4 rad, the pull cannot take it back")
def test_a_new_slot_width_keeps_what_no_constraint_sets():
    sk = px.Sketch()
    sk.Move((0, 0), dir=(1, 0)).Slot(20, 6.6)
    sk["Slot.1.w"] = 9
    sk.Update()
    assert sk.status == "under"
    assert math.isclose(sk.Face().mass, 20 * 9 + math.pi * 4.5**2, rel_tol=1e-14)
    start, end = sk.Location("Slot.1.spine.start"), sk.Location("Slot.1.spine.end")
    assert math.dist(start[:2], (0, 0)) <= 1e-12 and math.dist(end[:2], (20, 0)) <= 1e-12
