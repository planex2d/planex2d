# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import random

import pytest
from netgen import occ

import planex as px

# a few roundings of coordinates up to 10
TOL = 1e-12


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def random_plane(rng):
    o = tuple(rng.uniform(-10, 10) for _ in range(3))
    n = unit([rng.gauss(0, 1) for _ in range(3)])
    return px.Plane(origin=o, normal=n, axis=tuple(rng.gauss(0, 1) for _ in range(3)))


def frame(p):
    """the plane's origin, origin + u and origin + normal as OCC vertices"""
    o, u, n = p.origin, p.h, p.normal
    return [occ.Vertex(occ.Pnt(*q)) for q in (o, tuple(a + b for a, b in zip(o, u)), tuple(a + b for a, b in zip(o, n)))]


def point(shape):
    v = shape.vertices[0]
    return (v.p.x, v.p.y, v.p.z)


def compare(p, vertices):
    o, ou, on = (point(v) for v in vertices)
    assert p.origin == pytest.approx(o, abs=TOL)
    assert p.h == pytest.approx(tuple(b - a for a, b in zip(o, ou)), abs=TOL)
    assert p.normal == pytest.approx(tuple(b - a for a, b in zip(o, on)), abs=TOL)


def test_a_plane_is_right_handed_and_its_u_lies_in_it():
    a = px.Plane(origin=(0, 0, 2), normal=(0, 0, 3), axis=(1, 1, 5))
    assert a.normal == (0, 0, 1) and a.h == pytest.approx((2**-0.5, 2**-0.5, 0)) and a.v == pytest.approx((-2**-0.5, 2**-0.5, 0))
    assert a.ToXYZ((1, 0)) == pytest.approx((2**-0.5, 2**-0.5, 2))
    assert px.Plane(normal=(1, 0, 0)).v == (0, 0, 1)


def test_netgen_axes_convert_both_ways():
    a = px.Plane(occ.Axes((1, 2, 3), n=occ.X, h=occ.Y))
    assert a.origin == (1, 2, 3) and a.normal == (1, 0, 0) and a.h == (0, 1, 0) and a.v == (0, 0, 1)
    back = a.ToOCCAxes()
    assert isinstance(back, occ.Axes) and (back.p.x, back.p.y, back.p.z) == (1, 2, 3)
    assert px.Plane(back).normal == (1, 0, 0) and px.Plane(a).h == (0, 1, 0)


def test_rotations_match_occ():
    rng = random.Random(1)
    for _ in range(200):
        p = random_plane(rng)
        c, d, angle = tuple(rng.uniform(-10, 10) for _ in range(3)), unit([rng.gauss(0, 1) for _ in range(3)]), rng.uniform(-360, 360)
        # netgen takes degrees
        t = occ.gp_Trsf.Rotation(occ.Axis(c, occ.Dir(*d)), angle)
        compare(p.Rotated(c, d, angle), [t(v) for v in frame(p)])


def test_translations_and_offsets_match_occ():
    rng = random.Random(2)
    for _ in range(200):
        p = random_plane(rng)
        d = tuple(rng.uniform(-10, 10) for _ in range(3))
        t = occ.gp_Trsf.Translation(occ.Vec(*d))
        compare(p.Translated(d), [t(v) for v in frame(p)])
        s = rng.uniform(-5, 5)
        t = occ.gp_Trsf.Translation(occ.Vec(*(s * c for c in p.normal)))
        compare(px.Plane(p, s), [t(v) for v in frame(p)])


def test_mirrors_match_occ_with_v_reversed():
    rng = random.Random(3)
    for _ in range(200):
        p = random_plane(rng)
        q, n = tuple(rng.uniform(-10, 10) for _ in range(3)), unit([rng.gauss(0, 1) for _ in range(3)])
        m = p.Mirrored(q, n)
        compare(m, [v.Mirror(occ.Axes(q, n=occ.Dir(*n), h=occ.Dir(*px.Plane(normal=n).h))) for v in frame(p)])
        # right-handed again: v is the mirrored v reversed
        mv = tuple(c - 2 * sum(a * b for a, b in zip(p.v, n)) * k for c, k in zip(p.v, n))
        assert m.v == pytest.approx(tuple(-c for c in mv), abs=TOL)


def test_scaling_a_shape_moves_its_plane_as_occ_does():
    rng = random.Random(4)
    for _ in range(200):
        p = random_plane(rng)
        c, s = tuple(rng.uniform(-10, 10) for _ in range(3)), rng.uniform(0.1, 5)
        face = px.QuickSketch(p).Rectangle(1, 1).Face().Scaled(c, s)
        o = occ.gp_Trsf.Scale(occ.Pnt(*c), s)(occ.Vertex(occ.Pnt(*p.origin)))
        assert face.ToXYZ((0, 0)) == pytest.approx(point(o), abs=TOL)
        assert px.Plane(face).normal == pytest.approx(p.normal, abs=TOL)
