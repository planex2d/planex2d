# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""px.Intersect3d of an OCC shape or of a mesh (NGSolve, dolfinx)"""
from __future__ import annotations

import sys
import warnings
from typing import TYPE_CHECKING, Any

from . import _planex as px
from ._geom import read_name
from ._plot_fields import _Dolfinx, _NGSolve

# a mesh vertex this close to the plane, relative to the mesh's size, lies on it
ON_PLANE = 1e-9

if TYPE_CHECKING:
    from ._planex import Shape
    from .types import Plane


def Intersect3d(plane: Plane | None, shape: Any, /) -> Shape:
    """Where the plane meets an OCC shape or a 3d mesh, as a Shape in the plane's coordinates.

    An OCC shape gives the cross-sections of its solids as faces with the solids' names and properties (several solids
    glued as by px.Glue, an overlap a face of its own; a face of a solid lying in the plane belongs to its
    cross-section), its other faces as faces where they lie in the plane, else as the wires where they meet it, and its
    edges on no face as the stretches lying in the plane; every edge is named after the face or edge it lies on. The
    points where such edges cross or touch the plane are left out with a warning; a sketch's Intersect3d keeps them.

    A mesh is cut at its boundary facets as they are, straight (a curved boundary gives a polygon): one face per
    material (NGSolve's materials, the cell tags' physical groups; a bare dolfinx mesh is one region) named after it,
    glued, a face of it lying in the plane included; every edge is named after the boundary of its facet (NGSolve's
    boundary names, the facet tags' physical groups). NGSolve's name "default" counts as no name. A plane that misses
    the shape gives an empty Shape and a warning.

    Args:
        plane: the cutting plane: px.Plane, occ.Axes, a sketch, a planar face or shape, a compound on one plane; None
            is the xy plane.
        shape: an OCC shape, an ngsolve.Mesh, a serial dolfinx.mesh.Mesh of tetrahedra or hexahedra, or the MeshData
            of dolfinx.io.gmsh (read_from_msh, model_to_mesh).

    Raises:
        ValueError: the boundary of a region of the mesh is not watertight (an edge belongs to one boundary facet only),
            the mesh is not 3d or has no volume elements, a dolfinx mesh is distributed over several MPI
            processes or has other cells.

    Examples:
        px.Intersect3d(px.XY, occ_shape)
        px.Intersect3d(px.XY.Offset(0.5), mesh)
    """
    boundary = _boundary(shape)
    if boundary is None:
        return px.Intersect3d(plane, shape)
    import numpy as np

    points, tris, names, regions = boundary
    frame = px.Plane(plane)
    o, u, v, n = (np.asarray(a, dtype=float) for a in (frame.origin, frame.h, frame.v, frame.normal))
    rel = points - o
    d = rel @ n
    d[np.abs(d) <= ON_PLANE * np.ptp(points, axis=0).max()] = 0.0
    uv = np.c_[rel @ u, rel @ v]
    parts = []
    for name, rows in regions:
        _require_watertight(name, tris[rows])
        part = _region(frame, uv, d, tris[rows], names[rows])
        if part is not None:
            if name is not None:
                part.name = read_name(name)
            parts.append(part)
    if not parts:
        warnings.warn("Intersect3d: the plane misses the shape or only touches it, the result is empty",
                      px.PlanexWarning, stacklevel=2)
        return px._EmptyShape(frame)
    return parts[0] if len(parts) == 1 else px.Glue(parts)


def _boundary(shape):
    """a mesh's boundary facets as triangles: the points (n, 3), the triangles (m, 3) as point numbers, the name of
    each (None for none) and per region its name and the rows of the triangles bounding it; None for anything else"""
    if _NGSolve.is_mesh(shape):
        return _ngsolve_boundary(shape)
    if _Dolfinx.is_mesh(shape):
        return _dolfinx_boundary(shape, None, None, {})
    gmsh = sys.modules.get("dolfinx.io.gmsh")
    if gmsh is not None and isinstance(shape, gmsh.MeshData):
        return _dolfinx_boundary(shape.mesh, shape.cell_tags, shape.facet_tags, shape.physical_groups)
    return None


def _name(name):
    return None if not name or name == "default" else name


def _ngsolve_boundary(mesh):
    import numpy as np

    if mesh.dim != 3:
        raise ValueError(f"Intersect3d: a plane cuts a 3d mesh with volume elements, this one has dim {mesh.dim}")
    if mesh.ne == 0:
        raise ValueError("Intersect3d: a plane cuts a 3d mesh with volume elements, this one has none (a surface mesh)")
    ngmesh = mesh.ngmesh
    elements = ngmesh.Elements2D().NumPy()
    nodes = elements["nodes"].astype(np.int64) - 1
    # the nodes of an element, unused slots 0 (netgen counts from 1): a triangle 3 or 6, a quad 4 or 8; netgen 6.2.2608
    # no longer has the field "np"
    quad = np.isin((elements["nodes"] > 0).sum(axis=1), (4, 8))
    tris = np.concatenate([nodes[~quad][:, :3], nodes[quad][:, [0, 1, 2]], nodes[quad][:, [0, 2, 3]]])
    index = elements["index"][np.concatenate([np.flatnonzero(~quad), np.flatnonzero(quad), np.flatnonzero(quad)])]
    faces = {i: ngmesh.FaceDescriptor(int(i)) for i in np.unique(index)}
    names = np.array([_name(faces[i].bcname) for i in index], dtype=object)
    domin = np.array([faces[i].domin for i in index])
    domout = np.array([faces[i].domout for i in index])
    regions = [(_name(m), np.flatnonzero((domin == k) != (domout == k))) for k, m in enumerate(mesh.GetMaterials(), 1)]
    return np.asarray(ngmesh.Coordinates(), dtype=float), tris, names, regions


