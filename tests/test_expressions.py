# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re

import pytest

import planex as px
from planex import _expressions

DIMENSIONS = {"h": "length", "w": "length", "width": "length", "hot spot": "length", "a": "angle", "b": "angle",
              "phi": "angle"}
ELEMENTS = {"Line.2": "Line", "Arc.1": "Arc", "Circle.1": "Circle", "p": "Point", "Spline.1": "Spline"}
POINTS = {"p", "Line.2.start", "Line.2.end", "Line.2.mid", "Arc.1.center", "Circle.1.center", "Spline.1.p3"}
FUNCTIONS = {"sqrt": math.sqrt, "abs": abs, "neg": lambda x: -x, "sin": lambda x: math.sin(math.radians(x)),
             "cos": lambda x: math.cos(math.radians(x)), "tan": lambda x: math.tan(math.radians(x)),
             "asin": lambda x: math.degrees(math.asin(x)), "acos": lambda x: math.degrees(math.acos(x))}
BINARY = {"add": lambda x, y: x + y, "sub": lambda x, y: x - y, "mul": lambda x, y: x * y, "div": lambda x, y: x / y,
          "pow": lambda x, y: x**y, "min": min, "max": max, "atan2": lambda y, x: math.degrees(math.atan2(y, x))}


def parse(text, rhs=None, **names):
    names.setdefault("dimensions", DIMENSIONS)
    names.setdefault("elements", ELEMENTS)
    names.setdefault("ispoint", lambda ref: ref in POINTS)
    return _expressions._Parse(text, rhs, **names)


def value(parsed, **values):
    """the left side minus the right side: the nodes evaluated in Python, values by name ("Line.2.length" for a
    measure)"""
    stack = []
    for op, arg in parsed["nodes"]:
        if op == "num":
            stack.append(arg)
        elif op == "val":
            leaf = parsed["names"][arg]
            stack.append(values[leaf[1] if leaf[0] == "dimension" else "%s.%s" % leaf[1:]])
        elif op in BINARY:
            y = stack.pop()
            stack.append(BINARY[op](stack.pop(), y))
        else:
            stack.append(FUNCTIONS[op](stack.pop()))
    assert len(stack) == 1
    return stack[0]


def right(text, **values):
    """the value of the right side of "h = text\""""
    return -value(parse("h = " + text), h=0.0, **values)


def fails(text, message, rhs=None, **names):
    with pytest.raises(ValueError, match=re.escape(message)):
        parse(text, rhs, **names)


def test_numbers_are_read_whole():
    assert parse("h = 1e-3")["nodes"] == [("val", 0), ("num", 1e-3), ("sub", 0)]
    assert right("w-1e-3", w=1.0) == 1.0 - 1e-3
    assert [right(t) for t in ("2.5", ".5", "5.", "1E+2", "2.5e-1")] == [2.5, 0.5, 5.0, 100.0, 0.25]


def test_names_are_runs_between_operators():
    parsed = parse("h = hot spot / 2 + w")
    assert parsed["names"] == [("dimension", "h"), ("dimension", "hot spot"), ("dimension", "w")]
    assert right(" hot spot /2", **{"hot spot": 3.0}) == 1.5
    assert parse("h = w + w")["names"] == [("dimension", "h"), ("dimension", "w")]
    assert parse("phi = 2*a")["names"] == [("dimension", "phi"), ("dimension", "a")]


def test_units_and_pi_are_planex_values():
    for name in ("mm", "cm", "m", "um", "inch", "mil"):
        assert right("40*" + name) == 40 * getattr(px, name)
    assert right("pi") == math.pi


def test_precedence_brackets_and_powers():
    assert right("2 + 3*4") == 14
    assert right("(2 + 3)*4") == 20
    assert right("2^3^2") == 512
    assert right("-2^2") == -4
    assert right("2*-3") == -6
    assert right("+w", w=2.0) == 2
    assert right("10/4/5") == 0.5
    assert parse("h = w^2")["nodes"] == parse("h = w**2")["nodes"]


