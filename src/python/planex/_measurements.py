# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
import weakref
from typing import Any, Callable

from . import _planex as px
from ._auto import Auto, is_auto
from ._sketches import sketch_types
from ._select import Point, Selection, SketchElement, _did_you_mean, _point, as_target, owner
from ._caller import warn

# the stored measurements of each object: {id: (f, args, params)}
_STORED = weakref.WeakKeyDictionary()

MEASURE_DOC = ("\n\nWith a callable first: f(obj, *args, **params), its value; id= also stores it, obj.measures then "
               "holds it next to the standard measurements, RemoveMeasure(id) drops it (see planex.measurements); a stored "
               "measurement stays with this object, copies, moved copies and boolean results do not carry it")
MEASURE_FORM = "Measure(f: Callable, /, *args, id: str | None = None, **params) -> object"
MEASURE_ARGS = [
    "    f: a measurement, a callable f(obj, *args, **params) (see px.measurements): Measure returns its value.",
    "    args: the further arguments of f.",
    "    id: stores the measurement f under this id: obj.measures then holds it next to the standard measurements,",
    "        RemoveMeasure(id) drops it; it stays with this object (copies and boolean results do not carry it).",
    "    params: the keyword arguments of f.",
]
MEASURE_RAISES = "    TypeError: id without a measurement f."


def _pick(obj, spec):
    """the elements spec names in obj: a callable of obj, ids (obj(spec)), else edges and faces of that name"""
    if callable(spec) and not isinstance(spec, str):
        return spec(obj)
    if not isinstance(spec, str):
        return spec
    try:
        return obj(spec)
    except ValueError:
        if not hasattr(obj, "edges"):
            raise
    named = list(obj.edges(name=spec))
    if hasattr(obj, "faces") and not isinstance(obj, px.Face):
        named += list(obj.faces(name=spec))
    if not named:
        raise ValueError("%r: no element with that id or name%s" % (spec, _did_you_mean(spec, obj())))
    return Selection(named)


def _edges(x):
    if isinstance(x, px.EdgeView):
        return [x]
    if isinstance(x, SketchElement):
        return list(x.Wire().edges)
    if isinstance(x, Selection):
        return [e for i in x for e in _edges(i)]
    return list(x.edges)


def _is_point(x):
    if isinstance(x, Selection) and len(x) == 1:
        x = x[0]
    return isinstance(x, (Point, px.VertexView)) or (isinstance(x, SketchElement) and x.kind == "point")


def _measure_doc(doc):
    """the standard Measure's doc with the form that takes a measurement: its signature line, Args, Raises and an
    example (the docstring form), else MEASURE_DOC appended"""
    lines = doc.split("\n")
    if "Examples:" not in lines:
        return doc + MEASURE_DOC
    k = 0
    while lines[k].startswith("Measure("):
        k += 1
    lines.insert(k, MEASURE_FORM)
    at = lines.index("Examples:")
    x = lines[at + 1].strip().partition(".Measure(")[0]
    lines[at + 2:] = ["    %s.Measure(px.measurements.MinThickness(), id=\"wall\")" % x]
    if "Raises:" in lines:
        lines.insert(_section_end(lines, "Raises:"), MEASURE_RAISES)
    else:
        lines[at:at] = ["Raises:", MEASURE_RAISES, ""]
    if "Args:" in lines:
        end = _section_end(lines, "Args:")
        lines[end:end] = MEASURE_ARGS
    else:
        at = lines.index("Raises:")
        lines[at:at] = ["Args:"] + MEASURE_ARGS + [""]
    return "\n".join(lines)


def _section_end(lines, section):
    """the index after the last indented line of the section"""
    k = lines.index(section) + 1
    while k < len(lines) and lines[k].startswith("    "):
        k += 1
    return k


