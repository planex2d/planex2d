# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import sys

# points along a curved element edge
CURVED_EDGE_POINTS = 8
# how far outside an element a point still counts as in it, in the element's reference coordinates (so relative to
# its size): enough for rounding on shared faces; NGSolve's default 1e-4 also finds points outside the mesh. Its 2d
# search ignores it and keeps a fixed 1e-6. dolfinx's search finds points up to 1e-6 absolute outside a cell (a curved
# one: outside its nodes' convex hull), so its cells are checked by the pull-back of the point
MESH_TOL = 1e-10
# the dolfinx cells Plot draws, and those of them whose map is affine at geometry degree 1
DOLFINX_CELLS, SIMPLICES = ("triangle", "quadrilateral", "tetrahedron", "hexahedron"), ("triangle", "tetrahedron")
# contours and field lines: thin dark lines over the colours


class _NGSolve:
    """NGSolve as the field plots use it: a CoefficientFunction (a GridFunction is one) on an ngsolve.Mesh. Its types
    exist only once ngsolve is imported"""

    @staticmethod
    def is_field(f):
        ng = sys.modules.get("ngsolve")
        return ng is not None and isinstance(f, ng.CoefficientFunction)

    @staticmethod
    def is_mesh(mesh):
        ng = sys.modules.get("ngsolve")
        return ng is not None and isinstance(mesh, ng.Mesh)

    @staticmethod
    def check(mesh):
        if mesh.dim not in (2, 3):
            raise ValueError(f"Plot: fields and meshes are drawn from 2d and 3d meshes, this one has dim {mesh.dim}")
        if mesh.dim == 3 and mesh.ne == 0:
            raise ValueError("Plot: surface meshes (a 3d mesh without volume elements) are not supported yet")

    @staticmethod
    def dim(mesh):
        return mesh.dim

    @staticmethod
    def own_mesh(f):
        from ngsolve import GridFunction

        return f.space.mesh if isinstance(f, GridFunction) else None

    @staticmethod
    def size(f):
        return f.dim

    @staticmethod
    def is_complex(f):
        return f.is_complex

    @staticmethod
    def locate(mesh, coords):
        """which of the (n, dim) points lie in the mesh, and where"""
        import numpy as np

        found = mesh(*(np.ascontiguousarray(c) for c in coords.T), tol=MESH_TOL)
        # NGSolve crashes evaluating at a point outside the mesh
        inside = found["nr"] >= 0
        return inside, found[inside]

    @staticmethod
    def evaluate(f, where):
        import numpy as np

        return np.asarray(f(where))

    @staticmethod
    def lines(mesh):
        """the element edges of a 2d mesh as an (m, k, 2) array of points along them, curved on a curved mesh"""
        import numpy as np
        from ngsolve import CF, ET, VOL, IntegrationRule, x, y

        k = CURVED_EDGE_POINTS if mesh.GetCurveOrder() > 1 else 2
        t = [i / (k - 1) for i in range(k)]
        trig = [(1.0 - s, s) for s in t] + [(0.0, 1.0 - s) for s in t] + [(s, 0.0) for s in t]
        quad = [(s, 0.0) for s in t] + [(1.0, s) for s in t] + [(1.0 - s, 1.0) for s in t] + [(0.0, 1.0 - s) for s in t]
        rules = {ET.TRIG: IntegrationRule(points=trig, weights=[0.0] * len(trig)),
                 ET.QUAD: IntegrationRule(points=quad, weights=[0.0] * len(quad))}
        return np.asarray(CF((x, y))(mesh.MapToAllElements(rules, VOL))).reshape(-1, k, 2)

    @staticmethod
    def boundary(mesh):
        """the boundary edges of a 2d mesh as lines() gives them"""
        return _once(_NGSolve.lines(mesh))

    @staticmethod
    def corners(mesh):
        """the volume elements of a 3d mesh per kind: their (e, k, 3) corners (of a curved element where its map puts
        them) and the kind's edges as corner pairs"""
        import numpy as np
        from ngsolve import CF, ET, VOL, IntegrationRule, x, y, z

        rules = {getattr(ET, kind): IntegrationRule(points=list(corners), weights=[0.0] * len(corners))
                 for kind, (corners, _) in ELEMENTS3D.items()}
        mips = mesh.MapToAllElements(rules, VOL)
        xyz = np.asarray(CF((x, y, z))(mips)).reshape(-1, 3)
        count = np.bincount(mips["nr"], minlength=mesh.ne)
        first = np.cumsum(count) - count
        return [(xyz[first[count == len(corners)][:, None] + np.arange(len(corners))], edges)
                for corners, edges in ELEMENTS3D.values()]

    @staticmethod
    def geometry(mesh):
        """the OCC shape the mesh was made from, None without one"""
        try:
            return mesh.ngmesh.GetGeometry().shape
        except AttributeError:
            return None


