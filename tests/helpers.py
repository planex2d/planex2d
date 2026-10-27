# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import collections
import math

import planex as px


def point(v):
    return tuple(v)


def vertex(p):
    return px.Vertex(p)


def line(p, q):
    return px.LineEdge(vertex(p), vertex(q))


def arc(p, q, center, ccw=True):
    return px.ArcEdge(vertex(p), vertex(q), vertex(center), reverse=not ccw)


def ends(edge):
    return (point(edge.start), point(edge.end))


def seg(p, q):
    return frozenset((p, q))


def segments(polygon):
    return {seg(*ends(l)) for l in polygon.lines}


def is_empty(polygon):
    return not (polygon.lines or polygon.arcs or polygon.circles or polygon.splines)


def area(shape):
    return sum(f.mass for f in shape.faces)


def polygon_area(polygon):
    return 0.0 if is_empty(polygon) else area(polygon.ToOCCFace())


def open_vertices(polygon):
    degree = collections.Counter()
    for e in polygon.lines + polygon.arcs:
        for v in (e.start, e.end):
            degree[(round(v.x, 9), round(v.y, 9))] += 1
    return [v for v, n in degree.items() if n % 2]


def shortest_edge(polygon):
    return min(math.dist(*ends(e)) for e in polygon.lines + polygon.arcs)


def square(x, y, s=1.0):
    return px.Move((x, y)).Rectangle(s, s).Face()


def rect(x, y, w, h):
    return px.Move((x, y)).Rectangle(w, h).Face()


def circle(x, y, r):
    return px.QuickSketch().Circle((x, y), r).Face()


def stadium_area(length, d):
    return 2 * d * length + math.pi * d * d


def unit_boundary(polygon):
    cover = collections.Counter()
    for l in polygon.lines:
        (x0, y0), (x1, y1) = ends(l)
        if y0 == y1:
            for k in range(int(min(x0, x1)), int(max(x0, x1))):
                cover[("h", k, int(y0))] += 1
        else:
            for k in range(int(min(y0, y1)), int(max(y0, y1))):
                cover[("v", int(x0), k)] += 1
    return cover


def cell_boundary(cells):
    edges = collections.Counter()
    for i, j in cells:
        if (i, j - 1) not in cells:
            edges[("h", i, j)] += 1
        if (i, j + 1) not in cells:
            edges[("h", i, j + 1)] += 1
        if (i - 1, j) not in cells:
            edges[("v", i, j)] += 1
        if (i + 1, j) not in cells:
            edges[("v", i + 1, j)] += 1
    return edges