def hausdorff(a, b, samples=64):
    """the largest distance from a point of a to b or of b to a (a and b: elements, selections or objects): how far
    apart two curves are at their worst"""

    def one_way(source, target):
        dst = as_target(target)
        if _is_point(source):
            return dst.Distance(_point(as_target(source)))

        # a spline's samples moved from its approximation onto its exact curve, where Distance measures the target
        def far(e, t):
            p = e.PointAt(t)
            return dst.Distance(e.ClosestPoint((p.x, p.y)) if exact else (p.x, p.y))

        best = 0.0
        g = (math.sqrt(5) - 1) / 2
        for e in _edges(source):
            exact = isinstance(e.edge, px.SplineEdge)
            ts = [k / samples for k in range(samples + 1)]
            ds = [far(e, t) for t in ts]
            best = max(best, max(ds))
            # every sampled peak refined, not only the highest: a peak between samples can be higher than the sampled best
            for k in range(samples + 1):
                left, right = ds[max(k - 1, 0)], ds[min(k + 1, samples)]
                if ds[k] < left or ds[k] < right or (0 < k < samples and ds[k] == left == right):
                    continue
                lo, hi = ts[max(k - 1, 0)], ts[min(k + 1, samples)]
                for _ in range(48):
                    p, q = hi - g * (hi - lo), lo + g * (hi - lo)
                    if far(e, p) < far(e, q):
                        lo = p
                    else:
                        hi = q
                best = max(best, far(e, 0.5 * (lo + hi)))
        return best

    return max(one_way(a, b), one_way(b, a))


def Hausdorff(a: str | Callable | Any, b: str | Callable | Any, /, samples: int = 64) -> Callable:
    """A measurement: the Hausdorff distance between the elements a and b of the object.

    The Hausdorff distance is the largest distance from a point of a to b or of b to a: how far apart two curves are at
    their worst.

    Args:
        a: the first elements: an id, a name of edges or faces, a callable of the object returning them, or elements.
        b: the second elements, as a.
        samples: the samples per edge before the search for the largest distance refines.

    Returns:
        the measurement f(obj), for obj.Measure(f).

    Examples:
        shape.Measure(px.measurements.Hausdorff("Line.1", "Arc.2"))
        sk.Measure(px.measurements.Hausdorff("inner", "outer", samples=128), id="gap")
    """
    return lambda obj, **_: hausdorff(_pick(obj, a), _pick(obj, b), samples)


def Clearance(a: str | Callable | Any, b: str | Callable | Any) -> Callable:
    """A measurement: the smallest distance between the elements a and b of the object.

    Args:
        a: the first elements: an id, a name of edges or faces, a callable of the object returning them, or elements.
        b: the second elements, as a.

    Returns:
        the measurement f(obj), for obj.Measure(f).

    Examples:
        shape.Measure(px.measurements.Clearance("pad", "trace"))
        sk.Measure(px.measurements.Clearance("Circle.1", "Line.4"), id="gap")
    """
    return lambda obj, **_: as_target(_pick(obj, a)).Distance(as_target(_pick(obj, b)))


def HydraulicDiameter(*args: Any) -> Callable:
    """A measurement: the hydraulic diameter 4 A / P, four times the area over the perimeter (every edge wetted).

    Returns:
        the measurement f(obj), for obj.Measure(f).

    Raises:
        TypeError: given an object: the measurement is HydraulicDiameter(), with brackets.

    Examples:
        face.Measure(px.measurements.HydraulicDiameter())
        px.measurements.HydraulicDiameter()(face)
    """
    if args:
        raise TypeError("HydraulicDiameter() takes no arguments: the measurement is HydraulicDiameter(), "
                        "face.Measure(px.measurements.HydraulicDiameter())")

    def hydraulic_diameter(obj: Any, **_: Any) -> float:
        m = obj.Measure()
        return 4.0 * m["area"] / m["length"]

    return hydraulic_diameter


def _region(obj):
    """faces to measure: a face or shape itself, a sketch's region"""
    if isinstance(obj, (px.Face, px.Shape, px.FaceView)):
        return obj
    if not hasattr(obj, "Shape"):
        raise TypeError("a measurement field needs an area: a Face, Shape or sketch, not a %s" % type(obj).__name__)
    return obj.Shape()


def _convex_corners(region):
    """(point, interior angle in degrees) of every corner where the material is thinner than a half plane"""
    out = []
    for face in region.faces:
        for loop in [face.outer] + list(face.inner):
            edges = list(loop.edges)
            for prev, nxt in zip(edges, edges[1:] + edges[:1]):
                a, b = prev.TangentAt(1.0), nxt.TangentAt(0.0)
                turn = math.degrees(math.atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y))
                if turn > 1e-9:
                    out.append((tuple(nxt.start), 180.0 - turn))
    return out


