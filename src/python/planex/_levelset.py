# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""planex polygons from NGSolve level set functions"""
from __future__ import annotations

import warnings
from typing import TYPE_CHECKING, Any

import numpy as np

from . import _planex as px
from ._geom import oriented, scale

NEWTON_STEPS = 3

if TYPE_CHECKING:
    from ._planex import Shape
    from .types import Plane


def _lattice(k):
    """barycentric nodes (a, b, c) with a + b + c = k of a triangle and the sub-triangles between them"""
    nodes = [(a, b, k - a - b) for a in range(k, -1, -1) for b in range(k - a, -1, -1)]
    index = {n: i for i, n in enumerate(nodes)}
    tris = []
    for a, b, c in nodes:
        if a >= 1:
            tris.append((index[(a, b, c)], index[(a - 1, b + 1, c)], index[(a - 1, b, c + 1)]))
        if a >= 2:
            tris.append((index[(a - 1, b + 1, c)], index[(a - 2, b + 1, c + 1)], index[(a - 1, b, c + 1)]))
    return nodes, tris


def _global_nodes(mesh, tri, nodes, k):
    """global id of every lattice node of every element: the mesh vertices, then k - 1 nodes per mesh edge counted
    from its smaller vertex number, then the interior nodes per element"""
    from ngsolve import VOL
    ne, nv = len(tri), mesh.nv
    ev = np.array([[v.nr for v in e.vertices] for e in mesh.edges])
    eledges = np.array([[e.nr for e in el.edges] for el in mesh.Elements(VOL)])
    evs = np.sort(ev[eledges], axis=2)

    def edge_of(p, q):
        want = np.sort(tri[:, [p, q]], axis=1)
        match = (evs == want[:, None, :]).all(axis=2)
        if not match.any(axis=1).all():
            raise RuntimeError("element edge not found")
        return eledges[np.arange(ne), match.argmax(axis=1)]

    interior = [i for i, n in enumerate(nodes) if min(n) > 0]
    base = nv + len(ev) * (k - 1)
    g = np.empty((ne, len(nodes)), dtype=np.int64)
    for i, w in enumerate(nodes):
        nonzero = [l for l in range(3) if w[l] > 0]
        if len(nonzero) == 1:
            g[:, i] = tri[:, nonzero[0]]
        elif len(nonzero) == 2:
            p, q = nonzero
            m = np.where(tri[:, p] < tri[:, q], w[q], w[p])
            g[:, i] = nv + edge_of(p, q) * (k - 1) + (m - 1)
        else:
            g[:, i] = base + np.arange(ne) * len(interior) + interior.index(i)
    return g, base + ne * len(interior)


def _crossings(pairs, coords, vals):
    va, vb = vals[pairs[:, 0]], vals[pairs[:, 1]]
    t = (va / (va - vb))[:, None]
    return coords[pairs[:, 0]] + t * (coords[pairs[:, 1]] - coords[pairs[:, 0]])


def _segments(T, coords, vals):
    """one line per cut triangle between the zero crossings on its edges, every crossing computed once"""
    s = vals[T] >= 0.0
    count = s.sum(axis=1)
    cut = (count == 1) | (count == 2)
    T, s, count = T[cut], s[cut], count[cut]
    odd = (s == (count == 1)[:, None]).argmax(axis=1)
    rows = np.arange(len(T))
    a, b, c = T[rows, odd], T[rows, (odd + 1) % 3], T[rows, (odd + 2) % 3]
    pairs = np.sort(np.concatenate([np.stack([a, b], 1), np.stack([a, c], 1)]), axis=1)
    uniq, inv = np.unique(pairs, axis=0, return_inverse=True)
    inv = inv.ravel()
    X = _crossings(uniq, coords, vals)
    i1, i2 = inv[:len(T)], inv[len(T):]
    keep = i1 != i2
    return X[i1[keep]], X[i2[keep]], {tuple(p): i for i, p in enumerate(uniq)}, X


