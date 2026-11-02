# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

""".CATPart files: their sketches as planex sketches, the file read by _catpartdata"""


from __future__ import annotations

import math
import os
import re
import warnings

from . import _catpartdata
from . import _planex as px
from ._auto import Auto
from ._caller import warn
from ._fileunits import read_factor
from ._parameters import Parameters
from ._units import mm

_SAME = {"fix": px.Fix, "coincidence": px.Coincident, "concentric": px.Concentric, "tangent": px.Tangent,
         "parallel": px.Parallel, "perpendicular": px.Perpendicular, "length": px.Distance, "distance": px.Distance,
         "radius": px.Radius, "diameter": px.Diameter, "angle": px.Angle}
_LENGTHS = {"length", "distance", "radius", "diameter"}
_RESERVED = {"h", "v", "t"}


def FromCATPart(path: str | os.PathLike, /, scale: float | Auto = Auto) -> dict:
    """Reads the sketches of a .CATPart file as planex sketches, with the part's parameters (tested with V5R21 files).

    Each sketch is drawn on its plane where it was saved and constrained as saved: geometric constraints and dimensions
    (reference dimensions as reference=True), the part's parameters as one px.Parameters set the sketches use,
    formulas as the sketches' expressions; nothing moves. Projected and intersected geometry comes in as fixed
    construction geometry, a spline in style "quintic" with its tangents and curvature radii. What planex has no
    counterpart for is named in one warning per sketch: an ellipse comes in as a fixed curve, an equidistant point, a
    projected face outline and a constraint on a spline's tangent are left out. The part's 3d features are not read and
    named in one warning. Names given in the file become the ids, the rest gets planex's ids.

    Args:
        path: the .CATPart file.
        scale: what the file's millimeters are multiplied by; px.Auto converts them into meters, 1 keeps them.

    Returns:
        {"sketches": {name: Sketch}, "parameters": Parameters}, the sketches in the file's order, named as in the file or
        Sketch.1, Sketch.2, ...

    Raises:
        ValueError: not a .CATPart file, or one the reader cannot follow.

    Examples:
        part = px.FromCATPart("bracket.CATPart")
        part["parameters"].Update(w=0.06)
    """
    try:
        data = _catpartdata.read(path)
    except _catpartdata.CATPartError as e:
        raise ValueError(f"FromCATPart('{os.fspath(path)}'): {e}") from None
    f = read_factor(mm, scale, "FromCATPart")
    renamed = []
    for p in data["parameters"]:
        if p["name"] in _RESERVED:
            renamed.append(f"{p['name']} -> {p['name']}_")
            p["name"] += "_"
    params = {p["name"]: p["value"] * f if p["type"] == "LENGTH" else p["value"]
              for p in data["parameters"] if p["value"] is not None}
    parameters = Parameters(params)
    named = {p["id"]: p["name"] for p in data["parameters"]}
    if renamed:
        warn(f"FromCATPart: parameters renamed, h, v and t being a curve's coordinates in planex ({', '.join(renamed)})",
             px.PlanexWarning)
    sketches = {}
    for k, s in enumerate(data["sketches"], 1):
        name = s["name"] or f"Sketch.{k}"
        sketches[name] = _Sketch(s, f, parameters, named, data["formulas"], name).sketch
    if data["features"]:
        kinds = sorted({x["class"] for x in data["features"]})
        warn(f"FromCATPart: the part's 3d features are not read ({len(data['features'])}: {', '.join(kinds)})",
             px.PlanexWarning)
    return {"sketches": sketches, "parameters": parameters}


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


