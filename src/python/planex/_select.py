# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""selecting elements: edges("Line.3"), edges(name="web")(kind="arc"), near=, Min/Max/Sorted, [(px.x > 5) & (px.h < 2)]

An element's center: of an arc or circle its circle's center, of another edge its midpoint along it, of a face its
centroid, of a vertex its point. h and v are coordinates in the object's plane, x, y and z in 3d.

Points by id, as in the Sketch: Line.3.start and Line.3.end the vertices there, Line.3.mid (or .midpoint)
halfway along, Arc.2.center and Face.1.center a Point. In a sketch .start, .end and .center are its point elements
(their properties and id writable), .mid a place; all act as their coordinates (h, v) in the plane, read live, with
.x, .y, .z in 3d."""
from __future__ import annotations

import functools
import math
import re
import weakref
from typing import TYPE_CHECKING, Any

from . import _planex as px
from ._sketches import Sketch, sketch_types

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Vertex


class _Owners:
    """the object each view and element was taken from (a face view's: its shape), by the view itself: a vertex view
    compares equal to its coordinates, so it is no key of a dict"""

    def __init__(self):
        self._of = {}

    def get(self, item):
        entry = self._of.get(id(item))
        return entry[1] if entry is not None and entry[0]() is item else None

    def __setitem__(self, item, owner):
        key = id(item)
        self._of[key] = (weakref.ref(item, lambda ref: self._of.pop(key, None)), owner)


_OWNER = _Owners()


def owner(item):
    return _OWNER.get(item)


def _local(item):
    """the id within its object: Line.3 of Shape.1.Line.3"""
    o = owner(item)
    i = item.id
    return i[len(o.id) + 1:] if o is not None and i.startswith(o.id + ".") else i


def _faces_of(edge):
    o = owner(edge)
    if isinstance(o, px.Shape):
        return [f for f in o.faces if any(e.id == edge.id for e in f.edges)]
    return [o] if isinstance(o, px.Face) else []


def _outline_of(face):
    """the edges on the face's outer loop, as edges of its object"""
    own = owner(face) if isinstance(face, px.FaceView) else face
    edges = list(face.edges)
    if not edges:
        return []
    edges[0].id
    copy = face.Copy() if isinstance(face, px.FaceView) else face
    outer = copy.outer
    wanted = {e.id[len(outer.id) + 1:] for e in outer.edges}
    return [e for e in edges if _local(e) in wanted] if own is not None else edges


def _dot(a, b):
    return sum(p * q for p, q in zip(a, b))


class SketchElement:
    """An element of a sketch by its id, as a selection holds it.

    Its properties (name, maxh, minh, hpref, layer, col, partition, construction) read and write the sketch's element.

    Examples:
        sk("Line.3")[0].Copy()
        sk(kind="arc")[0].Measure()
    """

    def __init__(self, sketch, local, kind):
        self.sketch, self.local, self.kind = sketch, local, kind.lower()

    @property
    def id(self) -> str:
        """The element's id, the sketch's id and its id in the sketch (Sketch.1.Line.3)."""
        return self.sketch.id + "." + self.local

    @property
    def center(self) -> tuple[float, float, float]:
        """The center (x, y, z) the sketch's Measure gives for the element."""
        return tuple(self.sketch.Measure(self.local)["center"])

    def Measure(self, *args: Any, **kwargs: Any) -> dict:
        """The element's measures as a dict, as sk.Measure(id, ...).

        Args:
            args: the further arguments of sk.Measure after the id.
            kwargs: its keyword arguments.

        Examples:
            sk("Line.3")[0].Measure()
            sk("Line.3")[0].Measure(to="Line.4")
        """
        return self.sketch.Measure(self.local, *args, **kwargs)

    def Distance(self, target: Any, /, *args: Any, **kwargs: Any) -> Any:
        """The distance from the element's solved edges (a point element: its point) to the target, as Wire.Distance.

        Args:
            target: anything Wire.Distance takes, also a selection or an element.
            args: the further arguments of Wire.Distance.
            kwargs: its keyword arguments.

        Examples:
            sk("Line.3")[0].Distance((5, 0))
            sk("Line.3")[0].Distance(sk("Arc.1"), components="hv")
        """
        return as_target(self).Distance(target, *args, **kwargs)

    def Wire(self) -> Wire:
        """The element's solved edges as a Wire of their own, as sk.Wire(id).

        Examples:
            sk("Line.3")[0].Wire()
        """
        return self.sketch.Wire(self.local)

    def Copy(self) -> Wire:
        """The element's solved edges as a Wire of their own, as sk.Wire(id).

        Examples:
            sk("Line.3")[0].Copy()
        """
        return self.Wire()

    def __copy__(self):
        return self.Copy()

    def __deepcopy__(self, memo):
        return self.Copy()

    def _ref_in(self, sketch):
        """its id in sketch, for the calls that take ids"""
        if sketch is not self.sketch:
            raise ValueError("%s belongs to another sketch" % self.id)
        return self.local

    def __repr__(self):
        return "<%s %s>" % (self.kind, self.id)


# the properties of sketch elements, writable as the drawing calls take them (points: name, maxh, hpref)
FIELDS = ("name", "maxh", "minh", "hpref", "layer", "col", "partition", "construction")
FIELD_TYPES = {"name": "str | None", "maxh": "float | None", "minh": "float | None", "hpref": "float | None",
               "layer": "int | None", "col": "tuple[float, float, float, float] | None", "partition": "list[float] | None",
               "construction": "bool | None", "quad_dominated": "bool | None"}


def _element_property(field):
    def get(self):
        if self.local is None:
            return None
        values = self.sketch._Properties([self.local], field)
        return values[0] if values else None

    def set(self, value):
        if self.local is None:
            raise AttributeError("%s is a place, it has no properties" % self.id)
        self.sketch._SetProperty([self.local], field, value)

    return property(get, set, doc="%s: %s\n\nThe element's %s, as the drawing calls take it." % (field, FIELD_TYPES[field], field))


for _field in FIELDS:
    setattr(SketchElement, _field, _element_property(_field))


class SketchPoint(SketchElement):
    """A point of a sketch: a point element, or the place a reference names (Line.3.mid).

    A point element's properties and id are writable; a place has neither. It acts as its coordinates (h, v) in the
    plane, read live: h, v = point, np.array(point), point == (h, v); .x, .y, .z give it in 3d.

    Examples:
        sk("Line.3.end")[0].Copy()
        sk("Line.3.mid")[0].Distance((0, 0))
    """

    def __init__(self, sketch, local, ref=None):
        super().__init__(sketch, local, "point")
        self.ref = ref

    @property
    def id(self) -> str:
        """The point element's id (Sketch.1.Point.3, or the one set: point.id = "corner"), a place's its reference."""
        return self.sketch.id + "." + (self.local or self.ref)

    @id.setter
    def id(self, value):
        if self.local is None:
            raise AttributeError("%s is a place, it has no id to set" % self.id)
        self.sketch._RenamePoint(self.local, value)
        self.local = value

    def _hv(self):
        return tuple(self.sketch.Location(self.ref or self.local))

    def _xyz(self):
        return tuple(self.sketch.ToXYZ(self._hv()))

    def _ref_in(self, sketch):
        if sketch is not self.sketch:
            raise ValueError("%s belongs to another sketch" % self.id)
        return self.ref or self.local

    @property
    def p(self) -> Vertex:
        """Where it is now, as a px.Vertex."""
        return px.Vertex(self._hv())

    def Copy(self) -> Vertex:
        """Where it is now, as a px.Vertex.

        Examples:
            sk("Line.3.end")[0].Copy()
        """
        return px.Vertex(self._hv())

    h = property(lambda self: self._hv()[0], doc="Where it is along the plane's first axis.")
    v = property(lambda self: self._hv()[1], doc="Where it is along the plane's second axis.")
    x = property(lambda self: self._xyz()[0], doc="Its x in 3d.")
    y = property(lambda self: self._xyz()[1], doc="Its y in 3d.")
    z = property(lambda self: self._xyz()[2], doc="Its z in 3d.")

    @property
    def center(self) -> tuple[float, float, float]:
        """Where it is, (x, y, z) in 3d."""
        return self._xyz()

    def Measure(self, *args: Any, **kwargs: Any) -> dict:
        """The point's measures as a dict, as sk.Measure(id, ...).

        Args:
            args: the further arguments of sk.Measure after the id.
            kwargs: its keyword arguments.

        Examples:
            sk("Line.3.end")[0].Measure(to="Line.4")
        """
        return self.sketch.Measure(self.ref or self.local, *args, **kwargs)

    def Distance(self, target: Any, /, project: bool = False) -> float:
        """The distance from the point to the target.

        Args:
            target: anything Distance takes (a Face, Shape, Wire, element, selection), or a point: (h, v) in this
                point's plane or (x, y, z).
            project: True projects a target in another plane into its own plane.

        Examples:
            sk("Line.3.end")[0].Distance((0, 0))
            sk("Line.3.end")[0].Distance(other_face, project=True)
        """
        return Point(self.id, self._hv(), _plane(self)).Distance(target, project)

    def __len__(self):
        return 2

    def __iter__(self):
        return iter(self._hv())

    def __getitem__(self, k):
        return self._hv()[k]

    def __array__(self, dtype=None, copy=None):
        import numpy

        return numpy.array(self._hv(), dtype=dtype)

    def __eq__(self, other):
        try:
            return self._hv() == tuple(other) if len(other) == 2 else NotImplemented
        except TypeError:
            return NotImplemented

    __hash__ = None

    def __repr__(self):
        h, v = self._hv()
        return "<point %s (%.6g, %.6g)>" % (self.id, h, v)


class Point(tuple):
    """A place an id names (Line.3.mid, Arc.2.center, Face.1.center): the tuple (h, v) in its plane.

    It has .id, .p (a px.Vertex), .h, .v, and .x, .y, .z in 3d; it has no properties.

    Examples:
        shape("Arc.2.center")[0].Distance((0, 0))
        shape("Line.3.mid")[0].Copy()
    """

    def __new__(cls, id, hv, plane):
        self = super().__new__(cls, (hv[0], hv[1]))
        self.__dict__.update(id=id, p=px.Vertex((hv[0], hv[1])), _plane=plane)
        return self

    def __setattr__(self, name, value):
        raise AttributeError("%s is a place, it has no %s" % (self.id, name))

    h = property(lambda self: self[0], doc="Where it is along the plane's first axis.")
    v = property(lambda self: self[1], doc="Where it is along the plane's second axis.")
    x = property(lambda self: self._xyz()[0], doc="Its x in 3d.")
    y = property(lambda self: self._xyz()[1], doc="Its y in 3d.")
    z = property(lambda self: self._xyz()[2], doc="Its z in 3d.")

    def _hv(self):
        return (self[0], self[1])

    def _xyz(self):
        o, u, w = self._plane
        return tuple(o[k] + self[0] * u[k] + self[1] * w[k] for k in range(3))

    def Copy(self) -> Vertex:
        """The place as a px.Vertex.

        Examples:
            shape("Line.3.mid")[0].Copy()
        """
        return px.Vertex(self._hv())

    def __copy__(self):
        return self.Copy()

    def __deepcopy__(self, memo):
        return self.Copy()

    def _ref_in(self, sketch):
        raise ValueError("%s is a place of its own, not an id of this sketch" % self.id)

    def Distance(self, target: Any, /, project: bool = False) -> float:
        """The distance from the place to the target.

        Args:
            target: anything Distance takes (a Face, Shape, Wire, element, selection), or a point: (h, v) in this
                place's plane or (x, y, z).
            project: True projects a target in another plane into this place's plane.

        Examples:
            shape("Arc.2.center")[0].Distance((0, 0))
            shape("Arc.2.center")[0].Distance(other_face, project=True)
        """
        t = as_target(target)
        if isinstance(t, Point):
            return math.dist(self._xyz(), t._xyz())
        if isinstance(t, (tuple, list)):
            return math.dist(self._xyz() if len(t) == 3 else self, t)
        return t.Distance(self._xyz(), project=project)

    def __repr__(self):
        return "<point %s (%.6g, %.6g)>" % (self.id, self.p.x, self.p.y)


def _in_plane(c, plane):
    o, u, w = plane
    d = tuple(a - b for a, b in zip(c, o))
    return _dot(d, u), _dot(d, w)


POINTS = ("start", "end", "mid", "midpoint", "center")


def _closed(spline):
    a, b = spline.start, spline.end
    return (a.x, a.y) == (b.x, b.y)


def _point_of(item, part):
    """the point part of an element names: a vertex at an edge's ends, else a Point; None where it names none"""
    id = item.id + "." + part
    if isinstance(item, SketchElement):
        ref = item.local + "." + ("mid" if part == "midpoint" else part)
        return SketchPoint(item.sketch, item.sketch._PointElement(ref), ref)
    if isinstance(item, px.EdgeView):
        if part in ("start", "end"):
            if isinstance(item.edge, px.CircleEdge):
                return item.edge.origin
            # a closed spline is one edge without a vertex, as a circle
            if isinstance(item.edge, px.SplineEdge) and _closed(item.edge):
                return item.edge.start
            v = item._vertex(part == "end")
            if owner(item) is not None:
                _OWNER[v] = owner(item)
            return v
        if part == "center":
            if not isinstance(item.edge, (px.ArcEdge, px.CircleEdge)):
                raise ValueError("%s: only arcs and circles have a center" % id)
            c = item.edge.center
        else:
            c = item.midpoint
        return Point(id, (c.x, c.y), item._plane)
    if isinstance(item, px.FaceView) and part == "center":
        return Point(id, _in_plane(item.center, item._plane), item._plane)
    return None


def _point_refs(items, key):
    """the points key names when it is an element's id and a part (Line.3.end); looked up in the items, else in
    their object"""
    base, _, part = key.rpartition(".")
    if part not in POINTS or not base:
        return []
    named = lambda i: _names(i, base)
    found = [i for i in items if named(i)]
    if not found and items and owner(items[0]) is not None:
        o = owner(items[0])
        pool = list(o.edges) + (list(o.faces) if isinstance(o, px.Shape) else [])
        found = [i for i in pool if named(i)]
    return [p for p in (_point_of(i, part) for i in found) if p is not None]


def as_target(x):
    """what Distance takes for a selection or an element: its one element, several edges as a wire, faces unified"""
    if isinstance(x, Selection):
        if len(x) == 1:
            x = x[0]
        elif x and all(isinstance(i, px.FaceView) for i in x):
            return px.Unify([i.Copy() for i in x])
        else:
            return px.Unify(px.Wires(x._edge_parts("Distance")))
    if isinstance(x, SketchPoint):
        return Point(x.id, x._hv(), _plane(x))
    if isinstance(x, SketchElement):
        return Point(x.id, _in_plane(x.center, _plane(x)), _plane(x)) if x.kind == "point" else x.Wire()
    return x


def _plane(item):
    if isinstance(item, SketchElement):
        s = item.sketch
        o = s.ToXYZ((0, 0))
        return o, tuple(a - b for a, b in zip(s.ToXYZ((1, 0)), o)), tuple(a - b for a, b in zip(s.ToXYZ((0, 1)), o))
    return item._plane


def _coords(item):
    """the center of an element as {h, v, x, y, z}"""
    o, u, w = _plane(item)
    if isinstance(item, px.EdgeView):
        e = item.edge
        c = e.center if isinstance(e, (px.ArcEdge, px.CircleEdge)) else item.midpoint
        hv = (c.x, c.y)
    elif isinstance(item, (px.VertexView, Point)):
        hv = (item.p.x, item.p.y)
    elif isinstance(item, SketchPoint):
        hv = item._hv()
    else:
        hv = _in_plane(item.center, (o, u, w))
    xyz = tuple(o[k] + hv[0] * u[k] + hv[1] * w[k] for k in range(3))
    return {"h": hv[0], "v": hv[1], "x": xyz[0], "y": xyz[1], "z": xyz[2]}


def _kind(item):
    if isinstance(item, SketchElement):
        return item.kind
    if isinstance(item, px.EdgeView):
        return {px.LineEdge: "line", px.ArcEdge: "arc", px.CircleEdge: "circle"}.get(type(item.edge), "spline")
    if isinstance(item, Point):
        return "point"
    return "vertex" if isinstance(item, px.VertexView) else "face"


def _point(target):
    """a point as (h, v) or (x, y, z): a point itself, else the center of an element, selection or object"""
    if isinstance(target, Selection):
        if len(target) != 1:
            raise ValueError("near=: a selection of %d elements has no single center" % len(target))
        target = target[0]
    if isinstance(target, (SketchPoint, Point)):
        return target._xyz()
    if isinstance(target, (px.EdgeView, px.VertexView, px.FaceView, SketchElement)):
        c = _coords(target)
        return (c["x"], c["y"], c["z"])
    if isinstance(target, (tuple, list)) and all(isinstance(c, (int, float)) for c in target):
        return tuple(float(c) for c in target)
    if hasattr(target, "x") and hasattr(target, "y") and not hasattr(target, "edges"):
        return (target.x, target.y)
    c = target.center
    if c is None:
        raise ValueError("near=: it is empty, it has no center")
    return tuple(c)


def _direction(key):
    """a key of Min/Max/Sorted as a function of the element: a measure ("length", "area", "radius", a coordinate
    "x".."v"), an axis (px.X, px.H, netgen's X), or a direction (2 values in the plane, 3 in 3d)"""
    if isinstance(key, str):
        if key in ("h", "v", "x", "y", "z"):
            return lambda i: _coords(i)[key]
        return lambda i: _measure(i, key)
    if isinstance(key, Coordinate):
        return key.of
    name = getattr(key, "name", None)
    if type(key).__name__ == "SketchAxis" and name in ("H", "V"):
        return lambda i: _coords(i)[name.lower()]
    d = (key.x, key.y, key.z) if all(hasattr(key, a) for a in "xyz") else tuple(key)
    if len(d) == 2:
        return lambda i: _coords(i)["h"] * d[0] + _coords(i)["v"] * d[1]
    return lambda i: sum(_coords(i)[a] * d[k] for k, a in enumerate("xyz"))


def _measure(item, key):
    if key in ("length", "area") and not isinstance(item, SketchElement):
        return item.mass
    if key == "radius" and isinstance(item, px.EdgeView) and isinstance(item.edge, (px.ArcEdge, px.CircleEdge)):
        return item.edge.radius
    value = item.Measure().get(key)
    if value is None:
        raise ValueError("no %r to sort by for %r" % (key, item))
    return value


class Predicate:
    """A condition on an element, made by comparing px.x, px.y, px.z, px.h or px.v with a number.

    & (and), | (or) and ~ (not) combine conditions; selection[condition] keeps the elements that meet it; calling it
    with an element tests it.

    Examples:
        shape.edges[(px.x > 5) & (px.h < 2)]
        shape.faces[~(px.v >= 0)]
    """

    def __init__(self, test, text):
        self.test, self.text = test, text

    def __call__(self, item):
        return self.test(item)

    def __and__(self, other):
        return Predicate(lambda i: self(i) and other(i), "(%s) & (%s)" % (self.text, other.text))

    def __or__(self, other):
        return Predicate(lambda i: self(i) or other(i), "(%s) | (%s)" % (self.text, other.text))

    def __invert__(self):
        return Predicate(lambda i: not self(i), "~(%s)" % self.text)

    def __repr__(self):
        return "Predicate(%s)" % self.text


class Coordinate:
    """A coordinate of an element's center: px.x, px.y, px.z in 3d, px.h, px.v in its plane.

    Compared with a number (<, <=, >, >=) it gives a Predicate; as the key of Min, Max and Sorted it orders by the
    coordinate. The center of an arc or circle is its circle's center, of another edge its midpoint along it, of a
    face its centroid, of a vertex its point.

    Examples:
        shape.edges[px.x > 5]
        shape.edges.Max(px.h)
    """

    def __init__(self, name):
        self.name = name

    def of(self, item: Any) -> float:
        """The coordinate of the element's center.

        Args:
            item: an element: an edge, face or vertex, or a sketch element.

        Examples:
            px.x.of(shape.edges[0])
        """
        return _coords(item)[self.name]

    def _compare(self, op, c, symbol):
        return Predicate(lambda i: op(self.of(i), c), "%s %s %r" % (self.name, symbol, c))

    def __lt__(self, c):
        return self._compare(lambda a, b: a < b, c, "<")

    def __le__(self, c):
        return self._compare(lambda a, b: a <= b, c, "<=")

    def __gt__(self, c):
        return self._compare(lambda a, b: a > b, c, ">")

    def __ge__(self, c):
        return self._compare(lambda a, b: a >= b, c, ">=")

    def __repr__(self):
        return "px." + self.name


def _names(item, key):
    """key is the item's id, or its id in its object; a sketch element's exactly (Line.1 is not LinearPattern.1.Line.1)"""
    i = item.id
    if i == key:
        return True
    if isinstance(item, SketchElement):
        return item.local == key or getattr(item, "ref", None) == key
    return i.endswith("." + key)


def _did_you_mean(key, items):
    """the closest ids of the items, global and within their object, as every id error suggests them"""
    ids = set()
    for i in items:
        ids.add(i.id)
        ids.add(i.local if isinstance(i, SketchElement) and i.local else _local(i))
    return px._DidYouMean(key, sorted(ids))


def _from_end(key, items):
    """the key with each negative number in a part (Line.-1) the number it counts from the end among the items' ids"""
    ids = {x for i in items for x in (i.id, i.local if isinstance(i, SketchElement) else _local(i)) if x}
    parts = key.split(".")
    for k in range(1, len(parts)):
        if re.fullmatch(r"-\d+", parts[k]):
            prefix = ".".join(parts[:k]) + "."
            numbers = sorted({int(n) for i in ids if i.startswith(prefix) for n in [i[len(prefix):].split(".")[0]] if n.isdigit()})
            n = -int(parts[k])
            if not 0 < n <= len(numbers):
                raise ValueError("'%s': there are %d numbers under '%s' (-1 is the last)" % (key, len(numbers), prefix[:-1]))
            parts[k] = str(numbers[-n])
    return ".".join(parts)


def _is_glob(key):
    return isinstance(key, str) and any(part in ("*", "**") for part in key.split("."))


@functools.lru_cache(maxsize=256)
def _glob(key):
    """the pattern of a glob key, dots separating the parts: * one part, ** any number of parts; like an id it names
    what it matches and what lies under that"""
    parts = key.split(".")
    out = ""
    for k, part in enumerate(parts):
        if part == "**":
            out += r"(?:[^.]+\.)*" if k == 0 else r"(?:\.[^.]+)*"
            continue
        if k > 0 and not (k == 1 and parts[0] == "**"):
            out += r"\."
        out += r"[^.]+" if part == "*" else re.escape(part)
    return re.compile(out + r"(?:\..+)?")


def _matches(item, key):
    """the item's id or one under it (Shape.1 its edges Shape.1.Line.3); a sketch element's id also within the sketch
    (wall: the offset's wall.Line.3); a glob key (* one part, ** any number) its pattern"""
    if not isinstance(key, str):
        key = key.id
    i = getattr(item, "id", None)
    if i is None:
        return False
    if _is_glob(key):
        local = item.local if isinstance(item, SketchElement) else _local(item)
        return bool(_glob(key).fullmatch(i) or (local and _glob(key).fullmatch(local)))
    within = isinstance(item, SketchElement) and item.local.startswith(key + ".")
    return _names(item, key) or i.startswith(key + ".") or within


NEAR = ("near", "nearh", "nearv", "nearx", "neary", "nearz")


class Selection:
    """Elements of an object, chosen: edges, faces, vertices, sketch elements and points.

    obj(...), obj.edges(...), obj.faces(...), obj.vertices(...) and sk(...) make one; calling it chooses further
    (see __call__); [] takes an index, a slice, a list of indices or a condition (px.x > 5, combined with & | ~).
    Setting a property (name, maxh, col, ...) sets it on every element; reading it gives the value they share, None if
    they differ.

    Args:
        items: the elements.

    Examples:
        shape.edges(name="web")(kind="arc")
        shape.edges[(px.x > 5) & (px.h < 2)].Min("length")
    """

    def __init__(self, items: Any, /) -> None:
        self._items = list(items)

    def __len__(self):
        return len(self._items)

    def __iter__(self):
        return iter(self._items)

    def __bool__(self):
        return bool(self._items)

    def __repr__(self):
        return "Selection([%s])" % ", ".join(i.id for i in self._items)

    def __getitem__(self, k):
        if isinstance(k, Predicate):
            return Selection(i for i in self._items if k(i))
        if isinstance(k, slice):
            return Selection(self._items[k])
        if isinstance(k, (list, tuple)):
            return Selection(self._items[j] for j in k)
        return self._items[k]

    def __call__(self, *keys: Any, name: str | None = None, kind: str | None = None, **near: Any) -> Selection:
        """The elements the keys name, of that name and kind, nearest to a point.

        Args:
            keys: what to keep: an index or a list of indices; an id ("Line.3", "Shape.1.Line.3"; a negative number
                counts from the end, "Line.-1"), which also names what lies under it; a glob ("Line.*", "**.end": *
                one part, ** any number); a point an element's id names ("Line.3.end", "Line.3.mid", "Arc.2.center");
                or an element. Several keys keep the elements of each, once.
            name: keeps the elements with this name; several joined by | ("web|flange").
            kind: keeps the elements of this kind: "line", "arc", "circle", "spline", "face", "vertex", "point";
                several joined by | ("arc|circle").
            near: near=point keeps the element whose center is nearest to the point ((h, v), (x, y, z), an element or
                a one-element selection); nearh=, nearv=, nearx=, neary=, nearz= the one nearest to that coordinate.
                Of an element and a point equally near, the element.

        Raises:
            ValueError: an id names no element here (the error names the closest ids), a negative number is out of
                range, or near= is a selection of several elements.
            TypeError: an unknown keyword.

        Examples:
            shape.edges("Line.3", "Line.5")
            shape.edges(kind="arc", near=(0, 10))
        """
        unknown = set(near) - set(NEAR)
        if unknown:
            raise TypeError("selection: unknown keyword %s (name=, kind=, %s)" % (", ".join(sorted(unknown)), ", ".join(NEAR)))
        items = self._items
        if keys:
            picked = []
            for key in keys:
                if isinstance(key, int):
                    picked.append(items[key])
                elif isinstance(key, (list, tuple)) and all(isinstance(k, int) for k in key):
                    picked.extend(items[k] for k in key)
                else:
                    if isinstance(key, str) and ".-" in key and not _is_glob(key):
                        key = _from_end(key, items)
                    hits = [i for i in items if _matches(i, key)] or (_point_refs(items, key) if isinstance(key, str) and not _is_glob(key) else [])
                    if not hits and isinstance(key, str) and not _is_glob(key):
                        raise ValueError("no element %r here%s" % (key, _did_you_mean(key, items)))
                    picked.extend(hits)
            items = list({id(i): i for i in picked}.values())
        if name is not None:
            names = {n.strip() for n in name.split("|")}
            items = [i for i in items if getattr(i, "name", None) in names]
        if kind is not None:
            kinds = {k.strip().lower() for k in kind.split("|")}
            items = [i for i in items if _kind(i) in kinds]
        for k, target in near.items():
            if not items:
                break
            if k == "near":
                p = _point(target)
                axes = ("h", "v") if len(p) == 2 else ("x", "y", "z")
                dist = lambda i: math.dist(p, [_coords(i)[a] for a in axes])
                scale = max(abs(c) for c in p)
            else:
                dist = lambda i, a=k[4:]: abs(_coords(i)[a] - target)
                scale = abs(target)
            found = [(dist(i), i) for i in items]
            best = min(d for d, _ in found)
            # equally near (a circle and its centre point): not the point
            tol = px.config.k * px.config.eps * max(1.0, scale, best)
            tied = [i for d, i in found if d <= best + tol]
            items = [next((i for i in tied if _kind(i) not in ("point", "vertex")), tied[0])]
        return Selection(items)

    def Min(self, key: str | Coordinate | Any) -> Selection:
        """The element with the smallest value of key, as a selection of one (none if it is empty).

        Args:
            key: as Sorted: a measure, a coordinate, an axis or a direction.

        Examples:
            selection.Min("length")
            selection.Min(px.x)
        """
        return self.Sorted(key)[0:1]

    def Max(self, key: str | Coordinate | Any) -> Selection:
        """The element with the largest value of key, as a selection of one (none if it is empty).

        Args:
            key: as Sorted: a measure, a coordinate, an axis or a direction.

        Examples:
            selection.Max("area")
            selection.Max((1, 1))
        """
        return self.Sorted(key, reverse=True)[0:1]

    def Sorted(self, key: str | Coordinate | Any, reverse: bool = False) -> Selection:
        """The elements sorted by key, smallest first.

        Args:
            key: a measure ("length", "area", "radius", or another key of Measure()), a coordinate of the center
                ("h", "v", "x", "y", "z", px.x, px.h, ...), an axis (px.X, the sketch's H or V, netgen's X), or a
                direction: the center's component along (dh, dv) in the plane or (dx, dy, dz) in 3d.
            reverse: True sorts largest first.

        Raises:
            ValueError: an element has no such measure.

        Examples:
            selection.Sorted("length")
            selection.Sorted(px.Y, reverse=True)
        """
        if not self._items:
            return Selection([])
        return Selection(sorted(self._items, key=_direction(key), reverse=reverse))

    @property
    def ids(self) -> list[str]:
        """The ids of the selected elements, in order."""
        return [i.id for i in self._items]

    def _union(self, items):
        seen, out = set(), []
        for i in items:
            if i.id not in seen:
                seen.add(i.id)
                out.append(i)
        return Selection(out)

    @property
    def faces(self) -> Selection:
        """The faces the selected edges bound and the selected faces themselves, each once."""
        return self._union(f for i in self._items for f in (_faces_of(i) if isinstance(i, px.EdgeView) else [i]))

    @property
    def edges(self) -> Selection:
        """The edges of the selected faces and the selected edges themselves, each once."""
        return self._union(e for i in self._items for e in self._edges_of(i))

    @staticmethod
    def _edges_of(item):
        if isinstance(item, SketchElement) and item.kind == "point":
            raise TypeError("edges: %s is a point, it has no edges; select the lines, arcs, circles or splines" % item.id)
        return [item] if isinstance(item, (px.EdgeView, SketchElement)) else item.edges

    @property
    def outline(self) -> Selection:
        """The edges on the outer loops of the selected faces and of the faces the selected edges bound."""
        return self._union(e for f in self.faces for e in _outline_of(f))

    def _edge_parts(self, call):
        """the selected edges, and the other elements' edges as wires"""
        parts = []
        for i in self._items:
            if isinstance(i, (px.VertexView, Point)):
                raise TypeError("%s: %s is a point, it has no edges" % (call, i.id))
            parts.append(i if isinstance(i, px.EdgeView) else i.Wire())
        return parts

    def Wire(self) -> Wire:
        """The selected edges as one Wire, joined as px.Wire(edges); other elements give their edges.

        Raises:
            ValueError: the edges do not form one chain (px.Wires of the edges builds several).
            TypeError: a point is selected.

        Examples:
            shape.edges(name="slot").Wire()
        """
        return px.Wire(self._edge_parts("Wire()"))

    def Copy(self) -> Compound:
        """Copies of everything selected as one Compound; the object they were selected from stays as it is.

        Faces become Faces, edges Wires with their properties, sketch elements their Wires.

        Raises:
            TypeError: a vertex or point is selected ([v.Copy() for v in selection] copies vertices).

        Examples:
            shape.edges(name="slot").Copy()
        """
        from ._compound import Compound

        copies = [i.Copy() for i in self._items]
        vertices = [i.id for i, c in zip(self._items, copies) if isinstance(c, px.Vertex)]
        if vertices:
            raise TypeError("Copy(): %s %s a vertex, a Compound holds faces, wires and shapes; [v.Copy() for v in "
                            "selection] copies vertices" % (", ".join(vertices), "is" if len(vertices) == 1 else "are"))
        return Compound(copies)

    def __copy__(self):
        return self.Copy()

    def __deepcopy__(self, memo):
        return self.Copy()

    def _one(self, what):
        if len(self._items) != 1:
            raise ValueError("%s: the selection has %d elements, select one" % (what, len(self._items)))
        return self._items[0]

    def Measure(self, *args: Any, **kwargs: Any) -> dict:
        """The measures of the one selected element, as its Measure.

        Args:
            args: the arguments of the element's Measure.
            kwargs: its keyword arguments.

        Raises:
            ValueError: the selection does not hold exactly one element.

        Examples:
            shape.edges("Line.3").Measure()
        """
        return self._one("Measure").Measure(*args, **kwargs)

    def Distance(self, *args: Any, **kwargs: Any) -> Any:
        """The distance from the nearest selected element, as their Distance.

        Args:
            args: the arguments of the elements' Distance.
            kwargs: their keyword arguments.

        Raises:
            ValueError: the selection is empty.

        Examples:
            shape.edges("Line.3").Distance((5, 0))
            face.edges(name="channel").Distance((30, 10))
        """
        if not self._items:
            raise ValueError("Distance: the selection is empty")
        return min(item.Distance(*args, **kwargs) for item in self._items)

    @property
    def center(self) -> tuple[float, ...]:
        """The center (x, y, z) of the one selected element."""
        return _point(self)

    def _hv(self):
        return self._one("a point")._hv()

    def _ref_in(self, sketch):
        return self._one("an id")._ref_in(sketch)


def _selection_property(field):
    """set on every element (a sketch's in one call), read as the value they share, None if they differ"""

    def parts(self):
        sketches, others = {}, []
        for i in self._items:
            if isinstance(i, SketchElement):
                if i.local is not None:
                    sketches.setdefault(id(i.sketch), (i.sketch, []))[1].append(i.local)
            else:
                others.append(i)
        return list(sketches.values()), others

    def get(self):
        found, others = parts(self)
        values = [v for sketch, ids in found for v in sketch._Properties(ids, field)]
        values += [getattr(i, field) for i in others if hasattr(type(i), field)]
        return values[0] if values and all(v == values[0] for v in values) else None

    def set(self, value):
        if not self._items:
            raise ValueError("%s = %r: the selection selects nothing" % (field, value))
        for i in self._items:
            if isinstance(i, SketchElement) and i.local is None:
                raise AttributeError("%s = %r: %s is a place, it has no properties" % (field, value, i.id))
        found, others = parts(self)
        for i in others:
            if not hasattr(type(i), field):
                raise AttributeError("%s = %r: %s has no %s" % (field, value, i.id, field))
        for sketch, ids in found:
            sketch._SetProperty(ids, field, value)
        for i in others:
            setattr(i, field, value)

    return property(get, set, doc="%s: %s\n\nThe selected elements' %s: set on all, read as the value they share "
                                  "(None if they differ)." % (field, FIELD_TYPES[field], field))


for _field in FIELDS + ("quad_dominated",):
    setattr(Selection, _field, _selection_property(_field))


def select(items):
    return Selection(items)


def _remember_owner(cls, prop, of_view=False):
    """the view a property returns remembers the object (of a face view: its shape)"""
    p = getattr(cls, prop)

    def get(self):
        v = p.fget(self)
        o = owner(self) if of_view else self
        if o is not None:
            try:
                _OWNER[v] = o
            except TypeError:
                pass
        return v

    setattr(cls, prop, property(get, p.fset, doc=p.__doc__))


def _taking_selections(cls, name):
    """cls.name with a selection or an element as its target"""
    f = getattr(cls, name)

    @functools.wraps(f)
    def call(self, *args, **kwargs):
        if args:
            args = (as_target(args[0]),) + args[1:]
        elif "target" in kwargs:
            kwargs["target"] = as_target(kwargs["target"])
        return f(self, *args, **kwargs)

    setattr(cls, name, call)


def add_selectors():
    """obj(...) and obj.edges(...) / faces(...) / vertices(...) choose elements, views take conditions in []"""
    for cls in (px.Shape, px.Face, px.Wire):
        for prop in ("edges", "faces", "vertices"):
            if prop in cls.__dict__:
                _remember_owner(cls, prop)
    for prop in ("edges", "vertices"):
        _remember_owner(px.FaceView, prop, of_view=True)
    for view in (px.EdgesView, px.FacesView, px.VerticesView):
        getitem = view.__getitem__

        def __getitem__(self, k, getitem=getitem):
            if isinstance(k, (Predicate, list, tuple)):
                return Selection(list(self))[k]
            item = getitem(self, k)
            o = owner(self)
            if o is not None:
                for x in item if isinstance(item, list) else [item]:
                    _OWNER[x] = o
            return item

        # the forms for type checkers (tools/stubgen.py reads them): an index gives one item
        item = {px.EdgesView: "EdgeView", px.FacesView: "FaceView", px.VerticesView: "VertexView"}[view]
        __getitem__.__doc__ = ("__getitem__(k: int) -> %s\n__getitem__(k: slice) -> list[%s]\n__getitem__(k: Any) -> Selection\n\n"
                               "The item at k, a list of them for a slice, a Selection for a condition or a list." % (item, item))
        view.__getitem__ = __getitem__
        view.__call__ = lambda self, *a, **k: Selection(list(self))(*a, **k)
        view.Min = lambda self, key: Selection(list(self)).Min(key)
        view.Max = lambda self, key: Selection(list(self)).Max(key)
        view.Sorted = lambda self, key, reverse=False: Selection(list(self)).Sorted(key, reverse)
        view.Copy = lambda self: Selection(list(self)).Copy()
        for method in ("Min", "Max", "Sorted", "Copy"):
            getattr(view, method).__doc__ = getattr(Selection, method).__doc__
        view.__copy__ = lambda self: self.Copy()
        view.__deepcopy__ = lambda self, memo: self.Copy()
    px.Shape.__call__ = lambda self, *a, **k: Selection(list(self.faces) + list(self.edges) + list(self.vertices))(*a, **k)
    for part in ("start", "end"):
        setattr(px.EdgeView, part, property(lambda self, part=part: _point_of(self, part),
                                            doc="%s: VertexView | Vertex\n\nThe vertex at the %s, with its id, its "
                                                "properties and its coordinates (h, v)." % (part, part)))
    for cls in (px.Face, px.Wire):
        cls.__call__ = lambda self, *a, **k: Selection(list(self.edges) + list(self.vertices))(*a, **k)

    def from_end(sk, key):
        if not isinstance(key, str) or ".-" not in key or _is_glob(key):
            return key
        own = sk.id + "."
        return own + sk._FromEnd(key[len(own):]) if key.startswith(own) else sk._FromEnd(key)

    def direct(sk, keys):
        """the items the keys name when each is an element's id (with what lies under it) or its point (Arc.1.center),
        looked up one by one; None when a key is anything else"""
        found = {}
        for key in keys:
            if not isinstance(key, str) or key in ("origin", "px.origin") or _is_glob(key) or ".-" in key:
                return None
            chosen = sk._Chosen(key)
            if chosen:
                found.update((i, SketchPoint(sk, i) if kind == "Point" else SketchElement(sk, i, kind)) for kind, i in chosen)
                continue
            base, _, part = key.rpartition(".")
            if part not in POINTS or sk._KindById(base) not in ("Line", "Arc", "Circle"):
                return None
            ref = base + "." + ("mid" if part == "midpoint" else part)
            found[len(found), ref] = SketchPoint(sk, sk._PointElement(ref), ref)
        return list(found.values())

    def sketch_call(self, *a, **k):
        if a:
            items = direct(self, a)
            if items is not None:
                return Selection(items)(**k)
        items = [SketchPoint(self, i) if kind == "Point" else SketchElement(self, i, kind)
                 for kind, ids in self.elements.items() if kind in ("Point", "Line", "Arc", "Circle", "Spline", "Curve")
                 for i in ids if i not in ("origin", "px.origin")]
        a = [from_end(self, key) for key in a]
        # the points of a cut element (Line.3.end), which is the span of its pieces and no element of its own here
        spans = set(self._Spans()) if any(isinstance(key, str) and "." in key for key in a) else set()
        items += [SketchPoint(self, self._PointElement(key), key) for key in a
                  if isinstance(key, str) and key.rpartition(".")[0] in spans and key.rpartition(".")[2] in ("start", "end", "mid", "center")]
        wires = self.wires
        under = lambda key: any(i.local.startswith(key + ".") for i in items)
        keys = [x for key in a for x in (([key] if under(key) else []) + wires[key] if isinstance(key, str) and key in wires else [key])]
        return Selection(items)(*keys, **k)

    Sketch.__call__ = px.QuickSketch.__call__ = sketch_call
    for cls in (px.Wire, px.Face, px.Shape, px.EdgeView, px.VertexView, px.FaceView, *sketch_types):
        if "Distance" in cls.__dict__:
            _taking_selections(cls, "Distance")


def _with_id(prefix, among=None):
    """the live objects and compounds with this id (only ids already given), of among if given"""
    from ._compound import _ALL

    found = list(px._FindObject(prefix)) + [c for c in list(_ALL) if c._id == prefix]
    return found if among is None else [o for o in found if any(o is p for p in among)]


def _found(id, objects, prefix):
    if len(objects) > 1:
        raise ValueError("Find(%r): %d objects have the id %r" % (id, len(objects), prefix))
    return objects[0] if objects else None


def _within(id, obj, rest):
    """what the rest of the id names in obj: in a compound its part (its id, then what follows in the part), else the
    elements"""
    from ._compound import Compound

    if not rest:
        return obj
    if not isinstance(obj, Compound):
        return obj(".".join(rest))
    for k in range(len(rest), 0, -1):
        prefix = ".".join(rest[:k])
        part = _found(id, _with_id(prefix, obj.parts), prefix)
        if part is not None:
            return _within(id, part, rest[k:])
    raise ValueError("Find(%r): no object has that id (an auto id is given when it is first asked for)" % id)


def Find(id: str) -> Any:
    """The object with this id, or the elements the rest of the id names in it.

    Only ids already given are found: an auto id is given when it is first asked for, a sketch's when it is made. A
    part of a compound is named by the compound's id, then the part's ("both.plate", "both.plate.Line.2"); a plain id
    names the object outside any compound, else the one part that has it.

    Args:
        id: the id: an object's ("Sketch.1"), or an object's and an element's in it ("Sketch.1.Line.5").

    Returns:
        the object, or a Selection of the elements.

    Raises:
        ValueError: no object has the id, several objects have it, or the rest names no element.

    Examples:
        px.Find("Sketch.1")
        px.Find("both.plate.Line.2")
    """
    from ._compound import _ALL

    inside = [p for c in list(_ALL) for p in c.parts]
    parts = id.split(".")
    for k in range(len(parts), 0, -1):
        prefix = ".".join(parts[:k])
        found = _with_id(prefix)
        outside = [o for o in found if not any(o is p for p in inside)]
        obj = _found(id, outside or found, prefix)
        if obj is not None:
            return _within(id, obj, parts[k:])
    raise ValueError("Find(%r): no object has that id (an auto id is given when it is first asked for)" % id)

