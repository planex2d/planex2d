# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Every example of the public docstrings runs. Each example line runs on fresh stand-ins for the names the examples
use (face, shape, sk, ...), after the earlier lines that define a name it uses; a docstring whose examples need more
(named elements, dimensions, a file to read, a solid) gets a context of its own, run first."""
import ast
import json
import pathlib
import re

import numpy as np
import pytest

import planex as px

try:
    import matplotlib
    matplotlib.use("Agg")
except ImportError:
    pass

API = json.loads((pathlib.Path(__file__).parent / "data" / "public_api.json").read_text())
TEMPLATES = pathlib.Path(px.__file__).parent / "templates"

OPTIONAL = {"matplotlib", "plotly", "ezdxf", "svgelements", "shapely", "scipy", "openpyxl", "netgen", "ngsolve",
            "pyngcore", "webgpu", "ngsolve_webgpu", "webgui_jupyter_widgets", "anywidget", "IPython", "gmsh", "kaleido"}
SOLIDS = {"occ", "box", "solid", "part", "shaft", "occ_shape", "mesh", "gf", "gfu"}

SKIP = {
    r"px\.WorkPlane\.": "px.WorkPlane is switched off",
    r"px\.FromCATPart$": "reads a .CATPart file; the test parts are private",
}

SQUARE = """
face = px.Move((0, 0)).Rectangle(4, 4).Face()
shape = px.Glue([face])
wire = face.outer
sk = new_sketch()
sk.Move((4, 4)).Line((0, 4)).Line((0, 0)).Line((4, 0)).Close()
"""

PLAIN = """
face = px.Move((0, 0)).Rectangle(10, 5).Face()
shape = px.Glue([face])
sk = new_sketch()
sk.Move((0, 0)).Line((10, 0)).Line((10, 5)).Line((0, 5)).Close()
"""

SIDES = """
face = px.Move((0, 0)).Rectangle(10, 5).Face()
face.edges[0].name, face.edges[1].name, face.edges[2].name, face.edges[3].name = "inner", "right", "outer", "left"
shape = px.Glue([face])
"""

PLATE = """
sk = px.Sketch()
sk.Move((0, 0)).Line((60, 0)).Line((60, 30)).Line((0, 30)).Close()
sk.Constrain("Line.1", px.Distance, 60, id="w")
sk.Constrain("Line.2", px.Distance, 30, id="length")
sk.Fillet(["Line.1.end", "Line.2.end", "Line.3.end", "Line.4.end"], 5)
sk.Circle((20, 15), 4)
sk.Constrain("Circle.1", px.Diameter, 8, id="hole")
dw = px.Drawing(sk, unit=px.mm)
"""

CROSSING = """
sk = new_sketch()
sk.Move((3, -3)).Line((3, 3))
sk.Move((5, -3)).Line((5, 3))
sk.Move((0, 0)).Line((8, 0))
sk.Move((0, 6)).Line((8, 6))
sk.Move((6, -2)).Line((6, 2))
sk.Circle((4, 0), 2)
"""

NAMED = """
sk = new_sketch()
sk.Move((0, 0)).Line((4, 0))
sk.Move((6, 2)).Arc(2, 90)
sk.Move((0, 10)).Line((4, 10))
sk.Circle((0, -20), 10)
sk.Vertex("A", (0, 3))
sk.Vertex("B", (4, -1))
sk.Move((-5, -1)).Line((5, -1), id="lower")
sk.Move((-5, 1)).Line((5, 1), id="upper")
sk.Circle((0, 0), 3, id="boss")
sk.Circle((20, 0), 1, id="hole")
sk.Circle((30, -10), 1, id="bolt")
sk.Move((0, 30)).Slot(6, 2, id="slot")
sk.Move((40, 0)).Line((44, 0), id="inlet")
sk.Move((48, 2)).Line((52, 2), id="outlet")
sk.Move((80, 0)).Rectangle(40, 20, id="plate")
sk.Move((0, 50)).Line((5, 50), id="track")
sk.Move((0, 70)).Spline([(20, 80), (50, 70)], id="s")
sk.Move((0, 0))
"""

DIMENSIONED = """
sk = px.Sketch()
sk.Move((0, 0)).Line((10, 0)).Line((10, 5)).Line((0, 5)).Close()
sk.Constrain("Line.1", px.Distance, 10, id="w")
sk.Constrain("Line.2", px.Distance, 5, id="height")
sk.Move((0, 0)).Line((2.598, 1.5), id="arm")
sk.Constrain("arm", px.Distance, 3)
sk.Constrain("Line.1", "arm", px.Angle, 30, id="crank")
sk.Move((10, 5)).Line((8.586, 3.586), id="arm2")
sk.Constrain("arm2", px.Distance, 2)
sk.Constrain("Line.3", "arm2", px.Angle, 45, id="phi")
"""

SVG = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
       'width="10mm" height="5mm" viewBox="0 0 10 5"><rect inkscape:label="outline" x="0" y="0" width="10" height="5"/>'
       '</svg>')

CONTEXT = [
    (r"\.(Chamfer|Chamfered|Fillet|Filleted)$", SQUARE),
    (r"px\.(Face|FaceView)\.(Wire|ToOCCWire)$|px\.Shape\.(Face|Wire|Offset)$|px\.(Quick)?Sketch\.(Wire|ToOCCWire)$",
     PLAIN),
    (r"\.Identify$", SIDES),
    (r"\.RemoveIdentification$", SIDES + "face.Identify('left', 'right')\nshape.Identify('left', 'right')\n"),
    (r"\.RemoveMeasure$", "shape.Measure(px.measurements.MinThickness(), id='gap')\n"),
    (r"px\.Selection\.Distance$", "face.edges[0].name = 'channel'\n"),
    (r"px\.Find$", "plate = px.Move((0, 0)).Rectangle(10, 5).Face()\nplate.id = 'plate'\n"
                   "both = px.Compound([plate], id='both')\n"),
    (r"px\.ClosedOpenWires$", "outline = px.Move((0, 0)).Rectangle(10, 5).Wire()\n"
                              "u = px.Wires([px.Line((2, 1), (4, 1)), px.Line((4, 1), (4, 3)), px.Line((4, 3), (2, 3))])[0]\n"),
    (r"px\.Drawing\.Add$", "holes = px.Sketch()\nholes.Circle((3, 2.5), 0.5)\n"),
    (r"px\.Drawing\.(Hide|Place|Update)$", PLATE),
    (r"px\.Drawing\.Section$", "front = px.Drawing(part, px.Front, unit=px.mm)\n"),
    (r"px\.Sheet$", "front = px.Drawing(part, px.Front, unit=px.mm)\ntop = px.Drawing(part, px.Top, unit=px.mm)\n"
                    "side = px.Drawing(part, px.Left, unit=px.mm)\n"),
    (r"px\.Sheet\.Add$", "detail = px.Drawing(sk, unit=px.mm)\n"),
    (r"px\.Sheet\.Title$", "import shutil\nshutil.copy(TEMPLATES / 'title_block.dxf', 'company.dxf')\n"),
    (r"px\.Sketch\.RemoveExpression$", DIMENSIONED + "sk.Expression('height = w/2')\n"),
    (r"px\.Sketch\.ReadSpreadsheet$", DIMENSIONED + "sk.AddToSpreadsheet('designs.csv', name='v1')\n"),
    (r"px\.Sketch\.(Animate|Update|Expression)$", DIMENSIONED),
    (r"px\.Sketch\.Use$", DIMENSIONED + "p = px.Parameters(th=3)\n"),
    (r"px\.Parameters\.Update$", "p = px.Parameters(th=3, w=40)\n"),
    (r"px\.Sketch\.BSpline$", "sk = new_sketch()\n"),
    (r"px\.Sketch\.Constrain$", "sk.Circle((2, 1), 0.5, id='a')\nsk.Circle((2, 4), 0.5, id='b')\n"),
    (r"px\.Sketch\.RigidGroup$", "sk = new_sketch()\nsk.Move((0, 0)).Line((4, 0)).Arc(2, 90)\nsk.Move((0, 5)).Line((4, 5))\n"),
    (r"px\.(Quick)?Sketch\.(Split|Trim)$", CROSSING),
    (r"px\.(Quick)?Sketch\.Close$", "sk = new_sketch()\nsk.Move((0, 0)).Line((4, 0)).Line((4, 3))\n"),
    (r"px\.(Quick)?Sketch\.Add$", "face.WriteDXF('part.dxf')\n"),
    (r"px\.(Quick)?Sketch\.Line$", NAMED + "sk.Move((200, -100)).Line((200, 300), id='wall')\nsk.Move((0, 0))\n"),
    (r"px\.(Quick)?Sketch\.Mirror$", "sk = new_sketch()\nsk.Move((2, 30)).Slot(6, 2, id='slot')\n"),
    (r"px\.(Quick)?Sketch\.(Arc|CircularPattern|Connect|Geometry|LinearPattern|Location|Offset)$", NAMED),
    (r"px\.(Quick)?Sketch\.Extend$", "sk = new_sketch()\nsk.Move((0, 0)).Line((2, 0))\nsk.Move((0, 2)).Line((2, 2))\n"
                                     "sk.Move((0, 4)).Line((2, 4))\nsk.Move((20, 0)).Arc((20, 1), 1)\n"
                                     "sk.Move((8, 1)).Arc((8, 7), 4)\n"),
    (r"px\.(Quick)?Sketch\.SymmetryLine$", "sk.Circle((7, 2.5), 1)\n"),
    (r"px\.measurements\.Clearance$", "pad = px.Circle((0, 0), 1).Face()\npad.edges[0].name = 'pad'\n"
                                      "trace = px.Move((3, -0.5)).Rectangle(6, 1).Face()\ntrace.edges[3].name = 'trace'\n"
                                      "shape = px.Glue([pad, trace])\n"),
    (r"px\.measurements\.Hausdorff$", "sk.Circle((20, 0), 3, id='inner')\nsk.Circle((20, 0), 3.2, id='outer')\n"),
    (r"px\.measurements\.TorsionConstant$", "face = px.Move((0, 0)).Rectangle(0.01, 0.005).Face()\n"),
    (r"px\.measurements\.Enclosing(Circle|Rectangle)$", "part = face\n"),
    (r"px\.FromNumpy$", "d = wire.ToNumpy()\n"),
    (r"px\.FromShapely$", "from shapely.geometry import LineString, Polygon\n"
                          "polygon = Polygon([(0, 0), (4, 0), (4, 3)])\nline_string = LineString([(0, 0), (1, 1)])\n"),
    (r"px\.GetTransformation$", "left, right = face.edges[3], face.edges[1]\n"),
    (r"px\.Glue$", "air = px.Move((-5, -5)).Rectangle(20, 15).Face()\niron = face\n"),
    (r"px\.Subtract$", "holes = [px.Circle((7, 2.5), 0.5).Face(), px.Circle((8.5, 2.5), 0.5).Face()]\n"),
    (r"px\.SymmetricDifference$", "layer1, layer2, layer3 = face, other, px.Circle((5, 0), 2).Face()\n"),
    (r"px\.Unify$", "c = px.Circle((5, 0), 2).Face()\n"),
    (r"px\.Wires$", "edges = [px.Line((0, 0), (2, 0)), px.Line((2, 0), (2, 1)), px.Line((1, -1), (1, 1))]\n"),
    (r"px\.Pad$", "sketch = px.Sketch()\nsketch.Move((0, 0)).Rectangle(4, 3)\n"),
    (r"px\.FromStep$", "face.WriteStep('plate.step')\n"),
    (r"px\.FromBrep$", "face.WriteBrep('plate.brep')\n"),
    (r"px\.FromOCC$", "occ_face = occ.WorkPlane().Rectangle(2, 1).Face()\nocc_face.WriteStep('bracket.step')\n"),
    (r"px\.Pocket$", "box = occ.Box((-5, -5, -4), (5, 5, 0))\n"),
    (r"px\.FromDXF$", "import ezdxf\ndoc = ezdxf.new()\n"
                      "doc.modelspace().add_lwpolyline([(0, 0), (10, 0), (10, 5), (0, 5)], close=True, "
                      "dxfattribs={'layer': 'outline'})\ndoc.saveas('part.dxf')\n"),
    (r"px\.FromSVG$", f"open('logo.svg', 'w').write({SVG!r})\nopen('part.svg', 'w').write({SVG!r})\n"),
    (r"px\.WriteKiCad$", "top = px.Move((1, 1)).Rectangle(8, 3).Face()\nbottom = px.Move((2, 1.5)).Rectangle(7, 2.5).Face()\n"
                         "board = px.Move((0, 0)).Rectangle(10, 5).Face()\nvias = px.Circle((6, 2.5), 0.3).Face()\n"),
    (r"px\.FromKiCad$", "copper = px.Move((1, 1)).Rectangle(4, 2).Face()\n"
                        "px.WriteKiCad('board.kicad_pcb', [copper] * 4, outline=px.Move((0, 0)).Rectangle(10, 5).Face(), "
                        "unit=px.mm)\n"),
]


def _examples():
    out = []
    for name in sorted(API):
        if name.split(".")[-1].startswith("_"):
            continue
        obj = px
        try:
            for part in name.split(".")[1:]:
                obj = getattr(obj, part)
        except AttributeError:
            continue
        m = re.search(r"\n\s*Examples?:\n((?:[ \t]+\S.*\n?)+)", getattr(obj, "__doc__", None) or "")
        if m:
            out.append((name, [line.strip() for line in m.group(1).splitlines() if line.strip()]))
    return out


EXAMPLES = _examples()


def _names(code, ctx):
    return {n.id for n in ast.walk(ast.parse(code)) if isinstance(n, ast.Name) and isinstance(n.ctx, ctx)}


def _sketch(kind):
    sk = kind()
    sk.Move((0, 0)).Line((10, 0)).Line((10, 5)).Line((0, 5)).Close()
    sk.Circle((3, 2.5), 1)
    sk.Move((6, 1)).Line((8, 2))
    return sk


def _solids(ns, used):
    occ = pytest.importorskip("netgen.occ")
    box = occ.Box((0, 0, 0), (10, 5, 4))
    ns.update(occ=occ, box=box, solid=box, occ_shape=box, part=box - occ.Cylinder((3, 2.5, 0), occ.Z, r=1, h=4),
              shaft=occ.Cylinder((0, 2.5, 2), occ.X, r=2, h=10))
    if used & {"mesh", "gf", "gfu"}:
        ngs = pytest.importorskip("ngsolve")
        ns["mesh"] = ngs.Mesh(occ.OCCGeometry(box).GenerateMesh(maxh=2))
        flat = ngs.Mesh(occ.OCCGeometry(ns["face"].ToOCCFace(), dim=2).GenerateMesh(maxh=1))
        ns["gf"] = ngs.GridFunction(ngs.H1(flat, order=1))
        ns["gf"].Set(ngs.x - 5)
        ns["gfu"] = ngs.GridFunction(ngs.VectorH1(flat, order=1))


def _context(name):
    return next((code for pattern, code in CONTEXT if re.search(pattern, name)), "")


def _stand_ins(name, owner, used):
    face = (px.Move((0, 0)).Rectangle(10, 5).Face() - px.Circle((3, 2.5), 1).Face()).Face()
    other = px.Circle((10, 2.5), 2).Face()
    shape = px.Glue([face, other])
    kind = px.QuickSketch if owner == "QuickSketch" else px.Sketch
    ns = {"px": px, "np": np, "face": face, "other": other, "shape": shape, "a": face, "b": other, "plate": face,
          "rib": face, "outline": face, "hole": px.Circle((3, 2.5), 1).Face(), "holes": px.Circle((7, 2.5), 0.5).Face(),
          "circle_face": px.Circle((0, 0), 1).Face(), "parts": [face, other],
          "wire": px.Move((0, 0)).Rectangle(4, 3).Wire(), "other_wire": px.Move((2, 1)).Rectangle(4, 3).Wire(),
          "compound": px.Compound([face, other]), "selection": shape.edges(kind="line"), "edge": shape.edges[0],
          "faceView": shape.faces[0], "plane": px.XY, "line": face.lines[0], "arc": shape.arcs[0],
          "circle": px.Circle((0, 0), 1).Face().circles[0], "spline": px.Spline([(0, 0), (1, 1), (2, 0)]).edges[0].edge,
          "vertex": shape.vertices[0], "p": (1.0, 1.0), "u": (1.0, 0.0), "d": 0.5,
          "cutter": px.Line((-1, 2), (12, 2)), "deformableShape": px.DeformableShape(face, 0.5),
          "sk": _sketch(kind), "new_sketch": kind}
    ns["TEMPLATES"] = TEMPLATES
    ns["dw"] = px.Drawing(_sketch(px.Sketch))
    ns["sh"] = px.Sheet(ns["dw"])
    if owner in ("LineEdge", "ArcEdge", "CircleEdge", "SplineEdge"):
        ns["other"] = px.Line((0.5, -1), (0.5, 1)).edges[0].edge
    context = _context(name)
    if (used | _names(context, ast.Load)) - _names(context, ast.Store) & SOLIDS:
        _solids(ns, used)
    exec(context, ns)
    if owner == "FaceView" and not isinstance(ns["face"], px.FaceView):
        ns["face"] = px.Glue([ns["face"]]).faces[0]
    return ns


def _run(name, owner, lines, k, folder, monkeypatch):
    folder.mkdir()
    monkeypatch.chdir(folder)
    loaded = _names(lines[k], ast.Load)
    before = [line for line in lines[:k] if _names(line, ast.Store) & loaded]
    used = set().union(*(_names(line, ast.Load) for line in before + [lines[k]]))
    ns = _stand_ins(name, owner, used)
    for line in before + [lines[k]]:
        exec(compile(line, name, "exec"), ns)


@pytest.mark.parametrize("name, lines", EXAMPLES, ids=[e[0] for e in EXAMPLES])
def test_a_docstring_example_runs(name, lines, tmp_path, monkeypatch):
    for pattern, reason in SKIP.items():
        if re.search(pattern, name):
            pytest.skip(reason)
    if any("(...)" in line for line in lines):
        pytest.skip("the example shows the call's form on a copy, faceView.Copy().X(...)")
    owner = name.split(".")[1] if name.count(".") >= 2 else ""
    missing = {}
    for k in range(len(lines)):
        try:
            _run(name, owner, lines, k, tmp_path / str(k), monkeypatch)
        except ImportError as e:
            package = (e.name or getattr(e.__cause__, "name", None) or "").split(".")[0]
            if package not in OPTIONAL and not isinstance(e, px._occ._OCCMissing):
                raise
            missing[k] = package or "the OCC module"
        except pytest.skip.Exception as e:
            missing[k] = str(e)
    if len(missing) == len(lines):
        pytest.skip("needs " + ", ".join(sorted(set(missing.values()))))
