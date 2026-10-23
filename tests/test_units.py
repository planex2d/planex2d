# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def test_lengths_are_factors_to_metres():
    assert (px.m, px.cm, px.mm, px.um) == (1, 0.01, 0.001, 1e-6)
    assert 1000 * px.mil == pytest.approx(px.inch) and px.inch == pytest.approx(25.4 * px.mm)
    plate = px.QuickSketch().Rectangle(40 * px.mm, 20 * px.mm).Face()
    assert plate.mass / px.mm**2 == pytest.approx(800)
    assert plate.mass / px.cm**2 == pytest.approx(8)
