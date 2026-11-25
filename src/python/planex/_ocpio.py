# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""CadQuery and build123d: planar objects to their shapes and back, as BREP in memory through OCP"""
from __future__ import annotations

import io
from typing import TYPE_CHECKING, Any

from . import _planex as px
from ._auto import Auto
from ._fileunits import read_factor, write_factor
from ._io import _assembled
from ._sketches import sketch_types

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire


def _joined(edges):
    """the OCP wire of a planex Wire's edges, which the BREP holds one by one; edges that make no wire stay as they are"""
    from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeWire
    from OCP.TopAbs import TopAbs_EDGE
    from OCP.TopExp import TopExp_Explorer
    # OCP 8 binds OpenCASCADE's lists in OCP.collections, OCP 7 as TopTools_ListOfShape
    try:
        from OCP.collections import List_TopoDS_Shape as ShapeList
    except ImportError:
        from OCP.TopTools import TopTools_ListOfShape as ShapeList

    pieces = ShapeList()
    found = TopExp_Explorer(edges, TopAbs_EDGE)
    while found.More():
        pieces.Append(found.Current())
        found.Next()
    wire = BRepBuilderAPI_MakeWire()
    wire.Add(pieces)
    return wire.Wire() if wire.IsDone() else edges


def _to_ocp(obj, scale, options, call):
    """obj as an OCP shape: one face or wire itself, more in a compound"""
    from OCP.BRep import BRep_Builder
    from OCP.BRepTools import BRepTools
    from OCP.TopAbs import TopAbs_COMPOUND
    from OCP.TopoDS import TopoDS_Iterator, TopoDS_Shape

    text = px._BrepText(obj.Shape() if isinstance(obj, sketch_types) else obj, write_factor(1.0, scale, call), **options)
    shape = TopoDS_Shape()
    BRepTools.Read_s(shape, io.BytesIO(text.encode("ascii")), BRep_Builder())
    if isinstance(obj, px.Wire):
        return _joined(shape)
    while shape.ShapeType() == TopAbs_COMPOUND:
        children = TopoDS_Iterator(shape)
        if not children.More():
            return shape
        first = children.Value()
        children.Next()
        if children.More():
            return shape
        shape = first
    return shape


def _leaves(shape):
    """the shapes in shape that are no compounds, located"""
    from OCP.TopAbs import TopAbs_COMPOUND
    from OCP.TopoDS import TopoDS_Iterator

    if shape.ShapeType() != TopAbs_COMPOUND:
        return [shape]
    out = []
    children = TopoDS_Iterator(shape)
    while children.More():
        out += _leaves(children.Value())
        children.Next()
    return out


def _sketch_plane(shape, call):
    """the plane of a sketch on shape, as for an OCC face: its normal as the first face is oriented"""
    from OCP.BRepAdaptor import BRepAdaptor_Surface
    from OCP.BRepLib import BRepLib_FindSurface
    from OCP.GeomAbs import GeomAbs_Plane
    from OCP.GeomAdaptor import GeomAdaptor_Surface
    from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopoDS import TopoDS

    find = BRepLib_FindSurface(shape, -1.0, True)
    if not find.Found() or GeomAdaptor_Surface(find.Surface()).GetType() != GeomAbs_Plane:
        raise ValueError(f"{call}: the shape does not lie in one plane")
    plane = GeomAdaptor_Surface(find.Surface()).Plane().Transformed(find.Location().Transformation())
    normal = plane.Axis().Direction()
    faces = TopExp_Explorer(shape, TopAbs_FACE)
    if faces.More():
        # OpenCASCADE 8 made TopoDS a namespace: OCP 8 binds TopoDS.Face, OCP 7 the static TopoDS.Face_s
        face = (getattr(TopoDS, "Face_s", None) or TopoDS.Face)(faces.Current())
        surface = BRepAdaptor_Surface(face)
        if surface.GetType() == GeomAbs_Plane:
            oriented = surface.Plane().Axis().Direction()
            if face.Orientation() == TopAbs_REVERSED:
                oriented.Reverse()
            if oriented.Dot(normal) < 0.0:
                normal.Reverse()
    p = plane.Location()
    return px.Plane((p.X(), p.Y(), p.Z()), (normal.X(), normal.Y(), normal.Z()))


def _from_ocp(shapes, scale, call):
    """the planex objects of OCP shapes in one plane, on the plane of a sketch on them"""
    from OCP.BRep import BRep_Builder
    from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace
    from OCP.BRepTools import BRepTools
    from OCP.TopoDS import TopoDS_Compound
    from OCP.gp import gp_Ax3, gp_Dir, gp_Pln, gp_Pnt

    leaves = [leaf for s in shapes for leaf in _leaves(s)]
    builder = BRep_Builder()
    content, part, root = TopoDS_Compound(), TopoDS_Compound(), TopoDS_Compound()
    for c in (content, part, root):
        builder.MakeCompound(c)
    for leaf in leaves:
        builder.Add(content, leaf)
    plane = _sketch_plane(content, call)
    # an unbounded face on the sketch's plane, first in the part: the BREP reader takes the part's axes from it
    axes = gp_Ax3(gp_Pnt(*plane.origin), gp_Dir(*plane.normal), gp_Dir(*plane.h))
    builder.Add(part, BRepBuilderAPI_MakeFace(gp_Pln(axes)).Face())
    for leaf in leaves:
        builder.Add(part, leaf)
    builder.Add(root, part)
    stream = io.BytesIO()
    BRepTools.Write_s(root, stream)
    return _assembled(px._FromBrepText(stream.getvalue().decode("latin-1"), read_factor(1.0, scale, call)))


