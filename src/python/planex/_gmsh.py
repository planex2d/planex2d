# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""gmsh's .geo format: points, lines, circle arcs and B-splines, curve loops and plane surfaces, physical groups"""
from __future__ import annotations

import math
import os
from typing import TYPE_CHECKING

from . import _planex as px
from ._sketches import sketch_types
from ._geom import oriented, xy
from ._compound import Compound
from ._caller import warn

PERIODIC = 2

if TYPE_CHECKING:
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire


def _shapes(obj):
    if isinstance(obj, px.Shape):
        return [obj]
    if isinstance(obj, px.Face):
        return [px.Shape(obj)]
    if isinstance(obj, px.Wire):
        return [obj]
    if isinstance(obj, sketch_types):
        return [obj.Shape()]
    if isinstance(obj, Compound):
        return [s for p in obj.parts for s in _shapes(p)]
    raise TypeError(f"WriteGeo: {type(obj).__name__} has no edges to write")


class _Geo:
    def __init__(self):
        self.lines = []
        self.points = {}
        self.sizes = {}
        self.curves = 0
        self.loops = 0
        self.surfaces = 0

    def point(self, xyz, size=None):
        key = tuple(float(c) for c in xyz)
        if key not in self.points:
            self.points[key] = len(self.points) + 1
        pid = self.points[key]
        if size is not None:
            self.sizes[pid] = min(size, self.sizes.get(pid, size))
        return pid

    def curve(self, kind, ids):
        self.curves += 1
        self.lines.append(f"{kind}({self.curves}) = {{{', '.join(map(str, ids))}}};")
        return self.curves


def _affine(trafo, shape):
    """the plane's map trafo as gmsh's 4x4 matrix in 3d, row by row"""
    o = shape.ToXYZ((0, 0))
    ex = [a - b for a, b in zip(shape.ToXYZ((1, 0)), o)]
    ey = [a - b for a, b in zip(shape.ToXYZ((0, 1)), o)]
    n = shape.Normal()
    r = [[ex[i], ey[i], n[i]] for i in range(3)]
    (m00, m01), (m10, m11) = trafo.matrix
    local = [[m00, m01, 0.0], [m10, m11, 0.0], [0.0, 0.0, 1.0]]
    g = [[sum(r[i][k] * local[k][l] * r[j][l] for k in range(3) for l in range(3)) for j in range(3)] for i in range(3)]
    t = trafo.vector
    shift = [sum(r[i][k] * (t[0], t[1], 0.0)[k] for k in range(3)) + o[i] - sum(g[i][k] * o[k] for k in range(3)) for i in range(3)]
    return [g[0][0], g[0][1], g[0][2], shift[0], g[1][0], g[1][1], g[1][2], shift[1], g[2][0], g[2][1], g[2][2], shift[2], 0, 0, 0, 1]


def _write_shape(geo, shape, faces, names, unnamed):
    """the edges of shape once, its faces as plane surfaces; names: physical curve and surface ids by name, unnamed: the
    unnamed ones (1: curves, 2: surfaces)"""
    size_at = {}
    for f in faces:
        if f.maxh is not None:
            for loop in [f.outer] + list(f.inner):
                for e in loop.edges:
                    for p in (e.start, e.end):
                        size_at[xy(p)] = min(f.maxh, size_at.get(xy(p), f.maxh))
    edges = list(shape.edges)
    pieces = {}
    for i, view in enumerate(edges):
        e = view.edge
        size = view.maxh

        def at(p):
            s = [v for v in (size, size_at.get(xy(p))) if v is not None]
            return geo.point(shape.ToXYZ(xy(p)), min(s) if s else None)

        if isinstance(e, px.LineEdge):
            ids = [geo.curve("Line", [at(e.start), at(e.end)])]
        elif isinstance(e, (px.ArcEdge, px.CircleEdge)):
            c = geo.point(shape.ToXYZ(xy(e.center)))
            ids = []
            for arc in e.approximation if isinstance(e, px.CircleEdge) else [e]:
                # gmsh's circle arcs are shorter than half a circle
                if abs(arc.sweep) < 180 * (1 - 1e-9):
                    ids.append(geo.curve("Circle", [at(arc.start), c, at(arc.end)]))
                else:
                    m = at(arc.MidPoint())
                    ids += [geo.curve("Circle", [at(arc.start), c, m]), geo.curve("Circle", [m, c, at(arc.end)])]
        else:
            # gmsh's .geo has no exact NURBS: a B-spline through planex's approximation
            parts = e.approximation
            pts = [at(e.start)]
            for k, part in enumerate(parts):
                pts.append(geo.point(shape.ToXYZ(xy(part.MidPoint()))))
                pts.append(at(part.end) if k + 1 < len(parts) else at(e.end))
            ids = [geo.curve("BSpline", pts)]
        pieces[i] = ids
        (names[1].setdefault(view.name, []) if view.name else unnamed[1]).extend(ids)

    def key(e):
        return (xy(e.start), xy(e.end), xy(e.MidPoint()))

    index = {}
    for i, view in enumerate(edges):
        k = key(view.edge)
        index[k] = (i, 1)
        if k[0] != k[1]:
            index[(k[1], k[0], k[2])] = (i, -1)
    eps = 1e-12 * max([1.0] + [abs(c) for view in edges for p in (view.edge.start, view.edge.end) for c in xy(p)])
    for f in faces:
        loops = []
        for loop in [f.outer] + list(f.inner):
            ids = []
            for e, rev in oriented([v.edge for v in loop.edges], eps):
                i, sign = index[key(e)]
                sign = -sign if rev else sign
                ids.extend(pieces[i] if sign > 0 else [-c for c in reversed(pieces[i])])
            geo.loops += 1
            geo.lines.append(f"Curve Loop({geo.loops}) = {{{', '.join(map(str, ids))}}};")
            loops.append(geo.loops)
        geo.surfaces += 1
        geo.lines.append(f"Plane Surface({geo.surfaces}) = {{{', '.join(map(str, loops))}}};")
        (names[2].setdefault(f.name, []) if f.name else unnamed[2]).append(geo.surfaces)
    return edges, pieces