def _warn_corner_pinch(obj, method, samples, value, at):
    """MinThickness: say when its minimum is the tip of a sharp corner, which every sharp corner is"""
    region = _region(obj)
    away = px._Thickness(region, samples, method == "ball", 179.999, -1)
    corners = _convex_corners(region)
    if not away or not corners or min(s[4] for s in away) <= 2.0 * value:
        return
    point, angle = min(corners, key=lambda c: math.dist(c[0], at))
    warn("MinThickness: the minimum %.4g is the tip of the corner at (%.6g, %.6g) (%.4g degrees); "
                  "ignore_corners=%d leaves out corners up to that angle" % (value, *point, angle, math.floor(angle) + 1),
                  px.PlanexWarning)


def _thickness(method, statistic, samples, ignore_corners, label):
    if method not in ("ball", "ray"):
        raise ValueError("method: 'ball' (the largest disc that fits) or 'ray' (along the inward normal)")

    def measure(obj, plot=False, samples=samples, **_):
        found = px._Thickness(_region(obj), samples, method == "ball", ignore_corners or 0.0, -1 if statistic is min else 1)
        if not found:
            raise ValueError("thickness: no sample found a wall (empty, or every one pinched by a corner)")
        values = [s[4] for s in found]
        # the extreme of each face's refined sample, which lies on the exact curves (the others on a spline's
        # approximation)
        at = statistic((s for s in found if s[7]), key=lambda s: s[4])
        value = at[4]
        if statistic is min and not ignore_corners:
            _warn_corner_pinch(obj, method, samples, value, (at[0], at[1]))
        if not plot:
            return value
        data = {"points": [(s[0], s[1]) for s in found], "values": values, "label": label, "value": value,
                "marks": [((at[0], at[1]), "%s %.4g" % ("min" if statistic is min else "max", value)), ((at[2], at[3]), "")]}
        if method == "ball":
            # every point inside takes the largest disc that covers it, as CAD thickness maps show it
            data["discs"] = [(s[5], s[6], 0.5 * s[4], s[4]) for s in found]
        return data

    return measure


def MinThickness(method: str = "ball", samples: int = 400, ignore_corners: float | None = None) -> Callable:
    """A measurement: the thinnest wall of the faces; px.PlotField draws it.

    A sharp corner is infinitely thin: without ignore_corners a warning says when the minimum is such a corner's tip
    and which value leaves it out.

    Args:
        method: "ball": at each sample the diameter of the largest disc inside that touches the boundary there;
            "ray": the distance along the inward normal to the other side, which never finds less than the disc and can
            miss thin spots at tapers.
        samples: the number of samples along the boundary.
        ignore_corners: leaves out where a convex corner sharper than this many degrees holds the disc (or the ray) in
            its wedge: every point where it touches the boundary lies within 2.2 r / tan(angle / 2) of the corner, r
            its radius (half the ray's length). On a tapered edge, such as an airfoil's trailing edge, that is the
            whole taper.

    Returns:
        the measurement f(obj, plot=False, samples=samples), for obj.Measure(f) and px.PlotField(f, obj); it raises
        ValueError when no sample finds a wall.

    Raises:
        ValueError: method is neither "ball" nor "ray".

    Examples:
        face.Measure(px.measurements.MinThickness())
        px.PlotField(px.measurements.MinThickness(method="ray", ignore_corners=60), shape)
    """
    return _thickness(method, min, samples, ignore_corners, "wall thickness (%s)" % method)


