# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import numpy as np
import pytest

import planex as px

pytestmark = pytest.mark.constraints


def distance(d):
    """|P - Q| = d for the unknowns (px, py, qx, qy)"""

    def f(v):
        dx, dy = v[0] - v[2], v[1] - v[3]
        n = math.sqrt(dx * dx + dy * dy)
        return [n - d], [[dx / n, dy / n, -dx / n, -dy / n]]

    return f


def value(c):
    """the unknown equals c"""
    return lambda v: ([v[0] - c], [[1.0]])


def solve(values, constraints, **kw):
    return px._planex._SolveConstraints(values, constraints, **kw)


def analyze(values, constraints, **kw):
    return px._planex._AnalyzeConstraints(values, constraints, **kw)


# A = (0, 0) fixed, B, C free: the 3-4-5 triangle with AB on the x axis
A_FIXED = [True, True, False, False, False, False]
TRIANGLE = [([0, 1, 2, 3], 1, distance(3)), ([2, 3, 4, 5], 1, distance(4)), ([4, 5, 0, 1], 1, distance(5)), ([3], 1, value(0))]


@pytest.mark.parametrize("shape", [(3, 5), (5, 3), (4, 4), (1, 3), (3, 1)])
def test_svd_matches_numpy(shape):
    rng = np.random.default_rng(1)
    a = rng.standard_normal(shape)
    u, s, v = (np.array(x) for x in px._planex._JacobiSVD(a.tolist()))
    assert s[: min(shape)] == pytest.approx(np.linalg.svd(a, compute_uv=False), abs=1e-13)
    assert np.allclose(u @ np.diag(s) @ v.T, a, atol=1e-13)
    assert np.allclose(v.T @ v, np.eye(shape[1]), atol=1e-13)


def test_svd_of_a_rank_deficient_matrix():
    rng = np.random.default_rng(2)
    a = rng.standard_normal((5, 2)) @ rng.standard_normal((2, 4))
    u, s, v = (np.array(x) for x in px._planex._JacobiSVD(a.tolist()))
    assert s[2:] == pytest.approx([0, 0], abs=1e-14)
    assert np.allclose(a @ v[:, 2:], 0, atol=1e-13)


@pytest.mark.parametrize("c, expected", [((2.5, 3.5), (3, 4)), ((2.5, -3.5), (3, -4)), ((10, 10), (3, 4))])
def test_triangle_from_three_lengths_keeps_the_drawn_side(c, expected):
    r = solve([0, 0, 2.8, 0.3, *c], TRIANGLE, size=5, fixed=A_FIXED)
    assert r["solved"]
    assert r["values"] == pytest.approx([0, 0, 3, 0, *expected], abs=1e-12)
    assert r["residual"] <= 1e-14 * 5
    assert r["iterations"] <= 20


def test_a_determined_triangle_has_no_freedom_left():
    a = analyze([0, 0, 3, 0, 3, 4], TRIANGLE, fixed=A_FIXED)
    assert (a["rank"], a["dof"]) == (4, 0)
    assert all(a["determined"])
    assert not any(a["redundant"])


def test_what_no_constraint_determines_moves_as_little_as_possible():
    b = (2.0, 1.0)
    r = solve([0, 0, *b], [([0, 1, 2, 3], 1, distance(3))], fixed=[True, True, False, False])
    assert r["solved"]
    s = 3 / math.hypot(*b)
    assert r["values"][2:] == pytest.approx([b[0] * s, b[1] * s], abs=1e-14)
    a = analyze(r["values"], [([0, 1, 2, 3], 1, distance(3))], fixed=[True, True, False, False])
    assert (a["rank"], a["dof"]) == (1, 1)
    assert a["determined"] == [True, True, False, False]


def test_unknowns_no_constraint_touches_stay_and_count_as_free():
    r = solve([0, 0, 2, 1, 7, 8], [([0, 1, 2, 3], 1, distance(3))], fixed=[True, True, False, False, False, False])
    assert r["values"][4:] == [7, 8]
    a = analyze(r["values"], [([0, 1, 2, 3], 1, distance(3))], fixed=[True, True, False, False, False, False])
    assert a["dof"] == 3
    assert a["determined"][4:] == [False, False]


def test_a_redundant_but_consistent_constraint_is_solved_and_flagged():
    constraints = [([0, 1, 2, 3], 1, distance(3)), ([3], 1, value(0)), ([3], 1, value(0))]
    r = solve([0, 0, 2.5, 0.4], constraints, fixed=[True, True, False, False])
    assert r["solved"]
    assert r["values"] == pytest.approx([0, 0, 3, 0], abs=1e-14)
    a = analyze(r["values"], constraints, fixed=[True, True, False, False])
    assert (a["rank"], a["dof"]) == (2, 0)
    assert a["redundant"] == [False, True, True]


