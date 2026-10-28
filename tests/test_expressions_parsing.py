# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""ConstraintSketch.Expression read and solved: ids, numbers, operators, functions, units, measures, equations, systems,
what happens to expressions when the sketch is edited, the messages, and random expressions evaluated in C++ against
Python"""
import math
import random
import re
import warnings

import pytest

import planex as px
from test_expressions import evaluated, parse

TOL = 1e-12


def plate(height=2.0, ht=2.0, reference=False):
    """a rectangle at the origin, its bottom w = 4 and its left side drawn height high, held by ht (None: free; reference:
    a reference dimension)"""
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0, px.Horizontal, id="bottom").Line(0, height, px.Perpendicular, id="right") \
        .Line(-4, 0, px.Perpendicular, id="top").Close(px.Perpendicular, id="left")
    sk.Constrain("bottom", px.Distance, 4, id="w")
    if reference:
        sk.Constrain("left", px.Distance, id="ht", reference=True)
    elif ht is not None:
        sk.Constrain("left", px.Distance, ht, id="ht")
    return sk


def length(sk, line):
    return sk.Geometry(line)["length"]


def refused(sk, text, message):
    with pytest.raises(ValueError, match=re.escape("Expression '%s': %s" % (text, message))):
        sk.Expression(text)


# ids

@pytest.mark.parametrize("name", ["hot spot", "wall_2", "gap 2", "A0"])
@pytest.mark.constraints
def test_ids_with_spaces_underscores_and_digits(name):
    sk = plate()
    sk.Constrain("top", px.Distance, id=name, reference=True)
    sk.Expression("ht = %s/4" % name)
    assert sk["ht"] == pytest.approx(1, abs=TOL) and length(sk, "left") == pytest.approx(1, abs=TOL)


@pytest.mark.parametrize("name", ["Ø ring", "φ", "gap 2.5", "x.y", "hot  spot", "a\tb"])
@pytest.mark.constraints
def test_ids_with_non_ascii_letters_dots_double_spaces_or_tabs_raise(name):
    sk = plate()
    with pytest.raises(ValueError, match="(contains|must start with a letter)"):
        sk.Constrain("top", px.Distance, id=name, reference=True)


@pytest.mark.parametrize("name, text, meaning", [("mm", "ht = 1000*mm", "the unit mm"), ("inch", "ht = inch", "the unit inch"),
                                                 ("pi", "ht = pi", "the constant pi"), ("sqrt", "ht = sqrt(4)", "the function sqrt"),
                                                 ("min", "ht = min(1, 2)", "the function min")])
@pytest.mark.constraints
def test_an_id_that_is_also_a_unit_constant_or_function_is_ambiguous(name, text, meaning):
    sk = plate()
    sk.Constrain("top", px.Distance, id=name, reference=True)
    refused(sk, text, "'%s' is both a dimension and %s; rename the dimension" % (name, meaning))
    assert sk.expressions == {} and sk["ht"] == 2


@pytest.mark.constraints
def test_e_is_no_constant():
    sk = plate()
    sk.Constrain("top", px.Distance, id="e", reference=True)
    sk.Constrain("bottom", px.Distance, id="e2", reference=True)
    sk.Expression("ht = e/8 + e2/8")
    assert sk["ht"] == pytest.approx(1, abs=TOL)


CREATORS = {
    "Constrain": lambda sk, i: sk.Constrain("top", px.Distance, id=i, reference=True),
    "Vertex": lambda sk, i: sk.Vertex(i, (5, 5)),
    "Expression": lambda sk, i: sk.Expression("w = 4", id=i),
    "Circle": lambda sk, i: sk.Circle((9, 9), 1, id=i),
    "Arc": lambda sk, i: sk.Move((9, 0)).Arc(1, 90, id=i),
    "LinearPattern": lambda sk, i: sk.LinearPattern("top", px.H, 2, 10, id=i),
    "Rectangle": lambda sk, i: sk.Move((20, 0)).Rectangle(1, 1, id=i),
    "Close": lambda sk, i: sk.Move((30, 0)).Line(1, 0).Line(0, 1).Close(id_wire=i),
}


@pytest.mark.parametrize("creator", list(CREATORS))
@pytest.mark.constraints
def test_no_kind_of_id_may_contain_an_operator(creator):
    sk = plate()
    for c in "+-*/^()=,%":
        with pytest.raises(ValueError, match=re.escape("id 'a%sb' contains '%s': ids may not contain + - * / ^ ( ) = , %%" % (c, c))):
            CREATORS[creator](sk, "a%sb" % c)
    assert sk.expressions == {} and set(sk.dimensions) == {"w", "ht"}


@pytest.mark.parametrize("name", ["gap ", "\u00a0gap", "\u00b2gap"], ids=["trailing space", "leading no-break space", "leading superscript"])
def test_an_id_no_expression_can_name_is_refused(name):
    sk = plate()
    with pytest.raises(ValueError, match="id '"):
        sk.Constrain("top", px.Distance, id=name, reference=True)


# numbers

E_DIMENSIONS = {"h": "length", "e": "length", "e2": "length"}


def right_side(text, **values):
    """the C++ value of the right side of "h = text", with the dimensions e and e2"""
    parsed = parse("h = " + text, dimensions=E_DIMENSIONS)
    values["h"] = 0.0
    return -px._planex._EvaluateExpression(parsed["nodes"], [values[leaf[1]] for leaf in parsed["names"]])["value"]


@pytest.mark.parametrize("text, value", [("2.5", 2.5), (".5", 0.5), ("1e-3", 1e-3), ("1E3", 1e3), ("1e+3", 1e3), ("5.", 5.0),
                                         ("12.5e-1", 1.25), ("2e2", 200.0), ("e", 0.5), ("2*e", 1.0), ("e2", 3.0),
                                         ("1e-3*e", 5e-4), ("e-3", -2.5), ("e2-1E3", -997.0), ("e^2", 0.25), ("1e1^e2", 1000.0),
                                         ("e2*.5e1", 15.0)])
@pytest.mark.constraints
def test_numbers_are_read_before_names(text, value):
    assert right_side(text, e=0.5, e2=3.0) == pytest.approx(value, rel=1e-15)


@pytest.mark.parametrize("text, message", [("2e", "cannot read it (a number followed by a name: write 2*e)"),
                                           ("2 e2", "cannot read it (a number followed by a name: write 2*e2)"),
                                           ("1e", "cannot read it (a number followed by a name: write 1*e)"),
                                           ("1.5.5", "cannot read it (a number followed by a number: write the operator between them)"),
                                           (".e5", "cannot read it at '.e5'"), ("1_000", "cannot read it at '_000' (a name starts with a letter)")])
def test_numbers_that_cannot_be_read(text, message):
    with pytest.raises(ValueError, match=re.escape("Expression 'h = %s': %s" % (text, message))):
        parse("h = " + text, dimensions=E_DIMENSIONS)


# operators

@pytest.mark.parametrize("text, value", [("a + b*a", 8), ("(a + b)*a", 10), ("a - b - 1", -2), ("a - (b - 1)", 0), ("a/b/2", 1 / 3),
                                         ("a/(b/2)", 4 / 3), ("a^b", 8), ("a**b", 8), ("a^b^2", 512), ("a**b**2", 512),
                                         ("(a^b)^2", 64), ("2^a^b", 256), ("-a^2", -4), ("(-a)^2", 4), ("-a^b", -8),
                                         ("a^-1", 0.5), ("a^-b^2", 2.0**-9), ("2*-a^2", -8), ("a*-b", -6), ("-a*b", -6),
                                         ("a/-b", -2 / 3), ("a - -b", 5), ("--a", 2), ("- - a", 2), ("-(-a)", 2), ("+a", 2),
                                         ("+-a", -2), ("((a))", 2), ("((a + b)*(a - b))", -5), ("a^b*2", 16), ("2*a^b", 16)])
@pytest.mark.constraints
def test_precedence_power_and_unary_minus(text, value):
    got = evaluated("h = " + text, h=0.0, a=2.0, b=3.0)
    assert (got["error"], -got["value"]) == (None, pytest.approx(value, rel=1e-15))


@pytest.mark.parametrize("text, message", [("(w", "'(' was never closed"), ("w)", "unmatched ')'"), ("((w)", "'(' was never closed"),
                                           ("w))", "unmatched ')'"), ("(", ""), (")", ""), ("()", ""),
                                           ("w ^^ 2", ""), ("w * * 2", ""), ("w *** 2", ""), ("w, 2", ""), ("(w)(2)", ""),
                                           ("2(w)", ""), ("sqrt(4)(2)", "")])
def test_brackets_and_operators_that_do_not_fit(text, message):
    with pytest.raises(ValueError, match=re.escape("Expression 'h = %s': cannot read it" % text) + ".*" + re.escape(message)):
        parse("h = " + text)


def test_a_name_before_a_bracket_is_read_as_a_call():
    with pytest.raises(ValueError, match=re.escape("Expression 'h = )w(': 'w' is a dimension, not a function")):
        parse("h = )w(")


# functions

@pytest.mark.parametrize("text, value", [("sqrt(6.25)", 2.5), ("abs(-2.5)", 2.5), ("abs(2.5)", 2.5), ("min(2, 3)", 2), ("min(3, 1, 2)", 1),
                                         ("max(2, 3)", 3), ("max(3, 1, 2)", 3), ("sin(30)", 0.5), ("sin(-90)", -1), ("cos(60)", 0.5),
                                         ("cos(180)", -1), ("tan(45)", 1), ("tan(-45)", -1), ("asin(0.5)", 30), ("asin(-1)", -90),
                                         ("acos(0.5)", 60), ("acos(-1)", 180), ("atan2(1, 1)", 45), ("atan2(1, -1)", 135),
                                         ("atan2(-1, -1)", -135), ("atan2(0, -1)", 180), ("atan2(-1, 0)", -90)])
@pytest.mark.constraints
def test_every_function_in_degrees(text, value):
    got = evaluated("h = " + text, h=0.0)
    assert (got["error"], -got["value"]) == (None, pytest.approx(value, abs=1e-13))


@pytest.mark.parametrize("text, message", [("sqrt()", "sqrt takes 1 argument, 0 given"), ("sqrt(1, 2)", "sqrt takes 1 argument, 2 given"),
                                           ("abs(1, 2)", "abs takes 1 argument, 2 given"), ("sin()", "sin takes 1 argument, 0 given"),
                                           ("acos(1, 2)", "acos takes 1 argument, 2 given"), ("atan2(1)", "atan2 takes 2 arguments, 1 given"),
                                           ("atan2(1, 2, 3)", "atan2 takes 2 arguments, 3 given"),
                                           ("min(1)", "min takes 2 or more arguments, 1 given"),
                                           ("max()", "max takes 2 or more arguments, 0 given")])
def test_wrong_argument_counts(text, message):
    with pytest.raises(ValueError, match=re.escape("Expression 'h = %s': %s" % (text, message))):
        parse("h = " + text)


@pytest.mark.parametrize("text, message", [("floor(w)", "no function 'floor' (sqrt, abs, min, max, sin, cos, tan, asin, acos, atan2, exp, log, sinh, cosh, tanh, asinh, acosh, atanh)"),
                                           ("Sqrt(w)", "no function 'Sqrt' (sqrt, abs,"), ("sqr(w)", "no function 'sqr' (sqrt, abs,"),
                                           ("w(2)", "'w' is a dimension, not a function"),
                                           ("Line.2.length(2)", "'Line.2.length' is the length of Line.2, not a function"),
                                           ("pi(2)", "'pi' is the constant pi, not a function"),
                                           ("mm (2)", "'mm' is the unit mm, not a function"), ("sqrt", "'sqrt' is a function: sqrt(...)")])
def test_unknown_functions_and_names_that_are_no_function(text, message):
    with pytest.raises(ValueError, match=re.escape("Expression 'h = %s': %s" % (text, message))):
        parse("h = " + text)


def arm():
    """a line from a fixed point, its direction phi = 30 degrees and its length reach = 1"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1, 0.2, id="arm")
    sk.Constrain(px.H, "arm", px.Angle, 30, id="phi")
    sk.Constrain("arm", px.Distance, 1, id="reach")
    return sk


