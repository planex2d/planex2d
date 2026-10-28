# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""the sketch classes of this build: px.QuickSketch always, px.Sketch with the constraints (CMake PLANEX_CONSTRAINTS)"""
import re
from typing import TYPE_CHECKING

from . import _planex

QuickSketch = _planex.QuickSketch


class _NoConstraintSketch:
    """px.Sketch: not in this build of planex (CMake PLANEX_CONSTRAINTS=OFF); px.QuickSketch draws without constraints"""

    def __init__(self, *args, **kwargs):
        raise ImportError("px.Sketch: this planex is built without the constraint sketch (CMake PLANEX_CONSTRAINTS=OFF); "
                          "px.QuickSketch draws without constraints")


# type checkers read px.Sketch from the stub
if TYPE_CHECKING:
    from ._planex import Sketch
else:
    Sketch = getattr(_planex, "Sketch", _NoConstraintSketch)
sketch_types = (Sketch, QuickSketch)


KINDS = ("Point", "Line", "Arc", "Circle", "Spline", "Curve", "Axis", "Chain", "Rectangle", "Offset", "Mirrored", "LinearPattern",
         "CircularPattern", "Projection", "Intersect3d")
_IRREGULAR = {"Axis": "axes", "Mirrored": "mirrors", "Intersect3d": "intersections"}
_ATTRIBUTES = {_IRREGULAR.get(k, re.sub(r"(?<!^)(?=[A-Z])", "_", k).lower() + "s"): k for k in KINDS}


class Elements(dict):
    """The ids of a sketch by kind, in the order made.

    The keys are the kinds of the elements ("Point", "Line", "Arc", "Circle", "Spline", "Curve", "Axis") and of the groups by
    the call that made them ("Chain", "Rectangle", "Offset", "Mirrored", "LinearPattern", "CircularPattern",
    "Projection", "Intersect3d"). The same lists are lowercase plural attributes (lines, linear_patterns, axes,
    mirrors, intersections, ...), empty when there is none; another attribute raises AttributeError.

    Examples:
        sk.elements["Chain"]
        sk.elements.linear_patterns
    """

    def __getattr__(self, name):
        kind = _ATTRIBUTES.get(name)
        if kind is None:
            raise AttributeError("sk.elements has no %r; the attributes are %s" % (name, ", ".join(sorted(_ATTRIBUTES))))
        return self.get(kind, [])

    def __dir__(self):
        return sorted(set(dir(dict)) | set(_ATTRIBUTES))


for _cls in sketch_types:
    if _cls is not _NoConstraintSketch:
        _cls.elements = property(lambda self: Elements(self._elements),
                             doc="elements: Elements\n\nThe ids of the sketch by kind (Elements), in the order made.")
