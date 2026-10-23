# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""planex: exact 2d geometry of lines, arcs and splines (sketches, constraint sketches, booleans, offsets) for netgen,
NGSolve and OCC.

Coordinates and sizes must lie below about 1e70: beyond that the tolerance tests, which square their values,
overflow and booleans become silently wrong. Lengths below about 1e-14 count as rounding: the tolerance is
relative to the coordinates above 1 and absolute (1e-14) below; OCC and netgen resolve only 1e-7."""
from importlib.metadata import PackageNotFoundError as _PackageNotFoundError, version as _version

try:
    __version__ = _version("planex2d")
except _PackageNotFoundError:
    __version__ = "unknown"
from ._planex import (Arc, ArcEdge, Circle, CircleEdge, Constraint, EdgeView, EdgesView, Face, FaceView,
                      FacesView, FromPolyline, GetTransformation, Glue, H, Intersect, Line, LineEdge, Move,
                      OCCGeometry, Pad, Plane, PlanexWarning, Pocket, Project, QuickSketch, ResetIdCounters, Rotation,
                      Shape, Spline, SplineEdge, Subtract, Translation, Unify, V, Vertex, VertexView,
                      VerticesView, Wire, Wires, XY, YZ, ZX, origin)
# outside __all__, re-exported for type checkers by the alias
from ._planex import BSpline as BSpline, config as config, first as first, last as last
from ._planex import (Angle, Coincident, Concentric, Diameter, Distance, Equal, Fix, Horizontal, Parallel,
                      Perpendicular, Radius, RigidGroup, Symmetric, SymmetricCarriers, Tangent, Vertical)
from ._closing import ClosedOpenWires
from ._compound import Compound
from ._deformable import DeformableShape
from ._dxf import FromDXF, WriteDXF
from ._gmsh import WriteGeo
from ._io import FromOCC, WriteBrep, WriteStep
from ._meshsection import Intersect3d
from ._kicad import FromKiCad
from ._kicadwrite import WriteKiCad
from . import _geom, debug, stackups, types
from ._levelset import FromLevelSet
from ._naca import FromNACA
from . import _plot
from ._plot import Plot, PlotField, PlotMesh
from ._plot3d import Plot3d
from ._workplane import WorkPlane
from . import _plot_sketch
from ._shapelyio import FromShapely, ToShapely
from . import _svg
from ._svg import ToSVG
from ._units import cm, inch, m, mil, mm, um
from . import _views
from ._views import Edge

for _cls in (Face, Shape, Wire, Compound):
    _cls.Plot = Plot
    _cls.Plot3d = Plot3d
    _cls.WriteStep = WriteStep
    _cls.WriteBrep = WriteBrep
    _cls.WriteDXF = WriteDXF
    _cls.WriteGeo = WriteGeo
    _cls.ToShapely = ToShapely
    _cls.ToSVG = ToSVG
    _cls._repr_svg_ = _svg._repr_svg
del _cls

# the two kinds of sketch: with constraints (Sketch, a stand-in that raises when the build has none) and without
from ._sketches import Sketch, sketch_types as _sketch_types  # noqa: E402
for _cls in _sketch_types:
    _cls.ToSVG = ToSVG
    _cls.Plot3d = Plot3d
    _cls.WriteStep = WriteStep
    _cls.WriteBrep = WriteBrep
    _cls.WriteDXF = WriteDXF
    _cls.WriteGeo = WriteGeo
    _cls.ToShapely = ToShapely
    _cls.Plot = _plot_sketch.Plot
    _cls._repr_svg_ = _svg._repr_svg
    _cls.Polylines = _geom.shape_polylines
del _cls
DeformableShape.Plot = Plot
DeformableShape.ToSVG = ToSVG
DeformableShape._repr_svg_ = _svg._repr_svg
_views.complete_face_view()
_views.add_wire_operators(Sketch, EdgeView)
_views.add_wire_operators(QuickSketch, EdgeView)
from . import _ids, _select  # noqa: E402
from ._select import Find, Selection  # noqa: E402

from . import _measurements, measurements  # noqa: E402

_ids.add_id_arguments(globals())
_select.add_selectors()
_measurements.add_measures()
# coordinates of an element's centre (3d, and h, v in its plane) for conditions; the axes for Min, Max and Sorted
x, y, z, h, v = (_select.Coordinate(c) for c in "xyzhv")
X, Y, Z = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)

# not x, y, z, h, v, X, Y, Z, BSpline and config: a star import after NGSolve's or netgen.occ's keeps theirs
__all__ = ["Angle", "Arc", "ArcEdge", "Circle", "CircleEdge", "ClosedOpenWires", "Coincident", "Compound", "Concentric", "Constraint",
           "DeformableShape", "Diameter", "Distance", "Edge", "EdgeView",
           "EdgesView", "Equal", "Face", "FaceView", "FacesView", "Find", "Fix", "FromDXF", "FromKiCad", "FromLevelSet",
           "FromNACA", "FromOCC", "FromPolyline", "FromShapely", "GetTransformation", "Glue", "H", "Horizontal", "Intersect",
           "Intersect3d", "Line", "LineEdge", "Move", "OCCGeometry", "Pad", "Parallel", "Perpendicular", "Plane",
           "PlanexWarning", "Plot", "Plot3d", "PlotField", "PlotMesh", "Pocket", "Project", "QuickSketch", "Radius", "ResetIdCounters",
           "RigidGroup", "Rotation", "Selection", "Shape", "Sketch", "Spline", "SplineEdge", "Subtract", "Symmetric",
           "SymmetricCarriers", "Tangent", "ToSVG", "ToShapely", "Translation", "Unify", "V", "Vertex", "VertexView",
           "Vertical", "VerticesView", "Wire", "Wires", "WorkPlane", "WriteBrep", "WriteDXF", "WriteGeo", "WriteKiCad",
           "WriteStep", "XY", "YZ", "ZX", "cm", "inch", "m", "mil", "mm", "origin", "um"]

types._unquote_annotations([globals()[name] for name in __all__] + [getattr(measurements, name) for name in measurements.__all__])
