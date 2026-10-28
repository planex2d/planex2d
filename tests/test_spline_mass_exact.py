# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""areas, lengths and moments of a spline are integrated on its exact curve, not its approximation"""
import math
from fractions import Fraction

import pytest

import planex as px

R2 = math.sqrt(0.5)


def quarter_circle():
    # the rational quadratic quarter of the unit circle: exactly the circle
    return px.BSpline([(1, 0), (1, 1), (0, 1)], [0, 0, 0, 1, 1, 1], 2, weights=[1, R2, 1])


def quarter_disc():
    w = px.Line((0, 0), (1, 0))
    w.Add(quarter_circle())
    w.Add(px.Line((0, 1), (0, 0)))
    return w.Face()


def test_a_quarter_disc_bounded_by_a_rational_spline_measures_the_circle():
    f = quarter_disc()
    c = 4 / (3 * math.pi)
    assert f.mass == pytest.approx(math.pi / 4, rel=1e-14)
    assert f.center == pytest.approx((c, c, 0), rel=1e-14)
    # about the centre: the integrals of y^2 and xy over the quarter disc are pi/16 and 1/8
    jvv, juv = math.pi / 16 - math.pi / 4 * c * c, 1 / 8 - math.pi / 4 * c * c
    i = f.inertia
    assert (i[0][0], i[0][1], i[1][1]) == pytest.approx((jvv, -juv, jvv), rel=1e-13)


def test_a_rational_quarter_circle_has_the_length_of_the_circle():
    w = quarter_circle()
    assert w.mass == pytest.approx(math.pi / 2, rel=1e-14)
    assert w.edges[0].mass == pytest.approx(math.pi / 2, rel=1e-14)
    assert w.center == pytest.approx((2 / math.pi, 2 / math.pi, 0), rel=1e-14)


POLES = [(0, 0), (1.3, 2.1), (2.9, 1.7), (4, 0.3)]


def bezier_area():
    """the area between the cubic Bezier curve and its chord, exactly: (x y' - y x') / 2 integrated over [0, 1]"""
    def poly(c):
        b = [[1, -3, 3, -1], [0, 3, -6, 3], [0, 0, 3, -3], [0, 0, 0, 1]]
        return [sum(Fraction(c[i]) * b[i][k] for i in range(4)) for k in range(4)]

    def mul(p, q):
        r = [Fraction(0)] * (len(p) + len(q) - 1)
        for i, a in enumerate(p):
            for j, b in enumerate(q):
                r[i + j] += a * b
        return r

    x, y = poly([p[0] for p in POLES]), poly([p[1] for p in POLES])
    dx, dy = [k * x[k] for k in range(1, 4)], [k * y[k] for k in range(1, 4)]
    f = [a - b for a, b in zip(mul(x, dy), mul(y, dx))]
    return float(sum(c / (k + 1) for k, c in enumerate(f)) / 2)


def test_a_cubic_spline_face_has_the_exact_area():
    w = px.BSpline(POLES, [0, 0, 0, 0, 1, 1, 1, 1], 3)
    w.Add(px.Line(POLES[-1], POLES[0]))
    assert abs(w.Face().mass) == pytest.approx(abs(bezier_area()), rel=1e-14)


def spline_length(f):
    return sum(e.mass for e in f.edges if isinstance(e.edge, px.SplineEdge))


def test_the_pieces_of_a_cut_spline_face_add_up():
    p = px.QuickSketch().Spline([(0, 0), (1.5, 2), (3, -1), (4.5, 1.5), (2, 3)], periodic=True,
                                start_from_localpos=False).Face()
    r = px.Move((1, -2)).Rectangle(1.7, 6).Face()
    assert (p * r).mass + (p - r).mass == pytest.approx(p.mass, rel=1e-14)
    assert spline_length(p * r) + spline_length(p - r) == pytest.approx(spline_length(p), rel=1e-14)
