# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Views of solids dimensioned automatically (px.Drawing.AutoDimension): the view's visible edges as a sketch of its
own, a pattern of holes or of groups of edges as one instance and a pattern of it, the dimensions of the view's own
sketches taken as given, auto constrained as a drafter would; and the sizes several views on a sheet show, drawn once."""
from __future__ import annotations

import math
import warnings

import numpy as np

ID = "Auto"


def _pt(v):
    return np.array([v[0], v[1]], float)


def _data(e):
    """an edge as a plain record: its kind, its points (a line's ends, an arc's ends and centre, a circle's centre) and
    its size (a line's length, a radius)"""
    kind = type(e).__name__
    if kind == "LineEdge":
        a, b = _pt(e.start), _pt(e.end)
        return {"kind": "line", "pts": [a, b], "size": float(np.linalg.norm(b - a))}
    if kind == "ArcEdge":
        return {"kind": "arc", "pts": [_pt(e.start), _pt(e.end), _pt(e.center)], "size": float(e.radius), "ccw": bool(e.ccw)}
    if kind == "CircleEdge":
        return {"kind": "circle", "pts": [_pt(e.center)], "size": float(e.radius)}
    return None


def _merged(edges, plane, tol):
    """the lines, arcs and circles of edges longer than tol (a view leaves its other curves, an ellipse or a helix,
    undimensioned), the lines on one straight line that overlap or touch joined into one (a view's outline comes split
    where a hidden hole meets it, and a circle seen edge-on lies on it), the arcs on one circle likewise (a boss's top
    and bottom fall together in a view along its axis)"""
    import planex as px

    from ._carriers import TURN, Joined, Lines, Rounds

    kept = [e for e in edges if type(e.edge).__name__ in ("LineEdge", "ArcEdge", "CircleEdge") and e.mass > tol]
    out = []
    for d, foot, spans in Lines([e for e in kept if type(e.edge).__name__ == "LineEdge"], tol).values():
        out += [px.Line(tuple(foot + s * d), tuple(foot + f * d), plane=plane) for s, f in Joined(spans, tol)]
    for c, r, spans in Rounds([e for e in kept if type(e.edge).__name__ != "LineEdge"], tol).values():
        for s, f in Joined(spans, tol / r, True):
            out.append(px.Circle(tuple(c), r, plane=plane) if f - s >= TURN - tol / r
                       else px.Arc(_on(c, r, s), _on(c, r, f), tuple(c), plane=plane))
    return out


def _on(c, r, t):
    return tuple(c + r * np.array([math.cos(t), math.sin(t)]))


def _wires(edges, plane, tol):
    """the edges (_merged) as wires, without the stubs under tol px.Wires leaves where a line ends a hair inside a
    circle it touches (cut at their near crossing)"""
    import planex as px

    return px.Wires([e for w in px.Wires(_merged(edges, plane, tol), tol) for e in w.edges if e.mass > tol], tol)


def _turned(p, c, t):
    d = p - c
    return c + np.array([d[0] * math.cos(t) - d[1] * math.sin(t), d[0] * math.sin(t) + d[1] * math.cos(t)])


def _angle(p, c):
    return math.atan2(p[1] - c[1], p[0] - c[0]) % (2 * math.pi)


def _carries(a, b, c, t, tol):
    """whether turning a by t about c gives b"""
    return all(np.linalg.norm(_turned(p, c, t) - q) <= tol for p, q in zip(a["pts"], b["pts"]))


def _run(members, data, c, tol):
    """members in angle order from the gap, the step, whether the run closes the circle; None if they are no pattern"""
    members = sorted(members, key=lambda i: _angle(data[i]["pts"][0], c))
    n = len(members)
    angles = [_angle(data[i]["pts"][0], c) for i in members]
    gaps = [(angles[(k + 1) % n] - angles[k]) % (2 * math.pi) or 2 * math.pi for k in range(n)]
    for gap in range(n):
        order = members[gap + 1:] + members[:gap + 1]
        step = gaps[(gap + 1) % n]
        if all(_carries(data[order[k]], data[order[k + 1]], c, step, tol) for k in range(n - 1)):
            return order, step, _carries(data[order[-1]], data[order[0]], c, step, tol)
    return None


def _touching(data, tol):
    """for each edge, the edges that share one of its ends"""
    ends, out = {}, {i: set() for i in range(len(data))}
    for i, d in enumerate(data):
        if d is None or d["kind"] == "circle":
            continue
        for p in d["pts"][:2]:
            key = tuple(np.round(p / tol).astype(int))
            for j in ends.get(key, ()):
                if j != i:
                    out[i].add(j)
                    out[j].add(i)
            ends.setdefault(key, []).append(i)
    return out


def _patterns(edges, tol):
    """the patterns among the edges: circular ones (three or more equal edges or groups of edges about a circle's or an
    arc's centre, each carried onto the next by one step, one gap allowed), then linear ones (rows and grids); dicts of
    the kind, the motif (the edges of the first instance), the members (every instance's edges) and the pattern's
    centre, step, count and full, or u, count, v and count2"""
    data = [_data(e) for e in edges]
    centres, held = [], []
    for d in data:
        if d is not None and d["kind"] in ("arc", "circle"):
            c = d["pts"][-1]
            at = next((k for k, q in enumerate(centres) if np.linalg.norm(c - q) <= tol), None)
            if at is None:
                centres.append(c)
                held.append(set())
                at = len(centres) - 1
            held[at].add((d["kind"], round(d["size"] / tol)))
    touching = _touching(data, tol)
    used, out = set(), []
    for c, own in zip(centres, held):
        groups = {}
        for i, d in enumerate(data):
            if d is None or i in used or (d["kind"] == "circle" and np.linalg.norm(d["pts"][0] - c) <= tol):
                continue
            # a pattern's middle is no circle of the members' own size (a row of holes is no bolt circle about one)
            if d["kind"] == "circle" and ("circle", round(d["size"] / tol)) in own:
                continue
            key = (d["kind"], d.get("ccw"), round(d["size"] / tol), tuple(round(float(np.linalg.norm(p - c)) / tol) for p in d["pts"]))
            groups.setdefault(key, []).append(i)
        runs = [found for members in groups.values() if len(members) >= 3 and (found := _run(members, data, c, tol))]
        member_run = {i: r for r in runs for i in r[0]}
        for order, step, full in runs:
            if order[0] in used:
                continue
            motif = _motif(order, step, data, touching, member_run)
            if motif is None:
                continue
            members = set()
            for i in motif:
                members.update(member_run[i][0])
            if members & used:
                continue
            used |= members
            out.append({"kind": "circular", "centre": c, "step": step, "count": len(order), "full": full, "motif": motif,
                        "members": sorted(members)})
    return out + _linear(data, touching, used, tol)


def _motif(order, step, data, touching, member_run):
    """a circular pattern's first instance: the edge alone for a circle, else the edges walked from it along the outline
    to its next copy"""
    first, nxt, copies = order[0], order[1], set(order)
    if data[first]["kind"] == "circle":
        return [first]
    for start in touching[first]:
        walk, prev, cur = [first], first, start
        while True:
            if cur == nxt:
                if all(len(member_run[i][0]) == len(order) for i in walk):
                    return walk
                break
            if cur in copies or cur in walk or cur not in member_run or abs(member_run[cur][1] - step) > 1e-6:
                break
            walk.append(cur)
            options = [j for j in touching[cur] if j != prev and j not in walk]
            if not options:
                break
            prev, cur = cur, options[0]
    return None


def _move_key(d, tol):
    """what an edge is up to a translation"""
    q = lambda v: tuple(int(round(x / tol)) for x in v)
    if d["kind"] == "line":
        return ("line", q(d["pts"][1] - d["pts"][0]))
    if d["kind"] == "arc":
        return ("arc", d["ccw"], q(d["pts"][0] - d["pts"][2]), q(d["pts"][1] - d["pts"][2]))
    return ("circle", int(round(d["size"] / tol)))


def _canonical(u, tol):
    return -u if u[0] < -tol or (abs(u[0]) <= tol and u[1] < 0) else u


def _steps(group, anchors, tol):
    """the two most common steps from a member to its nearest neighbour"""
    counts = {}
    for i in group:
        j = min((j for j in group if j != i), key=lambda j: float(np.linalg.norm(anchors[j] - anchors[i])))
        u = _canonical(anchors[j] - anchors[i], tol)
        counts.setdefault(tuple(int(round(x / tol)) for x in u), [0, u])[0] += 1
    return [u for _, u in sorted(counts.values(), key=lambda t: -t[0])[:2]]


def _grid(group, anchors, u, v, tol):
    """the members as rows along u repeated along v: [({member: (k, j)}, n, m)]; rows of one run when v is None"""
    at = {tuple(int(round(x / tol)) for x in anchors[i]): i for i in group}
    find = lambda p: at.get(tuple(int(round(x / tol)) for x in p))
    rows = []
    for i in group:
        if find(anchors[i] - u) is None:
            run, p = [i], anchors[i] + u
            while (j := find(p)) is not None:
                run.append(j)
                p = p + u
            rows.append(run)
    if v is None:
        return [({i: (k, 0) for k, i in enumerate(run)}, len(run), 1) for run in rows if len(run) >= 3]
    out, starts = [], {r[0]: r for r in rows if len(r) >= 2}
    for s0, run in starts.items():
        if find(anchors[s0] - v) in starts:
            continue
        grid, j, cur = {}, 0, s0
        while cur in starts and len(starts[cur]) == len(run):
            for k, i in enumerate(starts[cur]):
                grid[i] = (k, j)
            j += 1
            cur = find(anchors[s0] + v * j)
        # a 2 x 2 is no grid: four corner holes are dimensioned as such
        if j >= 2 and (len(run) >= 3 or j >= 3):
            out.append((grid, len(run), j))
    return out


def _linear(data, touching, used, tol):
    """the linear patterns among the edges not in a circular one: rows of three or more and grids of equal edges, a
    closed group of edges moving as one instance"""
    groups = {}
    for i, d in enumerate(data):
        if d is not None and i not in used:
            groups.setdefault(_move_key(d, tol), []).append(i)
    place = {}
    for members in groups.values():
        if len(members) < 3:
            continue
        anchors = {i: data[i]["pts"][0] for i in members}
        best, most = None, 0
        steps = _steps(members, anchors, tol)
        for u in steps:
            for v in [w for w in steps if abs(u[0] * w[1] - u[1] * w[0]) > tol * np.linalg.norm(u)] + [None]:
                found = _grid(members, anchors, u, v, tol)
                covered = sum(len(g) for g, _, _ in found)
                if covered > most:
                    best, most = (found, u, v), covered
        if best:
            for g, n, m in best[0]:
                for i, (k, j) in g.items():
                    place[i] = (k, j, n, m, best[1], best[2], g)
    out = []
    for i in sorted(place):
        k, j, n, m, u, v, g = place[i]
        if (k, j) != (0, 0) or i in used:
            continue
        motif = _component(i, data, touching) or [i]
        if not all(q in place and place[q][:4] == (0, 0, n, m) and np.allclose(place[q][4], u) for q in motif):
            continue
        members = set()
        for q in motif:
            members.update(place[q][6])
        if members & used:
            continue
        used |= members
        out.append({"kind": "linear", "u": u, "count": n, "v": v, "count2": m, "motif": motif, "members": sorted(members)})
    return out


def _component(i, data, touching):
    """the closed group of edges i belongs to, or None for a circle or an open chain"""
    if data[i]["kind"] == "circle":
        return None
    seen, todo = {i}, [i]
    while todo:
        for j in touching[todo.pop()]:
            if j not in seen:
                seen.add(j)
                todo.append(j)
    return sorted(seen) if all(len(touching[j]) == 2 for j in seen) else None


def _element_at(drawn, d, tol):
    """the sketch's element that is the edge d"""
    for g in drawn:
        if d["kind"] == "circle" and g["kind"] == "Circle" and np.linalg.norm(_pt(g["center"]) - d["pts"][0]) <= tol:
            return g["id"]
        if d["kind"] in ("line", "arc") and g["kind"] == d["kind"].title():
            ends, a, b = [_pt(g["start"]), _pt(g["end"])], d["pts"][0], d["pts"][1]
            if (np.linalg.norm(ends[0] - a) <= tol and np.linalg.norm(ends[1] - b) <= tol) or \
               (np.linalg.norm(ends[0] - b) <= tol and np.linalg.norm(ends[1] - a) <= tol):
                return g["id"]
    return None


def _centre_at(drawn, c, tol):
    """the centre point of a drawn circle or arc at c"""
    for g in drawn:
        if g["kind"] in ("Circle", "Arc") and not g["construction"] and np.linalg.norm(_pt(g["center"]) - c) <= tol:
            return g["id"] + ".center"
    return None


def _sweep(d):
    u, v = d["pts"][0] - d["pts"][2], d["pts"][1] - d["pts"][2]
    t = math.atan2(u[0] * v[1] - u[1] * v[0], u @ v)
    return (t if t > 0 else t + 2 * math.pi) if d["ccw"] else (-t if t < 0 else 2 * math.pi - t)


def _place(p, data, ids, tol):
    """the point of the first instance a circular pattern's pitch circle goes through, with its reference: a hole's
    centre; else the centre of its arc of the widest sweep off the pattern's centre (a lobe, a roller seat); else its
    point farthest out"""
    c = p["centre"]
    arcs = [(data[i], ref) for i, ref in zip(p["motif"], ids)
            if data[i]["kind"] in ("circle", "arc") and np.linalg.norm(data[i]["pts"][-1] - c) > tol]
    if arcs:
        d, ref = max(arcs, key=lambda t: 2 * math.pi if t[0]["kind"] == "circle" else _sweep(t[0]))
        return d["pts"][-1], ref + ".center"
    d, ref, end = max(((data[i], ref, end) for i, ref in zip(p["motif"], ids) for end in (0, 1)),
                      key=lambda t: float(np.linalg.norm(t[0]["pts"][t[2]] - c)))
    return d["pts"][end], ref + (".start" if end == 0 else ".end")


def _axis(u, tol):
    """px.H or px.V for a step along h or v, else None"""
    import planex as px

    if abs(u[1]) <= tol and u[0] > 0:
        return px.H
    if abs(u[0]) <= tol and u[1] > 0:
        return px.V
    return None


def _datum(data):
    """the datum of a view's edges (records of _data), as auto constraints take it: the corner nearest the lowest corner
    and centre coordinates, else the largest circle's centre"""
    corners = [p for d in data if d["kind"] in ("line", "arc") for p in d["pts"][:2]]
    if corners:
        low = np.min(corners + [d["pts"][-1] for d in data if d["kind"] in ("arc", "circle")], axis=0)
        return min(corners, key=lambda p: float(np.linalg.norm(p - low)))
    return max((d for d in data if d["kind"] == "circle"), key=lambda d: d["size"])["pts"][0]


def _sketch(wires, plane, shift, tol):
    """a sketch on plane of the wires' edges (on the view's plane, its origin at shift in the view's coordinates; the
    sketch takes them through 3D), each pattern as its first instance and a pattern of it; a circular one also gets its
    pitch circle (one per centre and radius) and a construction line from its centre to the instance"""
    import planex as px

    views = [e for w in wires for e in w.edges]
    edges = [v.edge for v in views]
    found = [p for p in _patterns(edges, tol) if p["kind"] == "circular"
             or (_axis(p["u"], tol) is not None and (p["v"] is None or _axis(p["v"], tol) is not None))]
    copies = {i for p in found for i in p["members"] if i not in p["motif"]}
    sk = px.Sketch(plane, id=ID, auto_constraint=False)
    for w in px.Wires([v for i, v in enumerate(views) if i not in copies], tol):
        sk.Add(w)
    # the edges and the patterns' centres in the sketch's coordinates
    data = [None if d is None else {**d, "pts": [p - shift for p in d["pts"]]} for d in map(_data, edges)]
    for p in found:
        if p["kind"] == "circular":
            p["centre"] = p["centre"] - shift
    drawn = sk._Drawing()
    circles = {}
    for k, p in enumerate(found):
        ids = [_element_at(drawn, data[i], tol) for i in p["motif"]]
        centre = _centre_at(drawn, p["centre"], tol) if p["kind"] == "circular" else ""
        if None in ids or centre is None:
            continue
        if p["kind"] == "linear":
            step = float(np.linalg.norm(p["u"]))
            if p["v"] is None:
                sk.LinearPattern(ids, _axis(p["u"], tol), p["count"], step)
            else:
                sk.LinearPattern(ids, _axis(p["u"], tol), p["count"], step, dir2=_axis(p["v"], tol), count2=p["count2"],
                                 spacing2=float(np.linalg.norm(p["v"])))
            continue
        sk.CircularPattern(ids, centre, p["count"], angle=px.Auto if p["full"] else math.degrees(p["step"]))
        point, ref = _place(p, data, ids, tol)
        r = float(np.linalg.norm(point - p["centre"]))
        pitch = next((q for q, (qc, qr) in circles.items() if np.linalg.norm(qc - p["centre"]) <= tol and abs(qr - r) <= tol),
                     None)
        if pitch is None:
            pitch = f"pitch{len(circles) + 1}"
            sk.Circle(tuple(p["centre"]), r, id=pitch, construction=True)
            circles[pitch] = (p["centre"], r)
        spoke = f"spoke{k + 1}"
        sk.Line(tuple(p["centre"]), tuple(point), id=spoke, construction=True)
        sk.Constrain(ref, pitch, px.Coincident, id=f"pattern{k + 1}_on")
        sk.Constrain(spoke + ".start", centre, px.Coincident, id=f"pattern{k + 1}_spoke_start")
        sk.Constrain(spoke + ".end", ref, px.Coincident, id=f"pattern{k + 1}_spoke_end")
    return sk


# a view's own sketch's constraints that carry over: its dimensions and these relations
RELATIONS = ("Horizontal", "Vertical", "Equal", "Concentric", "Coincident", "Parallel", "Perpendicular", "Tangent",
             "Symmetric", "Collinear")


def _given(sk, sketched, k, shift, tol):
    """the dimensions and relations of a view's own sketch (its k-th) put on the matching elements and points of sk, whose
    origin lies at shift in the view's coordinates: the ids of the dimensions added; one whose references are not all
    among the view's edges stays out, as does one that would over-define sk"""
    import planex as px

    drawn = {g["id"]: g for g in sk._Drawing() if g["kind"] in ("Line", "Arc", "Circle") and not g["construction"]}

    def point(q):
        q = np.asarray(q, float) - shift
        for i, g in drawn.items():
            for end in ("start", "end", "center"):
                if end in g and np.allclose(np.array(g[end]), q, atol=tol):
                    return f"{i}.{end}"
        return None

    def element(g):
        for i, h in drawn.items():
            if h["kind"] != g["kind"]:
                continue
            if g["kind"] in ("Circle", "Arc"):
                if np.allclose(np.array(h["center"]), np.asarray(g["center"]) - shift, atol=tol) and abs(h["r"] - g["r"]) <= tol:
                    return i
            elif any(np.allclose(np.array(h["start"]), np.asarray(a) - shift, atol=tol)
                     and np.allclose(np.array(h["end"]), np.asarray(b) - shift, atol=tol)
                     for a, b in ((g["start"], g["end"]), (g["end"], g["start"]))):
                return i
        return None

    o, h = sketched.to_view([(0.0, 0.0), (1.0, 0.0)])
    # the sketch's H and V are sk's only where its origin and axes are sk's
    axes = np.allclose(o - shift, 0.0, atol=tol) and np.allclose(h - o, (1.0, 0.0), atol=1e-9)
    seen, out = sketched.seen, []
    for n, c in enumerate(sketched.sketch._Constraints()):
        dimension = c["kind"] in ("Distance", "Diameter", "Radius", "Angle") and c["value"] is not None
        if c.get("derived") or not (dimension or c["kind"] in RELATIONS):
            continue
        refs = []
        for r in c["refs"]:
            if r in ("H", "V"):
                refs.append((px.H if r == "H" else px.V) if axes else None)
            elif r in seen and seen[r]["kind"] in ("Line", "Circle", "Arc"):
                refs.append(element(seen[r]))
            elif r in seen and seen[r]["kind"] == "Point":
                refs.append(point(seen[r]["point"]))
            elif "." in r and r.rsplit(".", 1)[0] in seen:
                g, end = seen[r.rsplit(".", 1)[0]], r.rsplit(".", 1)[1]
                q = (np.array(g["start"]) + np.array(g["end"])) / 2 if end == "mid" else g.get(end)
                refs.append(None if q is None else point(np.asarray(q, float)))
            else:
                refs.append(None)
        if any(r is None for r in refs):
            continue
        name, before = f"given{k}_{n + 1}", sk.dof
        try:
            if dimension:
                sk.Constrain(*refs, getattr(px, c["kind"]), c["value"], id=name)
            else:
                sk.Constrain(*refs, getattr(px, c["kind"]), id=name)
        except (ValueError, RuntimeError):
            continue
        if sk.dof < before and name not in sk.redundant:
            if dimension:
                out.append(name)
        else:
            sk.Remove(name)
    return out


def _chamfers(sk, tol):
    """the sketch's 45 degree chamfers whose angle it dimensions, each with its leg as a dimension: a distance on the
    chamfer as long as the leg, else the leg (from the chamfer's start to the edge at its end) in place of a distance on
    the chamfer or its corners that the sketch then needs no more; {the leg's id: (the angle's id, the chamfer's middle,
    the direction out of the part)}"""
    lines = {g["id"]: (_pt(g["start"]), _pt(g["end"])) for g in sk._Drawing() if g["kind"] == "Line" and not g["construction"]}
    out = {}
    for i, (s, e) in lines.items():
        joints = [[(j, "start" if np.linalg.norm(a - p) <= tol else "end") for j, (a, b) in lines.items()
                   if j != i and min(np.linalg.norm(a - p), np.linalg.norm(b - p)) <= tol] for p in (s, e)]
        cut = next(((x, y, corner) for x in joints[0] for y in joints[1]
                    if (corner := _corner(lines, x, y, s, e)) is not None), None)
        if cut is None:
            continue
        (side, side_end), (edge, edge_end), corner = cut
        constraints = [c for c in sk._Constraints() if not c.get("derived") and c["value"] is not None]
        angle = next((c["id"] for c in constraints if c["kind"] == "Angle" and i in c["refs"]
                      and abs(abs(c["value"]) - 45) <= 1e-6), None)
        if angle is None:
            continue
        leg = float(np.linalg.norm(e - s)) * math.sqrt(0.5)
        # the distances that place the chamfer: on it or on the other lines that end where it does (the edge between
        # the next chamfer's face and the part's), then the lengths of the two it cuts, then on its corners
        near = {i, f"{i}.start", f"{i}.end"} | {j for j, _ in joints[0] + joints[1]} - {side, edge}
        ends = {f"{j}.{end}" for j, end in joints[0] + joints[1]}
        distances = [c for c in constraints if c["kind"] == "Distance"]
        lengths = [c for c in distances if len(c["refs"]) == 1 and c["refs"][0] in (side, edge)]
        on = [c for c in distances if near & set(c["refs"])] + lengths + \
             [c for c in distances if not near & set(c["refs"]) and c not in lengths and ends & set(c["refs"])]
        found = next((c["id"] for c in on if abs(c["value"] - leg) <= tol), None)
        if found is None:
            found = _swap(sk, on, f"{i}.start", edge, leg, f"chamfer{len(out) + 1}")
        if found is not None:
            middle = (s + e) / 2
            out[found] = (angle, middle, (corner - middle) / np.linalg.norm(corner - middle))
    return out


def _corner(lines, x, y, s, e):
    """the corner a chamfer from s to e cut off, where the line x ends at s and the line y at e: the lines square to
    each other, the chamfer at 45 degrees to them, their corner beyond both ends; else None"""
    (a, a_end), (b, b_end) = x, y
    u = (e - s) / np.linalg.norm(e - s)
    v, w = ((q - p) / np.linalg.norm(q - p) for p, q in (lines[a], lines[b]))
    if abs(v @ w) > 1e-6 or abs(abs(u @ v) - math.sqrt(0.5)) > 1e-6:
        return None
    corner = s + v * ((e - s) @ v)
    far_a = lines[a][1] if a_end == "start" else lines[a][0]
    far_b = lines[b][1] if b_end == "start" else lines[b][0]
    return corner if (corner - s) @ (far_a - s) < 0 and (corner - e) @ (far_b - e) < 0 else None


def _swap(sk, on, point, edge, leg, id):
    """id, a new distance leg from point to the line edge, once it takes the place of one of the distances on with the
    sketch still fully defined and nothing more redundant; else None, the sketch as it was (an automatic distance put
    back under a new id)"""
    import planex as px

    before = set(sk.redundant)
    for c in on:
        sk.Remove(c["id"])
        try:
            sk.Constrain(point, edge, px.Distance, leg, id=id)
        except (ValueError, RuntimeError):
            pass
        made = id in {k["id"] for k in sk._Constraints()}
        if made and sk.dof == 0 and set(sk.redundant) <= before:
            return id
        if made:
            sk.Remove(id)
        sk.Constrain(*(getattr(px, r) if r in ("H", "V") else r for r in c["refs"]), px.Distance, c["value"],
                     id=None if "." in c["id"] else c["id"])
    return None


def Chamfered(found, chamfers):
    """the generated sketch's dimensions (Dimensions) with each chamfer's angle and leg (_chamfers) as one callout of
    kind "chamfer": its value the leg, at the chamfer's middle (c), out of the part along a"""
    from ._dimensions import Dimension

    by_id = {d.id: d for d in found}
    pairs = {leg: angle for leg, (angle, _, _) in chamfers.items() if leg in by_id and angle in by_id}
    out = [d for d in found if d.id not in pairs and d.id not in pairs.values()]
    for leg, angle in pairs.items():
        d = Dimension(leg, "chamfer", by_id[leg].value, over=by_id[leg].over or by_id[angle].over,
                      refs=by_id[leg].refs + by_id[angle].refs)
        d.c, d.a = chamfers[leg][1], chamfers[leg][2]
        out.append(d)
    return out


def _dimensioned(drawing, wires, shift, tol):
    """the view's sketch with its origin at shift in the view's coordinates (_sketch), the view's own sketches' dimensions
    and relations on it (_given), then every relation but equal lengths and the dimensions a drafter gives, a 45
    degree chamfer by its angle and leg (_chamfers); with the ids of the dimensions given and the chamfers"""
    import planex as px

    plane = drawing.plane
    h, v = np.asarray(tuple(plane.h), float), np.asarray(tuple(plane.v), float)
    sk = _sketch(wires, plane.Translated(tuple(shift[0] * h + shift[1] * v)), shift, tol)
    given = [i for k, s in enumerate(drawing._sketched) if s.sketch is not drawing._auto for i in _given(sk, s, k, shift, tol)]
    # every relation but equal lengths: two edges of one length are mostly a coincidence of the design
    relations = {k: k != "equal_length" for k in px.config.auto_constraint_options if k != "named_helpers"}
    sk.AutoConstrain(relations, True, tol)
    # the closed outline already repeats a pattern's instances: its step angle or its ties to the pitch circle are then
    # redundant, the last first
    while ours := [r for r in sk.redundant if (r.startswith("CircularPattern.") and r.endswith(".angle"))
                   or r.startswith("pattern")]:
        sk.Remove(ours[-1])
    return sk, given, _chamfers(sk, tol)


def _axes(sk, kinds):
    """the sketch's axes (0 for V, 1 for H) its constraints of kinds, or any with a value for None, refer to"""
    return {0 if r == "V" else 1 for c in sk._Constraints() if not c.get("derived")
            and (c["value"] is not None if kinds is None else c["kind"] in kinds) for r in c["refs"] if r in ("H", "V")}


def ViewSketch(drawing):
    """the sketch that dimensions a view of a solid (_dimensioned): its visible lines, arcs and circles (_merged), each
    pattern as one instance and a pattern of it; on the view's plane, its origin moved to the datum along
    an axis a dimension measures from and no point of the part lies on (the part's place on the plane is no size, a
    section's plane through its middle no line it has), and auto constrained anew; with the ids of the dimensions taken
    from the view's own sketches (they draw them), the axes (0 for V, 1 for H) the part is symmetric about or measured
    from and no edge of it lies along, which the view draws as center lines, and its chamfers (_chamfers)"""
    import planex as px

    solid, plane = drawing._solid, drawing.plane
    tol = 1e-6 * solid.size
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", px.PlanexWarning)
        wires = _wires([e for w in solid.wires["visible"] for e in w.edges], plane, tol)
        data = [d for w in wires for e in w.edges if (d := _data(e.edge)) is not None]
        pts = np.array([p for d in data for p in d["pts"]]) if data else np.zeros((0, 2))
        shift = np.zeros(2)
        for _ in range(3):
            sk, given, chamfers = _dimensioned(drawing, wires, shift, tol)
            off = [k for k in _axes(sk, None) if not np.any(np.abs(pts[:, k] - shift[k]) <= tol)]
            if not off or not data:
                break
            for k in off:
                shift[k] = _datum(data)[k]
        lines = [d for d in data if d["kind"] == "line"]
        drawn = {k for k in _axes(sk, ("Symmetric", "Distance"))
                 if not any(all(abs(p[k] - shift[k]) <= tol for p in d["pts"]) for d in lines)}
    return sk, given, drawn, chamfers


def _rank(view):
    """a view's place in the order the sheet keeps a size in: front, top, the side views, sections, others"""
    from ._drawing import _side

    if view._section:
        return 3
    side = _side(view.plane)
    return {"front": 0, "top": 1, "other": 4}.get(side, 2)


def _lift(plane, p):
    return np.asarray(plane.ToXYZ((float(p[0]), float(p[1]))), float)


def _canonical3(u):
    u = np.asarray(u, float) / np.linalg.norm(u)
    for c in u:
        if abs(c) > 1e-9:
            return u if c > 0 else -u
    return u


def _size_of(view, d, tol):
    """what a dimension of a view measures in 3D: ("linear", direction, (from, to) along it), ("round", axis, centre,
    radius), ("angle", the plane's normal, the vertex, the value) or ("chamfer", the plane's normal, its middle, its
    leg); None for another"""
    plane = view.plane
    n = _canonical3(tuple(plane.normal))
    if d.kind == "linear" and d.u is not None:
        h, v = np.asarray(tuple(plane.h), float), np.asarray(tuple(plane.v), float)
        u = _canonical3(d.u[0] * h + d.u[1] * v)
        return ("linear", u, tuple(sorted((float(_lift(plane, d.p) @ u), float(_lift(plane, d.q) @ u)))))
    if d.kind in ("radius", "diameter") and d.c is not None:
        c = _lift(plane, d.c)
        return ("round", n, c - (c @ n) * n, float(d.r))
    if d.kind == "angle" and d.c is not None:
        c = _lift(plane, d.c)
        return ("angle", n, c - (c @ n) * n, round(abs(float(d.value)), 6))
    if d.kind == "chamfer":
        c = _lift(plane, d.c)
        return ("chamfer", n, c - (c @ n) * n, round(float(d.value), 9))
    return None


def _same(a, b, tol):
    if a[0] != b[0]:
        return False
    if a[0] == "linear":
        return np.allclose(a[1], b[1], atol=1e-9) and np.allclose(a[2], b[2], atol=tol)
    if a[0] == "round":
        return np.allclose(a[1], b[1], atol=1e-9) and np.allclose(a[2], b[2], atol=tol) and abs(a[3] - b[3]) <= tol
    return np.allclose(a[1], b[1], atol=1e-9) and np.allclose(a[2], b[2], atol=tol) and a[3] == b[3]


def _across(a, rounds, tol):
    """whether the linear size a is a circle's diameter seen from the side: across it, square to its axis"""
    if a[0] != "linear":
        return False
    for _, axis, foot, r in rounds:
        mid = foot @ a[1]
        if abs(a[1] @ axis) < 1e-9 and abs(a[2][1] - a[2][0] - 2 * r) <= tol and abs((a[2][0] + a[2][1]) / 2 - mid) <= tol:
            return True
    return False


def Repeats(views):
    """for each view (by its index), the ids of the generated dimensions another view on the sheet already shows: the
    sizes of the views' own sketches first, then the generated ones in the views' order (_rank), a diameter or radius in
    the view where its circle is round; a view keeps all of its own"""
    out = {k: set() for k in range(len(views))}
    sized = [v._solid.size for v in views if v._auto is not None]
    if not sized:
        return out
    tol = 1e-6 * max(sized)
    order = sorted(range(len(views)), key=lambda k: (_rank(views[k]), k))
    # the sizes taken, with the view that took them: None for a view's own sketch, which counts for every view
    taken, generated = [], []
    for k in order:
        auto = views[k]._generated()
        for d in views[k]._shown():
            size = _size_of(views[k], d, tol)
            if size is None:
                continue
            if d.id in auto:
                generated.append((k, d.id, size))
            else:
                taken.append((None, size))
    rounds = [(j, s) for j, s in taken if s[0] == "round"] + [(k, s) for k, _, s in generated if s[0] == "round"]
    for k, i, size in sorted(generated, key=lambda t: t[2][0] != "round"):
        if any(_same(size, s, tol) for j, s in taken if j != k) or _across(size, [s for j, s in rounds if j != k], tol):
            out[k].add(i)
        else:
            taken.append((k, size))
    return out
