# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import sys

from . import _planex as px
from ._plot_fields import _checked_mesh, _dim, _field_library, _mesh_library, _once

# a field on a mesh: pixels and arrows along the longer side of the faces' box
FIELD_PIXELS, FIELD_ARROWS = 400, 20


class _MeshRegion:
    """a mesh without an OCC geometry as the region of a plot: its extent (of a 3d mesh: of its cut at the plane),
    every point inside (the mesh's own search leaves out the rest), its boundary as the outline"""

    def __init__(self, mesh, plane):
        import numpy as np

        lib = _mesh_library(mesh)
        self.plane = px.XY if plane is None else px.Plane(plane)
        if _dim(mesh) == 2:
            lines, self.outline = lib.lines(mesh), lib.boundary(mesh)
        else:
            lines = _cut_lines(mesh, self.plane)
            self.outline = _once(lines)
        if not len(lines):
            raise ValueError("Plot: the plane does not cut the mesh")
        pts = np.asarray(lines, dtype=float).reshape(-1, 2)
        self.bounding_box = (tuple(pts.min(axis=0)), tuple(pts.max(axis=0)))

    def Contains(self, pts):
        import numpy as np

        return np.ones(len(pts), dtype=bool)


def _region(obj):
    if hasattr(obj, "Contains"):
        return obj
    if not hasattr(obj, "Shape"):
        raise TypeError("a field needs an area: a Face, Shape or sketch, not a %s" % type(obj).__name__)
    return obj.Shape()


def _plane_of(obj):
    return obj.plane if isinstance(obj, _MeshRegion) else px.Plane(obj)


def _on_xy(obj):
    p = _plane_of(obj)
    return p.origin == (0.0, 0.0, 0.0) and p.h == (1.0, 0.0, 0.0) and p.v == (0.0, 1.0, 0.0)


def _is_occ(obj):
    """a netgen OCC shape (a face, a solid, a compound, ...); netgen is loaded with planex"""
    occ = sys.modules.get("netgen.occ")
    return occ is not None and isinstance(obj, occ.TopoDS_Shape)


def _flat(shape):
    """a flat OCC shape as a planex Shape on its plane (px.FromOCC); anything else raises with the way to a section"""
    from ._io import FromOCC

    try:
        return FromOCC(shape)
    except ValueError as e:
        raise ValueError(f"Plot: an OCC shape is drawn when it is flat, or cut at the axes given: px.Plot(shape, "
                         f"px.XY); {e}") from None


def _is_plane(obj):
    """a px.Plane or netgen's occ.Axes: a plane without bounds"""
    return isinstance(obj, px.Plane) or (type(obj).__name__ == "Axes" and type(obj).__module__.startswith("netgen"))


def _mesh_cut(plane, mesh):
    """what the plane cuts from the OCC geometry the mesh was made from, as px.Intersect3d; a mesh without one is its
    own region"""
    shape = _mesh_library(mesh).geometry(mesh)
    if shape is not None:
        return px.Intersect3d(plane, shape)
    if _dim(mesh) == 2 and not _on_xy(plane):
        raise ValueError("Plot: a 2d mesh lies on the XY plane, this plane is another one")
    return _MeshRegion(mesh, plane)


def _mesh_region(mesh):
    """the region of a field or mesh plot without an object: a 2d mesh's geometry, the XY cut of a 3d mesh's"""
    if _dim(mesh) == 3:
        return _mesh_cut(px.XY, mesh)
    shape = _mesh_library(mesh).geometry(mesh)
    return _MeshRegion(mesh, None) if shape is None else _flat(shape)