def test_a_contradiction_is_not_solved_and_ends_at_the_least_squares_point():
    constraints = [([0, 1, 2, 3], 1, distance(3)), ([0, 1, 2, 3], 1, distance(4))]
    r = solve([0, 0, 2, 1], constraints, fixed=[True, True, False, False])
    assert not r["solved"]
    assert r["residual"] == pytest.approx(0.5, abs=1e-9)
    assert math.hypot(*r["values"][2:]) == pytest.approx(3.5, abs=1e-9)
    assert r["iterations"] < 200


def test_constraints_on_fixed_unknowns_only():
    fixed = [True, True, True, True]
    ok = solve([0, 0, 3, 0], [([0, 1, 2, 3], 1, distance(3))], fixed=fixed)
    assert ok["solved"]
    assert analyze([0, 0, 3, 0], [([0, 1, 2, 3], 1, distance(3))], fixed=fixed)["redundant"] == [True]
    assert not solve([0, 0, 2, 0], [([0, 1, 2, 3], 1, distance(3))], fixed=fixed)["solved"]


def test_independent_parts_are_solved_separately():
    # B is determined, C only lies on a circle, D is contradictory: B still ends where it belongs
    constraints = [([0, 1, 2, 3], 1, distance(3)), ([3], 1, value(0)), ([0, 1, 4, 5], 1, distance(5)),
                   ([0, 1, 6, 7], 1, distance(1)), ([0, 1, 6, 7], 1, distance(2))]
    fixed = [True, True] + [False] * 6
    r = solve([0, 0, 2.5, 0.4, 3, 4.5, 1, 1], constraints, fixed=fixed)
    assert not r["solved"]
    assert r["values"][2:4] == pytest.approx([3, 0], abs=1e-15)
    assert math.hypot(*r["values"][4:6]) == pytest.approx(5, abs=1e-14)
    a = analyze(r["values"], constraints[:3], fixed=fixed)
    assert a["determined"] == [True, True, True, True, False, False, False, False]


@pytest.mark.parametrize("scale, expected", [([1, 1], [0.5, 0.5]), ([1, 0.1], [100 / 101, 1 / 101])])
def test_scale_weights_the_smallest_change(scale, expected):
    # p + t = 1 from (0, 0): the change is smallest in sum((dx / scale)^2)
    r = solve([0, 0], [([0, 1], 1, lambda v: ([v[0] + v[1] - 1], [[1.0, 1.0]]))], scale=scale)
    assert r["values"] == pytest.approx(expected, abs=1e-14)


def test_wrong_rows_from_a_constraint_raise():
    with pytest.raises(ValueError, match="wrong number of rows"):
        solve([0.0], [([0], 2, value(1))])


def sparse_cholesky(a, b, shifts, columns=()):
    out = px._planex._SparseCholeskySolve(np.asarray(a).tolist(), list(b), list(shifts), [list(c) for c in columns])
    return [None if x is None else np.array(x) for x in out]


def sparse_rows(rng, m, n, per_row):
    j = np.zeros((m, n))
    for i in range(m):
        j[i, rng.choice(n, per_row, replace=False)] = rng.standard_normal(per_row)
    return j


@pytest.mark.parametrize("dense", [0, 1, 3])
def test_sparse_cholesky_matches_numpy(dense):
    rng = np.random.default_rng(3)
    j = sparse_rows(rng, 40, 60, 4)
    v = rng.standard_normal((40, dense))
    b = rng.standard_normal(40)
    shifts = [1e-3, 1.0, 1e-8]
    for x, shift in zip(sparse_cholesky(j @ j.T, b, shifts, v.T), shifts):
        expected = np.linalg.solve(j @ j.T + v @ v.T + shift * np.eye(40), b)
        assert x == pytest.approx(expected, rel=1e-10, abs=1e-10 * np.abs(expected).max())


def test_sparse_cholesky_of_an_arrow():
    # one row and column full: the ordering takes it last, the factor stays sparse
    n = 50
    a = np.eye(n) * 4
    a[0, :] = a[:, 0] = 1
    a[0, 0] = n
    b = np.arange(n, dtype=float)
    (x,) = sparse_cholesky(a, b, [0.0])
    assert x == pytest.approx(np.linalg.solve(a, b), rel=1e-12)


def test_sparse_cholesky_refuses_what_is_not_positive_definite():
    a = [[1.0, 2.0], [2.0, 1.0]]
    indefinite, shifted = sparse_cholesky(a, [1.0, 0.0], [0.0, 5.0])
    assert indefinite is None
    assert shifted == pytest.approx(np.linalg.solve(np.array(a) + 5 * np.eye(2), [1.0, 0.0]), rel=1e-14)


def test_dense_columns_fill_a_singular_sparse_part():
    # the step's J J^T + l I at a tiny l: without its two dense columns J has a dependent row and a row of zeros, with
    # them it has full rank; the updates on the nearly singular sparse factor stay as accurate as a dense solve
    rng = np.random.default_rng(4)
    js = sparse_rows(rng, 30, 58, 3)
    js[20] = js[0] + js[1]
    js[21] = 0
    jd = rng.standard_normal((30, 2))
    j = np.hstack([js, jd])
    shift = 1e-12 * np.max(np.sum(j * j, axis=1))
    b = rng.standard_normal(30)
    (x,) = sparse_cholesky(js @ js.T, b, [shift], jd.T)
    expected = np.linalg.solve(j @ j.T + shift * np.eye(30), b)
    assert x == pytest.approx(expected, rel=1e-9, abs=1e-9 * np.abs(expected).max())