def _boundary_pieces(mesh, k, nv, coords, vals, crossing, X, renamed):
    """the parts of the mesh boundary where the level set is inside, cut at its zero crossings; renamed: the names of
    boundary elements (by their sorted vertices) that differ from the mesh's"""
    from ngsolve import BND
    pieces, cuts = [], {}
    for b in mesh.Elements(BND):
        e = b.edges[0].nr
        v0, v1 = sorted(v.nr for v in b.vertices)
        name = renamed.get((v0, v1), b.mat)
        chain = [v0] + [nv + e * (k - 1) + m for m in range(k - 1)] + [v1]
        for n0, n1 in zip(chain, chain[1:]):
            s0, s1 = vals[n0] >= 0.0, vals[n1] >= 0.0
            if s0 and s1:
                pieces.append((coords[n0], coords[n1], name))
            elif s0 or s1:
                key = (min(n0, n1), max(n0, n1))
                xc = X[crossing[key]] if key in crossing else _crossings(np.array([key]), coords, vals)[0]
                pieces.append((coords[n0 if s0 else n1], xc, name))
                cuts.setdefault((v0, v1), []).append(xc)
    return pieces, cuts


def _periodic_maps(ngmesh):
    """vertex map of every periodic identification of the mesh (netgen counts its points from 1)"""
    from netgen.meshing import IdentificationType
    maps = {}
    for p1, p2, nr in ngmesh.GetIdentifications():
        if ngmesh.GetIdentificationType(nr) == IdentificationType.PERIODIC:
            maps.setdefault(nr, {})[p1.nr - 1] = p2.nr - 1
    return maps


def _boundary_pairs(mesh, maps):
    """pairs of identified boundary elements: (vertices of a, vertices of b, name of a, name of b, identification), a's
    vertices mapped to b's in this order"""
    from ngsolve import BND
    names = {tuple(sorted(v.nr for v in b.vertices)): b.mat for b in mesh.Elements(BND)}
    pairs = []
    for nr, vmap in maps.items():
        for (u, v), name in names.items():
            if u in vmap and v in vmap and tuple(sorted((vmap[u], vmap[v]))) in names:
                pairs.append(((u, v), (vmap[u], vmap[v]), name, names[tuple(sorted((vmap[u], vmap[v])))], nr))
    return pairs


def _side_shift(coords, side):
    """the translation of an identified side pair: the mean shift of its identified vertices"""
    images = {va[i]: vb[i] for va, vb, *_ in side for i in (0, 1)}
    return np.mean([coords[b] - coords[a] for a, b in images.items()], axis=0)


def _sides(coords, pairs):
    """the identified boundary elements of every side pair (one per periodic identification of the mesh) with its
    translation"""
    grouped = {}
    for pair in pairs:
        grouped.setdefault(pair[4], []).append(pair)
    return [(side, _side_shift(coords, side)) for _, side in sorted(grouped.items())]


def _side_names(sides):
    """names for the sides whose boundary names do not tell them apart (a name on both sides of an identification or in
    two of them): name.periodic.ka and name.periodic.kb for the k-th identification, with a warning. Returns the new
    names of boundary elements by their sorted vertices, and per side pair its (name of a, name of b) pairs"""
    sets = [{pair[2] for pair in side} for side, _ in sides] + [{pair[3] for pair in side} for side, _ in sides]
    shared = {n for n in set().union(*sets) if sum(n in s for s in sets) > 1} if sets else set()
    renamed, names = {}, []
    for k, (side, _) in enumerate(sides, 1):
        if not any(pair[2] in shared or pair[3] in shared for pair in side):
            names.append(sorted({pair[2:4] for pair in side}))
            continue
        pairs = set()
        for va, vb, na, nb, _ in side:
            a, b = f"{na}.periodic.{k}a", f"{nb}.periodic.{k}b"
            renamed[tuple(sorted(va))], renamed[tuple(sorted(vb))] = a, b
            pairs.add((a, b))
        names.append(sorted(pairs))
        warnings.warn(f"FromLevelSet: the sides of periodic identification {k} share their boundary names with other "
                      f"sides; they are named {', '.join(f'{a!r} and {b!r}' for a, b in sorted(pairs))}",
                      px.PlanexWarning, stacklevel=3)
    return renamed, names


