# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""sampling and loop helpers shared by the plot and the file formats"""
from __future__ import annotations

import math
from typing import TYPE_CHECKING

from . import _planex as px
from ._sketches import sketch_types
from ._caller import warn

ARC_POINTS_PER_CIRCLE = 64


def read_name(name):
    """a name read from a file or a mesh; "|" joins names in a selection (name="a|b"), so it becomes "_" """
    if name is None or "|" not in name:
        return name
    renamed = name.replace("|", "_")
    warn(f"name {name!r} read as {renamed!r}: '|' joins names in a selection", px.PlanexWarning)
    return renamed

if TYPE_CHECKING:
    import numpy as np

    from ._planex import QuickSketch, Sketch


def xy(v):
    return (v[0], v[1])


def arc_points(arc, tol=None):
    """points along the arc, every tol of sagitta if given, else by a fixed count per circle"""
    c = arc.center
    sx, sy = arc.start.x - c.x, arc.start.y - c.y
    sweep = math.radians(arc.sweep)
    if tol is not None and arc.radius > tol:
        step = 2.0 * math.acos(1.0 - tol / arc.radius)
        n = max(1, math.ceil(abs(sweep) / step))
    else:
        n = max(2, math.ceil(abs(sweep) / (2 * math.pi) * ARC_POINTS_PER_CIRCLE))
    pts = []
    for k in range(n + 1):
        t = sweep * k / n
        cs, sn = math.cos(t), math.sin(t)
        pts.append((c.x + sx * cs - sy * sn, c.y + sx * sn + sy * cs))
    pts[0] = xy(arc.start)
    pts[-1] = xy(arc.end)
    return pts


def points(edge, tol=None):
    if isinstance(edge, px.LineEdge):
        return [xy(edge.start), xy(edge.end)]
    if isinstance(edge, px.ArcEdge):
        return arc_points(edge, tol)
    if tol is not None and not isinstance(edge, px.CircleEdge):
        return [(x, y) for x, y in edge.Sample(tol).tolist()]
    pts = []
    for part in edge.approximation:
        part_pts = points(part, tol)
        pts.extend(part_pts if not pts else part_pts[1:])
    return pts


def shape_polylines(sketch: Sketch | QuickSketch, tol: float) -> list[np.ndarray]:
    """The loops and open chains of the sketch's Shape() as (n, 2) arrays of plane coordinates, as Shape.Polylines.

    Args:
        tol: the largest distance of a chord from its edge, a length > 0.

    Raises:
        ValueError: tol is not positive and finite, or below the rounding of an arc's or a spline's coordinates.

    Examples:
        sk.Polylines(0.01)
    """
    return sketch.Shape().Polylines(tol)


def close(a, b, eps):
    return abs(a.x - b.x) <= eps and abs(a.y - b.y) <= eps


def oriented(edges, eps):
    """(edge, runs backwards) along a connected chain, starting in the direction of its first edge"""
    if len(edges) == 1:
        return [(edges[0], False)]
    first, second = edges[0], edges[1]
    rev = not (close(first.end, second.start, eps) or close(first.end, second.end, eps))
    out = [(first, rev)]
    current = first.start if rev else first.end
    for e in edges[1:]:
        rev = not close(e.start, current, eps)
        out.append((e, rev))
        current = e.start if rev else e.end
    return out


def chain_points(oriented_edges, tol=None):
    pts = []
    for e, rev in oriented_edges:
        p = points(e, tol)
        if rev:
            p = p[::-1]
        pts.extend(p if not pts else p[1:])
    return pts


def signed_area(pts):
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))


def inside(pts, p):
    """ray parity of p against the closed polyline pts"""
    odd = False
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
        if (y0 > p[1]) != (y1 > p[1]) and x0 + (p[1] - y0) * (x1 - x0) / (y1 - y0) > p[0]:
            odd = not odd
    return odd


def scale(edges):
    return max((max(abs(v.x), abs(v.y)) for e in edges for v in (e.start, e.end)), default=1.0)


def parts(obj):
    """the faces of a Shape (its wires if it has none), a Compound's parts recursively, else the object"""
    if isinstance(obj, px.Shape):
        return list(obj.faces) or list(obj.wires)
    if hasattr(obj, "parts"):
        return [x for p in obj.parts for x in parts(p)]
    return [obj]


def edge_views(obj):
    """every edge of the object once, with its name and col, in the order of obj.edges: a Shape's shared edges once
    and its free wires too, a Sketch's the edges of its Wires(), a Compound's those of its parts in turn"""
    if isinstance(obj, sketch_types):
        return [e for w in obj.Wires() for e in w.edges]
    if hasattr(obj, "parts"):
        return [e for p in obj.parts for e in edge_views(p)]
    return list(obj.edges)


def views(part):
    if isinstance(part, sketch_types):
        return [e for w in part.Wires() for e in w.edges]
    return list(part.edges)


def loops(obj, face):
    if isinstance(obj, sketch_types):
        return obj.Shape().wires if face else obj.Wires()
    return obj.wires


def closed_loops(obj, eps, tol=None):
    """(points, depth) of every closed loop, outer loops counterclockwise and holes clockwise"""
    info = []
    for w in loops(obj, True):
        if not w.IsClosed() or len(w.edges) == 0:
            continue
        chain = oriented([e.edge for e in w.edges], eps)
        pts = chain_points(chain, tol)[:-1]
        if len(pts) >= 3:
            info.append((pts, xy(chain[0][0].MidPoint())))
    out = []
    for i, (pts, probe) in enumerate(info):
        depth = sum(1 for j, (other, _) in enumerate(info) if j != i and inside(other, probe))
        if (signed_area(pts) > 0) != (depth % 2 == 0):
            pts = pts[::-1]
        out.append((pts, depth))
    return out