def test_functions_take_and_give_degrees():
    assert right("sin(30)") == pytest.approx(0.5, abs=1e-15)
    assert right("cos(60)") == pytest.approx(0.5, abs=1e-15)
    assert right("tan(45)") == pytest.approx(1.0, abs=1e-15)
    assert right("asin(0.5)") == pytest.approx(30.0, abs=1e-13)
    assert right("acos(0.5)") == pytest.approx(60.0, abs=1e-13)
    assert right("atan2(1, -1)") == pytest.approx(135.0, abs=1e-13)
    assert right("sqrt(w*w + 9)", w=4.0) == 5
    assert right("abs(-w)", w=2.0) == 2
    assert right("min(3, w, 1)", w=2.0) == 1 and right("max(3, w, 1)", w=4.0) == 4
    assert right("sqrt (16)") == 4


def test_measures_of_elements_and_points():
    parsed = parse("h = Line.2.length/2 + Arc.1.r")
    assert parsed["names"] == [("dimension", "h"), ("measure", "Line.2", "length"), ("measure", "Arc.1", "r")]
    assert right("Line.2.length/2 + Arc.1.r", **{"Line.2.length": 4.0, "Arc.1.r": 1.0}) == 3
    measures = parse("h = Line.2.angle + Arc.1.sweep + Circle.1.r + p.h + Line.2.start.v + Arc.1.center.h + Spline.1.p3.v")
    assert [n[1:] for n in measures["names"][1:]] == [("Line.2", "angle"), ("Arc.1", "sweep"), ("Circle.1", "r"), ("p", "h"),
                                                      ("Line.2.start", "v"), ("Arc.1.center", "h"), ("Spline.1.p3", "v")]
    assert parse("Line.2.length = w")["drives"] is None


def test_which_dimension_an_equation_drives():
    assert parse("h = w/2")["drives"] == "h"
    assert parse("  hot spot = w")["drives"] == "hot spot"
    assert parse("h/2 = w/2")["drives"] is None
    assert parse("(h) = w")["drives"] is None
    assert parse("sin(30) = 0.5") == {"text": "sin(30) = 0.5", "drives": None, "names": [],
                                      "nodes": [("num", 30.0), ("sin", 0), ("num", 0.5), ("sub", 0)]}


def test_two_sides_given_apart():
    assert parse("h", " w/2") == dict(parse("h = w/2"), text="h = w/2")
    fails("h", "Expression('h', 'w = 2'): a side has no '='", rhs="w = 2")


def test_one_equals_sign():
    fails("h", "Expression 'h': one '=' (left side = right side), found none")
    fails("h = w = 2", "found 2")
    fails("h == w", "found 2")
    fails("h = ", "Expression 'h = ': the right side is empty")
    fails(" = w", "the left side is empty")


def test_unknown_ids_with_suggestions():
    fails("h = wdith/2", "Expression 'h = wdith/2': unknown id 'wdith' (did you mean 'width'?)")
    fails("h = zzz", "Expression 'h = zzz': unknown id 'zzz'")
    fails("h = Line.2.lenght", "a line has no measure 'lenght' (length, angle)")
    fails("h = Line.2.r", "Expression 'h = Line.2.r': a line has no measure 'r' (length, angle)")
    fails("h = Arc.1.length", "an arc has no measure 'length' (r, sweep)")
    fails("h = p.r", "a point has no measure 'r' (h, v)")
    fails("h = Spline.1.length", "a spline has no measure 'length'")
    fails("h = Line.2/2", "'Line.2' is an element, not a value (its measures: Line.2.length, Line.2.angle)")
    fails("h = Line.2.end", "'Line.2.end' is a point, not a value (its measures: Line.2.end.h, Line.2.end.v)")
    fails("n = CircularPattern.1.count", "a pattern's count is no dimension for expressions",
          dimensions={"n": "length"}, others={"CircularPattern.1.count": "count"})
    fails("h = Coincident.1", "'Coincident.1' (px.Coincident) has no value", others={"Coincident.1": "Coincident"})
    fails("h = Expression.1", "'Expression.1' is an expression, not a value", others={"Expression.1": "Expression"})
    fails("h = Line.2.lengths", "a line has no measure 'lengths' (length, angle)")
    fails("h = Line.3.length", "unknown id 'Line.3.length' (did you mean 'Line.2.length'")