@pytest.mark.constraints
def test_angle_dimensions_are_degrees_and_removing_an_expression_gives_the_value_back():
    sk = arm()
    sk.Expression("reach = phi/10")
    assert sk["reach"] == pytest.approx(3, abs=TOL) and length(sk, "arm") == pytest.approx(3, abs=TOL)
    sk.Expression("phi = 2*asin(0.5)")
    assert sk["phi"] == pytest.approx(60, abs=1e-10) and sk.Geometry("arm")["angle"] == pytest.approx(60, abs=1e-10)
    assert sk["reach"] == pytest.approx(6, abs=TOL)
    sk.RemoveExpression("Expression.2")
    assert sk.expressions == {"Expression.1": "reach = phi/10"} and sk["phi"] == pytest.approx(60, abs=1e-10)
    sk.Update({"phi": 45})
    assert sk.Geometry("arm")["angle"] == pytest.approx(45, abs=1e-10) and sk["reach"] == pytest.approx(4.5, abs=TOL)


# units and the constant

@pytest.mark.parametrize("unit", ["mm", "cm", "m", "um", "inch", "mil"])
@pytest.mark.constraints
def test_units_are_the_values_of_px(unit):
    assert evaluated("h = 3*%s" % unit, h=0.0)["value"] == -3 * getattr(px, unit)


