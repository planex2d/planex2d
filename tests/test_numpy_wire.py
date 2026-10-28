# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import numpy as np
import pytest

import planex as px


def outline():
    sk = px.QuickSketch().Move((0, 0)).Line(4, 0).Arc(1, 90).Spline([(3, 4), (1, 5)]).Line((0, 3)).Close()
    return sk.Face().outer


def same(d, e):
    for key in ("lines", "arcs", "circles", "order"):
        assert np.array_equal(d[key], e[key]), key
    assert len(d["splines"]) == len(e["splines"])
    for s, t in zip(d["splines"], e["splines"]):
        assert s["degree"] == t["degree"] and (s["weights"] is None) == (t["weights"] is None)
        assert np.allclose(s["poles"], t["poles"], rtol=0, atol=1e-14)
        assert np.allclose(s["knots"], t["knots"], rtol=0, atol=1e-14)


def test_the_arrays_of_a_wire():
    w = outline()
    d = w.ToNumpy()
    assert d["lines"].shape[1] == 4 and d["arcs"].shape == (1, 7) and d["circles"].shape == (0, 6)
    assert len(d["splines"]) == 1 and d["splines"][0]["poles"].shape[1] == 2
    assert d["order"].shape == (len(w.edges), 2) and d["order"].dtype == np.int64
    kinds = {0: "lines", 1: "arcs", 2: "circles", 3: "splines"}
    assert [kinds[k] for k in d["order"][:, 0]] == ["lines", "arcs", "splines", "lines", "lines"]
    assert d["lines"][0].tolist() == [0, 0, 4, 0]
    assert d["arcs"][0, 6] == 1.0


def test_a_wire_comes_back_from_its_arrays():
    d = outline().ToNumpy()
    w = px.FromNumpy(d)
    same(w.ToNumpy(), d)
    assert w.IsClosed()


def test_a_circle_keeps_its_start_and_a_closed_spline_stays_closed():
    d = {"lines": np.zeros((0, 4)), "arcs": np.zeros((0, 7)), "circles": np.array([[5.0, 5.0, 1.0, 5.0, 6.0, -1.0]]),
         "splines": [], "order": np.array([[2, 0]])}
    assert np.array_equal(px.FromNumpy(d).ToNumpy()["circles"], d["circles"])
    ring = px.Spline([(1, 0), (0, 1), (-1, 0), (0, -1)], periodic=True)
    back = px.FromNumpy(ring.ToNumpy())
    assert len(back.edges) == 1 and back.IsClosed()
    same(back.ToNumpy(), ring.ToNumpy())


def test_the_plane():
    w = px.FromNumpy(px.Line((0, 0), (2, 1)).ToNumpy(), plane=px.XZ)
    assert w.center == pytest.approx((1, 0, 0.5), abs=1e-15)


def test_bad_data_names_what_is_wrong():
    d = outline().ToNumpy()
    with pytest.raises(ValueError, match="the data has no 'arcs'"):
        px.FromNumpy({k: v for k, v in d.items() if k != "arcs"})
    with pytest.raises(ValueError, match="'lines' must be an \\(n, 4\\) array"):
        px.FromNumpy({**d, "lines": np.zeros((2, 3))})
    with pytest.raises(ValueError, match="order row 0 names no edge"):
        px.FromNumpy({**d, "order": np.array([[7, 0]])})