def test_a_name_with_two_meanings_is_ambiguous():
    fails("h = m*2", "Expression 'h = m*2': 'm' is both a dimension and the unit m; rename the dimension",
          dimensions={"h": "length", "m": "length"})
    fails("h = sin(30)", "'sin' is both a dimension and the function sin", dimensions={"h": "length", "sin": "length"})
    fails("h = pi", "'pi' is both a dimension and the constant pi", dimensions={"h": "length", "pi": "length"})
    fails("h = gap.h", "'gap.h' is both a dimension and the h coordinate of gap; rename the dimension",
          dimensions={"h": "length", "gap.h": "length"}, elements={"gap": "Point"})


def test_functions_and_operators_that_are_not_allowed():
    fails("h = log(w)", "no function 'log' (sqrt, abs, min, max, sin, cos, tan, asin, acos, atan2)")
    fails("h = w(2)", "'w' is a dimension, not a function")
    fails("h = sqrt", "'sqrt' is a function: sqrt(...)")
    fails("h = atan2(w)", "atan2 takes 2 arguments, 1 given")
    fails("h = sqrt(w, h)", "sqrt takes 1 argument, 2 given")
    fails("h = min(w)", "min takes 2 or more arguments, 1 given")
    fails("h = w % 2", "'%' is not allowed (+ - * / ^ **, brackets and the functions)")
    fails("h = w // 2", "'//' is not allowed")
    fails("h = 2w", "cannot read it (a number followed by a name: write 2*w)")
    fails("h = 2.5.3", "a number followed by a number")
    fails("h = (w", "cannot read it")
    fails("h = w/", "cannot read it (invalid syntax)")
    fails("h = (w, 2)", "Expression 'h = (w, 2)': cannot read it")
    fails("h = _w", "cannot read it at '_w' (a name starts with a letter)")
    fails("h = 1e400", "the number 1e400 is too large")


@pytest.mark.parametrize("c", list("+-*/^()=,%"))
@pytest.mark.constraints
def test_ids_may_not_contain_operators(c):
    sk = px.Sketch()
    with pytest.raises(ValueError, match=re.escape("id 'a%sb' contains '%s': ids may not contain + - * / ^ ( ) = , %%" % (c, c))):
        sk.Move((0, 0)).Line(1, 0, id="a%sb" % c)
    assert "Line" not in sk.elements


@pytest.mark.constraints
def test_ids_start_with_a_letter():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0, id="base")
    for bad in ("2a", "_a", " a", ".a"):
        with pytest.raises(ValueError, match=re.escape("id '%s' must start with a letter" % bad)):
            sk.Constrain("base", px.Distance, 1, id=bad)
    with pytest.raises(ValueError, match="must start with a letter"):
        sk.Vertex("1st", (0, 1))
    with pytest.raises(ValueError, match="contains '-'"):
        sk.Move((0, 2)).Line(1, 0).Finish(id_wire="a-b")
    for good in ("hot spot", "wall_2", "A0"):
        sk.Constrain("base", px.Distance, id=good, reference=True)
    assert {"hot spot", "wall_2", "A0"} <= set(sk.dimensions)
    for bad in ("x.y", "Ø ring", "φ"):
        with pytest.raises(ValueError, match="id '%s' (contains|must start with a letter)" % bad):
            sk.Constrain("base", px.Distance, id=bad, reference=True)


def evaluated(text, **values):
    """the C++ evaluation of the parsed text with values by name: value, plain, gradient (in the order of the names),
    error"""
    parsed = parse(text)
    names = [leaf[1] if leaf[0] == "dimension" else "%s.%s" % leaf[1:] for leaf in parsed["names"]]
    return px._planex._EvaluateExpression(parsed["nodes"], [values[name] for name in names])


@pytest.mark.parametrize("text, values", [
    ("h = w/2", dict(h=1.0, w=3.0)),
    ("h/2 = w/2 + hot spot^2", dict(h=1.0, w=3.0, **{"hot spot": 0.5})),
    ("h = sqrt(w*w + 9) - abs(-w)", dict(h=1.0, w=4.0)),
    ("h = min(w, 2, h*3) + max(w, 2)", dict(h=0.4, w=1.5)),
    ("a = asin(h/w) + acos(h/w/2) + atan2(h, -w)", dict(a=10.0, h=1.0, w=3.0)),
    ("sin(a)*cos(b) = tan(a/2) + w^h", dict(a=30.0, b=50.0, w=2.0, h=1.5)),
    ("h = Line.2.length*sin(Line.2.angle) - p.h", {"h": 1.0, "Line.2.length": 2.0, "Line.2.angle": 40.0, "p.h": -0.25}),
])
@pytest.mark.constraints
def test_the_cpp_evaluation_matches_python_with_exact_derivatives(text, values):
    got = evaluated(text, **values)
    assert got["error"] is None
    assert got["value"] == got["plain"]
    assert got["value"] == pytest.approx(value(parse(text), **values), rel=1e-14, abs=1e-14)
    parsed = parse(text)
    names = [leaf[1] if leaf[0] == "dimension" else "%s.%s" % leaf[1:] for leaf in parsed["names"]]
    for k, name in enumerate(names):
        step = 1e-6 * max(1.0, abs(values[name]))
        up, down = dict(values), dict(values)
        up[name] += step
        down[name] -= step
        # central differences: the error is about step^2 times the third derivative
        assert got["gradient"][k] == pytest.approx((value(parsed, **up) - value(parsed, **down)) / (2 * step), rel=1e-6, abs=1e-8)