def MaxThickness(method: str = "ball", samples: int = 400, ignore_corners: float | None = None) -> Callable:
    """A measurement: the thickest wall of the faces, as MinThickness; px.PlotField draws it.

    Args:
        method: "ball" (the largest disc inside) or "ray" (along the inward normal), as MinThickness.
        samples: the number of samples along the boundary.
        ignore_corners: leaves out where a convex corner sharper than this many degrees holds the disc (or the ray) in
            its wedge: every point where it touches the boundary lies within 2.2 r / tan(angle / 2) of the corner, r
            its radius (half the ray's length). On a tapered edge, such as an airfoil's trailing edge, that is the
            whole taper.

    Returns:
        the measurement f(obj, plot=False, samples=samples), for obj.Measure(f) and px.PlotField(f, obj).

    Raises:
        ValueError: method is neither "ball" nor "ray".

    Examples:
        face.Measure(px.measurements.MaxThickness())
        shape.Measure(px.measurements.MaxThickness(samples=1000), id="thickest")
    """
    return _thickness(method, max, samples, ignore_corners, "wall thickness (%s)" % method)


def TorsionConstant(order: int = 3, maxh: float | Auto = Auto) -> Callable:
    """A measurement: the St. Venant torsion constant J of the faces, a cross-section in the xy plane.

    J is computed by NGSolve from the warping function w: J = integral of x^2 + y^2 + x dw/dy - y dw/dx about the
    centroid, with laplace(w) = 0 and dw/dn = y n_x - x n_y on every boundary (holes included). The measurement raises
    ImportError without NGSolve.

    Args:
        order: the polynomial order of the finite elements and of the curved mesh.
        maxh: the mesh size; px.Auto takes a twentieth of the larger side of the bounding box.

    Returns:
        the measurement f(obj), for obj.Measure(f).

    Examples:
        face.Measure(px.measurements.TorsionConstant())
        face.Measure(px.measurements.TorsionConstant(order=4, maxh=0.001))
    """

    computed = is_auto(maxh, "TorsionConstant", "maxh")

    def measure(obj, **_):
        try:
            import ngsolve as ngs
        except ImportError as e:
            raise ImportError("TorsionConstant needs NGSolve (pip install ngsolve)") from e
        region = _region(obj)
        (x0, y0), (x1, y1) = region.bounding_box
        mesh = ngs.Mesh(px.OCCGeometry(region).GenerateMesh(maxh=max(x1 - x0, y1 - y0) / 20 if computed else maxh))
        mesh.Curve(order)
        area = ngs.Integrate(1, mesh)
        X = ngs.x - ngs.Integrate(ngs.x, mesh) / area
        Y = ngs.y - ngs.Integrate(ngs.y, mesh) / area
        fes = ngs.H1(mesh, order=order)
        u, v = fes.TnT()
        n = ngs.specialcf.normal(2)
        a = ngs.BilinearForm(ngs.grad(u) * ngs.grad(v) * ngs.dx + 1e-10 * u * v * ngs.dx).Assemble()
        f = ngs.LinearForm((Y * n[0] - X * n[1]) * v * ngs.ds).Assemble()
        w = ngs.GridFunction(fes)
        w.vec.data = a.mat.Inverse(fes.FreeDofs(), inverse="sparsecholesky") * f.vec
        return ngs.Integrate(X * X + Y * Y + X * ngs.grad(w)[1] - Y * ngs.grad(w)[0], mesh)

    return measure


def _vertex_edges(vertex):
    o, p = owner(vertex), vertex.p
    close = lambda q: abs(q.x - p.x) + abs(q.y - p.y) <= 1e-12 * max(1.0, abs(p.x), abs(p.y))
    out = []
    for e in o.edges:
        if close(e.edge.start):
            out.append((e, e.TangentAt(0.0)))
        if close(e.edge.end):
            t = e.TangentAt(1.0)
            out.append((e, px.Vertex((-t.x, -t.y))))
    return o, out


def _corner_angle(vertex):
    """the angle at the vertex in degrees: inside the face where it bounds one (up to 360), else between its edges"""
    o, at = _vertex_edges(vertex)
    if len(at) != 2:
        return None
    (_, a), (_, b) = at
    angle = math.degrees(math.acos(max(-1.0, min(1.0, a.x * b.x + a.y * b.y))))
    if hasattr(o, "Contains"):
        m = (a.x + b.x, a.y + b.y)
        size = max(abs(c) for box in o.bounding_box for c in box) or 1.0
        n = math.hypot(*m)
        if n > 1e-12 and not o.Contains((vertex.p.x + m[0] / n * 1e-7 * size, vertex.p.y + m[1] / n * 1e-7 * size)):
            angle = 360.0 - angle
    return angle


