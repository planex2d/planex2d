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
    touching = _touching(data, tol)
    data = _counterclockwise(data, touching, tol)
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
            motif = _motif(order, step, data, touching, member_run, tol)
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


def _motif(order, step, data, touching, member_run, tol):
    """a circular pattern's first instance: a circle with the circles about its centre that repeat with it (a
    counterbored hole), else the edges walked from it along the outline to its next copy"""
    first, nxt, copies = order[0], order[1], set(order)
    if data[first]["kind"] == "circle":
        return [first] + [i for i in _concentric(first, member_run, data, tol)
                          if len(member_run[i][0]) == len(order) and abs(member_run[i][1] - step) <= 1e-6]
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


def _reversed(d):
    """an edge record run the other way"""
    return {**d, "pts": [d["pts"][1], d["pts"][0]] + d["pts"][2:], **({"ccw": not d["ccw"]} if "ccw" in d else {})}


def _counterclockwise(data, touching, tol):
    """the records with every closed group of edges chained and running counterclockwise: a view's edges come each its
    own way, and a pattern tells a square's opposite sides apart by their directions"""
    out, seen = list(data), set()
    for i, d in enumerate(data):
        group = None if d is None or i in seen else _component(i, data, touching)
        if group is None:
            continue
        seen.update(group)
        loop, cur = [i], i
        while True:
            end = out[cur]["pts"][1]
            near = lambda p: np.linalg.norm(p - end) <= 2 * tol
            nxt = next((j for j in touching[cur] if j not in loop and (near(out[j]["pts"][0]) or near(out[j]["pts"][1]))), None)
            if nxt is None:
                break
            if not near(out[nxt]["pts"][0]):
                out[nxt] = _reversed(out[nxt])
            loop.append(nxt)
            cur = nxt
        area = 0.0
        for j in loop:
            area += _cross(out[j]["pts"][0], out[j]["pts"][1]) / 2
            if out[j]["kind"] == "arc":
                t = _sweep(out[j])
                area += (1 if out[j]["ccw"] else -1) * out[j]["size"] ** 2 / 2 * (t - math.sin(t))
        if area < 0:
            for j in loop:
                out[j] = _reversed(out[j])
    return out


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
        motif = _component(i, data, touching) or [i] + [q for q in _concentric(i, place, data, tol)
                                                         if place[q][:4] == (0, 0, n, m) and np.allclose(place[q][4], u)]
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


def _concentric(i, others, data, tol):
    """the circles among others about the centre of the circle i"""
    if data[i]["kind"] != "circle":
        return []
    return [j for j in others if j != i and data[j]["kind"] == "circle"
            and np.linalg.norm(data[j]["pts"][0] - data[i]["pts"][0]) <= tol]


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


def _swap(sk, on, refs, value, id):
    """id, a new distance value between refs, once it takes the place of one of the distances on with the sketch still
    fully defined and nothing more redundant; else None, the sketch as it was (an automatic distance put back under a
    new id)"""
    import planex as px

    before = set(sk.redundant)
    for c in on:
        sk.Remove(c["id"])
        try:
            sk.Constrain(*refs, px.Distance, value, id=id)
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
    """the generated sketch's dimensions (Dimensions) with a callout of kind "chamfer" per chamfer seen (_chamfer_view):
    its value the leg (r the other leg where they differ), at the chamfer's middle (c), out of the part along a"""
    from ._dimensions import Dimension

    out = list(found)
    for c in chamfers:
        d = Dimension(c["id"], "chamfer", c["legs"][0])
        d.r = 0.0 if c["equal"] else c["legs"][1]
        d.c, d.a, d.chain, d.end_on = c["c"], c["a"], c["chain"], c["end_on"]
        out.append(d)
    return out


def Diametral(found, diameters):
    """the generated sketch's dimensions (Dimensions) with the distances across a turned axis (_diameters) written as
    diameters: "⌀25" """
    for d in found:
        if d.id in diameters and d.kind == "linear":
            d.prefix = "⌀"
    return found


def _dimensioned(drawing, wires, shift, tol):
    """the view's sketch with its origin at shift in the view's coordinates (_sketch), the view's own sketches' dimensions
    and relations on it (_given), then every relation but equal lengths and the dimensions a drafter gives; with the
    ids of the dimensions given"""
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
    return sk, given


