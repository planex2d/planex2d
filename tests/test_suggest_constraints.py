# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def hv_rectangle(start=(1, 2)):
    sk = px.Sketch(start)
    sk.Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Vertical, id="right") \
        .Line(-4, 0, px.Horizontal, id="top").Close(px.Vertical, id="left")
    return sk


def near_rectangle():
    """a rectangle whose sides are off horizontal and vertical by 1e-7, as from a file with rounded coordinates"""
    sk = px.Sketch((1, 2))
    sk.Line(4, 1e-7, id="bottom").Line(-1e-7, 2, id="right").Line(-4, -1e-7, id="top").Close(id="left")
    return sk


def test_sizes_come_before_positions_each_removing_one_motion():
    sk = hv_rectangle()
    found = sk.SuggestConstraints(geometric=False)
    assert [g["call"] for g in found] == ['sk.Constrain("bottom", px.Distance, 4)', 'sk.Constrain("right", px.Distance, 2)',
                                          'sk.Constrain("bottom.start", px.V, px.Distance, 1)',
                                          'sk.Constrain("bottom.start", px.H, px.Distance, 2)']
    assert [g["removes"] for g in found] == ["bottom.end.h", "right.end.v", "bottom.start.h", "bottom.start.v"]
    assert [g["kind"] for g in found] == ["Distance"] * 4
    assert found[2]["args"] == ("bottom.start", px.V, px.Distance, 1)
    assert sk.dof == 4 and len(sk.constraints.get("Distance", {})) == 0


def test_the_args_replay_the_suggestions():
    sk = hv_rectangle()
    for g in sk.SuggestConstraints():
        sk.Constrain(*g["args"])
    assert (sk.dof, sk.status, sk.redundant) == (0, "full", [])


def test_a_point_on_an_axis_gets_a_coincident():
    sk = hv_rectangle(start=(0, 2))
    found = sk.SuggestConstraints(geometric=False)
    assert found[2]["call"] == 'sk.Constrain("bottom.start", px.V, px.Coincident)'
    assert found[2]["args"] == ("bottom.start", px.V, px.Coincident) and "removes" in found[2]


def test_auto_constrain_defines_the_sketch_without_moving_it():
    sk = hv_rectangle()
    before = sk.Location("right.end")
    added = sk.AutoConstrain()
    assert added == ["Distance.1", "Distance.2", "Distance.3", "Distance.4"]
    assert (sk.dof, sk.status, sk.redundant) == (0, "full", [])
    assert sk.Location("right.end") == pytest.approx(before, abs=1e-14)


def test_what_the_constraints_hold_already_is_not_suggested():
    sk = hv_rectangle()
    sk.Constrain("bottom", px.Distance, 4)
    found = sk.SuggestConstraints()
    assert all("bottom\"" not in g["call"] and "top\"" not in g["call"] for g in found)
    assert len(found) == sk.dof == 3


def test_relations_within_tol():
    hv = {"horizontal": True, "vertical": True}
    assert near_rectangle().SuggestConstraints(geometric=hv, dimensional=False) == []
    found = near_rectangle().SuggestConstraints(geometric=hv, dimensional=False, tol=1e-6)
    assert sorted(g["call"] for g in found) == ['sk.Constrain("bottom", px.Horizontal)', 'sk.Constrain("left", px.Vertical)',
                                                'sk.Constrain("right", px.Vertical)', 'sk.Constrain("top", px.Horizontal)']
    assert all("removes" not in g for g in found)


def test_auto_constrain_moves_the_geometry_onto_its_relations_and_warns():
    sk = near_rectangle()
    with pytest.warns(px.PlanexWarning, match="AutoConstrain: the relations moved the geometry by up to"):
        added = sk.AutoConstrain(geometric={"horizontal": True, "vertical": True}, dimensional=False, tol=1e-6)
    assert sorted(added) == ["Horizontal.1", "Horizontal.2", "Vertical.1", "Vertical.2"]
    (h0, v0), (h1, v1) = sk.Location("bottom.start"), sk.Location("bottom.end")
    assert v1 == pytest.approx(v0, abs=1e-14) and sk.dof == 4


def test_auto_constrain_rounds_up_a_mixed_sketch():
    sk = px.Sketch((1, 1))
    sk.Line(3, 0, px.Horizontal).Arc(1, 90, px.Tangent).Line(0, 2, px.Tangent)
    sk.Circle((8, 1), 0.5)
    dof = sk.dof
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert len(sk.SuggestConstraints(geometric=False)) == dof
        sk.AutoConstrain()
    assert (sk.dof, sk.redundant) == (0, [])


def test_the_motions_removed_are_the_free_motions():
    sk = hv_rectangle()
    drags = {m["drag"] for m in sk.FreeMotions()}
    assert {g["removes"] for g in sk.SuggestConstraints()} == drags


def test_wrong_arguments_raise():
    sk = hv_rectangle()
    with pytest.raises(ValueError, match="tol"):
        sk.SuggestConstraints(tol=-1)
    with pytest.raises(TypeError, match=r"AutoConstrain\(geometric=\): no relation 'nonsense'"):
        sk.AutoConstrain(geometric={"nonsense": True})
    with pytest.raises(TypeError, match=r"SuggestConstraints\(geometric=\)"):
        sk.SuggestConstraints(geometric=None)
    assert not hasattr(px.QuickSketch(), "SuggestConstraints") and not hasattr(px.QuickSketch(), "AutoConstrain")