@pytest.mark.constraints
def test_pi_and_a_unit_in_a_sketch():
    assert evaluated("h = pi", h=0.0)["value"] == -math.pi
    sk = plate()
    sk.Expression("ht = 1000*mm + pi*0")
    assert length(sk, "left") == pytest.approx(1, abs=TOL)


# measures

def zoo():
    """an element of every kind, each held where it is, a copy of L in a pattern, and the line X (its length x = 1) for
    an expression to drive"""
    sk = px.Sketch()
    sk.Move((1, 2)).Line(3, 4, px.Fix, id="L")
    sk.Move((0, -5)).Line(-3, -4, px.Fix, id="down")
    sk.Move((10, 0), dir=(1, 0)).Arc(2, 90, px.Fix, id="A")
    sk.Move((10, -10), dir=(1, 0)).Arc(2, -90, px.Fix, id="cw")
    sk.Circle((30, 0), 1.5, px.Fix, id="C")
    sk.Vertex("P", (-3, 7))
    sk.Constrain("P", px.Fix)
    sk.Move((0, 20)).Spline([(1, 21), (2, 20), (3, 22)], px.Fix, id="S")
    sk.LinearPattern("L", px.H, 2, 50)
    sk.Move((40, 0), px.Fix).Line(1, 0, px.Horizontal, id="X")
    sk.Constrain("X", px.Distance, 1, id="x")
    return sk


def geometry(element, key):
    return lambda sk: sk.Geometry(element)[key]


def location(ref, k):
    return lambda sk: sk.Location(ref)[k]


MEASURES = {"L.length": geometry("L", "length"), "L.angle": geometry("L", "angle"), "down.angle": geometry("down", "angle"),
            "A.r": geometry("A", "r"), "A.sweep": geometry("A", "sweep"), "cw.sweep": geometry("cw", "sweep"),
            "C.r": geometry("C", "r"), "LinearPattern.1.1.L.length": geometry("LinearPattern.1.1.L", "length"),
            "LinearPattern.1.1.L.angle": geometry("LinearPattern.1.1.L", "angle")}
for ref in ("P", "origin", "L.start", "L.end", "L.mid", "down.end", "A.center", "A.start", "A.end", "A.mid", "cw.mid", "C.center",
            "S.p2", "S.start", "S.end", "LinearPattern.1.1.L.end"):
    MEASURES[ref + ".h"] = location(ref, 0)
    MEASURES[ref + ".v"] = location(ref, 1)


@pytest.mark.parametrize("measure", list(MEASURES))
@pytest.mark.constraints
def test_every_measure_reads_the_solved_geometry(measure):
    sk = zoo()
    want = 50 + MEASURES[measure](sk) / 10
    sk.Expression("x = 50 + %s/10" % measure)
    assert sk["x"] == pytest.approx(want, abs=1e-9) and length(sk, "X") == pytest.approx(want, abs=1e-9)
    assert [i for i in sk.dimensions if i.startswith("Reference.")] == ["Reference.1"]


