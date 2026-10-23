# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""a face whose boundary vertices follow a deformation field"""
from __future__ import annotations

from typing import TYPE_CHECKING, Any, Callable

import numpy as np

from . import _planex as px

if TYPE_CHECKING:
    from ._planex import Face, Shape


def _nearest_segment(p, a, b):
    """index of the segment a-b closest to every point and the parameter along it"""
    ab = b - a
    l2 = np.maximum((ab * ab).sum(axis=1), 1e-300)
    t = np.clip(((p[:, None, :] - a[None]) * ab[None]).sum(axis=2) / l2[None], 0.0, 1.0)
    q = a[None] + t[..., None] * ab[None]
    k = ((q - p[:, None, :]) ** 2).sum(axis=2).argmin(axis=1)
    return k, t[np.arange(len(p)), k]


def _displacements(gf, pts, names):
    """the vector GridFunction at the vertices, evaluated as its trace on the mesh boundary: every vertex lies on a
    boundary element of the mesh built from the shape, found among the elements of the vertex's edge name. This
    avoids the volume elements, whose values are unreliable in slivers and whose point search rejects boundary
    points"""
    from ngsolve import BND, ElementId
    mesh = gf.space.mesh
    coords = np.asarray(mesh.ngmesh.Coordinates())[:, :2]
    elements = list(mesh.Elements(BND))
    seg = np.array([[v.nr for v in el.vertices] for el in elements])
    mats = np.array([el.mat for el in elements])
    # which end of a boundary element is reference coordinate zero; a mapped point only lives as long as its trafo
    trafo = mesh.GetTrafo(ElementId(BND, 0))
    zero_is_first = np.allclose(np.array(trafo(0.0, 0.0, 0.0).point)[:2], coords[seg[0, 0]])
    d = np.empty_like(pts)
    names = np.asarray(names)
    for name in np.unique(names):
        rows = np.flatnonzero(names == name)
        candidates = np.flatnonzero(mats == name)
        if len(candidates) == 0:
            candidates = np.arange(len(seg))
        k, t = _nearest_segment(pts[rows], coords[seg[candidates, 0]], coords[seg[candidates, 1]])
        for i, el, xi in zip(rows, candidates[k], t if zero_is_first else 1.0 - t):
            trafo = mesh.GetTrafo(ElementId(BND, int(el)))
            d[i] = np.asarray(gf(trafo(float(xi), 0.0, 0.0))).ravel()
    return d


class DeformableShape(px.DeformableShape):
    """A face as dense closed polylines whose vertices follow displacement arrays or NGSolve GridFunctions.

    Every edge is sampled at most h apart; outer loops run counterclockwise, holes clockwise. Boundary parts that move
    closer than min_distance to each other are cut out.

    Args:
        shape: the Face or Shape whose faces are sampled.
        h: the largest segment length.
        min_distance: the smallest distance between boundary parts; None takes h.
        clean: True cleans the given shape of parts closer than min_distance right away.
        topology_changed_callback: called with the DeformableShape whenever SetDeformation, Clean or Defeature
            changes its loops.

    Examples:
        px.DeformableShape(face, 0.01)
        px.DeformableShape(shape, 0.01, min_distance=0.005, topology_changed_callback=print)
    """

    def __init__(self, shape: Face | Shape, h: float, min_distance: float | None = None, clean: bool = False,
                 topology_changed_callback: Callable[[DeformableShape], Any] | None = None) -> None:
        super().__init__(shape, h, min_distance, clean)
        self.topology_changed_callback = topology_changed_callback

    def SetDeformation(self, d: np.ndarray | Any, commit: bool = True) -> bool:
        """Moves the vertices to the reference vertices plus displacements, then refines and cleans the loops.

        With commit the result becomes the new reference; without, the reference stays and the next call replaces
        the deformation (like mesh.SetDeformation) until the topology changes, which always commits.

        Args:
            d: the displacements, shape (n, 2), or a vector GridFunction on a mesh of the reference shape, evaluated on
                the mesh boundary at the reference vertices.
            commit: True makes the result the new reference.

        Returns:
            True if the topology changed.

        Raises:
            ValueError: d is an array not of shape (n, 2).

        Examples:
            deformableShape.SetDeformation([(0, 0)] * len(deformableShape))
            deformableShape.SetDeformation(gfu, commit=False)
        """
        if hasattr(d, "space"):
            d = _displacements(d, self.ReferenceVertices(), self.VertexNames())
        changed = super().SetDeformation(np.asarray(d, dtype=float))
        if commit or changed:
            self.Commit()
        self._notify(changed)
        return changed

    def Clean(self) -> bool:
        """Cuts the loops against each other and keeps the boundary of the material without parts too close.

        The material is where the winding number is 1 or more; parts closer than min_distance to each other are
        removed. If anything changed, the current vertices become the
        reference.

        Returns:
            True if anything changed.

        Examples:
            deformableShape.Clean()
        """
        changed = super().Clean()
        self._notify(changed)
        return changed

    def Defeature(self) -> bool:
        """Removes boundary vertices closer than min_distance to another part of the boundary and joins the loose ends.

        The loose ends are joined by straight segments, nearest first.

        Returns:
            True if anything changed.

        Examples:
            deformableShape.Defeature()
        """
        changed = super().Defeature()
        self._notify(changed)
        return changed

    def _notify(self, changed):
        if changed and self.topology_changed_callback is not None:
            self.topology_changed_callback(self)