class _Dolfinx:
    """dolfinx as the field plots use it: a dolfinx.fem.Function on its own serial mesh of triangles, quadrilaterals,
    tetrahedra or hexahedra. Its types exist only once dolfinx is imported"""

    @staticmethod
    def is_field(f):
        """a dolfinx.fem.Function; raises for another dolfinx or UFL expression"""
        fem = sys.modules.get("dolfinx.fem")
        if fem is None:
            return False
        if isinstance(f, fem.Function):
            return True
        ufl = sys.modules.get("ufl.core.expr")
        if isinstance(f, fem.Expression) or (ufl is not None and isinstance(f, ufl.Expr)):
            raise ValueError(f"Plot: a dolfinx field is a dolfinx.fem.Function, not a {type(f).__name__}: interpolate "
                             "it into one first, w = fem.Function(W); w.interpolate(fem.Expression(expr, "
                             "W.element.interpolation_points))")
        return False

    @staticmethod
    def is_mesh(mesh):
        dm = sys.modules.get("dolfinx.mesh")
        return dm is not None and isinstance(mesh, dm.Mesh)

    @staticmethod
    def check(mesh):
        if mesh.comm.size > 1:
            raise ValueError(f"Plot: this dolfinx mesh is distributed over {mesh.comm.size} MPI processes, Plot draws "
                             "serial meshes only")
        tdim, cell = mesh.topology.dim, mesh.topology.cell_type.name
        if tdim not in (2, 3):
            raise ValueError(f"Plot: fields and meshes are drawn from 2d and 3d meshes, this one has dim {tdim}")
        if mesh.geometry.dim != tdim:
            raise ValueError("Plot: surface meshes (a 3d mesh without volume elements) are not supported yet")
        if cell not in DOLFINX_CELLS:
            raise ValueError(f"Plot: dolfinx meshes of triangles, quadrilaterals, tetrahedra or hexahedra, not {cell}")

    @staticmethod
    def dim(mesh):
        return mesh.topology.dim

    @staticmethod
    def own_mesh(f):
        return f.function_space.mesh

    @staticmethod
    def size(f):
        return math.prod(f.ufl_shape)

    @staticmethod
    def is_complex(f):
        import numpy as np

        return np.issubdtype(f.dtype, np.complexfloating)

    @staticmethod
    def locate(mesh, coords):
        """which of the (n, dim) points lie in the mesh, and where: of the cells dolfinx finds for a point the first
        its pull-back lies in within MESH_TOL"""
        import numpy as np
        from dolfinx import geometry

        tdim = mesh.topology.dim
        pts = np.zeros((len(coords), 3), dtype=mesh.geometry.x.dtype)
        pts[:, :tdim] = coords
        cmap, nodes, x = mesh.geometry.cmaps[0], np.asarray(mesh.geometry.dofmaps[0]), mesh.geometry.x[:, :tdim]
        simplex = mesh.topology.cell_type.name in SIMPLICES
        cells = geometry.compute_collisions_points(geometry.bb_tree(mesh, tdim), pts)
        if not (simplex and cmap.degree == 1):
            # Newton's pull-back into a non-affine cell fails for points far outside it: only the cells near the point
            cells = geometry.compute_colliding_cells(mesh, cells, pts)
        point = np.repeat(np.arange(len(pts)), np.diff(np.asarray(cells.offsets)))
        cell = np.asarray(cells.array)
        ok = np.zeros(len(cell), dtype=bool)
        order = np.argsort(cell, kind="stable")
        for group in np.split(order, np.flatnonzero(np.diff(cell[order])) + 1) if len(order) else ():
            X = _pull_back(cmap, pts[point[group], :tdim], x[nodes[cell[group[0]]]])
            low = (X >= -MESH_TOL).all(axis=1)
            ok[group] = low & (X.sum(axis=1) <= 1.0 + MESH_TOL) if simplex else low & (X <= 1.0 + MESH_TOL).all(axis=1)
        hit = np.flatnonzero(ok)
        first = hit[np.unique(point[hit], return_index=True)[1]]
        inside = np.zeros(len(pts), dtype=bool)
        inside[point[first]] = True
        return inside, (pts[point[first]], cell[first])

    @staticmethod
    def evaluate(f, where):
        return f.eval(*where)

    @staticmethod
    def lines(mesh):
        """the element edges of a 2d mesh as (m, 2, 2) lines, each once, straight between the vertices"""
        import basix
        import numpy as np

        edges = basix.topology(getattr(basix.CellType, mesh.topology.cell_type.name))[1]
        pairs = np.unique(np.sort(np.asarray(mesh.geometry.dofmaps[0])[:, edges].reshape(-1, 2), axis=1), axis=0)
        return mesh.geometry.x[pairs, :2]

    @staticmethod
    def boundary(mesh):
        """the boundary edges of a 2d mesh (those of one cell only) as (m, 2, 2) lines"""
        import basix
        import numpy as np

        edges = basix.topology(getattr(basix.CellType, mesh.topology.cell_type.name))[1]
        pairs = np.sort(np.asarray(mesh.geometry.dofmaps[0])[:, edges].reshape(-1, 2), axis=1)
        unique, count = np.unique(pairs, axis=0, return_counts=True)
        return mesh.geometry.x[unique[count == 1], :2]

    @staticmethod
    def corners(mesh):
        """the volume elements of a 3d mesh as one kind: their (e, k, 3) vertices and the cell's edges as vertex
        pairs"""
        import basix
        import numpy as np

        vertices, edges = basix.topology(getattr(basix.CellType, mesh.topology.cell_type.name))[:2]
        return [(mesh.geometry.x[np.asarray(mesh.geometry.dofmaps[0])[:, :len(vertices)]], edges)]

    @staticmethod
    def geometry(mesh):
        return None