@pytest.mark.constraints
def test_measures_drive_free_geometry():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1, 0.5, id="f")
    sk.Circle((5, 0), 1, id="c")
    sk.Constrain("c.center", px.Fix)
    sk.Vertex("P", (1, 3))
    for text in ("f.length = 2.5", "f.angle = 60", "c.r = 0.75", "P.h = -2", "P.v = f.end.v + 1"):
        sk.Expression(text)
    g = sk.Geometry("f")
    assert (g["length"], g["angle"]) == (pytest.approx(2.5, abs=TOL), pytest.approx(60, abs=1e-10))
    assert sk.Geometry("c")["r"] == pytest.approx(0.75, abs=TOL) and sk.dof == 0
    assert sk.Location("P") == pytest.approx((-2, 2.5 * math.sin(math.radians(60)) + 1), abs=TOL)


WRONG_KIND = [("x = L.r", "a line has no measure 'r' (length, angle)"), ("x = L.sweep", "a line has no measure 'sweep' (length, angle)"),
              ("x = L.x", "a line has no measure 'x' (length, angle)"), ("x = A.length", "an arc has no measure 'length' (r, sweep)"),
              ("x = A.angle", "an arc has no measure 'angle' (r, sweep)"), ("x = C.sweep", "a circle has no measure 'sweep' (r)"),
              ("x = C.length", "a circle has no measure 'length' (r)"), ("x = S.length", "a spline has no measure 'length'"),
              ("x = P.length", "a point has no measure 'length' (h, v)"), ("x = P.x", "a point has no measure 'x' (h, v)"),
              ("x = L.end.r", "a point has no measure 'r' (h, v)"), ("x = H.angle", "an axis has no measure 'angle'"),
              ("x = LinearPattern.1.length", "a pattern has no measure 'length'"),
              ("x = L", "'L' is an element, not a value (its measures: L.length, L.angle)"),
              ("x = C", "'C' is an element, not a value (its measures: C.r)"), ("x = S", "'S' is an element, not a value"),
              ("x = P", "'P' is a point, not a value (its measures: P.h, P.v)"),
              ("x = L.end", "'L.end' is a point, not a value (its measures: L.end.h, L.end.v)")]


@pytest.mark.parametrize("text, message", WRONG_KIND)
@pytest.mark.constraints
def test_a_measure_on_the_wrong_kind(text, message):
    sk = zoo()
    refused(sk, text, message)
    assert sk.expressions == {} and sk["x"] == 1


NO_SUCH_POINT = ["x = L.center.h", "x = C.start.v", "x = C.mid.h", "x = P.end.h"]


@pytest.mark.parametrize("text", NO_SUCH_POINT)
def test_a_point_part_the_element_lacks_is_refused_naming_the_expression(text):
    with pytest.raises(ValueError, match="^" + re.escape("Expression '%s': " % text)):
        zoo().Expression(text)


def test_a_line_has_no_centre_to_name():
    with pytest.raises(ValueError) as e:
        zoo().Expression("x = L.center")
    assert str(e.value).startswith("Expression 'x = L.center': ") and "is a point" not in str(e.value)


@pytest.mark.parametrize("text", NO_SUCH_POINT)
@pytest.mark.constraints
def test_a_refused_measure_leaves_the_sketch_as_it_was(text):
    sk = zoo()
    with pytest.raises(ValueError):
        sk.Expression(text)
    assert sk.expressions == {} and not [i for i in sk.dimensions if i.startswith("Reference.")] and sk["x"] == 1


@pytest.mark.constraints
def test_a_measure_of_a_removed_element():
    sk = zoo()
    sk.Remove("C")
    refused(sk, "x = C.r", "unknown id 'C.r'")


def test_a_pattern_tie_is_no_value_and_the_message_names_no_missing_kind():
    with pytest.raises(ValueError) as e:
        zoo().Expression("x = LinearPattern.1.copy.1")
    assert str(e.value).startswith("Expression 'x = LinearPattern.1.copy.1': ") and "px.Copy" not in str(e.value)


# equations

@pytest.mark.parametrize("text, message", [("ht", "one '=' (left side = right side), found none"), ("", "found none"),
                                           ("ht = w = 2", "found 2"), ("ht == w", "found 2"), ("= w", "the left side is empty"),
                                           ("ht =", "the right side is empty"), (" = ", "the left side is empty")])
@pytest.mark.constraints
def test_one_equals_sign_and_no_empty_side(text, message):
    sk = plate()
    with pytest.raises(ValueError, match=re.escape("Expression '%s': " % text) + ".*" + re.escape(message)):
        sk.Expression(text)
    with pytest.raises(ValueError, match=re.escape("Expression(' w ', '2 = 3'): a side has no '='")):
        sk.Expression(" w ", "2 = 3")
    with pytest.raises(ValueError, match=re.escape("Expression 'ht = ': the right side is empty")):
        sk.Expression("ht", "")
    assert sk.expressions == {}


@pytest.mark.parametrize("text, drives", [("ht = w/2", True), (" ht  = w/2", True), ("(ht) = w/2", False), ("+ht = w/2", False),
                                          ("ht*1 = w/2", False), ("w/2 = ht", False)])
@pytest.mark.constraints
def test_only_a_bare_id_on_the_left_is_driven(text, drives):
    sk = plate()
    if drives:
        sk.Expression(text)
        with pytest.raises(ValueError, match=re.escape("ht is driven by Expression.1")):
            sk["ht"] = 2
    else:
        with pytest.warns(px.PlanexWarning, match=re.escape("Expression.1 (%s) is redundant (consistent)" % text)):
            sk.Expression(text)
        sk["ht"] = 2
        assert "ht" in sk.redundant
    assert sk.expressions == {"Expression.1": text}


