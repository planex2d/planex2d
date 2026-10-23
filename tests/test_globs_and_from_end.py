# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def walls():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Rectangle(10, 5, id="r")
    sk.Offset("r", 1, both_sides=True, id="w")
    return sk


def test_a_glob_selects_by_parts():
    sk = walls()
    corners = sorted(i.local for i in sk("w.**.corner"))
    assert corners == [f"w.right.r.Line.{k}.corner" for k in range(1, 5)]
    assert sorted(i.local for i in sk("w.*.r.Line.1")) == ["w.left.r.Line.1", "w.right.r.Line.1", "w.right.r.Line.1.corner"]
    assert len(sk("w.left.*")) == len(sk("w.left"))


def test_a_glob_may_select_nothing_a_plain_id_raises():
    sk = walls()
    assert len(sk("w.*.nothing")) == 0
    with pytest.raises(ValueError, match="no element 'w.nothing'"):
        sk("w.nothing")


def test_a_glob_in_a_shape():
    s = px.QuickSketch().Move((0, 0)).Line((1, 0)).Line((1, 1)).Close().Shape()
    assert len(s("Line.*")) == 3


def chains():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((1, 0))
    sk.Move((0, 2)).Line((1, 2)).Line((1, 3))
    sk.Move((0, 5)).Line((2, 5))
    return sk


def test_a_negative_number_counts_from_the_end():
    sk = chains()
    assert [i.local for i in sk("Chain.-1")] == sk.wires["Chain.3"]
    assert [i.local for i in sk("Chain.-2")] == sk.wires["Chain.2"]
    assert sk.Location("Line.-1.end") == pytest.approx((2, 5))
    assert sk.Edge("Chain.-2").mass == pytest.approx(2)
    assert [i.local for i in sk(sk.id + ".Line.-2")] == ["Line.3"]
    with pytest.raises(ValueError, match=r"'Chain.-4': there are 3 numbers under 'Chain' \(-1 is the last\)"):
        sk("Chain.-4")
    with pytest.raises(ValueError, match="there are 3 numbers under 'Chain'"):
        sk.Edge("Chain.-4")


def test_a_negative_number_in_a_wire():
    w = chains().Edge("Chain.2")
    last = max(w.edges, key=lambda e: int(e.id.rsplit(".", 1)[1]))
    assert len(w.edges) == 2 and w("Line.-1")[0].id == last.id


@pytest.mark.constraints
def test_an_expression_keeps_the_id_it_counted_to():
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(1, 1)
    sk.LinearPattern("Rectangle.1", px.H, 2, 2)
    sk.LinearPattern("Rectangle.1", px.V, 2, 3)
    sk.Expression("LinearPattern.-1.spacing = LinearPattern.-2.spacing * 2")
    assert sk.expressions == {"Expression.1": "LinearPattern.2.spacing = LinearPattern.1.spacing * 2"}
    assert sk["LinearPattern.2.spacing"] == pytest.approx(4)