def _periodic(geo, shape, edges, pieces):
    for name, (a, b, trafo, kind) in getattr(shape, "identifications", {}).items():
        if int(kind) != PERIODIC:
            warn(f"WriteGeo: identification '{name}' is not periodic; gmsh's .geo writes periodic curves only",
                          px.PlanexWarning)
            continue
        side = {s: [i for i, v in enumerate(edges) if v.name == s] for s in (a, b)}
        if trafo == px.Auto:
            trafo = px.GetTransformation([edges[i] for i in side[a]], [edges[i] for i in side[b]])
        slaves, masters = [], []
        size = max([1.0] + [abs(c) for v in edges for c in xy(v.midpoint)])
        for i in side[a]:
            m = trafo(edges[i].midpoint)
            j = min(side[b], key=lambda k: math.dist(xy(edges[k].midpoint), xy(m)))
            if math.dist(xy(edges[j].midpoint), xy(m)) > 1e-9 * size or len(pieces[i]) != len(pieces[j]):
                raise ValueError(f"WriteGeo: identification '{name}': no edge named '{b}' is the image of one named '{a}'")
            same = math.dist(xy(trafo(edges[i].edge.start)), xy(edges[j].edge.start)) <= 1e-9 * size
            masters.extend(pieces[i] if same else [-c for c in reversed(pieces[i])])
            slaves.extend(pieces[j])
        numbers = ", ".join(repr(float(x)) for x in _affine(trafo, shape))
        geo.lines.append(f"Periodic Curve {{{', '.join(map(str, slaves))}}} = {{{', '.join(map(str, masters))}}} Affine {{{numbers}}};")


def WriteGeo(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, path: str | os.PathLike, /) -> None:
    """Writes the edges and faces as a gmsh .geo file.

    Points carry the maxh of their edges and faces as the characteristic length; lines, circle arcs (split at half a
    circle) and splines as B-splines through planex's approximation (not exact: .geo has no NURBS), every curve once;
    plane surfaces with their holes. Physical groups: a curve group per edge name, a surface group per face name, the
    unnamed edges and faces in a curve and a surface group named "unnamed" (gmsh writes only elements in a group once there
    is one, and netgen's ReadGmsh needs every group named and the boundary segments). Periodic
    identifications become Periodic Curve.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write.
        path: the file to write.

    Examples:
        face.WriteGeo("part.geo")
        px.WriteGeo(shape, "part.geo")
    """
    geo = _Geo()
    names = {1: {}, 2: {}}
    unnamed = {1: [], 2: []}
    for part in _shapes(obj):
        if isinstance(part, px.Wire):
            _write_shape(geo, part, [], names, unnamed)
            continue
        edges, pieces = _write_shape(geo, part, list(part.faces), names, unnamed)
        _periodic(geo, part, edges, pieces)
    out = ["// written by planex WriteGeo; splines are B-splines through planex's approximation"]
    for xyz, pid in geo.points.items():
        size = f", {geo.sizes[pid]!r}" if pid in geo.sizes else ""
        out.append(f"Point({pid}) = {{{xyz[0]!r}, {xyz[1]!r}, {xyz[2]!r}{size}}};")
    out += geo.lines
    for dim, ids in unnamed.items():
        if ids:
            names[dim].setdefault("unnamed", []).extend(ids)
    tag = 0
    for dim, kind in ((1, "Curve"), (2, "Surface")):
        for name, ids in names[dim].items():
            tag += 1
            out.append(f'Physical {kind}("{name}", {tag}) = {{{", ".join(map(str, ids))}}};')
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
