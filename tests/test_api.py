# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import subprocess
import sys

import pytest

import planex as px

ALL = ["Angle", "Arc", "ArcEdge", "Auto", "Chamfer", "Circle", "CircleEdge", "ClosedOpenWires", "Coincident", "Compound", "Concentric", "Constraint", "Curvature", "Curve",
       "DeformableShape", "Diameter", "Distance", "Drawing", "Edge", "EdgeView",
       "EdgesView", "Ellipse", "Equal", "Face", "FaceView", "FacesView", "Find", "Fix", "Flat", "FromBrep", "FromDXF", "FromKiCad", "FromLevelSet",
       "FromNACA", "FromNumpy", "FromOCC", "FromPolyline", "FromSVG", "FromShapely", "FromStep", "GetTransformation", "Glue", "H", "Horizontal", "Intersect", "Intersect3d",
       "Line", "LineEdge", "Move", "OCCGeometry", "OffsetForm", "Pad", "Parallel", "Parameters", "Perpendicular", "Plane", "PlanexWarning", "Plot",
       "Plot3d", "PlotField", "PlotMesh", "Pocket", "Project", "QuickSketch", "Radius", "ResetIdCounters", "RigidGroup", "Rotation", "Round",
       "Selection", "Shape", "Sharp", "Sheet", "Sketch", "Spline", "SplineEdge", "Square", "Subtract", "Symmetric", "SymmetricCarriers", "SymmetricDifference", "Tangent",
       "ToSVG", "ToShapely", "Translation", "Unify", "V", "Vertex", "VertexView", "Vertical", "VerticesView", "Wire",
       "Wires", "WorkPlane", "WriteBrep", "WriteDXF", "WriteGeo", "WriteKiCad", "WriteSVG", "WriteStep", "XY", "XZ", "YZ", "ZX", "cm", "inch", "m",
       "mil", "mm", "origin", "um"]
NOT_IN_ALL = ["BSpline", "X", "Y", "Z", "config", "first", "h", "last", "v", "x", "y", "z"]
SUBPACKAGES = ["debug", "measurements", "stackups", "types"]


def public(module):
    return sorted(n for n in dir(module) if not n.startswith("_"))


def test_all_is_the_agreed_list():
    assert sorted(px.__all__) == ALL


def test_the_top_level_is_all_and_the_names_left_out_of_it_and_the_subpackages():
    assert public(px) == sorted(ALL + NOT_IN_ALL + SUBPACKAGES)


def test_the_subpackages_show_only_their_names():
    assert public(px.measurements) == ["Clearance", "Hausdorff", "HydraulicDiameter", "MaxThickness", "MinThickness",
                                       "TorsionConstant"]
    assert public(px.debug) == ["CheckShape", "ResetTimers", "StartTrace", "StopTrace", "Timers"]
    assert public(px.types) == ["Color", "Id", "Ids", "Plane", "Point", "Point3"]
    assert public(px.stackups) == ["jlc1080", "jlc1080a", "jlc1080b", "jlc1080c", "jlc2116", "jlc2116a", "jlc2116b",
                                   "jlc2116c", "jlc2116d", "jlc2116e", "jlc3313", "jlc3313a", "jlc3313c", "jlc3313d",
                                   "jlc3313e", "jlc7628", "jlc7628a", "jlc7628b", "jlc7628c", "jlc7628d", "jlc7628e",
                                   "jlc7628f", "jlc7628g", "kicad"]


@pytest.mark.subprocess
def test_a_star_import_after_ngsolve_keeps_its_names():
    pytest.importorskip("ngsolve")
    code = "\n".join([
        "import ngsolve",
        "from ngsolve import *",
        "names = %r" % (NOT_IN_ALL,),
        "taken = {n: globals()[n] for n in names if n in globals()}",
        "from planex import *",
        "assert x is ngsolve.x",
        "assert all(globals()[n] is taken[n] for n in taken), [n for n in taken if globals()[n] is not taken[n]]",
    ])
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


@pytest.mark.subprocess
def test_a_core_module_windows_cannot_load_names_the_visual_cpp_redistributable():
    code = """
import importlib.abc, sys
class NoRuntime(importlib.abc.MetaPathFinder):
    def find_spec(self, name, path, target=None):
        if name == "planex._planex":
            raise ImportError("DLL load failed while importing _planex: The specified module could not be found.")
sys.meta_path.insert(0, NoRuntime())
try:
    import planex
except ImportError as e:
    print(e)
"""
    out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, check=True).stdout.strip()
    assert out == ("DLL load failed while importing _planex: The specified module could not be found. planex needs the "
                   "latest Microsoft Visual C++ Redistributable (x64): https://aka.ms/vc14/vc_redist.x64.exe")
