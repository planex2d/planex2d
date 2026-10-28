# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The parser of Sketch.Expression: an equation between dimensions and measures of the solved geometry, read
into a node list for the solver (postfix, the left side minus the right side)."""
import ast
import difflib
import math
import re

from . import _units

# the functions with how many arguments they take (None: two or more); angles in degrees
FUNCTIONS = {"sqrt": 1, "abs": 1, "min": None, "max": None, "sin": 1, "cos": 1, "tan": 1, "asin": 1, "acos": 1, "atan2": 2,
             "exp": 1, "log": 1, "sinh": 1, "cosh": 1, "tanh": 1, "asinh": 1, "acosh": 1, "atanh": 1}
UNITS = {name: getattr(_units, name) for name in ("mm", "cm", "m", "um", "inch", "mil")}
CONSTANTS = {"pi": math.pi}
# per measure: the element kinds it measures ("point": any point reference) and what it is called in messages
MEASURES = {"length": (("Line",), "the length of"), "angle": (("Line",), "the direction of"),
            "r": (("Arc", "Circle"), "the radius of"), "sweep": (("Arc",), "the sweep of"),
            "h": (("point",), "the h coordinate of"), "v": (("point",), "the v coordinate of")}
OPERATORS = {ast.Add: "add", ast.Sub: "sub", ast.Mult: "mul", ast.Div: "div", ast.Pow: "pow"}
SEPARATORS = "+-*/^(),%"
NUMBER = re.compile(r"([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?")
# the constraints the sketch makes itself: no px name
INTERNAL = ("Copy", "OnCircle", "Value", "Offset", "OnCurve")
PARTS = ("start", "end", "center", "mid")
ALLOWED = "(+ - * / ^ **, brackets and the functions)"


class _Equation:
    """the names of a sketch an equation is read with, and what reading it found: numbers, values (names)"""

    def __init__(self, text, dimensions, elements, ispoint, others):
        self.text = text
        self.dimensions = dimensions
        self.elements = elements
        self.ispoint = ispoint
        self.others = others
        self.numbers = []
        self.names = []
        self.leaves = {}

    def Error(self, what):
        return ValueError("Expression '%s': %s" % (self.text, what))

    def kind(self, ref):
        """"point" for a point reference, else the element kind, None if ref names nothing"""
        return "point" if self.elements.get(ref) == "Point" or self.ispoint(ref) else self.elements.get(ref)

    def measure(self, name):
        """(reference, measure) if name is a measure of an element or point"""
        ref, dot, m = name.rpartition(".")
        kind = self.kind(ref) if dot else None
        if kind is None or m not in MEASURES:
            return None
        return (ref, m) if kind in MEASURES[m][0] else None

    def described(self, ref):
        """'a line', 'an arc' for the element ref"""
        kind = self.kind(ref).lower()
        return "%s %s" % ("an" if kind[0] in "aeiou" else "a", kind)

    def measures_of(self, ref):
        return [m for m, (kinds, _) in MEASURES.items() if self.kind(ref) in kinds]

    def meanings(self, name):
        """what name can mean: a dimension, a measure, a unit, a constant, a function; more than one is an error"""
        out = []
        if name in self.dimensions:
            out.append(("dimension", "a dimension"))
        if m := self.measure(name):
            out.append(("measure", "%s %s" % (MEASURES[m[1]][1], m[0])))
        if name in UNITS:
            out.append(("unit", "the unit %s" % name))
        if name in CONSTANTS:
            out.append(("constant", "the constant %s" % name))
        if name in FUNCTIONS:
            out.append(("function", "the function %s" % name))
        if out and self.kind(name) is not None:
            raise self.Error("'%s' is %s and an element id; rename the element" % (name, out[0][1]))
        if len(out) > 1:
            raise self.Error("'%s' is both %s and %s; rename the dimension" % (name, ", ".join(d for _, d in out[:-1]), out[-1][1]))
        return out

    def function(self, name):
        """name, followed by a bracket"""
        meanings = self.meanings(name)
        if meanings and meanings[0][0] == "function":
            return name
        if meanings:
            raise self.Error("'%s' is %s, not a function" % (name, meanings[0][1]))
        raise self.Error("no function '%s' (%s)" % (name, ", ".join(FUNCTIONS)))

    def number(self, value):
        self.numbers.append(value)
        return "_n%d" % (len(self.numbers) - 1)

    def value(self, name):
        """the placeholder of what name stands for"""
        meanings = self.meanings(name)
        if not meanings:
            raise self.Error(self.unknown(name))
        kind = meanings[0][0]
        if kind == "function":
            raise self.Error("'%s' is a function: %s(...)" % (name, name))
        if kind in ("unit", "constant"):
            return self.number(UNITS[name] if kind == "unit" else CONSTANTS[name])
        leaf = ("dimension", name) if kind == "dimension" else ("measure",) + self.measure(name)
        if leaf not in self.leaves:
            self.leaves[leaf] = len(self.names)
            self.names.append(leaf)
        return "_v%d" % self.leaves[leaf]

    def unknown(self, name):
        """why name is no value"""
        if name in self.others:
            what = self.others[name]
            if what == "count":
                return "a pattern's count is no dimension for expressions"
            if what == "Expression":
                return "'%s' is an expression, not a value" % name
            if what in INTERNAL:
                return "'%s' is a constraint planex made for a copy, a pattern or an offset; it has no value" % name
            return "'%s' (px.%s) has no value" % (name, what)
        if self.kind(name) is not None:
            ms = ", ".join("%s.%s" % (name, m) for m in self.measures_of(name))
            what = "a point" if self.kind(name) == "point" else "an element"
            return "'%s' is %s, not a value%s" % (name, what, " (its measures: %s)" % ms if ms else "")
        ref, dot, m = name.rpartition(".")
        stem, inner, part = ref.rpartition(".")
        if dot and inner and part in PARTS and self.kind(ref) is None and self.kind(stem) not in (None, "point"):
            return "%s has no %s point '%s.%s'" % (self.described(stem), part, stem, part)
        if dot and self.kind(ref) is not None:
            ms = ", ".join(self.measures_of(ref))
            return "%s has no measure '%s'%s" % (self.described(ref), m, " (%s)" % ms if ms else "")
        candidates = list(self.dimensions) + list(UNITS) + list(CONSTANTS)
        for ref, kind in self.elements.items():
            for m, (kinds, _) in MEASURES.items():
                if kind in kinds or (kind == "Point" and "point" in kinds):
                    candidates.append("%s.%s" % (ref, m))
        quoted = ["'%s'" % c for c in difflib.get_close_matches(name, candidates, n=3)]
        if not quoted:
            return "unknown id '%s'" % name
        listed = quoted[0] if len(quoted) == 1 else ", ".join(quoted[:-1]) + " or " + quoted[-1]
        return "unknown id '%s' (did you mean %s?)" % (name, listed)

    def tokens(self, side):
        """the side as tokens: ("op", text), ("number", value), ("name", name), ("call", name) for a name followed by
        a bracket"""
        out = []
        k, written = 0, ""
        while k < len(side):
            c = side[k]
            if c.isascii() and c.isspace():
                k += 1
            elif c == "%":
                raise self.Error("'%' is not allowed " + ALLOWED)
            elif side.startswith("//", k):
                raise self.Error("'//' is not allowed " + ALLOWED)
            elif side.startswith("**", k):
                out.append(("op", "**"))
                k += 2
            elif c in SEPARATORS:
                out.append(("op", "**" if c == "^" else c))
                k += 1
            elif c in "0123456789.":
                found = NUMBER.match(side, k)
                if found is None:
                    raise self.Error("cannot read it at '%s'" % side[k:])
                value = float(found.group(0))
                if not math.isfinite(value):
                    raise self.Error("the number %s is too large" % found.group(0))
                if out and out[-1][0] == "number":
                    raise self.Error("cannot read it (a number followed by a number: write the operator between them)")
                out.append(("number", value))
                written = found.group(0)
                k = found.end()
            elif c.isascii() and c.isalpha():
                end = k
                while end < len(side) and side[end] not in SEPARATORS:
                    end += 1
                name = side[k:end].strip()
                if out and out[-1][0] == "number":
                    raise self.Error("cannot read it (a number followed by a name: write %s*%s)" % (written, name))
                out.append(("call" if side[end:].lstrip().startswith("(") else "name", name))
                k = end
            elif not c.isascii():
                raise self.Error("'%s' is not an ASCII character (ids and numbers use ASCII only)" % c)
            else:
                raise self.Error("cannot read it at '%s' (a name starts with a letter)" % side[k:])
        return out

    def postfix(self, node, out):
        """a node of Python's tree in postfix order into out (a list of work, not a recursion: any depth)"""
        todo = [node]
        while todo:
            item = todo.pop()
            if isinstance(item, tuple):
                out.append(item)
            else:
                todo.extend(reversed(self.parts(item)))

    def parts(self, node):
        """a node as the nodes below it and the postfix entries, in postfix order"""
        if isinstance(node, ast.Name) and node.id[:2] in ("_n", "_v"):
            k = int(node.id[2:])
            return [("num", self.numbers[k]) if node.id[1] == "n" else ("val", k)]
        if isinstance(node, ast.BinOp) and type(node.op) in OPERATORS:
            return [node.left, node.right, (OPERATORS[type(node.op)], 0)]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
            return [node.operand] + ([("neg", 0)] if isinstance(node.op, ast.USub) else [])
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in FUNCTIONS and not node.keywords:
            name, given, wanted = node.func.id, len(node.args), FUNCTIONS[node.func.id]
            if given != wanted and (wanted is not None or given < 2):
                takes = "2 or more arguments" if wanted is None else "%d argument%s" % (wanted, "" if wanted == 1 else "s")
                raise self.Error("%s takes %s, %d given" % (name, takes, given))
            if wanted is not None:
                return list(node.args) + [(name, 0)]
            return [node.args[0]] + [part for arg in node.args[1:] for part in (arg, (name, 0))]
        raise self.Error("cannot read it")

    def side(self, text, which):
        """one side as postfix nodes"""
        if not text.strip():
            raise self.Error("the %s side is empty" % which)
        placed = []
        for kind, token in self.tokens(text):
            if kind == "op":
                placed.append(token)
            elif kind == "number":
                placed.append(self.number(token))
            elif kind == "call":
                placed.append(self.function(token))
            else:
                placed.append(self.value(token))
        try:
            tree = ast.parse(" ".join(placed), mode="eval")
        except (RecursionError, MemoryError):
            raise self.Error("too deeply nested to read (%d terms); split it into shorter expressions" % len(placed)) from None
        except SyntaxError as e:
            message = e.msg if e.msg and not re.search(r"_[nv]\d", e.msg) else "invalid syntax"
            raise self.Error("cannot read it (%s)" % message) from None
        out = []
        self.postfix(tree.body, out)
        return out


