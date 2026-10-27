# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import inspect

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def kinds(sk):
    return {kind: sorted(repr(c) for c in ids.values()) for kind, ids in sk.constraints.items()}


def test_auto_alone_and_with_relations():
    assert repr(px.Auto) == "px.Auto" and px.Auto.relations == {}
    a = px.Auto({"horizontal": True, "tangent": False})
    assert repr(a) == "px.Auto({'horizontal': True, 'tangent': False})"
    assert a.relations == {"horizontal": True, "tangent": False}
    assert a == px.Auto({"horizontal": True, "tangent": False}) and a != px.Auto
    with pytest.raises(TypeError, match="no truth value"):
        bool(px.Auto)
    with pytest.raises(TypeError, match=r"no relation 'tangency' \(coincident, tangent"):
        px.Auto({"tangency": True})
    with pytest.raises(TypeError, match="'tangent' is True or False"):
        px.Auto({"tangent": 1})


def test_a_drawing_call_with_auto_finds_its_relations_for_that_call_only():
    sk = px.Sketch().Move((1, 1))
    sk.Line(10, px.Auto({"horizontal": True}))
    assert kinds(sk)["Horizontal"] == ['"Line.1", px.Horizontal']
    assert sk.auto_constraint is False
    sk.Move((1, 5)).Line(10)
    assert len(sk.constraints["Horizontal"]) == 1


def test_auto_goes_after_the_values_once_and_not_in_a_quicksketch_or_a_new_sketch():
    with pytest.raises(TypeError, match="px.Auto is given twice"):
        px.Sketch().Move((1, 1)).Line(10, px.Auto, px.Auto)
    with pytest.raises(TypeError, match="come after the other arguments"):
        px.Sketch().Move((1, 1)).Line(px.Auto, 10)
    with pytest.raises(ValueError, match=r"Line\(px.Auto\): a QuickSketch has no constraints"):
        px.QuickSketch().Move((1, 1)).Line(10, px.Auto)
    with pytest.raises(TypeError, match="a new sketch draws nothing yet"):
        px.Sketch((0, 0), px.Auto)


def test_auto_constraint_takes_true_false_or_a_dict_never_auto():
    with pytest.raises(TypeError, match="True, False or a dict of relations"):
        px.Sketch(auto_constraint=px.Auto)


def test_copy_tools_tie_by_class_and_take_true_or_false():
    sk = px.Sketch().Move((1, 1)).Line(4, id="a")
    sk.Mirrored("a", px.V)
    assert "Mirrored.1" in str(sk.constraints)
    quick = px.QuickSketch().Move((1, 1)).Line(4, id="a").Mirrored("a", px.V)
    assert len(quick.elements["Line"]) == 2
    with pytest.raises(TypeError, match="True or False; a tie has no relations to switch"):
        px.Sketch().Move((1, 1)).Line(4, id="a").Mirrored("a", px.V, auto_constraint={"tangent": True})
    with pytest.raises(TypeError, match=r"Split\(auto_constraint=None\): True or False"):
        px.Sketch().Line((0, 0), (4, 0)).Line((2, -1), (2, 1)).Split("Line.1", auto_constraint=None)
    assert "auto_constraint: bool = True" in px.Sketch.Mirrored.__doc__
    assert "auto_constraint: bool = False" in px.QuickSketch.Mirrored.__doc__


@pytest.mark.parametrize("call, text", [
    (lambda: px.Sketch().Circle((0, 0), 1).Move((3, 0)).Circle(1).CircularPattern("Circle.2", "Circle.1.center", 4, angle=None),
     r"CircularPattern\(angle=None\): None is no value here; the default px.Auto works it out"),
    (lambda: px.Sketch().Move((1, 1)).Line(4, id="a").Offset("a", 1, both_sides=None), r"Offset\(both_sides=None\)"),
    (lambda: px.Sketch(error_on_branch_change=None), r"Sketch\(error_on_branch_change=None\)"),
    (lambda: px.DeformableShape(px.Face([px.Circle((0, 0), 1)]), 0.1, min_distance=None), r"min_distance=None"),
    (lambda: px.measurements.TorsionConstant(maxh=None), r"TorsionConstant\(maxh=None\)"),
    (lambda: px.Sketch().Move((1, 1)).Line(4).Offset("Line.1", 1, both_sides=px.Auto({"tangent": True})),
     "nothing to switch here")])
def test_computed_defaults_are_auto_and_none_raises(call, text):
    with pytest.raises(TypeError, match=text):
        call()


def test_the_computed_defaults_show_auto():
    assert "angle: float | Auto = px.Auto" in px.Sketch.CircularPattern.__doc__
    assert "both_sides: bool | Auto = px.Auto" in px.Sketch.Offset.__doc__
    assert inspect.signature(px.Plot3d).parameters["plane_size"].default is px.Auto
    assert inspect.signature(px.FromDXF).parameters["unit"].default is px.Auto
    pattern = px.Sketch().Circle((0, 0), 1).Move((3, 0)).Circle(1).CircularPattern("Circle.2", "Circle.1.center", 4, angle=px.Auto)
    assert pattern["CircularPattern.1.angle"] == pytest.approx(90)


def test_an_identification_found_at_export_reads_as_auto():
    face = px.Face([px.FromPolyline([(0, 0), (2, 0), (2, 1), (0, 1)], closed=True)])
    face.edges[3].name, face.edges[1].name = "left", "right"
    face.Identify("left", "right")
    assert face.identifications["left-right"][2] is px.Auto
    with pytest.raises(TypeError, match=r"Identify\(trafo=None\)"):
        face.Identify("left", "right", None)