def _dolfinx_boundary(mesh, cell_tags, facet_tags, groups):
    import numpy as np
    from dolfinx.mesh import entities_to_geometry

    if mesh.comm.size > 1:
        raise ValueError(f"Intersect3d: this dolfinx mesh is distributed over {mesh.comm.size} MPI processes, "
                         "Intersect3d cuts serial meshes only")
    topology = mesh.topology
    tdim, cell = topology.dim, topology.cell_type.name
    if tdim != 3:
        raise ValueError(f"Intersect3d: a plane cuts a 3d mesh with volume elements, this one has dim {tdim}")
    if cell not in ("tetrahedron", "hexahedron"):
        raise ValueError(f"Intersect3d: dolfinx meshes of tetrahedra or hexahedra, not {cell}")
    named = {(g.dim, g.tag): name for name, g in groups.items()}
    topology.create_connectivity(tdim - 1, tdim)
    adjacency = topology.connectivity(tdim - 1, tdim)
    offsets, cells = np.asarray(adjacency.offsets), np.asarray(adjacency.array)
    tag = np.full(topology.index_map(tdim).size_local, -1)
    if cell_tags is not None:
        tag[cell_tags.indices] = cell_tags.values
    regions = []
    for value in np.unique(tag):
        count = np.add.reduceat((tag[cells] == value).astype(np.int64), offsets[:-1])
        regions.append((named.get((tdim, int(value))) if value >= 0 else None, np.flatnonzero(count == 1)))
    facets = np.unique(np.concatenate([f for _, f in regions])).astype(np.int32)
    nodes = np.asarray(entities_to_geometry(mesh, tdim - 1, facets))
    # dolfinx numbers a quadrilateral's vertices as a tensor grid: 0, 1, 3, 2 around it
    split = [[0, 1, 2]] if cell == "tetrahedron" else [[0, 1, 3], [0, 3, 2]]
    tris = np.concatenate([nodes[:, corners] for corners in split])
    facet_name = np.full(topology.index_map(tdim - 1).size_local, None, dtype=object)
    if facet_tags is not None:
        facet_name[facet_tags.indices] = [named.get((tdim - 1, int(value))) for value in facet_tags.values]
    names = np.tile(facet_name[facets], len(split))
    rows = [(name, (np.searchsorted(facets, f) + len(facets) * np.arange(len(split))[:, None]).ravel())
            for name, f in regions]
    return np.asarray(mesh.geometry.x, dtype=float), tris, names, rows


def _require_watertight(name, tris):
    """raises ValueError if an edge of the triangles bounding a region belongs to an odd number of them: the region's
    boundary has a hole, and the cut of it would be wrong or empty"""
    import numpy as np

    edges = np.sort(np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]]), axis=1)
    _, count = np.unique(edges, axis=0, return_counts=True)
    open_edges = int((count % 2 == 1).sum())
    if open_edges:
        raise ValueError(f"Intersect3d: the boundary of the region {name!r} is not watertight ({open_edges} edges "
                         "belong to one boundary facet only); close the mesh boundary, a plane cuts closed regions")


def _crossing(uv, d, a, b):
    """where the edges from points a to points b cross the plane, each computed from its lower point number, so that
    the triangles sharing an edge get the same point"""
    import numpy as np

    lo, hi = np.minimum(a, b), np.maximum(a, b)
    t = (d[lo] / (d[lo] - d[hi]))[:, None]
    return (1.0 - t) * uv[lo] + t * uv[hi]


def _segments(uv, d, tris, up):
    """where the plane cuts the triangles, as segments from points to points in plane coordinates, and the row of the
    triangle of each; up: per point whether it lies above the plane"""
    import numpy as np

    above = up[tris]
    count = above.sum(axis=1)
    rows = np.flatnonzero((count == 1) | (count == 2))
    alone = (above[rows] == (count[rows] == 1)[:, None]).argmax(axis=1)
    a, b, c = (tris[rows, (alone + k) % 3] for k in range(3))
    return _crossing(uv, d, a, b), _crossing(uv, d, a, c), rows


def _region(frame, uv, d, tris, names):
    """the faces the plane cuts from the region the triangles bound, None if none: a point on the plane counts once
    above and once below it, and the two cuts are united, so a face lying in the plane belongs to it"""
    out = None
    for up in [d >= 0.0] + ([d > 0.0] if (d[tris] == 0.0).any() else []):
        start, end, rows = _segments(uv, d, tris, up)
        lines = []
        for p, q, name in zip(start.tolist(), end.tolist(), names[rows]):
            if p != q:
                line = px.Line(tuple(p), tuple(q), plane=frame)
                if name is not None:
                    line.name = read_name(name)
                lines.append(line)
        if lines:
            part = px._RegionOfEdges(lines)
            if len(part.faces):
                out = part if out is None else out + part
    return out
