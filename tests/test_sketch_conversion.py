# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def draw(sk):
    sk.Move((0, 0)).Line(10).Line(0, 5).Arc(2, 90).Line(3, 0).Arc(2, 360)
    sk.Move((20, 0)).Line(4, 0)
    sk.Move((24, 0)).Line(0, 3)
    sk.Move((30, 0)).Rectangle(4, 3, id="R")
    return sk


def relations(sk):
    return {kind: sorted(repr(c) for c in ids.values()) for kind, ids in sk.constraints.items()}


def test_a_converted_quicksketch_has_its_geometry_and_no_constraints():
    quick = draw(px.QuickSketch())
    converted = px.Sketch(quick, copy=True)
    assert isinstance(converted, px.Sketch) and not converted.auto_constraint
    assert converted.elements == quick.elements and converted.constraints == {}
    assert converted.Shape().mass == pytest.approx(quick.Shape().mass, rel=1e-12)


def test_auto_constraint_adds_what_the_points_show():
    """Line.3 turns back along Arc.1's end (a cusp): tangent all the same"""
    quick = draw(px.QuickSketch())
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        converted = px.Sketch(quick, copy=True, auto_constraint=True)
    assert converted.auto_constraint
    assert relations(converted) == {
        "Tangent": ['"Arc.1", "Line.3", px.Tangent', '"Line.2", "Arc.1", px.Tangent', '"Line.3", "Circle.1", px.Tangent'],
        "Coincident": ['"Point.5", "Circle.1", px.Coincident', '"Point.7", "Point.8", px.Coincident'],
    }
    assert converted.dof == px.Sketch(quick, copy=True).dof - 6


def test_the_drawing_tools_convert_as_their_lines():
    quick = px.QuickSketch().Move((0, 0)).Rectangle(4, 3, id="R")
    converted = px.Sketch(quick, copy=True, auto_constraint=True)
    assert converted.constraints == {} and converted.elements == quick.elements


def test_the_converted_sketchs_ids_go_on():
    quick = draw(px.QuickSketch())
    converted = px.Sketch(quick, copy=True)
    assert converted.Line(1).elements["Line"][-1] == quick.Copy().Line(1).elements["Line"][-1]


def test_a_sketch_becomes_a_quicksketch_without_its_constraints_and_without_a_warning():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(10).Line(0, 5).Close()
    sk.Constrain("Line.1", px.Distance, 8)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        quick = px.QuickSketch(sk, copy=True)
    assert type(quick) is px.QuickSketch
    assert quick.elements == sk.elements
    assert quick.Face().mass == pytest.approx(sk.Face().mass, rel=1e-12)


def test_copy_from_the_same_class_is_a_copy_and_without_copy_the_sketch_is_empty():
    sk = draw(px.Sketch())
    same = px.Sketch(sk, copy=True)
    assert relations(same) == relations(sk) and same.dof == sk.dof
    assert "Line" not in px.Sketch(sk).elements


def test_copy_takes_only_a_sketch_an_id_and_for_a_sketch_auto_constraint():
    with pytest.raises(TypeError, match=r"^Sketch\(sketch, copy=True\): only id= and auto_constraint= go with copy=True"):
        px.Sketch(px.QuickSketch(), copy=True, mode="strict")
    with pytest.raises(TypeError, match=r"^QuickSketch\(sketch, copy=True\): only id= goes with copy=True"):
        px.QuickSketch(px.Sketch(), copy=True, auto_constraint=True)
    with pytest.raises(TypeError, match=r"^QuickSketch: plane is a Plane"):
        px.QuickSketch(1, copy=True)
