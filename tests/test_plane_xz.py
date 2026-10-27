# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def test_xz_is_the_front_plane():
    p = px.XZ
    assert (p.origin, p.h, p.v, p.normal) == ((0, 0, 0), (1, 0, 0), (0, 0, 1), (0, -1, 0))
    assert p.ToXYZ((2, 3)) == pytest.approx((2, 0, 3), abs=1e-15)


def test_a_front_view_sketched_on_xz_reads_x_and_z():
    f = px.QuickSketch(px.XZ).Move((0, 0)).Rectangle(4, 2).Face()
    assert f.center == pytest.approx((2, 0, 1), abs=1e-14)
