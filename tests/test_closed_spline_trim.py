# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_closed_spline_trimmed_by_a_chord_leaves_one_closed_outline(make):
    q = make()
    q.Move((3, -10)).Spline([(20, 0), (15, 15), (0, 10), (-8, 2)], periodic=True, id="blob")
    q.Move((-20, 4)).Line(50, 0, id="chord")
    q.Trim(("blob", (3, -10)), "chord")
    q.Trim(("chord", (-19, 4)))
    q.Trim(("chord", (29, 4)))
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        area = q.Face().mass
    assert [(w.IsClosed(), len(w.edges)) for w in q.Wires()] == [(True, 2)]
    assert area == pytest.approx(213.3774311041892, rel=1e-6)