@pytest.mark.constraints
def test_a_driven_dimension_refuses_every_way_of_setting_it():
    sk = plate()
    sk.Expression("ht = w/2")
    for value in (3, 2):
        with pytest.raises(ValueError, match=re.escape("ht = %d: ht is driven by Expression.1 (ht = w/2); remove the expression to set it" % value)):
            sk["ht"] = value
        with pytest.raises(ValueError, match=re.escape("ht is driven by Expression.1 (ht = w/2)")):
            sk.Update({"ht": value}, steps=3)
    assert sk["ht"] == pytest.approx(2, abs=TOL) and sk.expressions == {"Expression.1": "ht = w/2"}


def test_an_update_refused_for_a_driven_dimension_sets_none_of_its_values():
    sk = plate(1.0, 1.0)
    sk.Expression("w = 4*ht")
    with pytest.raises(ValueError, match=re.escape("w is driven by Expression.1")):
        sk.Update({"ht": 1.5, "w": 6})
    assert sk["ht"] == 1 and length(sk, "left") == pytest.approx(1, abs=TOL)


@pytest.mark.constraints
def test_remove_expression_gives_the_driven_value_back():
    sk = plate()
    sk.Expression("ht = w/8")
    sk.Update({"w": 6})
    sk.RemoveExpression("Expression.1")
    assert sk["ht"] == pytest.approx(0.75, abs=TOL) and float(repr(sk.constraints["Distance"]["ht"]).rsplit(", ", 1)[1]) == pytest.approx(0.75, abs=TOL)
    sk["ht"] = 2
    sk.Update({"w": 3})
    assert (length(sk, "left"), length(sk, "bottom")) == (pytest.approx(2, abs=TOL), pytest.approx(3, abs=TOL))


# systems