def _corner_radius(vertex):
    """the smallest radius of the arcs that end at the vertex (a fillet's), None if none does"""
    _, at = _vertex_edges(vertex)
    radii = [e.edge.radius for e, _ in at if isinstance(e.edge, px.ArcEdge)]
    return min(radii) if radii else None


def _measured(obj, key, to=False):
    """what Measure's name or to names: a sketch resolves its ids and selected elements itself (as to= a selected
    element is its wire or point); in a shape, face or wire a name stays a name, an id names its face or edge (an
    unknown one raises), a selection its item"""
    if key is None:
        return key
    if isinstance(obj, sketch_types):
        return as_target(key) if to and not isinstance(key, str) else key
    if not isinstance(key, str):
        return as_target(key)
    parts = list(getattr(obj, "faces", [])) + list(getattr(obj, "edges", []))
    if not callable(obj) or any(getattr(x, "name", None) == key for x in parts):
        return key
    return as_target(obj(key))


def add_measures():
    """Measure(f), Measure(f, id=), measures, RemoveMeasure, Hausdorff; vertices' angle and radius"""
    for cls in (px.Wire, px.Face, px.Shape, px.EdgeView, px.FaceView, *sketch_types):
        if "Measure" not in cls.__dict__:
            continue
        standard = getattr(cls, "Measure")

        def Measure(self, *args, id=None, standard=standard, **kwargs):
            if args and callable(args[0]) and not isinstance(args[0], (str, Selection)):
                f, rest = args[0], args[1:]
                value = f(self, *rest, **kwargs)
                if id is not None:
                    _STORED.setdefault(self, {})[id] = (f, rest, kwargs)
                return value
            if id is not None:
                raise TypeError("Measure(f, id=...): id stores a measurement, a callable f")
            args = tuple(_measured(self, a, k == 1) if k < 2 else as_target(a) for k, a in enumerate(args))
            kwargs = {k: _measured(self, v, k == "to") if k in ("name", "to") else as_target(v) for k, v in kwargs.items()}
            return standard(self, *args, **kwargs)

        def measures(self, standard=standard):
            out = dict(standard(self))
            for k, (f, rest, kwargs) in _STORED.get(self, {}).items():
                out[k] = f(self, *rest, **kwargs)
            return out

        def RemoveMeasure(self, id: str) -> None:
            """Drops the stored measurement id.

            Args:
                id: the id the measurement was stored under (Measure(f, id=...)).

            Raises:
                ValueError: no measurement is stored under id.

            Examples:
                shape.RemoveMeasure("gap")
            """
            if id not in _STORED.get(self, {}):
                raise ValueError("RemoveMeasure(%r): no stored measurement %r" % (id, id))
            del _STORED[self][id]

        Measure.__doc__ = _measure_doc(standard.__doc__ or "")
        cls.Measure = Measure
        cls.measures = property(measures,
                                doc="measures: dict\n\nThe standard measurements and the stored ones, evaluated now, as a dict.")
        cls.RemoveMeasure = RemoveMeasure
    for cls in (px.Wire, px.Face, px.Shape, px.EdgeView, Selection):
        cls.Hausdorff = _hausdorff_of
    px.VertexView.angle = property(_corner_angle, doc="angle: float | None\n\nThe angle at the vertex in degrees: inside the face where "
                                   "it bounds one (up to 360), else between its two edges; None unless two edges meet "
                                   "there.")
    px.VertexView.radius = property(_corner_radius, doc="radius: float | None\n\nThe smallest radius of the arcs ending at the vertex (a "
                                    "fillet's), None if none does.")


def _hausdorff_of(self, other: Any, samples: int = 64) -> float:
    """The Hausdorff distance to other: the largest distance from a point of one to the other.

    Args:
        other: an object, element or selection.
        samples: the samples per edge before the search for the largest distance refines.

    Examples:
        wire.Hausdorff(other_wire)
        shape.edges("Line.1").Hausdorff(shape.edges("Line.2"), samples=128)
    """
    return hausdorff(self, other, samples)

