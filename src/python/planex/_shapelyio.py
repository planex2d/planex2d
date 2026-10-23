# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import warnings
from typing import TYPE_CHECKING, Any

from . import _planex as px
from ._sketches import sketch_types
from ._compound import Compound

if TYPE_CHECKING:
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire
    from .types import Plane


def _wires(coords, plane, tol):
    """the clean wires of the polyline through coords (px.Wires: ends within tol joined, crossings cut)"""
    pts = [tuple(c[:2]) for c in coords]
    return px.Wires([px.Line(p, q, plane=plane) for p, q in zip(pts, pts[1:]) if p != q], tol)


def _parts(obj):
    if isinstance(obj, list):
        return obj
    return list(obj.parts) if isinstance(obj, Compound) else [obj]


def FromShapely(geom: Any, /, plane: Plane | None = None, tol: float = 1e-7) -> Shape | list[Wire] | Compound:
    """Converts a shapely geometry: polygons become faces with their holes subtracted, line strings clean wires.

    The wires are built as by px.Wires: ends within tol joined, crossings cut. A GeometryCollection becomes a Compound
    of its parts as shapely stores them: parts sharing a side or overlapping stay so (px.Glue them for a conforming
    mesh).

    Args:
        geom: the shapely geometry.
        plane: the plane of the result; None is the xy plane.
        tol: ends closer than this are joined, a length.

    Returns:
        polygons alone a Shape of the united faces, lines alone a list of wires, both a Compound of the Shape and the
        wires; a GeometryCollection a Compound.

    Examples:
        px.FromShapely(polygon)
        px.FromShapely(line_string, tol=1e-9, plane=px.YZ)
    """
    import shapely
    plane = px.AxesOf(plane, "FromShapely")
    if geom.geom_type == "GeometryCollection":
        return Compound([p for part in shapely.get_parts(geom) for p in _parts(FromShapely(part, plane, tol))])

    faces, wires = [], []
    for part in shapely.get_parts(geom) if hasattr(geom, "geoms") else [geom]:
        kind = part.geom_type
        if kind == "Polygon":
            face = px.Unify([w.Face() for w in _wires(part.exterior.coords, plane, tol)])
            for hole in part.interiors:
                for w in _wires(hole.coords, plane, tol):
                    face = face - w.Face()
            faces.append(face)
        elif kind in ("LineString", "LinearRing"):
            wires.extend(_wires(part.coords, plane, tol))
        elif hasattr(part, "geoms"):
            for x in _parts(FromShapely(part, plane, tol)):
                if isinstance(x, px.Wire):
                    wires.append(x)
                else:
                    faces.extend(x.faces)
    if not faces:
        return px.Wires(wires) if len(wires) > 1 else wires
    if wires:
        return Compound([px.Unify(faces)] + (px.Wires(wires) if len(wires) > 1 else wires))
    return px.Unify(faces)


def _polygon(face, tol, which="the face"):
    """the polygon of a face from its own loops: outer loop first, then the holes. Where the face is thinner than tol
    the chords of two sides can cross: it is sampled again at tol/10, up to tol/1000, and else made valid"""
    import shapely

    first = None
    for step in range(4):
        t = tol / 10**step
        outer, *holes = face.Polylines(t)
        g = shapely.Polygon(outer, holes)
        if g.is_valid:
            if first:
                warnings.warn("ToShapely: %s is thinner than tol=%g, its chords cross (%s); sampled at tol=%g" % (which, tol, first, t),
                              px.PlanexWarning, stacklevel=3)
            return g
        first = first or shapely.is_valid_reason(g)
    warnings.warn("ToShapely: %s is thinner than tol=%g, its chords cross (%s) also at tol=%g; made valid with shapely's make_valid"
                  % (which, tol, first, t), px.PlanexWarning, stacklevel=3)
    return shapely.make_valid(g)


def _share_edges(faces):
    """whether two faces have an edge in common, as in a glued Shape: their copies of it are identical"""
    seen = {}
    for k, f in enumerate(faces):
        for e in f.edges:
            m = e.midpoint
            key = tuple(sorted((tuple(e.start), tuple(e.end)))) + ((m.x, m.y),)
            if seen.setdefault(key, k) != k:
                return True
    return False


def ToShapely(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, /, tol: float = 1e-3) -> Any:
    """Converts to a shapely geometry in plane coordinates, arcs and splines sampled within tol.

    Faces that share an edge (a glued Shape) give a GeometryCollection of their polygons, since a MultiPolygon must not
    have parts sharing an edge.

    Args:
        obj: the Face, Shape, Wire, sketch (its Shape()) or Compound.
        tol: the largest distance of a chord from its arc or spline, a length. Where a face is thinner than tol its
            polygon is sampled again finer (to tol/1000), with a warning; still invalid, it is made valid (make_valid).

    Returns:
        a Polygon or MultiPolygon for a Face, a Shape of faces or a sketch; a LineString or MultiLineString for a Wire
        or a Shape of wires; a GeometryCollection for a Compound or a Shape of both.

    Examples:
        face.ToShapely()
        px.ToShapely(shape, tol=1e-4)
    """
    import shapely

    if isinstance(obj, sketch_types):
        return ToShapely(obj.Shape(), tol)
    if isinstance(obj, Compound):
        return shapely.GeometryCollection([ToShapely(part, tol) for part in obj.parts])
    if isinstance(obj, px.Shape):
        faces = obj.faces
        if not faces:
            lines = [ToShapely(w, tol) for w in obj.wires]
            return lines[0] if len(lines) == 1 else shapely.MultiLineString([g for line in lines for g in shapely.get_parts(line)])
        polys = [_polygon(f, tol, "face %d%s" % (k, " ('%s')" % f.name if f.name else "")) for k, f in enumerate(faces)]
        free = obj._FreeWires()
        if free:
            return shapely.GeometryCollection(polys + [g for w in free for g in shapely.get_parts(ToShapely(w, tol))])
        if len(polys) == 1:
            return polys[0]
        return shapely.GeometryCollection(polys) if _share_edges(faces) else shapely.MultiPolygon(polys)
    if isinstance(obj, px.Wire):
        lines = [shapely.LineString(chain) for chain in obj.Polylines(tol)]
        return lines[0] if len(lines) == 1 else shapely.MultiLineString(lines)
    return _polygon(obj, tol)