@pytest.mark.parametrize("pairs", [100, 150])
def test_an_unknown_in_many_rows_keeps_the_smallest_change(pairs):
    # x_i + y_i + t = c_i and t = 1/2: t is in pairs + 1 rows, above 128 its column of J is a rank-one update. Linear
    # equations from the drawing: the solution is the smallest change, the drawing minus pinv(J) times the residuals
    rng = np.random.default_rng(5)
    c = rng.standard_normal(pairs)

    def pair(k):
        return lambda v: ([v[0] + v[1] + v[2] - c[k]], [[1.0, 1.0, 1.0]])

    constraints = [([0, 1 + k, 1 + pairs + k], 1, pair(k)) for k in range(pairs)] + [([0], 1, value(0.5))]
    x0 = rng.standard_normal(1 + 2 * pairs)
    r = solve(x0.tolist(), constraints)
    assert r["solved"]
    j = np.zeros((pairs + 1, 1 + 2 * pairs))
    for k in range(pairs):
        j[k, [0, 1 + k, 1 + pairs + k]] = 1
    j[pairs, 0] = 1
    residuals = j @ x0 - np.append(c, 0.5)
    assert r["values"] == pytest.approx(x0 - np.linalg.pinv(j) @ residuals, abs=1e-12)


def linear(coefficients):
    """rows of a . values, the coefficients as rows"""
    a = np.atleast_2d(np.asarray(coefficients, dtype=float))
    return lambda v: ((a @ np.asarray(v)).tolist(), a.tolist())


def dense_analysis(j, rows_of):
    """the analysis by the singular value decomposition of the whole J: rank, determined unknowns, redundant
    constraints and amplification as AnalyzeConstraints defines them (first order)"""
    zero = math.sqrt(1e-9)
    u, s, vt = np.linalg.svd(j)
    rank = int(np.sum(s > 1e-9 * s[0]))
    determined = [bool(np.linalg.norm(vt[rank:, k]) <= zero) for k in range(j.shape[1])]
    redundant, row = [], 0
    for rows in rows_of:
        block = u[row:row + rows, rank:]
        spanned = np.sum(np.linalg.svd(block, compute_uv=False) > zero) if block.size else 0
        redundant.append(bool(rows > 0 and spanned == rows))
        row += rows
    return rank, determined, redundant, 1 / s[rank - 1]


@pytest.mark.parametrize("seed", range(4))
def test_the_sparse_analysis_matches_the_dense_one(seed):
    # a chain of constraints over 20 unknowns, some on two rows, and constraints that follow from others
    rng = np.random.default_rng(10 + seed)
    n = 20
    parts = [(list(range(k, k + 4)), rng.standard_normal((1, 4))) for k in range(0, 18, 2)]
    parts += [(sorted(rng.choice(n, 4, replace=False).tolist()), rng.standard_normal((2, 4))) for _ in range(4)]
    (i0, a0), (i1, a1) = parts[0], parts[1]
    union = sorted(set(i0) | set(i1))
    summed = np.zeros((1, len(union)))
    for idx, a in ((i0, a0), (i1, a1)):
        summed[0, [union.index(i) for i in idx]] += a[0]
    parts.append((union, summed))
    parts.append((parts[9][0], 2 * parts[9][1]))
    j = np.zeros((sum(len(a) for _, a in parts), n))
    row = 0
    for idx, a in parts:
        j[row:row + len(a), idx] = a
        row += len(a)
    values = rng.standard_normal(n).tolist()
    result = analyze(values, [(idx, len(a), linear(a)) for idx, a in parts])
    rank, determined, redundant, amplification = dense_analysis(j, [len(a) for _, a in parts])
    assert (result["rank"], result["dof"]) == (rank, n - rank)
    assert result["determined"] == determined
    assert result["redundant"] == redundant
    assert any(redundant) and not all(determined)
    assert result["amplification"] == pytest.approx(amplification, rel=1e-3)


@pytest.mark.parametrize("delta, rank", [(1e-12, 2), (1e-6, 3)])
def test_a_nearly_dependent_constraint_counts_by_rank_tol(delta, rank):
    # the third row is the sum of the first two up to delta: below rank_tol (1e-9) relative it takes no rank
    a, b, c = np.array([1.0, 2.0, 0.5]), np.array([0.3, -1.0, 2.0]), np.array([0.7, 0.1, -0.4])
    constraints = [([0, 1, 2], 1, linear(r)) for r in (a, b, a + b + delta * c)]
    result = analyze([0.1, 0.2, 0.3], constraints)
    assert result["rank"] == rank
    assert result["redundant"] == [rank == 2] * 3