def _field_mesh(f, mesh):
    """the mesh to evaluate f on: a GridFunction's or dolfinx Function's own, else the mesh given"""
    lib = _field_library(f)
    own = lib.own_mesh(f)
    if own is not None:
        if mesh is not None and mesh is not own:
            raise ValueError("Plot: a GridFunction or dolfinx Function is drawn on its own mesh (gf.space.mesh, "
                             "u.function_space.mesh), the mesh given is another one")
        return _checked_mesh(own)
    if mesh is None:
        raise ValueError("Plot: this CoefficientFunction has no mesh of its own, give the mesh to evaluate it on: "
                         "px.PlotField(cf, mesh)")
    if _mesh_library(mesh) is not lib:
        raise ValueError(f"Plot: an NGSolve field is evaluated on an ngsolve.Mesh, the mesh given is a {type(mesh).__name__}")
    return _checked_mesh(mesh)


def _pixels(box, n):
    """the centres of square pixels over the box, n along its longer side, and the pixel size"""
    import numpy as np

    (x0, y0), (x1, y1) = box
    longer = max(x1 - x0, y1 - y0)
    step = longer / n
    centres = []
    for lo, hi in ((x0, x1), (y0, y1)):
        k = n if hi - lo == longer else max(1, math.ceil((hi - lo) / step))
        centres.append(0.5 * (lo + hi) + (np.arange(k) - 0.5 * (k - 1)) * step)
    return centres[0], centres[1], step


def _xyz(plane, pts):
    """the (n, 2) points (h, v) of the plane in 3d, as its ToXYZ"""
    import numpy as np

    o, u, v = (np.asarray(a, dtype=float) for a in (plane.origin, plane.h, plane.v))
    return o + pts[:, :1] * u + pts[:, 1:] * v


def _in_plane(values, plane):
    """(n, 2) vectors in the plane: 2d ones as they are, of 3d ones (plane given) their components along h and v"""
    import numpy as np

    return values if plane is None else values @ np.asarray((plane.h, plane.v), dtype=float).T


def _at(f, mesh, pts, plane):
    """f at the (n, 2) points as an (n, value size) array, NaN where the mesh has no element; on a 3d mesh at their 3d
    points on the plane"""
    import numpy as np

    lib = _field_library(f)
    size = lib.size(f)
    values = np.full((len(pts), size), np.nan, dtype=complex if lib.is_complex(f) else float)
    if len(pts):
        inside, where = lib.locate(mesh, pts if _dim(mesh) == 2 else _xyz(plane, pts))
        if inside.any():
            values[inside] = lib.evaluate(f, where).reshape(-1, size)
    return values


def _magnitude(values):
    """a real scalar as it is, a complex one as its modulus, a vector as its Euclidean norm"""
    import numpy as np

    if values.shape[1] == 1:
        return np.abs(values[:, 0]) if np.iscomplexobj(values) else values[:, 0]
    return np.linalg.norm(values, axis=1)


def _grid(region, n):
    """the pixel centres over the faces' box (n along its longer side) as xs, ys, the (m, 2) array of those inside
    the faces and their index into the grid, and the pixel size"""
    import numpy as np

    box = region.bounding_box
    if box is None:
        raise ValueError("Plot: no faces to colour")
    xs, ys, step = _pixels(box, n)
    gx, gy = np.meshgrid(xs, ys)
    pts = np.c_[gx.ravel(), gy.ravel()]
    inside = np.flatnonzero(region.Contains(pts))
    return xs, ys, pts[inside], inside, step


def _field_raster(region, f, mesh, plane, n, flow):
    """xs, ys and the field there (as _magnitude), NaN outside the faces and the mesh; with flow also the two
    components of its part in the plane there"""
    import numpy as np

    xs, ys, pts, inside, _ = _grid(region, n)
    values = _at(f, mesh, pts, plane)
    grids = []
    for part in [_magnitude(values)] + (list(_in_plane(values, plane).T) if flow else []):
        z = np.full(len(xs) * len(ys), np.nan)
        z[inside] = part
        grids.append(z.reshape(len(ys), len(xs)))
    return (xs, ys, *grids)


