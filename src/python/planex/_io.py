# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import os
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire


def WriteStep(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, path: str | os.PathLike, /,
              **options: Any) -> None:
    """Writes the OCC export (obj.ToOCCShape) as a STEP file.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write.
        path: the file to write.
        options: the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, ...).

    Examples:
        face.WriteStep("part.step")
        px.WriteStep(shape, "part.step", fit_arcs=True)
    """
    obj.ToOCCShape(**options).WriteStep(str(path))


def WriteBrep(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, path: str | os.PathLike, /,
              **options: Any) -> None:
    """Writes the OCC export (obj.ToOCCShape) as a BREP file.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write.
        path: the file to write.
        options: the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, ...).

    Examples:
        face.WriteBrep("part.brep")
        px.WriteBrep(shape, "part.brep", fit_arcs=True)
    """
    obj.ToOCCShape(**options).WriteBrep(str(path))


def FromOCC(source: Any, /) -> Shape:
    """Reads a planar OCC shape, or a STEP, BREP or IGES file of one, as a Shape on its plane.

    Files are read by netgen, which keeps the names. The faces come with their properties, glued as by px.Glue (an
    overlap becomes a face of its own); edges on no face become wires; every edge keeps its properties.
    px.Intersect3d(plane, shape) gives a section of a shape that is not planar, px.Project(shape, plane) a projection.

    Args:
        source: an OCC shape, or the path of a STEP, BREP or IGES file.

    Raises:
        ValueError: the shape does not lie in one plane, or it has no edges.

    Examples:
        px.FromOCC("bracket.step")
        px.FromOCC(occ_face)
    """
    from . import _planex as px

    if not hasattr(source, "faces"):
        from netgen.occ import OCCGeometry

        source = OCCGeometry(str(source)).shape
    return px._FromOCC(source)
