# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import numpy as np
import pytest

import planex as px


def perimeter(a, b):
    # the trapezoid rule on a smooth periodic integrand converges geometrically
    t = np.linspace(0, 2 * np.pi, 20001)[:-1]
    return float(np.mean(np.hypot(a * np.sin(t), b * np.cos(t)))) * 2 * np.pi


def on_ellipse(pts, c, a, b, angle):
    u = np.array([math.cos(math.radians(angle)), math.sin(math.radians(angle))])
    v = np.array([-u[1], u[0]])
    d = np.asarray(pts)[:, :2] - c
    return ((d @ u) / a) ** 2 + ((d @ v) / b) ** 2


def test_an_ellipse_is_one_closed_exact_edge():
    w = px.Ellipse((1, 2), 3, 2, angle=30)
    assert len(w.edges) == 1 and w.IsClosed() and isinstance(w.edges[0].edge, px.SplineEdge)
    e = w.edges[0].edge
    assert tuple(e.start)[:2] == pytest.approx((1 + 3 * math.cos(math.radians(30)), 2 + 3 * math.sin(math.radians(30))),
                                               abs=1e-14)
    assert on_ellipse(e.Sample(1e-4), (1, 2), 3, 2, 30) == pytest.approx(1, abs=1e-12)
    b = e.bspline
    assert b["degree"] == 2 and len(b["poles"]) == 9 and b["weights"] is not None


def test_its_area_and_length_are_exact():
    for a, b in ((3, 2), (5, 0.5), (1, 1)):
        w = px.Ellipse((0, 0), a, b, angle=17)
        assert w.Face().mass == pytest.approx(math.pi * a * b, rel=1e-13)
        assert w.mass == pytest.approx(perimeter(a, b), rel=1e-12)


def test_reverse_runs_clockwise():
    ccw, cw = (px.Ellipse((0, 0), 3, 2, reverse=r).edges[0].edge.bspline["poles"] for r in (False, True))
    assert ccw[0] == pytest.approx(cw[0]) and ccw[1][1] > 0 > cw[1][1]


def test_the_plane():
    w = px.Ellipse((0, 0, 1), 2, 1, plane=px.XZ)
    assert w.center == pytest.approx((0, 0, 1), abs=1e-14)


def test_booleans_with_an_ellipse():
    ring = px.Ellipse((0, 0), 4, 2).Face() - px.Circle((0, 0), 1).Face()
    assert ring.mass == pytest.approx(math.pi * (8 - 1), rel=1e-12)
    halves = px.Ellipse((0, 0), 4, 2).Face() * px.FromPolyline([(0, -3), (5, -3), (5, 3), (0, 3)], closed=True).Face()
    assert halves.mass == pytest.approx(4 * math.pi, rel=1e-9)


def test_wrong_sizes_name_what_is_wrong():
    with pytest.raises(ValueError, match="the minor radius 3 is larger than the major radius 2"):
        px.Ellipse((0, 0), 2, 3)
    with pytest.raises(ValueError, match="the major radius is 0, it must be positive and finite"):
        px.Ellipse((0, 0), 0, 1)
    with pytest.raises(ValueError, match="the angle is nan"):
        px.Ellipse((0, 0), 2, 1, angle=float("nan"))


def test_dxf_ellipses_are_read_exactly(tmp_path):
    ezdxf = pytest.importorskip("ezdxf")
    doc = ezdxf.new()
    msp = doc.modelspace()
    msp.add_ellipse((1, 1), major_axis=(2, 0), ratio=0.5)
    msp.add_ellipse((10, 0), major_axis=(0, 3), ratio=0.5, start_param=0.25, end_param=2.0)
    doc.saveas(tmp_path / "el.dxf")
    whole, part = sorted(px.FromDXF(tmp_path / "el.dxf", unit=px.m), key=lambda w: tuple(w.center)[0])
    assert whole.IsClosed() and whole.Face().mass == pytest.approx(2 * math.pi, rel=1e-13)
    e = part.edges[0].edge
    point = lambda t: (10 + 0 * math.cos(t) - 1.5 * math.sin(t), 0 + 3 * math.cos(t) + 0 * math.sin(t))
    assert tuple(e.start)[:2] == pytest.approx(point(0.25), abs=1e-14)
    assert tuple(e.end)[:2] == pytest.approx(point(2.0), abs=1e-14)
    assert on_ellipse(e.Sample(1e-4), (10, 0), 3, 1.5, 90) == pytest.approx(1, abs=1e-12)
    px.WriteDXF(whole, tmp_path / "back.dxf")
    assert px.Face(px.FromDXF(tmp_path / "back.dxf", unit=px.m)).mass == pytest.approx(2 * math.pi, rel=1e-13)
