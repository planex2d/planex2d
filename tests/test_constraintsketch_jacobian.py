# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import sys

import numpy as np
import pytest

import planex as px


def plate():
    """a rectangle at the origin: its bottom w = 4 and its left side ht = 2"""
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Perpendicular, id="right") \
        .Line(-4, 0, px.Perpendicular, id="top").Close(px.Perpendicular, id="left")
    sk.Constrain("bottom", px.Distance, 4, id="w")
    sk.Constrain("left", px.Distance, 2, id="ht")
    return sk


def dense(jacobian):
    """the matrix as a numpy array, from either form"""
    matrix = jacobian["matrix"]
    if not isinstance(matrix, tuple):
        return matrix.toarray()
    data, indices, indptr = matrix
    out = np.zeros(jacobian["shape"])
    for i in range(len(indptr) - 1):
        out[i, indices[indptr[i]:indptr[i + 1]]] = data[indptr[i]:indptr[i + 1]]
    return out


def without_scipy(monkeypatch):
    monkeypatch.setitem(sys.modules, "scipy", None)
    monkeypatch.setitem(sys.modules, "scipy.sparse", None)


@pytest.mark.constraints
def test_without_scipy_the_matrix_is_its_csr_arrays(monkeypatch):
    without_scipy(monkeypatch)
    sk = plate()
    j = sk.jacobian
    data, indices, indptr = j["matrix"]
    assert all(isinstance(a, np.ndarray) for a in (data, indices, indptr))
    assert j["shape"] == (len(j["rows"]), len(j["columns"])) == (len(indptr) - 1, len(j["scale"]))
    assert len(data) == len(indices) == indptr[-1]
    # the free unknowns less the rank of the Jacobian are the degrees of freedom
    assert j["shape"][1] - np.linalg.matrix_rank(dense(j)) == sk.dof


@pytest.mark.constraints
def test_with_scipy_the_matrix_is_a_csr_array_of_the_same_values(monkeypatch):
    sparse = pytest.importorskip("scipy.sparse")
    sk = plate()
    with_scipy = sk.jacobian
    assert isinstance(with_scipy["matrix"], sparse.csr_array)
    assert with_scipy["matrix"].shape == with_scipy["shape"]
    without_scipy(monkeypatch)
    plain = sk.jacobian
    assert isinstance(plain["matrix"], tuple)
    assert np.array_equal(dense(plain), dense(with_scipy))
    assert np.array_equal(sparse.csr_array(plain["matrix"], shape=plain["shape"]).toarray(), dense(with_scipy))


@pytest.mark.constraints
def test_rows_and_columns_are_labelled():
    sk = plate()
    j = sk.jacobian
    assert ("w", 0) in j["rows"] and ("ht", 0) in j["rows"]
    # a point by the first element drawn through it; the start of bottom is at the origin, held
    assert ("bottom.end", "x") in j["columns"] and ("right.start", "x") not in j["columns"]
    assert ("bottom.start", "x") not in j["columns"]
    assert np.all(j["scale"] == 1.0) and np.all(j["row_scale"] == 1.0)
    assert len(j["row_scale"]) == len(j["rows"])


@pytest.mark.constraints
def test_a_length_row_moves_the_end_along_the_line():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(3, 4, id="a")
    sk.Constrain("a", px.Distance, 5, id="d")
    j = sk.jacobian
    row = dense(j)[j["rows"].index(("d", 0))]
    x, y = (j["columns"].index(("a.end", c)) for c in ("x", "y"))
    assert row[x] != 0 and row[y] / row[x] == pytest.approx(4 / 3, rel=1e-12)


@pytest.mark.constraints
def test_an_expression_holds_the_dimension_it_uses_in_an_unknown():
    sk = plate()
    sk.Expression("ht = w/4")
    j = sk.jacobian
    assert ("w", "value") in j["rows"] and ("w", "value") in j["columns"]
    assert j["shape"][1] - np.linalg.matrix_rank(dense(j)) == sk.dof