@pytest.mark.constraints
def test_trig_in_degrees_and_constant_equations():
    assert evaluated("sin(30) = 0.5")["value"] == pytest.approx(0.0, abs=1e-15)
    assert evaluated("h = atan2(1, 1)", h=45.0)["value"] == pytest.approx(0.0, abs=1e-13)
    assert evaluated("h = 40*mm", h=0.04)["value"] == pytest.approx(0.0, abs=1e-18)
    # d sin(a)/da with a in degrees: cos(a) pi/180
    assert evaluated("h = sin(a)", h=0.0, a=60.0)["gradient"] == pytest.approx([1.0, -0.5 * math.pi / 180], rel=1e-15)


@pytest.mark.parametrize("text, values, message", [
    ("h = w/(w - 1)", dict(h=1.0, w=1.0), "division by zero"),
    ("h = sqrt(w - 10)", dict(h=1.0, w=5.0), "sqrt of a negative number (-5)"),
    ("h = asin(w)", dict(h=1.0, w=1.2), "asin of 1.2, outside [-1, 1]"),
    ("h = acos(w)", dict(h=1.0, w=-1.5), "acos of -1.5, outside [-1, 1]"),
    ("h = tan(w)", dict(h=1.0, w=90.0), "tan of 90 degrees"),
    ("h = w^0.5", dict(h=1.0, w=-2.0), "a negative number (-2) to the power 0.5"),
    ("h = (0 - 2)^w", dict(h=1.0, w=2.0), "a negative number (-2) to the power of a dimension"),
    ("h = w^(0 - 1)", dict(h=1.0, w=0.0), "0 to a negative power"),
    ("h = atan2(w, w)", dict(h=1.0, w=0.0), "atan2(0, 0) has no direction"),
    ("h = w*w", dict(h=1.0, w=1e200), "mul gives inf"),
    ("h = sqrt(w - 10)", dict(h=1.0, w=10.0), "sqrt of 0 has no derivative there, the solver cannot use it; write the equation without the root"),
    ("a = asin(h/w)", dict(a=90.0, h=2.0, w=2.0), "asin of 1 has no derivative there, the solver cannot use it; write the equation with sin instead"),
    ("a = acos(h)", dict(a=180.0, h=-1.0), "acos of -1 has no derivative there"),
    ("h = w^0.5", dict(h=1.0, w=0.0), "0 to the power 0.5 has no derivative there"),
])
@pytest.mark.constraints
def test_domain_errors(text, values, message):
    assert evaluated(text, **values)["error"].startswith(message)


@pytest.mark.constraints
def test_out_of_the_domain_is_nan_and_constants_have_no_derivative_to_miss():
    assert math.isnan(evaluated("h = sqrt(w - 10)", h=1.0, w=5.0)["value"])
    assert evaluated("h = sqrt(0) + w", h=1.0, w=1.0)["error"] is None
    assert evaluated("h = sqrt(w - 10)", h=1.0, w=26.0)["value"] == -3.0


@pytest.mark.constraints
def test_the_hook_checks_the_nodes():
    with pytest.raises(ValueError, match="add lacks an argument"):
        px._planex._EvaluateExpression([("add", 0)], [])
    with pytest.raises(ValueError, match="the nodes leave 2 values, not one"):
        px._planex._EvaluateExpression([("num", 1.0), ("num", 2.0)], [])
    with pytest.raises(ValueError, match="no operation 'log'"):
        px._planex._EvaluateExpression([("num", 1.0), ("log", 0)], [])
    with pytest.raises(ValueError, match="no value 1"):
        px._planex._EvaluateExpression([("val", 1)], [2.0])


