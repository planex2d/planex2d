# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""exp, log, sinh, cosh, tanh, asinh, acosh and atanh in expressions and curve formulas"""
import math

import pytest

import planex as px
from test_expressions_parsing import plate, refused

TOL = 1e-10


@pytest.mark.constraints
@pytest.mark.parametrize("text, value", [("exp(log(3))", 3.0), ("2*sinh(1)", 2 * math.sinh(1)), ("cosh(1)", math.cosh(1)),
                                         ("1 + tanh(2)", 1 + math.tanh(2)), ("asinh(4)", math.asinh(4)),
                                         ("acosh(3)", math.acosh(3)), ("1 + atanh(0.5)", 1 + math.atanh(0.5))])
def test_a_function_drives_a_dimension(text, value):
    sk = plate()
    sk.Expression("ht = " + text)
    assert sk["ht"] == pytest.approx(value, abs=TOL)
    assert sk.Geometry("left")["length"] == pytest.approx(value, abs=TOL)


@pytest.mark.constraints
def test_a_function_of_a_dimension_follows_it():
    sk = plate()
    sk.Expression("ht = log(w)")
    sk.Update({"w": 6})
    assert sk["ht"] == pytest.approx(math.log(6), abs=TOL)


@pytest.mark.constraints
@pytest.mark.parametrize("text, message", [("ht = log(w - 4)", "log of a number that is not positive (0)"),
                                           ("ht = acosh(w/8)", "acosh of 0.5, below 1"),
                                           ("ht = atanh(w/4)", "atanh of 1, outside (-1, 1)")])
def test_a_domain_error_where_the_expression_is_added(text, message):
    sk = plate()
    refused(sk, text, message)
    assert sk.expressions == {} and sk["ht"] == 2


@pytest.mark.parametrize("formula, f, lo, hi", [("2*cosh(t/2)", lambda t: 2 * math.cosh(t / 2), -2, 2),
                                                ("exp(t)", math.exp, -1, 1), ("log(t)", math.log, 0.5, 3),
                                                ("sinh(t)", math.sinh, -1, 1), ("tanh(t)", math.tanh, -2, 2),
                                                ("asinh(t)", math.asinh, -2, 2), ("acosh(t)", math.acosh, 1.5, 4),
                                                ("atanh(t)", math.atanh, -0.6, 0.6)])
def test_a_curve_of_a_function_is_exact(formula, f, lo, hi):
    w = px.Curve(("t", formula), (lo, hi))
    for k in range(11):
        t = lo + (hi - lo) * k / 10
        assert w.Distance((t, f(t))) < 1e-11 * max(1, abs(f(t)), hi - lo)