def _axes(sk, kinds):
    """the sketch's axes (0 for V, 1 for H) its constraints of kinds, or any with a value for None, refer to"""
    return {0 if r == "V" else 1 for c in sk._Constraints() if not c.get("derived")
            and (c["value"] is not None if kinds is None else c["kind"] in kinds) for r in c["refs"] if r in ("H", "V")}


def ViewSketch(drawing):
    """the sketch that dimensions a view of a solid (_dimensioned): its visible lines, arcs and circles (_merged), of
    the fold lines only those of faces round about a center line the view draws, along a turned axis no other lines
    (_along; a section keeps its cut's), each pattern as one instance and a pattern of it; on the view's plane, its
    origin moved to the datum along an axis a dimension measures from and no point of the part lies on (the part's place
    on the plane is no size, a section's plane through its middle no line it has), and auto constrained anew; with the
    ids of the dimensions taken from the view's own sketches (they draw them), the axes (0 for V, 1 for H) the part is
    symmetric about or measured from and no edge of it lies along, which the view draws as center lines, its chamfers'
    callouts (_chamfer_view) and its diameters across a turned axis (_diameters)"""
    import planex as px

    solid, plane = drawing._solid, drawing.plane
    tol = 1e-6 * solid.size
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", px.PlanexWarning)
        folds = solid.wires.get("folds", [])
        edges, chamfers = _chamfer_view(drawing, [e for w in solid.wires["visible"] for e in w.edges], folds, tol)
        axes = _turned_axes(folds, edges, plane, tol)
        sk, given, shift, data = _settled(drawing, edges, tol, axes)
        centres = _centre_lines(sk, _drawn(sk, data, shift, tol), plane, shift) + [(o - shift, a, q, u) for o, a, q, u in axes]
        odd = [f for f in folds if not (f[2] is not None and f[2][2]) and not any(_round_about(f[2], q, u, tol) for _, _, q, u in centres)]
        turned = [(o + shift, a, rounds) for o, a, q, u in centres if (rounds := [f for f in folds if _round_about(f[2], q, u, tol)])]
        lines = [d["pts"] for e in edges if (d := _data(e.edge)) is not None and d["kind"] == "line"]
        along = set() if drawing._section else {k for k, e in enumerate(edges) if _along(e, turned, lines, tol)}
        if odd or along:
            kept = [e for k, e in enumerate(edges) if k not in along and not any(_matches(e, f, tol) for f in odd)]
            kept += _whole([f for f in folds if f not in odd], plane, tol)
            sk, given, shift, data = _settled(drawing, kept, tol, axes)
            centres = _centre_lines(sk, _drawn(sk, data, shift, tol), plane, shift) + [(o - shift, a, q, u) for o, a, q, u in axes]
        drawn = _drawn(sk, data, shift, tol) | {1 if abs(a[1]) <= 1e-9 else 0 for o, a, _, _ in axes
                                               if abs(a[0]) <= 1e-9 or abs(a[1]) <= 1e-9}
        turned = [(o, a, [(_pt(f[0]) - shift, _pt(f[1]) - shift) for f in folds if _round_about(f[2], q, u, tol)])
                  for o, a, q, u in centres]
        diameters = _diameters(sk, turned, tol)
    for c in chamfers:
        c["c"] = c["c"] - shift
    return sk, given, drawn, chamfers, diameters


def _settled(drawing, edges, tol, turned=()):
    """the view's sketch of edges (_dimensioned) with its origin on the turned axes along H or V (_turned_axes), else
    moved to the datum along an axis a dimension measures from and no point of the part lies on; with the ids given,
    the shift and the edges' data"""
    wires = _wires(edges, drawing.plane, tol)
    data = [d for w in wires for e in w.edges if (d := _data(e.edge)) is not None]
    pts = np.array([p for d in data for p in d["pts"]]) if data else np.zeros((0, 2))
    shift, held = np.zeros(2), set()
    for o, a, _, _ in turned:
        for k in (0, 1):
            if abs(a[1 - k]) <= 1e-9 and 1 - k not in held:
                shift[1 - k] = o[1 - k]
                held.add(1 - k)
    for _ in range(3):
        sk, given = _dimensioned(drawing, wires, shift, tol)
        off = [k for k in _axes(sk, None) - held if not np.any(np.abs(pts[:, k] - shift[k]) <= tol)]
        if not off or not data:
            break
        for k in off:
            shift[k] = _datum(data)[k]
    return sk, given, shift, data


