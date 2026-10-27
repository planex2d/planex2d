# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import gc

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def line_sketch(dimension):
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(4, 0, px.Horizontal, id="a")
    sk.Constrain("a", px.Distance, 4, id=dimension)
    return sk


def length(sk):
    return sk.Location("a.end")[0]


def test_one_set_drives_several_sketches():
    p = px.Parameters(w=40)
    one, two = line_sketch("d1"), line_sketch("d2")
    one.Use(p).Expression("d1 = w")
    two.Use(p).Expression("d2 = 2*w")
    assert (length(one), length(two)) == pytest.approx((40, 80))
    assert p.Update(w=50) is p and p["w"] == 50
    assert (length(one), length(two)) == pytest.approx((50, 100))


def test_a_dict_works_as_keywords_do():
    p = px.Parameters({"th": 3, "w": 40})
    assert {name: p[name] for name in p} == {"th": 3.0, "w": 40.0} and len(p) == 2 and "th" in p
    p.Update({"th": 4})
    assert p["th"] == 4 and repr(p) == "Parameters(th=4, w=40)"


def test_a_sketch_without_a_solution_leaves_every_sketch_as_it_was():
    p = px.Parameters(w=40)
    one, two = line_sketch("d1"), line_sketch("d2")
    one.Use(p).Expression("d1 = w")
    two.Use(p).Expression("d2", "10*sqrt(60 - w)")
    with pytest.raises(ValueError, match="every sketch stays as it was"):
        p.Update(w=70)
    assert p["w"] == 40 and length(one) == pytest.approx(40) and length(two) == pytest.approx(10 * 20 ** 0.5)


def test_a_curve_refits_with_its_parameter():
    p = px.Parameters(a=2)
    sk = px.Sketch().Use(p)
    sk.Curve(("t", "a*t^2"), (0, 1))
    assert sk.Location("Curve.1.end") == pytest.approx((1, 2), abs=1e-12)
    p.Update(a=3)
    assert sk.Location("Curve.1.end") == pytest.approx((1, 3), abs=1e-12)


def test_a_global_parameter_is_set_only_by_its_update():
    p = px.Parameters(w=40)
    sk = line_sketch("d1").Use(p)
    sk.Expression("d1 = w")
    assert "Parameter" not in sk.constraints and sk["w"] == 40
    with pytest.raises(ValueError, match="global parameter"):
        sk["w"] = 5
    with pytest.raises(ValueError, match="global parameter"):
        sk.Expression("w = 2*d1")
    with pytest.raises(ValueError, match="global parameter"):
        sk.Remove("w")


def test_names_follow_the_id_rules():
    for name in ("h", "v", "t"):
        with pytest.raises(ValueError, match="reserved"):
            px.Parameters(**{name: 3})
    with pytest.raises(ValueError, match="letter"):
        px.Parameters({"1x": 3})
    with pytest.raises(ValueError, match="finite number"):
        px.Parameters(w="wide")
    with pytest.raises(ValueError, match="no parameter 'depth'"):
        px.Parameters(w=1).Update(depth=2)


def test_a_sketch_name_taken_already_raises_at_use():
    sk = line_sketch("w")
    with pytest.raises(ValueError, match="parameter's name must be free"):
        sk.Use(px.Parameters(w=40))
    with pytest.raises(TypeError, match="px.Parameters"):
        sk.Use({"w": 40})


def test_a_sketch_that_is_gone_no_longer_takes_updates():
    p = px.Parameters(w=40)
    sk = line_sketch("d1").Use(p)
    sk.Expression("d1 = w")
    keep = line_sketch("d2").Use(p)
    del sk
    gc.collect()
    p.Update(w=45)
    assert keep["w"] == 45