TOL = 1e-12


def plate(mode="normal"):
    """a rectangle at the origin: its bottom w = 4 and its left side h = 2"""
    sk = px.Sketch(mode=mode)
    sk.Move(sk.origin, px.Coincident).Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Perpendicular, id="right") \
        .Line(-4, 0, px.Perpendicular, id="top").Close(px.Perpendicular, id="left")
    sk.Constrain("bottom", px.Distance, 4, id="w")
    sk.Constrain("left", px.Distance, 2, id="h")
    return sk


def length(sk, line):
    return sk.Geometry(line)["length"]


@pytest.mark.constraints
def test_a_dimension_alone_on_the_left_is_driven():
    sk = plate()
    assert sk.Expression("h = w/4") is sk
    assert sk.expressions == {"Expression.1": "h = w/4"}
    assert sk["h"] == pytest.approx(1, abs=TOL) and length(sk, "left") == pytest.approx(1, abs=TOL)
    assert (sk.dof, sk.redundant) == (0, [])
    sk.Update({"w": 6})
    assert sk["h"] == pytest.approx(1.5, abs=TOL) and length(sk, "left") == pytest.approx(1.5, abs=TOL)
    assert float(repr(sk.constraints["Distance"]["h"]).rsplit(", ", 1)[1]) == pytest.approx(1.5, abs=TOL)
    assert "Expression.1" not in str(sk.constraints)
    with pytest.raises(ValueError, match=re.escape("h = 3: h is driven by Expression.1 (h = w/4); remove the expression to set it")):
        sk["h"] = 3
    with pytest.raises(ValueError, match=re.escape("h is driven by Expression.1")):
        sk.Update({"h": 3})
    with pytest.raises(ValueError, match=re.escape("Expression 'h = 3': h is driven by Expression.1 (h = w/4) already")):
        sk.Expression("h = 3")
    sk.RemoveExpression("Expression.1")
    assert sk.expressions == {} and sk["h"] == pytest.approx(1.5, abs=TOL)
    sk.Update({"w": 8})
    assert length(sk, "left") == pytest.approx(1.5, abs=TOL)
    sk.Update({"h": 3})
    assert length(sk, "left") == pytest.approx(3, abs=TOL)
    for wrong in ("Expression.1", "w"):
        with pytest.raises(ValueError, match=re.escape("RemoveExpression('%s'): no such expression" % wrong)):
            sk.RemoveExpression(wrong)


@pytest.mark.constraints
def test_two_sides_an_id_and_the_id_rule():
    sk = plate()
    sk.Expression("h", "w/4", id="h ratio")
    assert sk.expressions == {"h ratio": "h = w/4"}
    with pytest.raises(ValueError, match=re.escape("Expression 'w = 4': id 'bad-id' contains '-'")):
        sk.Expression("w = 4", id="bad-id")
    with pytest.raises(ValueError, match=re.escape("Expression 'w = h ratio': 'h ratio' is an expression, not a value")):
        sk.Expression("w = h ratio")
    assert sk.expressions == {"h ratio": "h = w/4"}
    sk.Remove("h ratio")
    assert sk.expressions == {}


@pytest.mark.constraints
def test_names_the_sketch_knows():
    sk = plate()
    for text, message in (("h = ww/2", "unknown id 'ww'"),
                          ("h = bottom/2", "'bottom' is an element, not a value (its measures: bottom.length, bottom.angle)"),
                          ("h = bottom.end", "'bottom.end' is a point, not a value (its measures: bottom.end.h, bottom.end.v)"),
                          ("h = Horizontal.1", "'Horizontal.1' (px.Horizontal) has no value"),
                          ("h = bottom.r", "a line has no measure 'r' (length, angle)")):
        with pytest.raises(ValueError, match=re.escape("Expression '%s': %s" % (text, message))):
            sk.Expression(text)
    assert sk.expressions == {} and sk.dof == 0


