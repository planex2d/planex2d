# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The type names in planex's signatures: each stands for every kind a call takes there.

    Point   (h, v) in the plane: a tuple or list of two numbers, px.Vertex, a vertex of a shape (VertexView), a
            sketch point, or a point id ("A", "Line.3.end")
    Point3  (x, y, z): a tuple or list of three numbers, a netgen or OCC point
    Id      an id ("Line.3", "Sketch.1.Line.3"), a selected element, or a selection of one
    Ids     one Id, a list of them, or a selection
    Plane   px.Plane, a sketch (its plane), a placed face or shape, netgen or OCC axes, a planar OCC face
    Color   a colour name ("red"), "#rrggbb", or (r, g, b) or (r, g, b, a) in 0..1

Example:
    sk.Distance("Line.3.end", (4, 0))   # an Id and a Point
"""
from typing import Sequence as _Sequence, Union as _Union

from . import _planex

Point = _Union[tuple[float, float], _Sequence[float], _planex.Vertex, _planex.VertexView, str]
Point3 = _Union[tuple[float, float, float], _Sequence[float]]
Id = _Union[str, object]
Ids = _Union[Id, _Sequence[Id]]
Plane = _Union[_planex.Plane, object]
Color = _Union[str, tuple[float, float, float], tuple[float, float, float, float]]

__all__ = ["Point", "Point3", "Id", "Ids", "Plane", "Color"]


class _Written(str):
    """a type name help() shows as written, not quoted"""
    __repr__ = str.__str__


def _unquote(f):
    for name, value in getattr(f, "__annotations__", {}).items():
        if type(value) is str:
            f.__annotations__[name] = _Written(value)
    signature = getattr(f, "__signature__", None)
    if signature is not None:
        back = signature.return_annotation
        f.__signature__ = signature.replace(
            parameters=[p.replace(annotation=_Written(p.annotation)) if type(p.annotation) is str else p
                        for p in signature.parameters.values()],
            return_annotation=_Written(back) if type(back) is str else back)
    wrapped = getattr(f, "__wrapped__", None)
    if wrapped is not None:
        _unquote(wrapped)


def _unquote_annotations(objects):
    """the string annotations of the functions and classes in objects shown without quotes in help()"""
    for obj in objects:
        if isinstance(obj, type):
            for value in vars(obj).values():
                if isinstance(value, property):
                    value = value.fget
                elif isinstance(value, (staticmethod, classmethod)):
                    value = value.__func__
                if callable(value):
                    _unquote(value)
        elif callable(obj):
            _unquote(obj)
