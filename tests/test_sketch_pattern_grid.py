# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

TOL = 1e-12


def grid(make, count=4, count2=3, **kw):
    sk = make()
    sk.Circle((1, 2), 0.5, *((px.Fix,) if make is px.Sketch else ()), id="hole")
    sk.LinearPattern("hole", px.H, count, 10, dir2=px.V, count2=count2, spacing2=8, **kw)
    return sk


def centers(sk):
    return {c: tuple(round(v, 9) + 0.0 for v in sk._Geometry(c)["center"]) for c in sk.elements["Circle"]}


def expected(count, count2):
    out = {"hole": (1.0, 2.0)}
    for j in range(count2):
        for k in range(count):
            if k or j:
                out[f"LinearPattern.1.{k}.{j}.hole"] = (1.0 + 10 * k, 2.0 + 8 * j)
    return out


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_grid_of_copies_named_by_their_two_steps(make):
    sk = grid(make)
    assert centers(sk) == expected(4, 3)


@pytest.mark.constraints
def test_a_kept_grid_follows_its_spacings_and_stays_complete_when_its_counts_change():
    sk = grid(px.Sketch)
    assert (sk.status, sk.dof) == ("full", 0)
    assert sk.dimensions["LinearPattern.1.spacing2"] == pytest.approx(8)
    assert (sk["LinearPattern.1.count"], sk["LinearPattern.1.count2"]) == (4, 3)
    assert "LinearPattern.1.2.1" in sk.constraints["LinearPattern"]
    sk.Update({"LinearPattern.1.spacing2": 5})
    assert centers(sk)["LinearPattern.1.3.2.hole"] == pytest.approx((31, 12), abs=TOL)
    sk.Update({"LinearPattern.1.count2": 2})
    assert centers(sk) == {c: (x, 2.0 + (y - 2.0) * 5 / 8) for c, (x, y) in expected(4, 2).items()}
    sk.Update({"LinearPattern.1.count": 5, "LinearPattern.1.count2": 3})
    assert centers(sk) == {c: (x, 2.0 + (y - 2.0) * 5 / 8) for c, (x, y) in expected(5, 3).items()}
    assert (sk.status, sk.dof) == ("full", 0)


@pytest.mark.constraints
def test_a_copy_of_a_grid_names_both_spacings_and_counts():
    sk = grid(px.Sketch)
    with pytest.raises(ValueError, match="LinearPattern.1.spacing2, LinearPattern.1.count or LinearPattern.1.count2"):
        sk["LinearPattern.1.2.1"] = 3


def test_the_second_direction_takes_all_three_and_two_instances_at_least():
    sk = px.QuickSketch()
    sk.Circle((0, 0), 1, id="hole")
    with pytest.raises(TypeError, match="dir2=, count2= and spacing2= go together"):
        sk.LinearPattern("hole", px.H, 3, 10, dir2=px.V, count2=2)
    with pytest.raises(ValueError, match="count times count2 at least 2"):
        sk.LinearPattern("hole", px.H, 1, 10, dir2=px.V, count2=1, spacing2=5)
    sk.LinearPattern("hole", px.H, 1, 10, dir2=px.V, count2=3, spacing2=5)
    assert sorted(sk.elements["Circle"]) == ["LinearPattern.1.0.1.hole", "LinearPattern.1.0.2.hole", "hole"]