def _drawn(sk, data, shift, tol):
    """the axes (0 for V, 1 for H) the sketch is symmetric about or measured from and no line lies along"""
    lines = [d for d in data if d["kind"] == "line"]
    return {k for k in _axes(sk, ("Symmetric", "Distance"))
            if not any(all(abs(p[k] - shift[k]) <= tol for p in d["pts"]) for d in lines)}


def _centre_lines(sk, drawn, plane, shift):
    """the center lines the view draws: the axes drawn and the sketch's own symmetry axes, each as (a point and the
    direction in the sketch, a point and the direction in 3D)"""
    h, v = np.asarray(tuple(plane.h), float), np.asarray(tuple(plane.v), float)
    out = [(np.zeros(2), np.array([1.0, 0.0] if k else [0.0, 1.0])) for k in drawn]
    lines = {g["id"]: g for g in sk._Drawing() if g["kind"] == "Line" and g["construction"]}
    axes = {c["refs"][-1] for c in sk._Constraints() if c["kind"] == "Symmetric"} | \
           {c["refs"][1] for c in sk._Constraints() if c["kind"] == "Coincident" and c["refs"][0].endswith(".mid")}
    for i in axes & set(lines):
        a, b = _pt(lines[i]["start"]), _pt(lines[i]["end"])
        out.append((a, (b - a) / np.linalg.norm(b - a)))
    return [(o, a, _lift(plane, o + shift), a[0] * h + a[1] * v) for o, a in out]


