# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The edges of a view on one carrier: lines on one straight line and arcs on one circle grouped, their spans joined
and taken away from each other."""
from __future__ import annotations

import math

import numpy as np

TURN = 2 * math.pi


def _pt(v):
    return np.array([v[0], v[1]], float)


def _angle(p, c):
    return math.atan2(p[1] - c[1], p[0] - c[0]) % TURN


def Lines(edges, tol):
    """the line edges by their straight line: {key: (d, foot, spans)}, d the line's direction (to the right, or up),
    foot its point nearest the origin, spans (s, f) along d from foot"""
    groups = {}
    for e in edges:
        a, b = _pt(e.start), _pt(e.end)
        d = (b - a) / np.linalg.norm(b - a)
        if d[0] < -1e-9 or (abs(d[0]) <= 1e-9 and d[1] < 0):
            d = -d
        offset = float(d[0] * a[1] - d[1] * a[0])
        key = (round(d[0], 6), round(d[1], 6), round(offset / tol))
        groups.setdefault(key, (d, np.array([-d[1], d[0]]) * offset, []))[2].append(tuple(sorted((float(a @ d), float(b @ d)))))
    return groups


def Rounds(edges, tol):
    """the arc and circle edges by their circle: {key: (c, r, spans)}, spans counterclockwise angles (a, b) in radians,
    a in [0, 2 pi), b - a up to a full turn"""
    groups = {}
    for e in edges:
        g = e.edge
        c, r = _pt(g.center), float(g.radius)
        if type(g).__name__ == "CircleEdge":
            span = (0.0, TURN)
        else:
            a, b = _angle(_pt(g.start), c), _angle(_pt(g.end), c)
            if not g.ccw:
                a, b = b, a
            span = (a, a + ((b - a) % TURN or TURN))
        groups.setdefault((round(c[0] / tol), round(c[1] / tol), round(r / tol)), (c, r, []))[2].append(span)
    return groups


def Joined(spans, gap, turn=False):
    """the spans, sorted, those that overlap or come within gap of each other joined; turn: angles on a circle, the
    last joined across the full turn with the first"""
    spans = sorted(spans)
    out = [list(spans[0])]
    for s, f in spans[1:]:
        if s <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], f)
        else:
            out.append([s, f])
    if turn and len(out) > 1 and out[-1][1] >= out[0][0] + TURN - gap:
        s, f = out.pop()
        out[0] = [s, max(f, out[0][1] + TURN)]
    return out


def Without(spans, cut, gap, turn=False):
    """the spans joined (Joined) less the spans cut, pieces under gap left out; turn: angles on a circle"""
    def flat(spans):
        out = []
        for s, f in spans:
            out += [(s, TURN), (0.0, f - TURN)] if turn and f > TURN else [(s, f)]
        return out

    left = [tuple(s) for s in Joined(flat(spans), gap)]
    for cs, cf in (Joined(flat(cut), gap) if cut else []):
        left = [piece for s, f in left for piece in ((s, min(f, cs)), (max(s, cf), f)) if piece[1] - piece[0] > gap]
    return Joined(left, gap, turn) if left else []