def _side_crossings(coords, cuts, side):
    """crossings on the a side of an identified side pair and those of the b side, both ordered along the side"""
    mine, partner = [], []
    for va, vb, *_ in side:
        mine += cuts.get(tuple(sorted(va)), [])
        partner += cuts.get(tuple(sorted(vb)), [])
    along = np.linalg.svd(coords[[v for va, *_ in side for v in va]] - coords[side[0][0][0]])[2][0]
    return sorted(mine, key=lambda c: c @ along), sorted(partner, key=lambda c: c @ along)


def _periodic_targets(coords, cuts, sides, names):
    """common position of every crossing and its partner: m, the mean of the a crossing and the b crossing shifted
    back by the translation, and m + translation"""
    targets = {}
    for (side, shift), pairs in zip(sides, names):
        mine, partner = _side_crossings(coords, cuts, side)
        if len(mine) != len(partner):
            na, nb = ", ".join(a for a, _ in pairs), ", ".join(b for _, b in pairs)
            warnings.warn(f"FromLevelSet: {len(mine)} crossing(s) on the periodic side '{na}', {len(partner)} on '{nb}'; "
                          "they stay unmatched", px.PlanexWarning, stacklevel=4)
            continue
        for xa, xb in zip(mine, partner):
            middle = 0.5 * (xa + (xb - shift))
            targets[tuple(xa)], targets[tuple(xb)] = middle, middle + shift
    return targets


def _snapped(segments, targets, eps):
    """the segments with every end that lies within eps of a crossing moved to that crossing's target"""
    keys = np.array(list(targets)).reshape(-1, 2)
    values = np.array(list(targets.values())).reshape(-1, 2)

    def snap(p):
        near = (np.abs(keys - p).max(axis=1) < eps)
        return values[near.argmax()] if near.any() else p

    return [(snap(p), snap(q), nm) for p, q, nm in segments]


def _identify(shape, sides, names):
    """the periodic identification of every pair of boundary names that the shape contains"""
    present = {e.name for e in shape.edges}
    wanted = [((na, nb), shift) for (_, shift), pairs in zip(sides, names) for na, nb in pairs
              if na in present and nb in present]
    for k, ((na, nb), d) in enumerate(wanted, 1):
        shape.Identify(na, nb, px.Translation((float(d[0]), float(d[1]))), name=f"periodic.{k}")


def _face(segments, plane):
    lines = []
    for p, q, nm in segments:
        p, q = (float(p[0]), float(p[1])), (float(q[0]), float(q[1]))
        if p == q:
            continue
        w = px.Line(p, q, plane=plane)
        w.name = nm
        lines.append(w)
    if not lines:
        warnings.warn("level set has no inside region")
        return px.Shape()
    return px._RegionOfEdges(lines)


def _resample(pts, n):
    s = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    t = np.linspace(0.0, s[-1], n + 1)
    out = np.column_stack([np.interp(t, s, pts[:, 0]), np.interp(t, s, pts[:, 1])])
    out[0], out[-1] = pts[0], pts[-1]
    return out


def _project(gf, mesh, pts, ftol, sign, dirs=None, maxstep=None):
    """Newton steps of the points onto the zero level set, along the gradient or along the given unit directions"""
    from ngsolve import grad
    pts = pts.copy()
    active = np.ones(len(pts), bool)
    for _ in range(NEWTON_STEPS):
        idx = np.flatnonzero(active)
        if len(idx) == 0:
            break
        mips = mesh(pts[idx, 0], pts[idx, 1])
        found = mips["nr"] >= 0
        idx, mips = idx[found], mips[found]
        f = sign * np.asarray(gf(mips)).reshape(-1)
        g = sign * np.asarray(grad(gf)(mips)).reshape(-1, 2)
        if dirs is None:
            denom = (g * g).sum(axis=1)
            step = (f / np.where(denom > 0.0, denom, np.inf))[:, None] * g
        else:
            d = dirs[idx]
            gd = (g * d).sum(axis=1)
            step = (f / np.where(gd != 0.0, gd, np.inf))[:, None] * d
        if maxstep is not None:
            length = np.linalg.norm(step, axis=1)
            over = length > maxstep[idx]
            step[over] *= (maxstep[idx][over] / length[over])[:, None]
        pts[idx] -= step
        active[:] = False
        active[idx] = np.abs(f) > ftol
    return pts