def chain3():
    """the lines A, B, C in a chain from a fixed point, their lengths a = b = c = 1"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1, 0, px.Horizontal, id="A").Line(0, 1, px.Vertical, id="B").Line(1, 0, px.Horizontal, id="C")
    for line, name in (("A", "a"), ("B", "b"), ("C", "c")):
        sk.Constrain(line, px.Distance, 1, id=name)
    return sk


@pytest.mark.constraints
def test_expressions_of_dimensions_other_expressions_drive():
    sk = chain3()
    sk.Expression("b = a + 1")
    sk.Expression("c = 2*a + b")
    assert [sk[n] for n in "abc"] == pytest.approx([1, 2, 4], abs=TOL)
    sk.Update({"a": 1.5})
    assert [length(sk, n) for n in "ABC"] == pytest.approx([1.5, 2.5, 5.5], abs=TOL) and sk.dof == 0


@pytest.mark.constraints
def test_a_consistent_cycle_is_redundant():
    sk = chain3()
    sk.Expression("a = b + 1")
    sk.Expression("b = c + 1")
    with pytest.warns(px.PlanexWarning, match=re.escape("Expression.3 (c = a - 2) is redundant (consistent)")):
        sk.Expression("c = a - 2")
    assert [sk[n] for n in "abc"] == pytest.approx([3, 2, 1], abs=TOL)
    assert sk.dof == 1 and {"Expression.1", "Expression.2", "Expression.3"} <= set(sk.redundant)


@pytest.mark.constraints
def test_a_contradicting_cycle_is_suspended():
    sk = chain3()
    sk.Expression("a = b + 1")
    with pytest.warns(px.PlanexWarning, match="no solution, it contradicts the constraints there are"):
        sk.Expression("b = a + 1")
    assert sk.status == "over" and [sk[n] for n in "ab"] == pytest.approx([2, 1], abs=TOL)


@pytest.mark.constraints
@pytest.mark.parametrize("text", ["bottom.length = 5", "1 = 2", "ht = ht + 1", "origin.h = 1"])
def test_an_expression_against_the_geometry_is_suspended(text):
    sk = plate()
    with pytest.warns(px.PlanexWarning, match=re.escape("%s: no solution" % text)):
        sk.Expression(text)
    assert sk.status == "over" and {"w", "ht"} <= set(sk.dimensions)


@pytest.mark.parametrize("text, message", [("ht = w/(w - 4)", "division by zero"), ("ht = asin(w/2)", "asin of 2, outside [-1, 1]"),
                                           ("ht = acos(w)", "acos of 4, outside [-1, 1]"), ("ht = sqrt(3 - w)", "sqrt of a negative number (-1)"),
                                           ("ht = (0 - w)^0.5", "a negative number (-4) to the power 0.5"),
                                           ("ht = (w - 4)^(0 - 1)", "0 to a negative power"),
                                           ("ht = tan(90*w/4)", "tan of 90 degrees"), ("ht = atan2(w - 4, w - 4)", "atan2(0, 0) has no direction"),
                                           ("sqrt(w - 5) = ht", "sqrt of a negative number (-1)")])
@pytest.mark.constraints
def test_a_domain_error_where_the_expression_is_added(text, message):
    sk = plate()
    refused(sk, text, message)
    assert sk.expressions == {} and sk["ht"] == 2 and length(sk, "left") == pytest.approx(2, abs=TOL)


@pytest.mark.parametrize("text, w, message", [("ht = 1/(w - 3)", 3, "division by zero"), ("ht = 1 + asin(w/8)/30", 9, "asin of 1.125, outside [-1, 1]"),
                                              ("ht = sqrt(w - 3)", 2, "sqrt of a negative number (-1)")])
@pytest.mark.constraints
def test_a_domain_error_where_an_update_goes(text, w, message):
    sk = plate()
    sk.Expression(text)
    ht = sk["ht"]
    with pytest.raises(ValueError, match=re.escape("Update(w = %d): no solution, nothing changed; Expression.1 (%s): %s there" % (w, text, message))):
        sk.Update({"w": w})
    assert (sk["w"], sk["ht"]) == (4, pytest.approx(ht, abs=TOL)) and length(sk, "left") == pytest.approx(ht, abs=TOL)


def domain_by_expression():
    sk = plate(1.0, 1.0)
    sk.Expression("ht = sqrt(w - 3)")
    sk.Expression("w = 2")


def domain_by_constraint():
    sk = plate(1.0, None)
    sk.Remove("w")
    sk.Constrain("bottom", px.Distance, id="w", reference=True)
    sk.Constrain("left", px.Distance, 1, id="ht")
    sk.Expression("ht = sqrt(w - 3)")
    sk.Constrain("top", px.Distance, 2)


def domain_by_scale():
    sk = plate(1.0, 1.0)
    sk.Expression("ht = sqrt(w - 3)")
    sk.Scale((0, 0), 0.5)


@pytest.mark.parametrize("solve", [domain_by_expression, domain_by_constraint, domain_by_scale])
def test_a_domain_error_in_any_solve_names_the_expression(solve):
    with pytest.raises(ValueError, match=re.escape("Expression.1 (ht = sqrt(w - 3)): sqrt of a negative number")):
        solve()


# what happens to expressions when the sketch is edited

@pytest.mark.constraints
def test_removing_a_driven_dimension_removes_its_expression():
    sk = plate()
    sk.Expression("ht = w/4")
    with pytest.warns(px.PlanexWarning, match=re.escape("Remove('ht'): removed Expression.1 (ht = w/4), it used ht")):
        sk.Remove("ht")
    assert sk.expressions == {} and set(sk.dimensions) == {"w"} and sk.dof == 1
    sk.Update({"w": 5})
    assert sk.expressions == {} and sk["w"] == 5 and length(sk, "bottom") == pytest.approx(5, abs=TOL)


@pytest.mark.constraints
def test_removing_a_measured_point_removes_the_expression():
    sk = plate()
    sk.Vertex("P", (1, 1))
    sk.Constrain("P", px.Fix)
    sk.Expression("ht = 2*P.v")
    with pytest.warns(px.PlanexWarning, match=re.escape("Remove('P'): removed Expression.1 (ht = 2*P.v), it used Reference.1")):
        sk.Remove("P")
    assert sk.expressions == {} and set(sk.dimensions) == {"w", "ht"} and sk["ht"] == pytest.approx(2, abs=TOL)
    sk["ht"] = 3
    assert length(sk, "left") == pytest.approx(3, abs=TOL)


@pytest.mark.constraints
def test_split_takes_a_measured_end_to_its_piece():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(4, 0, px.Horizontal, id="base")
    sk.Move((1, -1), px.Fix).Line(0, 2, px.Vertical, id="post")
    sk.Constrain("post", px.Distance, 2, id="p")
    sk.Expression("p = base.end.h/2")
    sk.Split("base", "post")
    assert sk.expressions == {"Expression.1": "p = base.end.h/2"}
    assert repr(sk.constraints["Distance"]["Reference.1"]) == '"base.end", px.V, px.Distance, reference=True'
    sk.Constrain("base.2.end", px.V, px.Distance, 6)
    assert sk["p"] == pytest.approx(3, abs=TOL) and length(sk, "post") == pytest.approx(3, abs=TOL)


@pytest.mark.constraints
def test_trim_keeps_a_measured_direction():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, px.Horizontal, id="base")
    sk.Move((1, 2)).Line(0, -3, px.Vertical, id="post")
    sk.Move((10, 0), px.Fix).Line(1, 0.2, id="arm")
    sk.Constrain("arm", px.Distance, 1, id="reach")
    sk.Constrain(px.H, "arm", px.Angle, 10, id="phi")
    sk.Expression("phi = base.angle + 30")
    with warnings.catch_warnings(record=True) as seen:
        warnings.simplefilter("always")
        sk.Trim(("base", (3, 0)), "post")
    assert not [w for w in seen if "Expression" in str(w.message)]
    assert sk.expressions == {"Expression.1": "phi = base.angle + 30"}
    assert repr(sk.constraints["Angle"]["Reference.1"]) == 'px.H, "base", px.Angle, reference=True'
    assert sk["phi"] == pytest.approx(30, abs=1e-10) and sk.Geometry("arm")["angle"] == pytest.approx(30, abs=1e-10)


@pytest.mark.constraints
def test_a_lower_pattern_count_removes_the_expressions_on_its_copies():
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(1, 0.5)
    sk.Constrain("Rectangle.1.Line.1", px.Fix)
    sk.Constrain("Rectangle.1.Line.2", px.Distance, 0.5)
    sk.LinearPattern("Rectangle.1", px.H, 3, 2)
    for k, v in ((1, 5), (2, 7)):
        sk.Move((0, v), px.Fix).Line(1, 0, px.Horizontal, id="X%d" % k)
        sk.Constrain("X%d" % k, px.Distance, 1, id="x%d" % k)
    sk.Expression("x1 = LinearPattern.1.1.Rectangle.1.Line.1.start.h - 1")
    sk.Expression("x2 = LinearPattern.1.2.Rectangle.1.Line.1.start.h - 3")
    with pytest.warns(px.PlanexWarning, match=re.escape("LinearPattern.1.count = 2: removed Expression.2 (x2 = LinearPattern.1.2.Rectangle.1.Line.1.start.h - 3), "
                                                        "it used Reference.2")):
        sk.Update({"LinearPattern.1.count": 2})
    assert sk.expressions == {"Expression.1": "x1 = LinearPattern.1.1.Rectangle.1.Line.1.start.h - 1"}
    assert [i for i in sk.dimensions if i.startswith("Reference.")] == ["Reference.1"] and sk["x2"] == pytest.approx(1, abs=TOL)
    sk["x2"] = 1.5
    sk.Update({"LinearPattern.1.spacing": 3})
    assert (sk["x1"], sk["x2"]) == (pytest.approx(2, abs=TOL), pytest.approx(1.5, abs=TOL))
    assert length(sk, "X1") == pytest.approx(2, abs=TOL) and sk.dof == 0


# messages

@pytest.mark.parametrize("text", ["ht = wdith/4", "ht = bottom", "ht = bottom.end", "ht = sqrt(w, 2)", "ht = w % 2", "ht = (w", "ht = 2w",
                                  "ht = floor(w)", "ht = 1e999", "ht = w/0", "ht = sqrt(0 - w)", "ht = w = 2", "H = 3",
                                  "ht = Horizontal.1", "ht = Reference.9", "ht = bottom.r"])
@pytest.mark.constraints
def test_every_error_names_the_expression(text):
    sk = plate()
    with pytest.raises(ValueError) as e:
        sk.Expression(text)
    assert str(e.value).startswith("Expression '%s': " % text)
    with pytest.raises(ValueError, match=re.escape("Expression 'ht = 3': id 'w' is taken")):
        sk.Expression("ht = 3", id="w")
    assert sk.expressions == {} and set(sk.dimensions) == {"w", "ht"}


@pytest.mark.parametrize("text, message", [("ht = widht/4", "unknown id 'widht' (did you mean 'width'"),
                                           ("ht = botom.length/4", "unknown id 'botom.length' (did you mean 'bottom.length'"),
                                           ("ht = 2*mmm", "unknown id 'mmm' (did you mean 'mm'"), ("ht = pii", "unknown id 'pii' (did you mean 'pi'"),
                                           ("ht = bottom.lenght/4", "a line has no measure 'lenght' (length, angle)"),
                                           ("ht = bottom.end.hh", "a point has no measure 'hh' (h, v)")])
@pytest.mark.constraints
def test_suggestions_for_misspelt_names(text, message):
    sk = plate()
    sk.Constrain("top", px.Distance, id="width", reference=True)
    sk.Constrain("right", px.Distance, id="height", reference=True)
    refused(sk, text, message)


LONG = {"sum": "ht = w/4 + 0*(%s)" % " + ".join(["w"] * 1000), "unary minus": "ht = " + "-" * 1000 + "w/4",
        "power": "ht = w/4 + 0*w" + "^1" * 1000}


@pytest.mark.parametrize("text", list(LONG.values()), ids=list(LONG))
def test_a_long_expression_is_read_or_refused_naming_it(text):
    sk = plate()
    try:
        sk.Expression(text)
    except ValueError as e:
        assert str(e).startswith("Expression 'ht = ")
    else:
        assert sk["ht"] == pytest.approx(1, abs=TOL)


@pytest.mark.constraints
def test_a_hundred_terms_are_read():
    sk = plate()
    sk.Expression(LONG["sum"].replace(" + ".join(["w"] * 1000), " + ".join(["w"] * 100)))
    assert sk["ht"] == pytest.approx(1, abs=TOL)


# random expressions: the C++ evaluation against Python

NAMES = ["h", "w", "width", "hot spot", "a", "b", "phi", "Line.2.length", "Arc.1.r", "p.h", "Line.2.end.v"]
LITERALS = ["2", "3", "4", "0.5", ".5", "2.5", "1e-3", "1E3", "1e+1", "12.5e-1", "7.", "0.25"]
CONSTANTS = {"pi": math.pi, "mm": px.mm, "inch": px.inch}
ARITY = {"neg": 1, "sqrt": 1, "abs": 1, "sin": 1, "cos": 1, "tan": 1, "asin": 1, "acos": 1, "add": 2, "sub": 2, "mul": 2, "div": 2,
         "pow": 2, "atan2": 2, "min": None, "max": None}
PRECEDENCE = {"add": 1, "sub": 1, "mul": 2, "div": 2, "neg": 3, "pow": 4}
SYMBOLS = {"add": "+", "sub": "-", "mul": "*", "div": "/"}
DEGREE = math.pi / 180.0


class Rejected(Exception):
    """a random case near a domain edge, a kink or a branch cut, or with too large values: not checked"""


def need(ok):
    if not ok:
        raise Rejected


def grown(rng, depth, top=True):
    """a random tree: (operation, (extra brackets, space, power sign), arguments...), leaves ("name" | "num" | "const", text);
    its top is an operation"""
    if depth == 0 or (not top and rng.random() < 0.3):
        r = rng.random()
        if r < 0.5:
            return ("name", rng.choice(NAMES))
        return ("num", rng.choice(LITERALS)) if r < 0.85 else ("const", rng.choice(sorted(CONSTANTS)))
    op = rng.choice(sorted(ARITY))
    style = (rng.random() < 0.1, rng.choice(("", " ")), rng.choice(("^", "**")))
    return (op, style) + tuple(grown(rng, depth - 1, False) for _ in range(ARITY[op] or rng.choice((2, 3))))


def written(node, name=lambda n: n, power=None):
    """the text of a tree and its precedence: the fewest brackets that keep its order, and those its style adds"""
    if node[0] in ("name", "const"):
        return name(node[1]), 5
    if node[0] == "num":
        return node[1], 5
    (extra, space, sign), args = node[1], [written(a, name, power) for a in node[2:]]

    def kept(k, keep):
        return args[k][0] if keep else "(" + args[k][0] + ")"

    if node[0] == "neg":
        text = "-" + space + kept(0, args[0][1] >= 3)
    elif node[0] == "pow":
        text = kept(0, args[0][1] > 4) + space + (power or sign) + space + kept(1, args[1][1] >= 3)
    elif node[0] in SYMBOLS:
        p = PRECEDENCE[node[0]]
        text = kept(0, args[0][1] >= p) + space + SYMBOLS[node[0]] + space + kept(1, args[1][1] > p)
    else:
        text = node[0] + "(" + ("," + space).join(a[0] for a in args) + ")"
    return ("(" + text + ")", 5) if extra else (text, PRECEDENCE.get(node[0], 5))


def named(node):
    return node[0] == "name" or (node[0] not in ("num", "const") and any(named(a) for a in node[2:]))


def operated(node, x):
    """an operation of a tree on its argument values, as an expression means it (degrees, ^ the power)"""
    kind = node[0]
    if kind in ("min", "max"):
        need(all(abs(p - q) >= 0.01 for i, p in enumerate(x) for q in x[i + 1:]))
        out = x[0]
        for y in x[1:]:
            out = min(out, y) if kind == "min" else max(out, y)
        return out
    if kind == "add":
        return x[0] + x[1]
    if kind == "sub":
        return x[0] - x[1]
    if kind == "mul":
        return x[0] * x[1]
    if kind == "div":
        need(abs(x[1]) >= 0.05)
        return x[0] / x[1]
    if kind == "pow":
        need(abs(x[0]) >= 0.05 and (x[0] > 0 or (node[3][0] == "num" and float(node[3][1]).is_integer())))
        try:
            return x[0] ** x[1]
        except OverflowError:
            raise Rejected from None
    if kind == "neg":
        return -x[0]
    if kind in ("sqrt", "abs"):
        need(x[0] >= 0.05 if kind == "sqrt" else abs(x[0]) >= 0.05)
        return math.sqrt(x[0]) if kind == "sqrt" else abs(x[0])
    if kind in ("sin", "cos", "tan"):
        need(kind != "tan" or abs(math.cos(x[0] * DEGREE)) >= 0.1)
        return getattr(math, kind)(x[0] * DEGREE)
    if kind in ("asin", "acos"):
        need(abs(x[0]) <= 0.9)
        return getattr(math, kind)(x[0]) * (1.0 / DEGREE)
    need(x[0] * x[0] + x[1] * x[1] >= 0.01 and (x[1] > 0 or abs(x[0]) >= 0.2 * abs(x[1])))
    return math.atan2(x[0], x[1]) * (1.0 / DEGREE)


def evaluated_in_python(node, values, sizes):
    """the value of a tree; sizes gets the size of every value on the way"""
    if node[0] == "name":
        v = values[node[1]]
    elif node[0] == "num":
        v = float(node[1])
    elif node[0] == "const":
        v = CONSTANTS[node[1]]
    else:
        v = operated(node, [evaluated_in_python(a, values, sizes) for a in node[2:]])
    need(abs(v) <= 1e3)
    sizes.append(abs(v))
    return v


def random_cases(count, seed=2026):
    """count random trees of depth up to 4 with names in them, and values for the names: (tree, values, its value, the
    largest value on the way)"""
    rng = random.Random(seed)
    made = 0
    for _ in range(100 * count):
        tree = grown(rng, 4)
        values = {n: rng.choice((-1.0, 1.0)) * rng.uniform(0.3, 4.0) for n in NAMES}
        sizes = []
        if not named(tree):
            continue
        try:
            value = evaluated_in_python(tree, values, sizes)
        except Rejected:
            continue
        yield tree, values, value, max(sizes)
        made += 1
        if made == count:
            return


@pytest.mark.constraints
def test_random_expressions_agree_with_python_and_finite_differences():
    checked = 0
    for tree, values, value, size in random_cases(300):
        text = written(tree)[0] + " = 0"
        got = evaluated(text, **values)
        assert (got["error"], got["value"]) == (None, got["plain"]), text
        # rounding errors are relative to the largest value on the way
        assert abs(got["value"] - value) <= 1e-12 * max(size, 1.0), text
        names = [leaf[1] if leaf[0] == "dimension" else "%s.%s" % leaf[1:] for leaf in parse(text)["names"]]
        for k, name in enumerate(names):
            step = 1e-6 * max(1.0, abs(values[name]))
            up, down = dict(values), dict(values)
            up[name] += step
            down[name] -= step
            try:
                difference = (evaluated_in_python(tree, up, []) - evaluated_in_python(tree, down, [])) / (2 * step)
            except Rejected:
                continue
            # central differences: step^2 times the third derivative, and the rounding of the values over the step
            assert abs(got["gradient"][k] - difference) <= 1e-5 * max(1.0, abs(difference)) + 1e-13 * size / step, (text, name)
        checked += 1
    assert checked == 300


def test_a_driven_length_must_not_change_sign():
    sk = plate()
    with pytest.raises(ValueError, match=re.escape("Expression 'ht = w - 5': ht = -1, a length must not change sign (w = 4)")):
        sk.Expression("ht = w - 5")
    assert sk["ht"] == 2
    sk.Expression("ht = w - 1")
    with pytest.raises(ValueError, match=re.escape("Update(w = 0.5): ht = -0.5, a length must not change sign; nothing changed")):
        sk.Update({"w": 0.5})
    sk.Update({"w": 3})
    assert sk["ht"] == pytest.approx(2)
