# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import types
import weakref
from typing import TYPE_CHECKING, Any, Callable, Iterable, overload

import numpy as np

from . import _planex

if TYPE_CHECKING:
    from ._planex import EdgeView, Face, FaceView, Shape, VertexView, Wire
    from .types import Point, Point3

# every compound alive, for px.Find
_ALL = weakref.WeakSet()


def _same(part):
    if isinstance(part, Compound):
        same = Compound(part.parts)
        same._id = part._id
        return same
    return part._Same() if hasattr(part, "_Same") else part


def _points(x):
    """many points as one float64 array, converted once for all parts, anything else as it is; and the number of
    coordinates, None for what is no point"""
    if isinstance(x, (list, tuple)) and x and isinstance(x[0], (list, tuple, np.ndarray)):
        try:
            x = np.asarray(x, dtype=np.float64)
        except (TypeError, ValueError):
            return x, None
    if isinstance(x, np.ndarray):
        return x, x.shape[-1] if x.ndim in (1, 2) else None
    if isinstance(x, (list, tuple)):
        return x, len(x)
    return x, None


def _many(x):
    return isinstance(x, np.ndarray) and x.ndim == 2


def _nearest(values):
    """per row the index of the smallest of the parts' values, the first on ties"""
    return np.argmin(np.stack(values), axis=0)


def _on_one_plane(parts, message):

    if len(parts) > 1:
        try:
            _planex.AxesOf(types.SimpleNamespace(parts=parts))
        except ValueError:
            raise ValueError(message) from None


