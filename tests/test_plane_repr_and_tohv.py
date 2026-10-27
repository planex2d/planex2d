# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def test_the_repr_is_the_call_that_rebuilds_the_plane():
    assert repr(px.Plane(px.XY, 0.015)) == "Plane(origin=(0, 0, 0.015), normal=(0, 0, 1), axis=(1, 0, 0))"
    p = px.Plane(origin=(0.1, -2, 3), normal=(1, 2, 2), axis=(0, 1, -1))
    q = eval(repr(p), {"Plane": px.Plane})
    for a, b in ((p.origin, q.origin), (p.normal, q.normal), (p.h, q.h)):
        assert a == pytest.approx(b, abs=1e-15)


def test_a_plane_takes_3d_points_into_its_coordinates():
    plane = px.Plane(origin=(0, 0, 1), normal=(0, 0, 1))
    assert plane.ToHV((1, 2, 1)) == pytest.approx((1, 2))
    assert plane.ToHV((1, 2, 5)) == pytest.approx((1, 2))
    assert plane.ToHV([(0, 0, 1), (3, -1, 1)]) == [pytest.approx((0, 0)), pytest.approx((3, -1))]
    tilted = px.Plane(origin=(1, 2, 3), normal=(1, 1, 0))
    hv = (0.7, -1.3)
    assert tilted.ToHV(tilted.ToXYZ(hv)) == pytest.approx(hv, abs=1e-14)


@pytest.mark.occ
def test_tohv_takes_a_netgen_point_and_lists_of_them():
    import netgen.occ as occ
    f = occ.Box((0, 0, 0), (10, 10, 10)).faces.Max(occ.Z)
    sk = px.Sketch(f)
    assert sk.ToHV(f.center) == pytest.approx(sk.ToHV(tuple(f.center)), abs=1e-12)
    assert px.Plane(f).ToHV(f.center) == pytest.approx(sk.ToHV(tuple(f.center)), abs=1e-12)
    assert sk.ToHV([f.center, (0, 0, 10)]) == [pytest.approx(sk.ToHV(f.center)), pytest.approx(sk.ToHV((0, 0, 10)))]