def _arrows(region, f, mesh, plane):
    """the field's arrows (its part in the plane) at the centres of a coarse grid inside the faces and the mesh, the
    longest one cell long"""
    import numpy as np

    _, _, pts, _, step = _grid(region, FIELD_ARROWS)
    uv = _in_plane(_at(f, mesh, pts, plane), plane)
    found = ~np.isnan(uv[:, 0])
    pts, uv = pts[found], uv[found]
    longest = np.linalg.norm(uv, axis=1).max(initial=0.0)
    return pts, (uv * (step / longest) if longest > 0.0 else uv)


def _mesh_field(obj, f, mesh, plane, samples, vectors, flow):
    """the data a field on a mesh draws on the faces: a raster of pixels, samples (default FIELD_PIXELS) along the
    longer side of their box; with vectors its arrows, with flow its part in the plane on the same pixels (for the
    field lines). plane: the object's plane on a 3d mesh, else None"""
    n = FIELD_PIXELS if samples is None else samples
    xs, ys, z, *parts = _field_raster(_region(obj), f, mesh, plane, n, flow)
    data = {"raster": (xs, ys, z)}
    if flow:
        data["flow"] = (xs, ys, *parts)
    if vectors:
        data["arrows"] = _arrows(_region(obj), f, mesh, plane)
    return data


def _section(corners, edges, plane, tol):
    """where the plane cuts elements given by their (e, k, 3) corners and their edges as (j, 2) corner indices, as
    (m, 2, 2) segments in plane coordinates: per element the polygon through its corners on the plane (closer than
    tol) and the points where its edges cross it, in order around their centre; two points give one line, one none"""
    import numpy as np

    o, u, v, n = (np.asarray(a, dtype=float) for a in (plane.origin, plane.h, plane.v, plane.normal))
    p = corners - o
    d = p @ n
    side = np.where(np.abs(d) <= tol, 0.0, np.sign(d))
    near = (side.min(axis=1) <= 0.0) & (side.max(axis=1) >= 0.0)
    p, d, side = p[near], d[near], side[near]
    uv = np.stack([p @ u, p @ v], axis=-1)
    a, b = np.asarray(edges).T
    crossing = side[:, a] * side[:, b] < 0.0
    t = np.where(crossing, d[:, a] / np.where(crossing, d[:, a] - d[:, b], 1.0), 0.0)
    pts = np.concatenate([uv[:, a] + t[..., None] * (uv[:, b] - uv[:, a]), uv], axis=1)
    valid = np.concatenate([crossing, side == 0.0], axis=1)
    count = valid.sum(axis=1)
    centre = np.where(valid[..., None], pts, 0.0).sum(axis=1) / np.maximum(count, 1)[:, None]
    angle = np.arctan2(pts[..., 1] - centre[:, None, 1], pts[..., 0] - centre[:, None, 0])
    pts = np.take_along_axis(pts, np.argsort(np.where(valid, angle, np.inf), axis=1)[..., None], axis=1)
    i, k = np.arange(count.max(initial=0)), count[:, None]
    keep = (i < k) & (k > 1) & ((k > 2) | (i == 0))
    following = np.take_along_axis(pts, np.where(i + 1 < k, i + 1, 0)[..., None], axis=1)
    return np.stack([pts[:, :len(i)][keep], following[keep]], axis=1)


def _cut_lines(mesh, plane):
    """where the plane cuts the volume elements of a 3d mesh, as (m, 2, 2) segments in plane coordinates (curved
    elements cut straight)"""
    import numpy as np

    kinds = _mesh_library(mesh).corners(mesh)
    # a corner this close to the plane, relative to the mesh's size, lies on it: points that fall together count once
    tol = 1e-9 * np.ptp(np.concatenate([corners.reshape(-1, 3) for corners, _ in kinds]), axis=0).max()
    return np.concatenate([np.empty((0, 2, 2))] + [_section(corners, edges, plane, tol) for corners, edges in kinds])