@pytest.mark.constraints
def test_a_plain_equation_is_one_more_constraint():
    sk = plate()
    with pytest.warns(px.PlanexWarning, match=re.escape("Expression.1 (h/2 = w/4) is redundant (consistent) with w, h")):
        sk.Expression("h/2 = w/4")
    assert sk.redundant == ["w", "h", "Expression.1"]
    with pytest.raises(ValueError, match=re.escape("Update(w = 5): no solution, nothing changed")):
        sk.Update({"w": 5})
    other = plate()
    with pytest.warns(px.PlanexWarning, match=re.escape("h/2 = w/2: no solution, it contradicts the constraints there are; Expression.1 is kept")):
        other.Expression("h/2 = w/2")
    assert other["h"] == 2 and other.status == "over"


@pytest.mark.constraints
def test_a_reference_takes_part_and_stays_a_reference():
    sk = plate()
    sk.Remove("h")
    sk.Constrain("left", px.Distance, id="h", reference=True)
    assert sk.dof == 1
    sk.Expression("h/2 = w/2")
    assert length(sk, "left") == pytest.approx(4, abs=TOL) and sk["h"] == pytest.approx(4, abs=TOL)
    assert (sk.dof, sk.redundant) == (0, [])
    sk.Update({"w": 3})
    assert length(sk, "left") == pytest.approx(3, abs=TOL)
    sk.RemoveExpression("Expression.1")
    sk.Expression("h = w + 1")
    assert length(sk, "left") == pytest.approx(4, abs=TOL)
    with pytest.raises(ValueError, match="a reference dimension measures, it is not set"):
        sk["h"] = 2
    sk.RemoveExpression("Expression.2")
    assert sk.dof == 1 and repr(sk.constraints["Distance"]["h"]) == '"left", px.Distance, reference=True'


def chain():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1, 0, px.Horizontal, id="A").Line(0, 1, px.Vertical, id="B").Line(1, 0, px.Horizontal, id="C")
    for line, name in (("A", "a"), ("B", "b"), ("C", "c")):
        sk.Constrain(line, px.Distance, 1, id=name)
    return sk


@pytest.mark.constraints
def test_chains_follow_and_a_cycle_is_redundant():
    sk = chain()
    sk.Expression("a = b + 1")
    sk.Expression("b = 2*c")
    assert [sk[n] for n in "abc"] == pytest.approx([3, 2, 1], abs=TOL)
    assert [length(sk, n) for n in "ABC"] == pytest.approx([3, 2, 1], abs=TOL)
    sk.Update({"c": 1.5}, steps=3)
    assert [length(sk, n) for n in "ABC"] == pytest.approx([4, 3, 1.5], abs=TOL)
    with pytest.warns(px.PlanexWarning, match=re.escape("Expression.3 (c = b/2) is redundant (consistent) with Expression.2")):
        sk.Expression("c = b/2")
    assert sk.dof == 1 and {"Expression.2", "Expression.3"} <= set(sk.redundant)


@pytest.mark.constraints
def test_measures_are_solved_with_the_geometry():
    sk = plate()
    sk.Move("bottom.start", px.Coincident).Line(4, 2, id="diagonal", construction=True)
    sk.Constrain("diagonal.end", "right.end", px.Coincident)
    # h = |diagonal| / 2 = sqrt(w^2 + h^2) / 2: 3 h^2 = w^2
    sk.Expression("h = diagonal.length/2")
    assert sk["h"] == pytest.approx(4 / math.sqrt(3), abs=TOL) and length(sk, "left") == pytest.approx(4 / math.sqrt(3), abs=TOL)
    assert repr(sk.constraints["Distance"]["Reference.1"]) == '"diagonal", px.Distance, reference=True'
    assert sk["Reference.1"] == pytest.approx(8 / math.sqrt(3), abs=TOL) and (sk.dof, sk.redundant) == (0, [])
    sk.Update({"w": 6})
    assert sk["h"] == pytest.approx(6 / math.sqrt(3), abs=TOL)


