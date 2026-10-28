# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import os
from typing import TYPE_CHECKING, Any

from ._auto import Auto, is_auto
from ._fileunits import check_length, read_factor, write_factor
from ._units import m

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire


def WriteStep(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, path: str | os.PathLike, /, unit: float = m,
              scale: float | Auto = Auto, **options: Any) -> None:
    """Writes the object as a STEP file (AP214) that netgen and OCC read, without OCC.

    The export cleans the edges as obj.ToOCCShape does with the same options. The faces of each part share their edges;
    the names of faces, edges and vertices, the faces' col and netgen's maxh and hpref go into the file as netgen writes
    them; free wires are named curves. A Compound keeps its parts apart, so px.FromStep reads them back as they were.
    The file counts in unit and names it; the coordinates are multiplied by scale, by default converted from meters
    into unit. netgen reads every STEP file in millimeters (a 4 m wide face as 4000 mm wide); maxh and the
    identifications are written for that.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write.
        path: the file to write.
        unit: the length the file counts in, one STEP names: the meter with an SI prefix (px.m, px.cm, px.mm, px.um),
            the inch, foot or mil (px.inch, px.mil).
        scale: what the model's numbers are multiplied by; px.Auto converts them from meters into unit, 1 writes them
            as they are (a model drawn in millimeter numbers: unit=px.mm, scale=1).
        options: the export options (fit_splines, fit_arcs, remove_edges_smaller, ...), as for obj.ToOCCShape.

    Raises:
        TypeError: unit is a name, not a length; scale is None.
        ValueError: unit is not a positive length or one STEP does not name; scale is not a positive number.

    Examples:
        face.WriteStep("part.step")
        px.WriteStep(shape, "part.step", unit=px.mm, fit_arcs=True)
    """
    from ._planex import _StepText
    from ._sketches import sketch_types
    unit = check_length(unit, "WriteStep")
    factor = write_factor(unit, scale, "WriteStep")
    text = _StepText(obj.Shape() if isinstance(obj, sketch_types) else obj, unit, factor, **options)
    with open(path, "w", encoding="ascii") as f:
        f.write(text)


def WriteBrep(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, path: str | os.PathLike, /,
              scale: float | Auto = Auto, **options: Any) -> None:
    """Writes the object as a BREP file in OCC's text format, without OCC.

    The export cleans the edges as obj.ToOCCShape does with the same options; the faces of each part share their edges.
    BREP keeps no names or other properties (WriteStep does). A Compound's parts stay apart, so px.FromBrep reads them
    back as they were. BREP names no unit: the numbers are written as they are, times scale.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write.
        path: the file to write.
        scale: what the model's numbers are multiplied by; px.Auto is 1.
        options: the export options (fit_splines, fit_arcs, remove_edges_smaller, ...), as for obj.ToOCCShape.

    Raises:
        TypeError: scale is None.
        ValueError: scale is not a positive number.

    Examples:
        face.WriteBrep("part.brep")
        px.WriteBrep(shape, "part.brep", fit_arcs=True)
    """
    from ._planex import _BrepText
    from ._sketches import sketch_types
    factor = write_factor(1.0, scale, "WriteBrep")
    text = _BrepText(obj.Shape() if isinstance(obj, sketch_types) else obj, factor, **options)
    with open(path, "w", encoding="ascii") as f:
        f.write(text)


# id= is the id wrapper's (_ids.with_id), which names the result; declared here so that type checkers see it
def FromOCC(source: Any, /, *, id: str | None = None) -> Shape:
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


def _reading(unit, scale, call):
    """a reader's unit= and scale= for the C++ readers: each None for px.Auto"""
    given = None if is_auto(unit, call, "unit") else check_length(unit, call)
    factor = None if is_auto(scale, call, "scale") else read_factor(1.0, scale, call)
    return given, factor


def _object(kind, shape):
    if kind == "Wire" and not shape.faces and len(shape.wires) == 1:
        return shape.wires[0]
    return shape


def FromStep(path: str | os.PathLike, /, unit: float | Auto = Auto,
             scale: float | Auto = Auto) -> Face | Shape | Wire | Compound:
    """Reads the planar faces and curves of a STEP file without OCC.

    Each part of the file lies in one plane; its faces are cleaned one by one and glued, its curves are exact (lines,
    arcs, circles, ellipses and b-splines), with the names, colors and netgen's maxh and hpref the file gives them.
    One part gives that object (a file px.WriteStep wrote: the Face, Wire or Shape it was); several give a Compound of
    them, nothing glued across parts. The coordinates come in meters, converted from the unit the file names, or
    multiplied by scale. Curved faces and 3d shapes are read by netgen.occ
    (OCCGeometry(path).shape); px.Intersect3d and px.Project make planar geometry from them.

    Args:
        path: the STEP file.
        unit: the length the file counts in; px.Auto takes the one it names.
        scale: what the file's numbers are multiplied by; px.Auto converts them from unit into meters, 1 keeps them.

    Returns:
        the Face, Wire or Shape of a single part, else a Compound of the parts.

    Raises:
        TypeError: unit is a name; unit or scale is None.
        ValueError: the file is no STEP file, a face is curved, or a part does not lie in one plane; unit is not a
            positive length, scale not a positive number.

    Examples:
        plate = px.FromStep("plate.step")
    """
    from ._planex import _FromStepText
    given, factor = _reading(unit, scale, "FromStep")
    with open(path, encoding="latin-1") as f:
        return _assembled(_FromStepText(f.read(), given, factor))


def _assembled(parts):
    """the object of the parts a reader found: one part itself, else the Compounds their paths ("1.0") describe"""
    from ._compound import Compound
    if len(parts) == 1:
        return _object(parts[0][0], parts[0][2])
    tree = {}
    for kind, where, shape in parts:
        node = tree
        keys = [int(k) for k in where.split(".")] if where else [0]
        for k in keys[:-1]:
            node = node.setdefault(k, {})
        node[keys[-1]] = _object(kind, shape)

    def build(node):
        return Compound([build(node[k]) if isinstance(node[k], dict) else node[k] for k in sorted(node)])

    return build(tree)


def FromBrep(path: str | os.PathLike, /, scale: float | Auto = Auto) -> Face | Shape | Wire | Compound:
    """Reads the planar faces and edges of a BREP file (OCC's text format) without OCC.

    Each part of the file lies in one plane; its faces are cleaned one by one and glued, its edges are exact (lines,
    arcs, circles, ellipses and b-splines). One part gives that object (a single face a Face, edges alone a Wire if they
    make one, else a Shape); several give a Compound of them, nothing glued across parts (a file px.WriteBrep wrote reads
    back with its Compounds as they were). BREP carries no names or other properties, and no unit: the numbers come as
    they are, times scale. Curved faces and 3d shapes are read by netgen.occ
    (OCCGeometry(path).shape); px.Intersect3d and px.Project make planar geometry from them.

    Args:
        path: the BREP file.
        scale: what the file's numbers are multiplied by; px.Auto is 1.

    Returns:
        the Face, Wire or Shape of a single part, else a Compound of the parts.

    Raises:
        TypeError: scale is None.
        ValueError: the file is no BREP file, a face is curved, or a part does not lie in one plane; scale is not a
            positive number.

    Examples:
        plate = px.FromBrep("plate.brep")
    """
    from ._planex import _FromBrepText
    factor = read_factor(1.0, scale, "FromBrep")
    with open(path, encoding="latin-1") as f:
        return _assembled(_FromBrepText(f.read(), factor))
