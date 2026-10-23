# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest
import planex as px


def rect(x, y, l, w, name=None):
    f = px.Face([px.FromPolyline([(x, y), (x + l, y), (x + l, y + w), (x, y + w)], True)])
    if name:
        f.name = name
    return f


def faces_of(shape):
    return sorted(((f.Measure()["area"], f.name) for f in shape.faces), key=lambda t: -t[0])


@pytest.mark.parametrize("flip", [False, True])
def test_glue_unnamed_inner_keeps_no_name(flip):
    parts = [rect(0, 0, 4, 4, "big"), rect(1, 1, 1, 1)]
    got = faces_of(px.Glue(parts[::-1] if flip else parts))
    assert len(got) == 2
    assert got[0][0] == pytest.approx(15, abs=1e-12) and got[0][1] == "big"
    assert got[1][0] == pytest.approx(1, abs=1e-12) and not got[1][1]


@pytest.mark.parametrize("flip", [False, True])
def test_glue_named_inner_keeps_name(flip):
    parts = [rect(0, 0, 4, 4, "big"), rect(1, 1, 1, 1, "small")]
    got = faces_of(px.Glue(parts[::-1] if flip else parts))
    assert [(round(a, 12), n) for a, n in got] == [(15.0, "big"), (1.0, "small")]


def test_glue_overlap_first_wins():
    a, b = rect(0, 0, 2, 1, "A"), rect(1, 0, 2, 1, "B")
    for parts in ([a, b], [b, a]):
        total = {}
        for ar, n in faces_of(px.Glue(parts)):
            total[n] = total.get(n, 0.0) + ar
        assert total[parts[0].name] == pytest.approx(2.0, abs=1e-12)
        assert total[parts[1].name] == pytest.approx(1.0, abs=1e-12)


def test_measure_inertia_keys():
    m = rect(0, 0, 2, 1).Measure()
    assert "Iu" not in m and "Iuv" not in m
    assert m["Ih"] == pytest.approx(2 * 1 ** 3 / 12, abs=1e-12)
    assert m["Iv"] == pytest.approx(1 * 2 ** 3 / 12, abs=1e-12)
    assert m["Ihv"] == pytest.approx(0, abs=1e-12)


def test_deformable3d_is_not_in_the_binding():
    assert "DeformableShape3d" not in px.__all__
    assert not hasattr(px, "DeformableShape3d")
    assert not hasattr(px._planex, "DeformableShape3d")
