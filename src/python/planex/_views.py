# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""what a FaceView offers besides its own properties, edges and vertices: everything of a px.Face that only reads,
working on Copy(); and px.Edge, for isinstance of any edge"""
import abc
import inspect

from ._planex import ArcEdge, CircleEdge, EdgeView, Face, FaceView, LineEdge, SplineEdge, Unify

# these change a face in place; on a view they would change a copy
IN_PLACE = ("Translate", "Rotate", "Mirror", "Scale", "Simplify", "FitArcs", "MergeCollinearEdges", "RemoveSmallGaps",
            "RemoveSmallEdges", "Identify", "RemoveIdentification", "Fillet", "Chamfer")
SPECIAL = ("__add__", "__sub__", "__mul__", "__radd__", "__copy__", "__deepcopy__", "_repr_svg_")


class Edge(abc.ABC):
    """Any edge, for isinstance: LineEdge, ArcEdge, CircleEdge, SplineEdge, or an EdgeView of a wire, face or shape.

    Examples:
        isinstance(wire.edges[0], px.Edge)
    """


for _cls in (LineEdge, ArcEdge, CircleEdge, SplineEdge, EdgeView):
    Edge.register(_cls)


def _refused(name):
    def method(self, *args, **kwargs):
        raise TypeError(f"FaceView.{name}: a face of a shape is not changed alone; Copy().{name}(...) changes a copy")

    method.__name__ = name
    method.__doc__ = (f"Refused: a face of a shape is not changed alone; Copy().{name}(...) changes a copy.\n\n"
                      f"Args:\n    args: the arguments of Face.{name}.\n    kwargs: its keyword arguments.\n\n"
                      f"Raises:\n    TypeError: always.\n\n"
                      f"Examples:\n    faceView.Copy().{name}(...)\n")
    return method


def _on_copy(name, attr):
    if isinstance(attr, property):
        return property(lambda self: getattr(self.Copy(), name), doc=attr.__doc__)

    def method(self, *args, **kwargs):
        return getattr(self.Copy(), name)(*args, **kwargs)

    method.__name__ = name
    method.__doc__ = getattr(attr, "__doc__", None)
    return method


def _wire_of(x):
    """an edge view as a wire, a sketch's drawn edges as a Shape of their wires"""
    if isinstance(x, EdgeView):
        return x.Wire()
    wires = x.Wires()
    if not wires:
        raise ValueError(f"{type(x).__name__} has no drawn edges")
    return Unify(wires)


def add_wire_operators(*classes):
    """* and - with faces through the drawn edges as a wire, as Wire * Face and Wire - Face"""
    for cls in classes:
        cls.__mul__ = lambda self, other: _wire_of(self) * other
        cls.__sub__ = lambda self, other: _wire_of(self) - other


def complete_face_view():
    """once px.Face has all its methods, also those added in Python (Plot, ToSVG, ...)"""
    for name in dir(Face):
        if name.startswith("_") and name not in SPECIAL:
            continue
        if name in IN_PLACE:
            setattr(FaceView, name, _refused(name))
        elif name not in SPECIAL and hasattr(FaceView, name):
            continue
        else:
            setattr(FaceView, name, _on_copy(name, inspect.getattr_static(Face, name)))
    FaceView.faces = property(lambda self: [self], doc="faces: list[FaceView]\n\nA list of this face alone.")