def _Parse(text, rhs=None, dimensions=None, elements=None, ispoint=None, others=None):
    """the equation text ("h = w/2"), or its two sides text and rhs, read with the names of a sketch: dimensions {id:
    "length" | "angle"}, elements {id: kind ("Point", "Line", "Arc", ...)}, ispoint(ref) whether ref names a point
    (default: a Point element), others {id: "count" | a constraint kind} the ids that are no values. A dict: text (as
    given, two sides joined by " = "), drives (the left side if it is a dimension id alone, else None), names (per
    value: ("dimension", id) or ("measure", reference, measure)) and nodes, the left side minus the right side in
    postfix: ("num", value), ("val", index into names), (operation, 0) with the operations add, sub, mul, div, pow, neg
    and the functions; angles in degrees"""
    dimensions = dimensions or {}
    elements = elements or {}
    if rhs is None:
        found = text.count("=")
        if found != 1:
            raise ValueError("Expression '%s': one '=' (left side = right side), found %s" % (text, found or "none"))
        lhs, rhs = text.split("=")
        given = text
    else:
        lhs = text
        if "=" in lhs or "=" in rhs:
            raise ValueError("Expression(%r, %r): a side has no '='" % (lhs, rhs))
        given = "%s = %s" % (lhs.strip(), rhs.strip())
    equation = _Equation(given, dimensions, elements, ispoint or (lambda ref: elements.get(ref) == "Point"), others or {})
    nodes = equation.side(lhs, "left") + equation.side(rhs, "right") + [("sub", 0)]
    drives = lhs.strip() if lhs.strip() in dimensions else None
    return {"text": given, "drives": drives, "names": list(equation.names), "nodes": nodes}