def _wrapped(items, kind, call):
    """the OCP shapes of a library's shapes"""
    from OCP.TopoDS import TopoDS_Shape

    if not items:
        raise ValueError(f"{call}: no shapes")
    for x in items:
        if not isinstance(getattr(x, "wrapped", None), TopoDS_Shape):
            raise TypeError(f"{call}: {type(x).__name__} is no {kind} shape")
    return [x.wrapped for x in items]


def ToCadQuery(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, /, scale: float | Auto = Auto,
               **options: Any) -> Any:
    """Converts to a CadQuery shape: a Face to a cq.Face, a Wire to a cq.Wire, anything else to a cq.Compound.

    The shape goes over in memory as BREP, which OCP reads as px.WriteBrep writes it, on its plane in 3d, without names
    or other properties; a Wire whose edges make no single wire comes as a cq.Compound of them. The export cleans the
    edges as obj.ToOCCShape does with the same options. The numbers come as they are, times scale.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to convert.
        scale: what the model's numbers are multiplied by; px.Auto is 1.
        options: the export options (fit_splines, fit_arcs, remove_edges_smaller, ...), as for obj.ToOCCShape.

    Raises:
        TypeError: scale is None.
        ValueError: scale is not a positive number.

    Examples:
        cq_face = face.ToCadQuery()
        px.ToCadQuery(shape, scale=1000, fit_arcs=True)
    """
    import cadquery as cq

    return cq.Shape.cast(_to_ocp(obj, scale, options, "ToCadQuery"))


def ToBuild123d(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, /, scale: float | Auto = Auto,
                **options: Any) -> Any:
    """Converts to a build123d shape: a Face to a Face, a Wire to a Wire, anything else to a Compound.

    The shape goes over in memory as BREP, which OCP reads as px.WriteBrep writes it, on its plane in 3d, without names
    or other properties; a Wire whose edges make no single wire comes as a Compound of them. The export cleans the edges
    as obj.ToOCCShape does with the same options. The numbers come as they are, times scale. With planex2d-occ
    installed, import build123d before planex: netgen's OpenCASCADE loaded before OCP 8 crashes the process.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to convert.
        scale: what the model's numbers are multiplied by; px.Auto is 1.
        options: the export options (fit_splines, fit_arcs, remove_edges_smaller, ...), as for obj.ToOCCShape.

    Raises:
        TypeError: scale is None.
        ValueError: scale is not a positive number.

    Examples:
        bd_face = face.ToBuild123d()
        px.ToBuild123d(shape, scale=1000, fit_arcs=True)
    """
    import build123d as bd

    return bd.Shape.cast(_to_ocp(obj, scale, options, "ToBuild123d"))


# id= is the id wrapper's (_ids.with_id), which names the result; declared here so that type checkers see it
def FromCadQuery(obj: Any, /, scale: float | Auto = Auto, *, id: str | None = None) -> Face | Shape | Wire:
    """Reads planar CadQuery faces, wires or edges in one plane as planex objects on the plane of a sketch on them.

    The plane is the one a sketch on an OCC face gets: the normal as the first face is oriented, the global origin and x
    axis projected onto it, so px.QuickSketch(px.FromCadQuery(face)) sketches on the face. The shapes come over in
    memory as BREP, without names; their edges are exact (lines, arcs, circles, ellipses and b-splines), their faces
    glued as by px.Glue. The numbers come as they are, times scale.

    Args:
        obj: a CadQuery shape (a cq.Face, cq.Wire, cq.Edge or a cq.Compound of them), or a cq.Workplane of such shapes.
        scale: what the numbers are multiplied by; px.Auto is 1.

    Returns:
        a Face for one face, a Wire for edges that make one, else a Shape.

    Raises:
        TypeError: obj is no CadQuery shape; scale is None.
        ValueError: no shapes; the shapes do not lie in one plane, or a face is curved; scale is not a positive number.

    Examples:
        px.QuickSketch(px.FromCadQuery(top_face))
        px.FromCadQuery(workplane, scale=0.001)
    """
    items = obj.vals() if hasattr(obj, "vals") else [obj]
    return _from_ocp(_wrapped(items, "CadQuery", "FromCadQuery"), scale, "FromCadQuery")


# id= is the id wrapper's (_ids.with_id), which names the result; declared here so that type checkers see it
def FromBuild123d(obj: Any, /, scale: float | Auto = Auto, *, id: str | None = None) -> Face | Shape | Wire:
    """Reads planar build123d faces, wires or edges in one plane as planex objects on the plane of a sketch on them.

    The plane is the one a sketch on an OCC face gets: the normal as the first face is oriented, the global origin and x
    axis projected onto it, so px.QuickSketch(px.FromBuild123d(face)) sketches on the face. The shapes come over in
    memory as BREP, without names; their edges are exact (lines, arcs, circles, ellipses and b-splines), their faces
    glued as by px.Glue. The numbers come as they are, times scale. With planex2d-occ installed, import build123d
    before planex: netgen's OpenCASCADE loaded before OCP 8 crashes the process.

    Args:
        obj: a build123d shape (a Face, Wire, Edge or a Compound of them), or a list of such shapes (a ShapeList).
        scale: what the numbers are multiplied by; px.Auto is 1.

    Returns:
        a Face for one face, a Wire for edges that make one, else a Shape.

    Raises:
        TypeError: obj is no build123d shape; scale is None.
        ValueError: no shapes; the shapes do not lie in one plane, or a face is curved; scale is not a positive number.

    Examples:
        px.QuickSketch(px.FromBuild123d(top_face))
        px.FromBuild123d(shape_list, scale=0.001)
    """
    items = list(obj) if isinstance(obj, list) else [obj]
    return _from_ocp(_wrapped(items, "build123d", "FromBuild123d"), scale, "FromBuild123d")