def _loop_points(loop, eps):
    """points of the loop in order and the name of every segment between them"""
    chain = oriented([e.edge for e in loop.edges], eps)
    pts = []
    for e, rev in chain:
        a, b = (e.end, e.start) if rev else (e.start, e.end)
        if not pts:
            pts.append((a.x, a.y))
        pts.append((b.x, b.y))
    return np.array(pts), [e.name for e in loop.edges]


def _near(p, fixed, eps):
    return bool((np.abs(fixed - p).max(axis=1) < eps).any())


def _refined(poly, gf, mesh, edge_length, name, ftol, sign, project, plane, fixed):
    """interface stretches resampled to edge_length and projected onto the level set, boundary pieces kept"""
    eps = 1e-9 * scale([e.edge for e in poly.edges])
    segments = []
    corners = {}
    for loop in poly.wires:
        pts, names = _loop_points(loop, eps)
        on_interface = [nm == name for nm in names]
        if all(on_interface):
            n = max(3, round(_length(pts) / edge_length))
            new = _resample(pts, n)
            if project:
                new[:-1] = _project(gf, mesh, new[:-1], ftol, sign)
                new[-1] = new[0]
            segments.extend((new[i], new[i + 1], name) for i in range(n))
            continue
        # start the walk on a boundary piece so every interface run has boundary pieces on both sides
        j = on_interface.index(False)
        m = len(names)
        order = [(j + i) % m for i in range(m)]
        pts = np.concatenate([pts[:-1][order], pts[:-1][order][:1]])
        names, on_interface = [names[i] for i in order], [on_interface[i] for i in order]
        runs = []
        i = 0
        while i < m:
            if on_interface[i]:
                b = i
                while i < m and on_interface[i]:
                    i += 1
                runs.append((b, i))
            else:
                i += 1
        for b, e in runs:
            run = pts[b:e + 1]
            new = _resample(run, max(1, round(_length(run) / edge_length)))
            if project:
                if len(new) > 2:
                    new[1:-1] = _project(gf, mesh, new[1:-1], ftol, sign)
                # the ends slide along their boundary pieces, at most to the pieces' far ends
                before, after = pts[b - 1] - pts[b], pts[(e + 1) % m] - pts[e]
                dirs = np.array([before, after])
                lengths = np.linalg.norm(dirs, axis=1)
                dirs /= lengths[:, None]
                ends = _project(gf, mesh, np.array([new[0], new[-1]]), ftol, sign, dirs, lengths)
                keep = np.array([_near(new[0], fixed, eps), _near(new[-1], fixed, eps)])
                ends[keep] = np.array([new[0], new[-1]])[keep]
                corners[tuple(new[0])], corners[tuple(new[-1])] = ends[0], ends[-1]
                new[0], new[-1] = ends
            segments.extend((new[i], new[i + 1], name) for i in range(len(new) - 1))
        for i in range(m):
            if not on_interface[i]:
                p, q = pts[i], pts[i + 1]
                segments.append((corners.get(tuple(p), p), corners.get(tuple(q), q), names[i]))
    return _face(segments, plane)


def _length(pts):
    return float(np.linalg.norm(np.diff(pts, axis=0), axis=1).sum())