def _chamfer_view(drawing, edges, folds, tol):
    """the view's edges without its chamfer lines (on a chamfer's edges found in 3D, or a fold line between them, and
    not on the sharp edge it cuts off), the lines that meet a chamfer seen end-on taken on to the sharp corner; with a
    callout per chamfer seen: its id, chain, legs, middle and the way out of the part in the view's coordinates, and
    whether seen end-on"""
    import planex as px

    solid, plane = drawing._solid, drawing.plane
    if getattr(solid, "chains", None) is None:
        solid.chains = px._planex._Chamfers(solid.shape, 2e-3 / drawing.unit)
    if not solid.chains:
        return edges, []
    h, v = np.asarray(tuple(plane.h), float), np.asarray(tuple(plane.v), float)
    zero = _lift(plane, (0.0, 0.0))

    def seen(line):
        pts = np.asarray(line, float) - zero
        return np.stack([pts @ h, pts @ v], axis=1)

    near = min(1e-3 * solid.size, 0.25 * min(min(c["legs"]) for c in solid.chains))
    on, sharp = {}, {}
    for k, c in enumerate(solid.chains):
        on[k] = np.concatenate([np.stack([p[:-1], p[1:]], axis=1) for p in map(seen, c["lines"]) if len(p) > 1])
        ends = []
        for p in map(seen, c["sharp"]):
            if len(p) > 1:
                p = np.vstack([p[0] - 2 * (p[1] - p[0]), p, p[-1] + 2 * (p[-1] - p[-2])])
                ends.append(np.stack([p[:-1], p[1:]], axis=1))
        sharp[k] = np.concatenate(ends) if ends else np.zeros((0, 2, 2))
    removed = {}
    for i, e in enumerate(edges):
        pts = _samples(e)
        if pts is None:
            continue
        for k in on:
            fold = any(_matches(e, f, tol) for f in folds) and _within(pts[[0, -1]], on[k], near).all()
            if (fold or _within(pts, on[k], near).all()) and not _within(pts, sharp[k], near).all():
                removed[i] = k
                break
    gone = [(k, np.array([d["pts"]])) for i, k in removed.items() if (d := _data(edges[i].edge)) is not None and d["kind"] == "line"]
    for i, e in enumerate(edges):
        d = _data(e.edge)
        if i in removed or d is None or d["kind"] != "line":
            continue
        pts = _samples(e)
        k = next((k for k, segment in gone if _within(pts, segment, near).all()), None)
        if k is not None:
            removed[i] = k
    kept = [e for i, e in enumerate(edges) if i not in removed]
    callouts = {}
    for i, k in removed.items():
        d = _data(edges[i].edge)
        if d is None or d["kind"] != "line":
            continue
        a, b = d["pts"]
        if not len(sharp[k]):
            continue
        x = _closest((a + b) / 2, sharp[k])
        if np.linalg.norm(x - (a + b) / 2) <= tol or abs(_cross(b - a, x - a)) <= tol * np.linalg.norm(b - a):
            continue
        corner = _to_corner(kept, a, b, x, plane, tol)
        if corner is not None:
            kept = corner
            callouts[k] = ((a + b) / 2, (x - (a + b) / 2) / np.linalg.norm(x - (a + b) / 2), True)
    kept = _closed(kept, [edges[i] for i in removed], tol)
    for i, k in removed.items():
        pts = _samples(edges[i])
        if k in callouts or pts is None or not len(sharp[k]):
            continue
        middle = pts[len(pts) // 2]
        out = _closest(middle, sharp[k]) - middle
        if np.linalg.norm(out) > tol:
            callouts[k] = (middle, out / np.linalg.norm(out), False)
    found = []
    for k, (middle, way, end_on) in sorted(callouts.items()):
        legs = solid.chains[k]["legs"]
        found.append({"id": f"chamfer{k + 1}", "chain": f"chain{k}", "legs": legs, "c": middle, "a": way, "end_on": end_on,
                      "equal": abs(legs[0] - legs[1]) <= 1e-3 * max(legs) + tol})
    return kept, found


def _closed(kept, removed, tol):
    """the kept edges with the removed ones back that end where a kept edge would end alone (a gap no sharp corner
    closes): the one going on straight from it, else all there, until none does (an outline that is a chamfer's edge,
    two chamfers meeting); edges and ends compared within ten times tol, a view having most edges more than once"""
    near = 10 * tol

    def ends(e):
        d = _data(e.edge)
        return None if d is None or d["kind"] not in ("line", "arc") else (d["pts"][0], d["pts"][1])

    def same(p, q):
        return min(np.linalg.norm(p[0] - q[0]) + np.linalg.norm(p[1] - q[1]),
                   np.linalg.norm(p[0] - q[1]) + np.linalg.norm(p[1] - q[0])) <= near

    back = list(removed)
    while True:
        lines = []
        for e in kept:
            p = ends(e)
            if p is not None and not any(same(p, q) for q in lines):
                lines.append(p)
        pts = np.array([x for p in lines for x in p]) if lines else np.zeros((0, 2))
        found = []
        for p, q in lines:
            for x, y in ((p, q), (q, p)):
                if np.sum(np.linalg.norm(pts - x, axis=1) <= near) != 1:
                    continue
                at = [e for e in back if (r := ends(e)) is not None and min(np.linalg.norm(r[0] - x), np.linalg.norm(r[1] - x)) <= near]
                on = [e for e in at if abs(_cross(x - y, ends(e)[1] - ends(e)[0])) <= 1e-6 * np.linalg.norm(x - y)
                      * np.linalg.norm(ends(e)[1] - ends(e)[0])]
                found += [e for e in (on or at) if all(e is not f for f in found)]
        if not found:
            return kept
        kept = kept + found
        back = [e for e in back if all(e is not f for f in found)]


def _cross(u, w):
    return float(u[0] * w[1] - u[1] * w[0])


def _samples(e):
    """points along the view's edge e (a line, an arc or a circle); None for another"""
    d = _data(e.edge)
    if d is None:
        return None
    if d["kind"] == "line":
        a, b = d["pts"]
        return np.array([a + (b - a) * s for s in np.linspace(0, 1, 5)])
    c, r = d["pts"][-1], d["size"]
    if d["kind"] == "circle":
        turns = np.linspace(0, 2 * math.pi, 9)[:-1]
    else:
        t0, t1 = _angle(d["pts"][0], c), _angle(d["pts"][1], c)
        sweep = (t1 - t0) % (2 * math.pi) if d["ccw"] else -((t0 - t1) % (2 * math.pi))
        turns = t0 + sweep * np.linspace(0, 1, 5)
    return np.stack([c[0] + r * np.cos(turns), c[1] + r * np.sin(turns)], axis=1)


def _within(pts, segments, near):
    """for each point whether it lies within near of one of the segments (n x 2 x 2)"""
    if not len(segments):
        return np.zeros(len(pts), bool)
    a, b = segments[:, 0], segments[:, 1]
    ab = b - a
    length = np.maximum((ab * ab).sum(axis=1), 1e-300)
    out = []
    for p in pts:
        s = np.clip(((p - a) * ab).sum(axis=1) / length, 0, 1)
        out.append(np.min(np.linalg.norm(a + s[:, None] * ab - p, axis=1)) <= near)
    return np.array(out)


def _closest(p, segments):
    a, b = segments[:, 0], segments[:, 1]
    ab = b - a
    s = np.clip(((p - a) * ab).sum(axis=1) / np.maximum((ab * ab).sum(axis=1), 1e-300), 0, 1)
    q = a + s[:, None] * ab
    return q[np.argmin(np.linalg.norm(q - p, axis=1))]


def _to_corner(kept, a, b, x, plane, tol):
    """the kept edges with the lines ending at a or at b that lie on the straight line from there to x (the sharp corner
    a chamfer from a to b seen end-on cut off, its sharp edge seen end-on) taken on to x; None where not both ends have
    such a line (lines of other chamfers ending there point elsewhere)"""
    import planex as px

    found = {}
    for i, e in enumerate(kept):
        d = _data(e.edge)
        if d is None or d["kind"] != "line" or d["size"] <= tol:
            continue
        for q, r in ((d["pts"][0], d["pts"][1]), (d["pts"][1], d["pts"][0])):
            for end in (0, 1):
                p = (a, b)[end]
                if np.linalg.norm(r - p) <= tol and np.linalg.norm(x - p) > tol \
                        and abs(_cross(r - q, x - p)) <= 1e-6 * np.linalg.norm(r - q) * np.linalg.norm(x - p) and (r - q) @ (x - p) > 0:
                    found[i] = (q, end)
    if {end for _, end in found.values()} != {0, 1}:
        return None
    line = lambda p, q: px.Wires([px.Line(tuple(p), tuple(q), plane=plane)], tol)[0].edges[0]
    return [e for k, e in enumerate(kept) if k not in found] + [line(q, x) for q, _ in found.values()]


def _turned_axes(folds, edges, plane, tol):
    """the axes of a turned part seen from the side: an axis in the view's plane that convex fold lines are round
    about, which bound the view across it on both sides alike (a shaft's or a disc's outline, not a corner's fillet or
    a boss on a bar); each as a point and the direction in the view's coordinates and in 3D"""
    h, v = np.asarray(tuple(plane.h), float), np.asarray(tuple(plane.v), float)
    zero = _lift(plane, (0.0, 0.0))
    data = [d for e in edges if (d := _data(e.edge)) is not None]
    out = []
    for f in folds:
        if f[2] is None or f[2][1] is None or not f[2][2]:
            continue
        q, u = np.asarray(f[2][0], float), np.asarray(f[2][1], float)
        a = np.array([u @ h, u @ v])
        if abs(np.linalg.norm(a) - 1) > 1e-9 or any(_round_about((q, u), p, w, tol) for _, _, p, w in out):
            continue
        o, n = np.array([(q - zero) @ h, (q - zero) @ v]), np.array([-a[1], a[0]])
        offs = [float(n @ (p - o)) for d in data for p in (d["pts"] if d["kind"] != "circle" else
                                                           [d["pts"][0] + d["size"] * n, d["pts"][0] - d["size"] * n])]
        bounds = [float(n @ (_pt(g[0]) - o)) for g in folds if _round_about(g[2], q, u, tol) and g[2][2]]
        if offs and abs(max(offs) + min(offs)) <= tol and any(abs(b - max(offs)) <= tol for b in bounds) \
                and any(abs(b - min(offs)) <= tol for b in bounds):
            out.append((o, a, q, u))
    return out


def _round_about(axis, q, u, tol):
    """whether a fold line's face with axis (a point and a direction, None for a sphere's centre) is round about the
    line through q along u"""
    if axis is None:
        return False
    w = np.asarray(axis[0], float) - q
    if np.linalg.norm(w - (w @ u) * u) > tol:
        return False
    return axis[1] is None or np.linalg.norm(np.cross(np.asarray(axis[1], float), u)) <= 1e-6


def _along(e, turned, lines, tol):
    """whether the view's edge e is a line along a turned axis (a point and its direction in the view, the fold lines
    round about it) that is no such fold line and has no mirror image across it among the view's lines: a flute's or a
    keyway's edge on the outline, sized where its feature is seen round or flat (a keyway's walls seen edge-on are a
    pair across the axis and keep their width)"""
    d = _data(e.edge)
    if d is None or d["kind"] != "line" or d["size"] <= tol:
        return False
    s, f = d["pts"]
    for o, a, rounds in turned:
        if abs(a[0] * (f - s)[1] - a[1] * (f - s)[0]) > tol or any(_matches(e, g, tol) for g in rounds):
            continue
        n = np.array([-a[1], a[0]])
        off, span = float(n @ (s - o)), sorted((float(a @ (s - o)), float(a @ (f - o))))
        mirrored = any(abs(float(n @ (p - o)) + off) <= tol and abs(float(n @ (q - p))) <= tol
                       and min(span[1], max(float(a @ (p - o)), float(a @ (q - o))))
                       - max(span[0], min(float(a @ (p - o)), float(a @ (q - o)))) > tol for p, q in lines)
        if abs(off) > tol and not mirrored:
            return True
    return False


def _whole(folds, plane, tol):
    """lines over the gaps between the pieces of one fold line, on one face and one straight line (a shaft's outline a
    keyway or a flute cuts), as edges to add to a view's"""
    import planex as px

    groups = {}
    for f in folds:
        a, b = _pt(f[0]), _pt(f[1])
        if f[2] is None or np.linalg.norm(b - a) <= tol:
            continue
        u = (b - a) / np.linalg.norm(b - a)
        u = u if u[0] > tol or (abs(u[0]) <= tol and u[1] > 0) else -u
        key = (f[2][3], round(float(a[0] * u[1] - a[1] * u[0]) / tol), round(float(u[0]) * 1e6), round(float(u[1]) * 1e6))
        groups.setdefault(key, []).append((a, b, u))
    out = []
    for pieces in groups.values():
        if len(pieces) > 1:
            u = pieces[0][2]
            along = [float(p @ u) for a, b, _ in pieces for p in (a, b)]
            foot = pieces[0][0] - (pieces[0][0] @ u) * u
            out.append(px.Wires([px.Line(tuple(foot + min(along) * u), tuple(foot + max(along) * u), plane=plane)], tol)[0].edges[0])
    return out


def _matches(e, fold, tol):
    """whether the view's edge e is the fold line (start, end, axis)"""
    a, b = np.asarray(fold[0], float), np.asarray(fold[1], float)
    d = _data(e.edge)
    if d is None:
        return False
    if d["kind"] == "circle":
        return np.linalg.norm(a - b) <= tol and abs(np.linalg.norm(a - d["pts"][0]) - d["size"]) <= tol
    s, f = d["pts"][:2]
    return min(max(np.linalg.norm(s - a), np.linalg.norm(f - b)), max(np.linalg.norm(s - b), np.linalg.norm(f - a))) <= tol


def _diameters(sk, turned, tol):
    """the ids of the distances across a turned axis (o, a, its fold lines), between a line on a fold line round about
    it and its mirror image, the largest first: the distance there, or a new one in place of a distance between lines
    along the axis (_swap)"""
    lines = {g["id"]: (_pt(g["start"]), _pt(g["end"])) for g in sk._Drawing() if g["kind"] == "Line" and not g["construction"]}
    out = set()
    for o, a, folds in turned:
        n = np.array([-a[1], a[0]])
        along = {i: (float(n @ (s - o)), sorted((float(a @ (s - o)), float(a @ (e - o)))))
                 for i, (s, e) in lines.items() if abs(float(n @ (e - s))) <= tol}
        round_ = {i for i, (off, span) in along.items() for f, g in folds
                  if abs(float(n @ (f - o)) - off) <= tol and abs(float(n @ (g - o)) - off) <= tol
                  and min(span[1], max(float(a @ (f - o)), float(a @ (g - o))))
                  - max(span[0], min(float(a @ (f - o)), float(a @ (g - o)))) > tol}
        pairs = sorted(((i, j, 2 * off) for i, (off, span) in along.items() for j, (other, there) in along.items()
                        if off > tol and abs(off + other) <= tol and {i, j} <= round_
                        and min(span[1], there[1]) - max(span[0], there[0]) > tol),
                       key=lambda p: -p[2])
        for i, j, value in pairs:
            distances = [c for c in sk._Constraints() if c["kind"] == "Distance" and not c.get("derived")
                         and c["value"] is not None and c["id"] not in out]
            have = next((c["id"] for c in distances if set(c["refs"]) == {i, j}), None)
            if have is not None:
                out.add(have)
                continue
            steps = [c for c in distances if len(c["refs"]) == 2 and set(c["refs"]) <= set(along)]
            steps.sort(key=lambda c: not {i, j} & set(c["refs"]))
            made = _swap(sk, steps, (i, j), value, f"diameter{len(out) + 1}")
            if made is not None:
                out.add(made)
    return out


def _rank(view):
    """a view's place in the order the sheet keeps a size in: front, top, the side views, sections, others"""
    from ._drawing import _side

    if view._section:
        return 3
    side = _side(view.plane)
    return {"front": 0, "top": 1, "other": 4}.get(side, 2)


def _circular(view):
    """whether the view's generated sketch has a circular pattern, which it sees round"""
    return view._auto is not None and any(g["id"].startswith("CircularPattern.") for g in view._auto._Drawing())


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
    radius) (also a turned part's diameter seen from the side), ("angle", the plane's normal, the vertex, the value) or
    ("chamfer", the plane's normal, its middle, its leg); None for another"""
    plane = view.plane
    n = _canonical3(tuple(plane.normal))
    if d.kind == "linear" and d.u is not None and d.prefix == "⌀":
        h, v = np.asarray(tuple(plane.h), float), np.asarray(tuple(plane.v), float)
        axis = _canonical3(-d.u[1] * h + d.u[0] * v)
        c = _lift(plane, (np.asarray(d.p, float) + np.asarray(d.q, float)) / 2)
        return ("round", axis, c - (c @ axis) * axis, abs(float(d.value)) / 2)
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
        return ("chamfer", getattr(d, "chain", None), round(float(d.value), 9), round(float(d.r), 9))
    return None


def _same(a, b, tol):
    if a[0] != b[0]:
        return False
    if a[0] == "chamfer":
        return a[1] is not None and a[1:] == b[1:]
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
    sizes of the views' own sketches first, then the generated ones in the views' order (a view with a circular pattern
    first, _circular, then _rank), a turned part's diameter in the view along its axis (_diameters), another diameter
    or radius in the view where its circle is round, a chamfer once, where it is seen end-on, and none of a size the
    general note gives (GeneralChamfers); a view keeps all of its own"""
    out = {k: set() for k in range(len(views))}
    sized = [v._solid.size for v in views if v._auto is not None]
    if not sized:
        return out
    tol = 1e-6 * max(sized)
    order = sorted(range(len(views)), key=lambda k: (not _circular(views[k]), _rank(views[k]), k))
    # the sizes taken, with the view that took them: None for a view's own sketch, which counts for every view
    taken, generated = [], []
    for k in order:
        auto = views[k]._generated()
        for d in views[k]._shown():
            size = _size_of(views[k], d, tol)
            if size is None:
                continue
            if d.id in auto:
                generated.append((k, d.id, size, d.prefix == "⌀" or getattr(d, "end_on", False)))
            else:
                taken.append((None, size))
    rounds = [(j, s) for j, s in taken if s[0] == "round"] + [(k, s) for k, _, s, _ in generated if s[0] == "round"]
    for k, i, size, _ in sorted(generated, key=lambda t: (not t[3], t[2][0] != "round")):
        if any(_same(size, s, tol) for j, s in taken if j != k) or _across(size, [s for j, s in rounds if j != k], tol):
            out[k].add(i)
        else:
            taken.append((k, size))
    for ids in GeneralChamfers(views).values():
        for k, i in ids:
            out[k].add(i)
    return out


def GeneralChamfers(views):
    """the chamfer sizes three or more chamfers of a sheet's views have, which the sheet gives in a general note:
    {(leg, the other leg where they differ, else 0): [(view index, callout id), ...]}"""
    sizes = {}
    for k, v in enumerate(views):
        for c in getattr(v, "_chamfers", []):
            legs = (round(float(c["legs"][0]), 9), 0.0 if c["equal"] else round(float(c["legs"][1]), 9))
            sizes.setdefault(legs, {}).setdefault(c["chain"], []).append((k, c["id"]))
    return {legs: [x for ids in chains.values() for x in ids] for legs, chains in sizes.items() if len(chains) >= 3}