class Compound:
    """A plain list of faces, shapes, wires and compounds, also on different planes.

    Nothing is cut or merged and there are no booleans (+, -, * raise TypeError); px.Unify(compound) merges its parts.
    It is exported as it is. The parts are copies that keep their ids, so moving the compound moves each part once and
    leaves the objects it was made of where they are. len(), iteration and indexing go over its parts.

    Args:
        parts: the faces, shapes, wires and compounds.
        id: the compound's id; None gives Compound.N when first asked for.

    Raises:
        ValueError: id does not follow the rule of a user-given id.

    Examples:
        px.Compound([plate, rib.Rotated((0, 0, 0), (1, 0, 0), 90)])
        px.Compound([outline, holes], id="board")
    """

    # attached by the planex module, as to Face, Shape and Wire; declared so that type checkers see them
    Plot: Callable[..., Any]
    Plot3d: Callable[..., Any]
    WriteStep: Callable[..., None]
    WriteBrep: Callable[..., None]
    WriteDXF: Callable[..., None]
    WriteSVG: Callable[..., None]
    WriteGeo: Callable[..., None]
    ToShapely: Callable[..., Any]
    ToSVG: Callable[..., str]

    def __init__(self, parts: Iterable[Face | Shape | Wire | Compound] = (), /, id: str | None = None) -> None:
        self.parts = [_same(p) for p in parts]
        if id is not None:
            _planex._CheckUserId(id)
        self._id = id
        _ALL.add(self)

    @property
    def id(self) -> str:
        """The compound's id: Compound.N, given when first asked for, or the one set."""
        if self._id is None:
            self._id = _planex._NextId("Compound")
        return self._id

    @id.setter
    def id(self, value):
        _planex._CheckUserId(value)
        self._id = value

    def __len__(self):
        return len(self.parts)

    def __iter__(self):
        return iter(self.parts)

    def __getitem__(self, i):
        return self.parts[i]

    def _no_booleans(self, other):
        raise TypeError("a Compound has no booleans, merge it first with px.Unify(comp)")

    __add__ = __radd__ = __sub__ = __rsub__ = __mul__ = __rmul__ = __xor__ = __rxor__ = _no_booleans

    def __repr__(self):
        return "Compound(%r)" % self.parts

    def Copy(self) -> Compound:
        """An independent copy of the compound and of its parts.

        Examples:
            compound.Copy()
        """
        c = Compound()
        c.parts = [p.Copy() for p in self.parts]
        return c

    def __copy__(self):
        return self.Copy()

    def __deepcopy__(self, memo):
        return self.Copy()

    @property
    def bounding_box(self) -> tuple[tuple[float, float], tuple[float, float]] | None:
        """The box ((hmin, vmin), (hmax, vmax)) over the parts, each in its own plane coordinates; None if it is empty."""
        boxes = [b for b in (p.bounding_box for p in self.parts) if b is not None]
        if not boxes:
            return None
        return ((min(b[0][0] for b in boxes), min(b[0][1] for b in boxes)),
                (max(b[1][0] for b in boxes), max(b[1][1] for b in boxes)))

    def ToOCCShape(self, **options: Any) -> Any:
        """The OCC compound of every part's ToOCCShape(**options).

        Args:
            options: the options of the parts' ToOCCShape (fit_splines, remove_edges_smaller, ...).

        Returns:
            a netgen.occ Compound.

        Examples:
            compound.ToOCCShape()
            compound.ToOCCShape(fit_arcs=True)
        """
        from netgen.occ import Compound as OCCCompound

        return OCCCompound([p.ToOCCShape(**options) for p in self.parts])

    def _each(self, name, *args, **kwargs):
        for p in self.parts:
            getattr(p, name)(*args, **kwargs)
        return self

    def _leaves(self):
        """the parts of this compound and of the compounds in it"""
        return [q for p in self.parts for q in (p._leaves() if isinstance(p, Compound) else [p])]

    def _filled(self):
        """the parts of this compound and of the compounds in it that are not empty"""
        return [p for p in self._leaves() if p.bounding_box is not None]

    def _measured(self, call, coordinates):
        """the parts that are not empty; (h, v) points only if they all lie on one plane"""
        leaves = self._filled()
        if coordinates == 2:
            _on_one_plane(leaves, call + ": the parts of the compound lie on different planes, give the points as (x, y, z)")
        return leaves

    def _union(self, call):
        """the union of the faces of the parts"""

        leaves = self._filled()
        if not leaves:
            raise ValueError("Distance: nothing to measure from")
        faces = [p for p in leaves if p.faces]
        if not faces:
            raise ValueError(call + ": the compound has no faces, so no inside")
        if len(faces) == 1:
            return faces[0]
        _on_one_plane(faces, call + ": the faces of the compound lie on different planes")
        return _planex.Unify(faces)

    def Polylines(self, tol: float) -> list[np.ndarray]:
        """Every part's Polylines(tol) in turn, (n, 2) arrays of plane coordinates.

        Args:
            tol: the largest distance of a chord from its edge, a length > 0.

        Raises:
            ValueError: the parts lie on different planes, or tol is not positive.

        Examples:
            compound.Polylines(0.01)
        """
        leaves = self._filled()
        _on_one_plane(leaves, "Polylines: the parts of the compound lie on different planes")
        return [a for p in leaves for a in p.Polylines(tol)]

    # the forms for type checkers: many points give an array
    @overload
    def Contains(self, points: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, boundary: bool = True) -> np.ndarray: ...
    @overload
    def Contains(self, points: Point | Point3, /, boundary: bool = True) -> bool: ...
    def Contains(self, points: Point | Point3 | np.ndarray, /, boundary: bool = True) -> bool | np.ndarray:
        """True if a part contains the point, as Shape.Contains.

        Args:
            points: (h, v) if all parts lie on one plane, else (x, y, z); many points as an (n, 2) or (n, 3) array give
                a bool array (n,).
            boundary: the answer for a point on a boundary.

        Raises:
            ValueError: points are (h, v) and the parts lie on different planes.

        Examples:
            compound.Contains((1, 2))
            compound.Contains([(0, 0, 0), (1, 2, 0)], boundary=False)
        """

        points, coordinates = _points(points)
        found = [p.Contains(points, boundary=boundary) for p in self._measured("Contains", coordinates)
                 if hasattr(p, "Contains") and not isinstance(p, _planex.Wire)]
        if not found:
            return _planex.Shape().Contains(points, boundary=boundary)
        if _many(points):
            return np.logical_or.reduce(found)
        return any(found)

    @overload
    def Distance(self, target: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, project: bool = False, components: str | None = None,
                 signed: bool = False) -> np.ndarray: ...
    @overload
    def Distance(self, target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView, /, project: bool = False,
                 components: None = None, signed: bool = False) -> float: ...
    @overload
    def Distance(self, target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView, /, project: bool = False, *,
                 components: str) -> float | tuple[float, ...]: ...
    def Distance(self, target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | np.ndarray, /,
                 project: bool = False, components: str | None = None,
                 signed: bool = False) -> float | tuple[float, ...] | np.ndarray:
        """The smallest distance of the parts to the target, as Shape.Distance.

        Args:
            target: what Shape.Distance takes; a point is (h, v) if all parts lie on one plane, else (x, y, z); many
                points as an (n, 2) or (n, 3) array.
            project: True projects a target in another plane into the parts' planes.
            components: letters of 'hvxyz': the components of the shortest connection to the target from the nearest
                part (the first of equally near ones) instead of its length.
            signed: True measures as Shape.Distance(signed=True) of the union of the faces of all parts.

        Returns:
            a float; with components a tuple of them, one letter a float; many points an array (n,), with k
            components (n, k), one letter (n,).

        Raises:
            ValueError: the compound is empty; points are (h, v) and the parts lie on different planes; signed and the
                compound has no faces or its faces lie on different planes.

        Examples:
            compound.Distance((5, 0))
            compound.Distance((5, 0, 1), components="xyz")
        """
        if signed:
            return self._union("Distance(signed=True)").Distance(target, project=project, components=components, signed=True)
        target, coordinates = _points(target)
        leaves = self._measured("Distance", coordinates)
        if not leaves:
            raise ValueError("Distance: nothing to measure from")
        d = [p.Distance(target, project=project) for p in leaves]
        if components is None:
            return np.min(np.stack(d), axis=0) if _many(target) else min(d)
        c = [p.Distance(target, project=project, components=components) for p in leaves]
        if _many(target):
            return np.stack(c)[_nearest(d), np.arange(len(target))]
        return c[d.index(min(d))]

    @overload
    def ClosestPoint(self, point: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /) -> np.ndarray: ...
    @overload
    def ClosestPoint(self, point: Point | Point3, /) -> tuple[float, ...]: ...
    def ClosestPoint(self, point: Point | Point3 | np.ndarray, /) -> tuple[float, ...] | np.ndarray:
        """The closest point of the nearest part, as Shape.ClosestPoint.

        Args:
            point: (h, v) if all parts lie on one plane, else (x, y, z); many points as an (n, 2) or (n, 3) array give
                an array of that shape.

        Raises:
            ValueError: the compound is empty, or point is (h, v) and the parts lie on different planes.

        Examples:
            compound.ClosestPoint((5, 0))
            compound.ClosestPoint([(0, 0, 0), (5, 0, 0)])
        """
        point, coordinates = _points(point)
        leaves = self._measured("ClosestPoint", coordinates)
        if not leaves:
            raise ValueError("ClosestPoint: nothing to measure from")
        c = [p.ClosestPoint(point) for p in leaves]
        d = [p.Distance(point) for p in leaves]
        if _many(point):
            return np.stack(c)[_nearest(d), np.arange(len(point))]
        return c[d.index(min(d))]

    def Rotate(self, *args: Any, **kwargs: Any) -> Compound:
        """Rotates every part in place, as Shape.Rotate.

        Args:
            args: the arguments of Shape.Rotate: center, angle in the plane, or point, dir, angle in 3d.
            kwargs: its keyword arguments.

        Returns:
            this compound.

        Examples:
            compound.Rotate((0, 0), 90)
            compound.Rotate((0, 0, 0), (0, 0, 1), 45)
        """
        return self._each("Rotate", *args, **kwargs)

    def Translate(self, *args: Any, **kwargs: Any) -> Compound:
        """Moves every part in place, as Shape.Translate.

        Args:
            args: the arguments of Shape.Translate: d or dh, dv in the plane, or d or dx, dy, dz in 3d.
            kwargs: its keyword arguments.

        Returns:
            this compound.

        Examples:
            compound.Translate((10, 0))
            compound.Translate(0, 0, 5)
        """
        return self._each("Translate", *args, **kwargs)

    def Mirror(self, *args: Any, **kwargs: Any) -> Compound:
        """Mirrors every part in place, as Shape.Mirror.

        Args:
            args: the arguments of Shape.Mirror: point, dir in the plane, or point, normal in 3d.
            kwargs: its keyword arguments.

        Returns:
            this compound.

        Examples:
            compound.Mirror((0, 0), (0, 1))
            compound.Mirror((0, 0, 0), (1, 0, 0))
        """
        return self._each("Mirror", *args, **kwargs)

    def Scale(self, *args: Any, **kwargs: Any) -> Compound:
        """Scales every part in place, as Shape.Scale.

        Args:
            args: the arguments of Shape.Scale: center, factor in the plane, or point, factor in 3d.
            kwargs: its keyword arguments.

        Returns:
            this compound.

        Examples:
            compound.Scale((0, 0), 2)
            compound.Scale((0, 0), 0.5, scale_maxh=True)
        """
        return self._each("Scale", *args, **kwargs)

    def Rotated(self, *args: Any, **kwargs: Any) -> Compound:
        """A copy with every part rotated, as Shape.Rotated; this compound is not changed.

        Args:
            args: the arguments of Shape.Rotated: center, angle in the plane, or point, dir, angle in 3d.
            kwargs: its keyword arguments.

        Examples:
            compound.Rotated((0, 0), 90)
            compound.Rotated((0, 0, 0), (0, 0, 1), 45)
        """
        return self.Copy().Rotate(*args, **kwargs)

    def Translated(self, *args: Any, **kwargs: Any) -> Compound:
        """A copy with every part moved, as Shape.Translated; this compound is not changed.

        Args:
            args: the arguments of Shape.Translated: d or dh, dv in the plane, or d or dx, dy, dz in 3d.
            kwargs: its keyword arguments.

        Examples:
            compound.Translated((10, 0))
            compound.Translated(0, 0, 5)
        """
        return self.Copy().Translate(*args, **kwargs)

    def Mirrored(self, *args: Any, **kwargs: Any) -> Compound:
        """A copy with every part mirrored, as Shape.Mirrored; this compound is not changed.

        Args:
            args: the arguments of Shape.Mirrored: point, dir in the plane, or point, normal in 3d.
            kwargs: its keyword arguments.

        Examples:
            compound.Mirrored((0, 0), (0, 1))
            compound.Mirrored((0, 0, 0), (1, 0, 0))
        """
        return self.Copy().Mirror(*args, **kwargs)

    def Scaled(self, *args: Any, **kwargs: Any) -> Compound:
        """A copy with every part scaled, as Shape.Scaled; this compound is not changed.

        Args:
            args: the arguments of Shape.Scaled: center, factor in the plane, or point, factor in 3d.
            kwargs: its keyword arguments.

        Examples:
            compound.Scaled((0, 0), 2)
            compound.Scaled((0, 0), 0.5, scale_maxh=True)
        """
        return self.Copy().Scale(*args, **kwargs)