def FromLevelSet(field: Any, /, edge_length: float | None, inside: str = "positive", name: str = "interface",
                 project: bool = True, subdivide: int | None = None, plane: Plane | None = None,
                 periodic: bool = True) -> Shape:
    """The region where an NGSolve level set function is positive (or negative) as a Shape of lines.

    The level set is cut on a lattice of subdivide steps per element; the interface is then resampled to lines about
    edge_length long. Where the region reaches the mesh boundary, the boundary is part of the polygon with its
    boundary names.

    Args:
        field: the level set function, a scalar GridFunction on a 2d triangular mesh.
        edge_length: the length of the interface lines; None keeps the lattice resolution.
        inside: 'positive' or 'negative': the sign of the field inside the region.
        name: the name of the interface edges.
        project: True moves the interface points onto the exact zero level set by Newton steps.
        subdivide: the lattice steps per element; None takes the polynomial order of the field's space.
        plane: the plane of the shape; None is the xy plane.
        periodic: True takes the periodic identifications of the mesh (field.space.mesh.ngmesh): a crossing on an
            identified boundary edge and its partner on the partner edge get the mean of their parameters along the
            edges, so the two sides match; a crossing without a partner warns (PlanexWarning) and stays. The shape
            carries the identifications between the boundary names ('periodic.1', ...) as translations. Sides whose
            names do not tell them apart (one name on several sides) are named name.periodic.ka and name.periodic.kb
            for the k-th identification, with a PlanexWarning. False ignores them.

    Raises:
        ValueError: inside is neither 'positive' nor 'negative', or the mesh is not 2d.
        NotImplementedError: the mesh is not triangular.

    Examples:
        px.FromLevelSet(gf, 0.01)
        px.FromLevelSet(gf, None, inside="negative", name="bubble", subdivide=4)
        px.FromLevelSet(gf, 0.01, periodic=False)
    """
    from ngsolve import CF, VOL, IntegrationRule, x, y
    plane = px.AxesOf(plane, "FromLevelSet")
    if inside not in ("positive", "negative"):
        raise ValueError("inside must be 'positive' or 'negative'")
    mesh = field.space.mesh
    if mesh.dim != 2:
        raise ValueError("FromLevelSet needs a 2d mesh")
    tri = np.array([[v.nr for v in el.vertices] for el in mesh.Elements(VOL)])
    if tri.shape[1:] != (3,):
        raise NotImplementedError("FromLevelSet needs a triangular mesh")
    k = subdivide or max(1, field.space.globalorder)
    nodes, tris = _lattice(k)
    ir = IntegrationRule(points=[(a / k, b / k) for a, b, c in nodes], weights=[1.0 / len(nodes)] * len(nodes))
    mips = mesh.MapToAllElements(ir, VOL)
    xy = np.asarray(CF((x, y))(mips)).reshape(-1, 2)
    vals = np.asarray(field(mips)).reshape(-1)
    if inside == "negative":
        vals = -vals
    g, nnodes = _global_nodes(mesh, tri, nodes, k)
    coords = np.empty((nnodes, 2))
    node_vals = np.empty(nnodes)
    coords[g.ravel()] = xy
    node_vals[g.ravel()] = vals
    T = g[:, np.array(tris)].reshape(-1, 3)
    P, Q, crossing, X = _segments(T, coords, node_vals)
    segments = [(p, q, name) for p, q in zip(P, Q)]
    pairs = _boundary_pairs(mesh, _periodic_maps(mesh.ngmesh)) if periodic else []
    sides = _sides(coords, pairs)
    renamed, names = _side_names(sides)
    pieces, cuts = _boundary_pieces(mesh, k, mesh.nv, coords, node_vals, crossing, X, renamed)
    segments += pieces
    targets = _periodic_targets(coords, cuts, sides, names)
    segments = _snapped(segments, targets, 1e-9 * float(np.ptp(coords, axis=0).max()))
    poly = _face(segments, plane)
    if edge_length is None or len(poly.edges) == 0:
        _identify(poly, sides, names)
        return poly
    ftol = 1e-10 * float(np.abs(node_vals).max())
    sign = -1.0 if inside == "negative" else 1.0
    fixed = np.array(list(targets.values())).reshape(-1, 2)
    shape = _refined(poly, field, mesh, edge_length, name, ftol, sign, project and ftol > 0.0, plane, fixed)
    _identify(shape, sides, names)
    return shape