@pytest.mark.constraints
def test_point_coordinates_and_directions_are_signed():
    sk = plate()
    sk.Vertex("P", (1, 1))
    sk.Expression("P.h = -1.5")
    sk.Expression("P.v = h/4 + Reference.1")
    assert sk.Location("P") == pytest.approx((-1.5, -1.0), abs=TOL)
    assert repr(sk.constraints["Distance"]["Reference.1"]) == '"P", px.V, px.Distance, reference=True'
    assert repr(sk.constraints["Distance"]["Reference.2"]) == '"P", px.H, px.Distance, reference=True'
    sk.Move((5, 0), px.Fix).Line(1, 1, id="arm")
    sk.Constrain("arm", px.Distance, 1, id="reach")
    sk.Expression("arm.angle = 120")
    assert sk.Geometry("arm")["angle"] == pytest.approx(120, abs=1e-10)
    sk.RemoveExpression("Expression.3")
    sk.Expression("arm.angle = -170")
    assert sk.Geometry("arm")["angle"] == pytest.approx(-170, abs=1e-10)
    assert repr(sk.constraints["Angle"]["Reference.4"]) == 'px.H, "arm", px.Angle, reference=True'


@pytest.mark.constraints
def test_radii_sweeps_and_one_reference_per_measure():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Arc(1, 90, id="bend")
    sk.Constrain("bend.center", px.Fix)
    sk.Circle(3, 0, 0.5, px.Fix, id="hole")
    sk.Expression("bend.r = 2*hole.r")
    sk.Expression("bend.sweep = 120 + 0*hole.r")
    assert sk.Geometry("bend")["r"] == pytest.approx(1, abs=TOL) and sk.Geometry("bend")["sweep"] == pytest.approx(120, abs=1e-10)
    references = [i for i in sk.dimensions if i.startswith("Reference.")]
    assert references == ["Reference.1", "Reference.2", "Reference.3"]
    sk.RemoveExpression("Expression.2")
    assert [i for i in sk.dimensions if i.startswith("Reference.")] == ["Reference.1", "Reference.2"]


@pytest.mark.constraints
def test_units_and_degrees():
    sk = plate()
    sk.Expression("h = 1500*mm")
    assert length(sk, "left") == pytest.approx(1.5, abs=TOL)
    sk.Move((6, 0), px.Fix).Line(1, 0.2, id="arm")
    sk.Constrain(px.H, "arm", px.Angle, 10, id="phi")
    sk.Constrain("arm", px.Distance, 1, id="reach")
    sk.Expression("phi = asin(0.5) + 15")
    assert sk["phi"] == pytest.approx(45, abs=1e-10) and sk.Geometry("arm")["angle"] == pytest.approx(45, abs=1e-10)
    sk.Expression("reach = 2*sin(phi - 15)")
    assert length(sk, "arm") == pytest.approx(1, abs=TOL)
    with pytest.warns(px.PlanexWarning, match=re.escape("Expression.4 (sin(30) = 0.5) is redundant (consistent)")):
        sk.Expression("sin(30) = 0.5")
    with pytest.warns(px.PlanexWarning, match=re.escape("sin(30) = 0.6: no solution")):
        sk.Expression("sin(30) = 0.6")
    assert sk.status == "over"


@pytest.mark.constraints
def test_domain_errors_name_the_expression():
    sk = plate()
    with pytest.raises(ValueError, match=re.escape("Expression 'h = sqrt(w - 10)': sqrt of a negative number (-6)")):
        sk.Expression("h = sqrt(w - 10)")
    with pytest.raises(ValueError, match=re.escape("Expression 'h = sqrt(w - 4)': sqrt of 0 has no derivative there")):
        sk.Expression("h = sqrt(w - 4)")
    assert sk.expressions == {} and (sk["h"], sk.dof) == (2, 0)
    sk.Expression("h = sqrt(w - 3)")
    assert sk["h"] == pytest.approx(1, abs=TOL)
    with pytest.raises(ValueError, match=re.escape("Update(w = 2): no solution, nothing changed; Expression.1 (h = sqrt(w - 3)): sqrt of a "
                                                   "negative number (-1) there")):
        sk.Update({"w": 2})
    assert (sk["w"], sk["h"]) == (4, pytest.approx(1, abs=TOL))


@pytest.mark.constraints
def test_strict_mode_and_sweeps():
    sk = plate("strict")
    sk.Expression("h = w/4")
    sk.Face()
    for w in (5, 6, 7):
        sk.Update({"w": w}, steps=2)
        assert length(sk, "left") == pytest.approx(w / 4, abs=TOL)
    with pytest.warns(px.PlanexWarning, match="redundant"):
        sk.Expression("w = 4*h")
    with pytest.raises(ValueError, match="mode='strict' and the sketch is not fully constrained"):
        sk.Face()