class _Sketch:
    """one sketch of the file drawn and constrained as a px.Sketch"""

    def __init__(self, s, f, parameters, named, formulas, name):
        self.f, self.name, self.left = f, name, []
        origin = tuple(c * f for c in s["origin"])
        self.sketch = px.Sketch(px.Plane(origin=origin, normal=_cross(s["h"], s["v"]), axis=s["h"]),
                                auto_constraint=False)
        if len(parameters):
            self.sketch.Use(parameters)
        self.elements = {e["id"]: e for e in s["elements"]}
        self.refs = {}
        self.same = {}
        for c in s["constraints"]:
            if c["kind"] == "coincidence" and all(self.elements.get(x, {}).get("kind") == "point" for x in c["elements"]):
                a, b = (self._first(x) for x in c["elements"])
                if a != b:
                    self.same[b] = a
                c["shared"] = True
        self.points, self.held = 0, set()
        for e in s["elements"]:
            self._draw(e)
        for e in s["elements"]:
            if e["kind"] == "point" and self._first(e["id"]) not in self.refs:
                self._point(self.elements[self._first(e["id"])])
        for e in s["elements"]:
            if e["kind"] == "point" and e.get("projected"):
                ref = self.refs[self._first(e["id"])]
                if ref not in self.held and ref.rsplit(".", 1)[0] not in self.held:
                    self.sketch.Constrain(ref, px.Fix)
                    self.held.add(ref)
        self.dimensions = {}
        self.constraints = s["constraints"]
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always", px.PlanexWarning)
            for c in s["constraints"]:
                self._constrain(c)
        self.left += [str(w.message) for w in caught]
        for formula in formulas:
            self._formula(formula, named)
        if self.left:
            warn(f"FromCATPart: {name}: " + "; ".join(self.left), px.PlanexWarning)

    def _at(self, point):
        e = self.elements[point]
        return (e["at"][0] * self.f, e["at"][1] * self.f)

    def _first(self, point):
        while point in self.same:
            point = self.same[point]
        return point

    def _end(self, point):
        """a curve's end: the planex point it shares, else where it is"""
        return self.refs.get(self._first(point)) or self._at(point)

    def _new(self, kind):
        return self.sketch.elements[kind][-1]

    def _point(self, e):
        self.points += 1
        ident = e["name"] or f"Point_{self.points}"
        while ident in self.sketch.locations:
            self.points += 1
            ident = f"Point_{self.points}"
        self.sketch.Vertex(ident, self._at(e["id"]))
        self.refs[e["id"]] = ident

    def _options(self, e):
        options = {"construction": True} if e.get("construction") else {}
        if e.get("name"):
            options["id"] = e["name"]
        return options

    def _draw(self, e):
        kind, sk = e["kind"], self.sketch
        if kind == "axis":
            self.refs[e["id"]] = {"h": px.H, "v": px.V, "origin": "origin"}[e["role"]]
        elif kind == "line":
            sk.Line(self._end(e["start"]), self._end(e["end"]), **self._options(e))
            self._drawn(e, "Line", {e["start"]: "start", e["end"]: "end"})
        elif kind == "circle":
            sk.Circle(self._at(e["center"]), e["r"] * self.f, **self._options(e))
            self._drawn(e, "Circle", {e["center"]: "center"})
        elif kind == "arc":
            start, end = (e["start"], e["end"]) if e["ccw"] else (e["end"], e["start"])
            sk.Arc(self._end(start), self._end(end), center=self._at(e["center"]), **self._options(e))
            self._drawn(e, "Arc", {start: "start", end: "end", e["center"]: "center"})
        elif kind == "spline":
            points = e["points"]
            radii = {k: math.inf if c == 0 else self.f / c for k, c in e.get("curvatures", {}).items()}
            sk.Spline([self._at(p) for p in points], start_from_localpos=False, periodic=e["closed"], style="quintic",
                      tangents=e.get("tangents") or None, curvature_radii=radii or None, **self._options(e))
            self._drawn(e, "Spline", {p: f"p{k}" for k, p in enumerate(points)})
        elif kind == "ellipse":
            self._ellipse(e)
        elif kind == "unread projection":
            self.left.append("a projected face outline left out (the file holds no curves of it)")

    def _drawn(self, e, kind, parts):
        """the element just drawn: its id for e, its ends and center for the points not placed yet"""
        ident = e.get("name") or self._new(kind)
        self.refs[e["id"]] = ident
        for point, part in parts.items():
            self.refs.setdefault(self._first(point), f"{ident}.{part}")
        if e.get("projected"):
            self.sketch.Constrain(ident, px.Fix)
            self.held.add(ident)

    def _ellipse(self, e):
        cx, cy = self._at(e["center"])
        a, b, t = e["a"] * self.f, e["b"] * self.f, e["angle"]
        h = f"{cx!r} + {a!r}*cos(t)*cos({t!r}) - {b!r}*sin(t)*sin({t!r})"
        v = f"{cy!r} + {a!r}*cos(t)*sin({t!r}) + {b!r}*sin(t)*cos({t!r})"
        self.sketch.Curve((h, v), (0, 360), **self._options(e))
        ident = e.get("name") or self._new("Curve")
        self.refs[e["id"]] = ident
        self.left.append(f"{ident}: an ellipse, read as a fixed curve (no ellipse in planex)")

    def _constrain(self, c):
        if c.get("shared"):
            return
        refs = [self.refs.get(self._first(x)) for x in c["elements"]]
        if any(r is None for r in refs):
            self.left.append(f"the {c['kind']} on elements not read left out")
            return
        options = {"id": c["name"]} if c.get("name") else {}
        kind, sk = c["kind"], self.sketch
        if kind in ("horizontal", "vertical"):
            sk.Constrain(next(r for r in refs if r not in (px.H, px.V)), px.Horizontal if kind == "horizontal" else px.Vertical,
                         **options)
        elif kind == "symmetry":
            first = self.elements.get(c["elements"][0], {})
            if first.get("kind") == "line" and self._ends_symmetric(c):
                return
            sk.Constrain(*refs, px.Symmetric if first.get("kind") == "point" else px.SymmetricCarriers, **options)
        elif kind == "midpoint":
            point, line = refs
            sk.Constrain(point, f"{line}.mid", px.Coincident, **options)
        elif kind == "fix together":
            sk.RigidGroup([r for r, x in zip(refs, c["elements"]) if self.elements.get(x, {}).get("kind") != "point"],
                          **options)
        elif kind in _SAME:
            if c["value"] is None and not c["reference"] and kind in _LENGTHS | {"angle"}:
                self.left.append(f"the {kind} without its value left out")
                return
            value = () if c["value"] is None or c["reference"] else (c["value"] * self.f if kind in _LENGTHS else c["value"],)
            sk.Constrain(*refs, _SAME[kind], *value, reference=c["reference"], **options)
            if c["parameter"] is not None:
                self.dimensions[c["parameter"]] = options.get("id") or _newest(sk, _SAME[kind])
        else:
            self.left.append(f"the {kind} constraint left out (none in planex)")

    def _ends_symmetric(self, c):
        """whether both pairs of ends of the two lines of symmetry c are symmetric about its axis too"""
        a, b, axis = (self.elements.get(x, {}) for x in c["elements"])
        pairs = {frozenset((self._first(s["elements"][0]), self._first(s["elements"][1])))
                 for s in self.constraints if s["kind"] == "symmetry" and s["elements"][2] == c["elements"][2]}
        return all(frozenset((self._first(p), self._first(q))) in pairs
                   for p, q in ((a.get("start"), b.get("start")), (a.get("end"), b.get("end")))) or \
            all(frozenset((self._first(p), self._first(q))) in pairs
                for p, q in ((a.get("start"), b.get("end")), (a.get("end"), b.get("start"))))

    def _formula(self, formula, named):
        target = self.dimensions.get(formula["output"])
        if target is None or formula["text"] is None:
            return
        inputs = [self.dimensions.get(p) or named.get(p) for p in formula["inputs"]]
        if any(i is None for i in inputs):
            self.left.append(f"the formula of {target} left out (an input is not in this sketch)")
            return
        text = re.sub(r"#(\d+)_", lambda m: inputs[int(m.group(1)) - 1], formula["text"])
        self.sketch.Expression(f"{target} = {text}")


def _newest(sketch, kind):
    """the id of the constraint of kind added last"""
    return list(sketch.constraints[kind.name if hasattr(kind, "name") else str(kind)])[-1]
