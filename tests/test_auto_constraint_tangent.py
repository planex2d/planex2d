# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def tangents(sk):
    return sorted(repr(c) for c in sk.constraints.get("Tangent", {}).values())


def crank(auto, flag=()):
    sk = px.Sketch(auto_constraint=auto)
    sk.Move((0, 0), dir=(0, -1)).Arc(10, 90, id="test").Line(30, *flag).Arc(10, 180, *flag).Line(15, *flag) \
        .Arc(5, -90, *flag).Line(10, *flag).Arc(10, 180, *flag).Close()
    return sk


def test_a_chain_drawn_tangent_gets_its_tangents():
    sk = crank(True)
    flagged = tangents(crank(False, (px.Tangent,)))
    # the flags' six and Close's two: from the last arc into the closing line, from it back into the first arc
    assert set(flagged) < set(tangents(sk))
    assert set(tangents(sk)) - set(flagged) == {'"Arc.3", "Line.4", px.Tangent', '"Line.4", "test", px.Tangent'}
    assert sk.dof == 12
    assert tangents(crank(False)) == []


def test_flags_and_auto_constraint_give_each_tangent_once_and_quietly():
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk = crank(True, (px.Tangent,))
    assert tangents(sk) == tangents(crank(True))


def test_a_corner_or_two_lines_get_no_tangent():
    sk = px.Sketch(auto_constraint=True)
    sk.Move((0, 0)).Line(10, id="a").Turn(90).Arc(5, 90, id="b").Line(4, id="c").Line(3, id="d")
    assert tangents(sk) == ['"b", "c", px.Tangent']