@pytest.mark.constraints
def test_an_arcs_own_equations_are_its_rows():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(2, 0, px.Horizontal, id="a").Arc(1, 90, px.Tangent, id="b")
    j = sk.jacobian
    assert any(r[0] == "b" for r in j["rows"])
    assert ("b", "r") in j["columns"] and ("b.center", "x") in j["columns"]


def four_bar():
    """crank 10 at phi = 20 degrees from the ground (40, construction), coupler 35, rocker 30"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(10, 3, id="crank").Line(32, 10, id="coupler")
    sk.Line(-2, -13, id="rocker")
    sk.Move("crank.start", px.Coincident).Line(40, 0, id="ground", construction=True)
    sk.Constrain("ground.end", px.Fix)
    sk.Constrain("rocker.end", "ground.end", px.Coincident)
    sk.Constrain("crank", px.Distance, 10, id="a")
    sk.Constrain("coupler", px.Distance, 35, id="b")
    sk.Constrain("rocker", px.Distance, 30, id="c")
    sk.Constrain("ground", "crank", px.Angle, 20, id="phi")
    return sk


def sensitivity(j, row):
    """the change of every unknown per unit (per degree) of the dimension in row"""
    spsolve = pytest.importorskip("scipy.sparse.linalg").spsolve
    i = j["rows"].index(row)
    e = np.zeros(j["shape"][0])
    e[i] = j["row_scale"][i]
    return dict(zip(j["columns"], j["scale"] * spsolve(j["matrix"].tocsc(), e)))


@pytest.mark.constraints
def test_an_angle_rows_scale_is_its_lever_per_degree():
    sk = four_bar()
    j = sk.jacobian
    rows = j["rows"]
    # the lever: the shorter of ground and crank
    assert j["row_scale"][rows.index(("phi", 0))] == pytest.approx(10 * np.pi / 180, rel=1e-12)
    assert all(s == 1.0 for r, s in zip(rows, j["row_scale"]) if r != ("phi", 0))


@pytest.mark.constraints
def test_the_four_bar_moves_per_degree_of_phi_as_a_finite_difference():
    sk = four_bar()
    d = sensitivity(sk.jacobian, ("phi", 0))
    h = 1e-5
    up, down = sk.Copy().Update({"phi": 20 + h}), sk.Copy().Update({"phi": 20 - h})
    for ref in ("crank.end", "coupler.end"):
        fd = (np.array(up.Location(ref)[:2]) - np.array(down.Location(ref)[:2])) / (2 * h)
        assert (d[(ref, "x")], d[(ref, "y")]) == pytest.approx(tuple(fd), abs=1e-6)
    assert (d[("crank.end", "x")], d[("crank.end", "y")]) == pytest.approx((-0.0597, 0.164), abs=5e-4)


@pytest.mark.constraints
def test_an_angle_column_is_in_degrees():
    sk = four_bar()
    sk.Expression("phi = 2*a")
    j = sk.jacobian
    k = j["columns"].index(("phi", "value"))
    # the solver scales an angle by the size of the sketch; scale turns it back into degrees
    assert j["scale"][k] != 1.0
    d = sensitivity(j, ("a", "value"))
    h = 1e-5
    up, down = sk.Copy().Update({"a": 10 + h}), sk.Copy().Update({"a": 10 - h})
    assert d[("phi", "value")] == pytest.approx((up["phi"] - down["phi"]) / (2 * h), rel=1e-6)
    assert d[("phi", "value")] == pytest.approx(2.0, rel=1e-9)
    fd = (np.array(up.Location("crank.end")[:2]) - np.array(down.Location("crank.end")[:2])) / (2 * h)
    assert (d[("crank.end", "x")], d[("crank.end", "y")]) == pytest.approx(tuple(fd), abs=1e-6)


def test_a_quick_sketch_has_no_jacobian():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line(1, 0)
    assert not hasattr(sk, "jacobian")