class _CurveFormula(_Equation):
    """the names a curve's formula is read with: its parameter t, its coordinates h and v (each in the other's
    formula), numbers, units, pi, the functions and the driving dimensions; driven {id: why} the dimensions it may not
    use; numbers_only: why it may use no dimension at all"""

    def __init__(self, text, dimensions, driven, elements, ispoint, others, numbers_only):
        super().__init__(text, dimensions, elements, ispoint, others)
        self.driven = driven
        self.numbers_only = numbers_only
        self.coordinates = set()

    def Error(self, what):
        return ValueError("%s: %s" % (self.text, what))

    def value(self, name):
        if name in ("t", "h", "v"):
            self.coordinates.add(name)
            leaf = (name,)
        else:
            meanings = self.meanings(name)
            kind = meanings[0][0] if meanings else None
            if kind in ("unit", "constant", "function"):
                return super().value(name)
            if self.numbers_only:
                raise self.Error("'%s': %s; a curve's formula there takes numbers, units, pi, t, h and v" % (name, self.numbers_only))
            if kind == "measure":
                raise self.Error("'%s' is %s, a measure of the solved geometry; a curve's shape is known before each solve, from "
                                 "numbers and driving dimensions" % (name, meanings[0][1]))
            if kind is None:
                raise self.Error(self.unknown(name))
            if name in self.driven:
                raise self.Error("'%s' is %s; a curve's shape is known before each solve, from driving dimensions" % (name, self.driven[name]))
            leaf = ("dimension", name)
        if leaf not in self.leaves:
            self.leaves[leaf] = len(self.names)
            self.names.append(leaf)
        return "_v%d" % self.leaves[leaf]