@pytest.mark.constraints
def test_copy_keeps_the_expressions():
    sk = plate()
    sk.Expression("h = bottom.length/4")
    copy = sk.Copy()
    assert copy.expressions == sk.expressions
    copy.Update({"w": 10})
    assert (copy["h"], sk["h"]) == (pytest.approx(2.5, abs=TOL), pytest.approx(1, abs=TOL))


@pytest.mark.constraints
def test_removing_what_an_expression_uses_removes_it():
    sk = plate()
    sk.Expression("h = w/4")
    with pytest.warns(px.PlanexWarning, match=re.escape("Remove('w'): removed Expression.1 (h = w/4), it used w")):
        sk.Remove("w")
    assert sk.expressions == {} and sk["h"] == pytest.approx(1, abs=TOL) and sk.dof == 1
    sk = plate()
    sk.Expression("h = bottom.length/4")
    sk.Expression("Reference.1 = 4")
    with pytest.warns(px.PlanexWarning, match=re.escape("Remove('Reference.1'): removed Expression.1 (h = bottom.length/4), it used Reference.1")):
        sk.Remove("Reference.1")
    assert sk.expressions == {}
    sk = plate()
    sk.Expression("h = bottom.length/4")
    with pytest.warns(px.PlanexWarning, match=re.escape("Remove('bottom'): removed Expression.1 (h = bottom.length/4), it used Reference.1")):
        sk.Remove("bottom")
    assert sk.expressions == {} and "Reference.1" not in sk.dimensions


@pytest.mark.constraints
def test_trim_and_split_take_their_expressions_along():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(4, 0, px.Horizontal, id="base")
    sk.Move((2, -1)).Line(0, 2, id="cut")
    sk.Constrain("base", px.Distance, 4, id="w")
    sk.Expression("cut.length = w/2")
    with pytest.warns(px.PlanexWarning, match=re.escape("Trim('base'): removed Expression.1 (cut.length = w/2), it used w")):
        sk.Trim(("base", (3, 0)), "cut")
    assert sk.expressions == {}


@pytest.mark.constraints
def test_scale_and_set_origin_with_expressions():
    sk = plate()
    sk.Expression("h = w/4")
    sk.Scale((0, 0), 2)
    assert (sk["w"], sk["h"]) == (pytest.approx(8, abs=TOL), pytest.approx(2, abs=TOL))
    sk = plate()
    sk.Expression("h = w - 3")
    sk.Scale((0, 0), 2)
    # the 3 does not scale: h = 8 - 3
    assert sk["h"] == pytest.approx(5, abs=TOL) and length(sk, "left") == pytest.approx(5, abs=TOL)
    sk = plate()
    sk.Vertex("P", (1, 1))
    sk.Expression("P.h = 1.5")
    sk.SetOrigin((1, 0))
    assert sk.Location("P")[0] == pytest.approx(1.5, abs=TOL) and sk.Location("bottom.start") == pytest.approx((0, 0), abs=TOL)


@pytest.mark.constraints
def test_a_driven_pattern_angle_stays_when_the_count_changes():
    sk = px.Sketch()
    sk.Move((1, 0)).Circle(0.2, id="bolt")
    sk.Constrain("bolt.center", px.Fix)
    sk.CircularPattern("bolt", sk.origin, 4, id="ring")
    sk.Expression("ring.angle = 45")
    sk.Update({"ring.count": 6})
    assert sk["ring.angle"] == pytest.approx(45, abs=1e-10) and sk["ring.count"] == 6


@pytest.mark.constraints
def test_the_parse_timer():
    px.debug.ResetTimers()
    plate().Expression("h = w/4")
    timer = {t["name"]: t for t in px.debug.Timers()}["planex Expression tree"]
    assert timer["counts"] == 1 and timer["time"] > 0


@pytest.mark.constraints
def test_the_plot_shows_driven_values_and_references_in_brackets():
    sk = plate()
    sk.Expression("h = bottom.length/4")
    sk.Constrain("top", px.Distance, id="L", reference=True)
    svg = sk.ToSVG(show_constraints=True)
    assert ">h = 1<" in svg and ">(L = 4)<" in svg and ">(4)<" in svg and "Reference.1" not in svg.replace("<title>Reference.1", "")
    texts = [a.text for a in sk.Plot(show_constraints=True, backend="plotly").layout.annotations]
    assert any("(L = 4)" in t for t in texts) and any("h = 1" in t for t in texts)