def _pull_back(cmap, pts, nodes):
    """the reference coordinates of the points in the cell with these nodes, NaN where Newton's method fails (a
    non-affine cell)"""
    import numpy as np

    try:
        return cmap.pull_back(pts, nodes)
    except RuntimeError:
        out = np.full(pts.shape, np.nan)
        for i in range(len(pts)):
            try:
                out[i] = cmap.pull_back(pts[i:i + 1], nodes)[0]
            except RuntimeError:
                pass
        return out


def _once(lines):
    """the (m, k, 2) polylines that occur once, in either direction: a mesh's boundary, since an inner edge belongs to
    two elements. An edge is known by its rounded ends and the mean of its points"""
    import collections

    import numpy as np

    lines = np.asarray(lines, dtype=float)
    if not len(lines):
        return lines
    # the same edge computed from two elements agrees far closer than this, relative to the mesh's size
    step = 1e-7 * np.ptp(lines.reshape(-1, 2), axis=0).max()
    ends = np.round(lines[:, [0, -1]] / step).astype(np.int64).tolist()
    mean = np.round(lines.mean(axis=1) / step).astype(np.int64).tolist()
    keys = [(*sorted(map(tuple, e)), tuple(m)) for e, m in zip(ends, mean)]
    count = collections.Counter(keys)
    return lines[[count[k] == 1 for k in keys]]


# the libraries whose fields and meshes Plot draws, each recognised by its types once it is imported, never importing it
LIBRARIES = (_NGSolve, _Dolfinx)


def _field_library(f):
    """the library f is a field of, None for anything else (a measurement)"""
    return next((lib for lib in LIBRARIES if lib.is_field(f)), None)


def _mesh_library(mesh):
    lib = next((lib for lib in LIBRARIES if lib.is_mesh(mesh)), None)
    if lib is None:
        raise TypeError(f"Plot: the mesh is an ngsolve.Mesh or a dolfinx.mesh.Mesh, not {type(mesh).__name__}")
    return lib


def _is_mesh(mesh):
    return any(lib.is_mesh(mesh) for lib in LIBRARIES)


def _checked_mesh(mesh):
    _mesh_library(mesh).check(mesh)
    return mesh


def _dim(mesh):
    return _mesh_library(mesh).dim(mesh)


# NGSolve's 3d reference elements: their corners (each kind a different number) and their edges as corner pairs; the
# pyramid's apex as NGSolve places it, since its element map is singular at the apex itself
ELEMENTS3D = {
    "TET": (((1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0)), ((0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3))),
    "PYRAMID": (((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1 - 1e-12)),
                ((0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4))),
    "PRISM": (((1, 0, 0), (0, 1, 0), (0, 0, 0), (1, 0, 1), (0, 1, 1), (0, 0, 1)),
              ((0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5))),
    "HEX": (((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)),
            ((0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7))),
}