def _ParseCurve(text, point, span, dimensions=None, driven=None, elements=None, ispoint=None, others=None, numbers_only=None):
    """a curve's point (h, v), two expressions in t, and its range span (t0, t1), numbers or expressions, read with the names
    of a sketch (as _Parse takes them; driven {id: why} the dimensions the formula may not use, numbers_only: why it
    may use none) for the call text. A dict: h and v (an h in v or v in h written out), from and to as postfix nodes
    whose ("val", 0) is t and ("val", k) the dimension k - 1 of dimensions, the ids of the dimensions it uses"""
    elements = elements or {}
    curve = _CurveFormula(text, dimensions or {}, driven or {}, elements, ispoint or (lambda ref: elements.get(ref) == "Point"),
                          others or {}, numbers_only)
    sides, uses = {}, {}
    for name, formula in zip("hv", point):
        curve.coordinates = set()
        sides[name] = curve.side(formula, name)
        uses[name] = curve.coordinates & {"h", "v"}
    for name in "hv":
        if name in uses[name]:
            raise curve.Error("%s uses %s itself; write it in t (and the other coordinate)" % (name, name))
    if uses["h"] and uses["v"]:
        raise curve.Error("h uses v and v uses h, a cycle; write one of them in t only")
    ends = []
    for which, end in zip(("start", "end"), span):
        if not isinstance(end, str):
            ends.append([("num", float(end))])
            continue
        curve.coordinates = set()
        ends.append(curve.side(end, "range's " + which))
        if curve.coordinates:
            raise curve.Error("the range's %s uses %s; the range takes numbers and driving dimensions, not t, h or v" %
                              (which, " and ".join(sorted(curve.coordinates))))
    dimensions_used = [leaf[1] for leaf in curve.names if leaf[0] == "dimension"]

    def resolved(nodes):
        out = []
        for op, k in nodes:
            leaf = curve.names[k] if op == "val" else None
            if leaf is None:
                out.append((op, k))
            elif leaf[0] in ("h", "v"):
                out.extend(resolved(sides[leaf[0]]))
            else:
                out.append(("val", 0 if leaf == ("t",) else 1 + dimensions_used.index(leaf[1])))
        return out

    return {"h": resolved(sides["h"]), "v": resolved(sides["v"]), "from": resolved(ends[0]), "to": resolved(ends[1]),
            "dimensions": dimensions_used}
