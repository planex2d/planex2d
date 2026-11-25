# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re

import numpy as np
import pytest

import planex as px

BAD = [math.nan, math.inf, -math.inf]
POINT_ERRORS = (ValueError, TypeError)


def square():
    return px.FromPolyline([(0, 0), (4, 0), (4, 3), (0, 3)], closed=True)


def face():
    return px.Face([square()])


def chain():
    return px.FromPolyline([(0, 0), (4, 0), (4, 3)])


EDGES = {
    "line-start": lambda x: px.Line((x, 0), (1, 0)),
    "line-end": lambda x: px.Line((0, 0), (1, x)),
    "arc-start": lambda x: px.Arc((x, 0), (0, 1), (0, 0)),
    "arc-end": lambda x: px.Arc((1, 0), (0, x), (0, 0)),
    "arc-center": lambda x: px.Arc((1, 0), (0, 1), (x, 0)),
    "circle-center": lambda x: px.Circle((x, 0), 1),
    "circle-radius": lambda x: px.Circle((0, 0), x),
    "ellipse-center": lambda x: px.Ellipse((0, x), 2, 1),
    "ellipse-major": lambda x: px.Ellipse((0, 0), x, 1),
    "ellipse-minor": lambda x: px.Ellipse((0, 0), 2, x),
    "ellipse-angle": lambda x: px.Ellipse((0, 0), 2, 1, angle=x),
    "vertex": lambda x: px.Vertex((x, 0)),
    "polyline-list": lambda x: px.FromPolyline([(0, 0), (1, x), (1, 1)]),
    "polyline-array": lambda x: px.FromPolyline(np.array([[0.0, 0.0], [1.0, 0.0], [x, 1.0]])),
    "rotation-center": lambda x: px.Rotation(30, (x, 0)),
}

MOTIONS = {
    "rotate-angle": lambda t, x: t.Rotate((0, 0), x),
    "rotate-center": lambda t, x: t.Rotate((x, 0), 30),
    "rotated-angle": lambda t, x: t.Rotated((0, 0), x),
    "translate-pair": lambda t, x: t.Translate((0, x)),
    "translate-numbers": lambda t, x: t.Translate(x, 0),
    "translated": lambda t, x: t.Translated(x, 0),
    "mirror-point": lambda t, x: t.Mirror((x, 0), (0, 1)),
    "mirror-direction": lambda t, x: t.Mirror((0, 0), (x, 1)),
    "mirrored": lambda t, x: t.Mirrored((0, 0), (x, 1)),
    "scale-factor": lambda t, x: t.Scale((0, 0), x),
    "scale-center": lambda t, x: t.Scale((x, 0), 2),
    "scaled": lambda t, x: t.Scaled((0, 0), x),
}

SIZES = {
    "rotation-angle": lambda x: px.Rotation(x),
    "translation": lambda x: px.Translation(x, 0),
    "translation-pair": lambda x: px.Translation((0, x)),
    "wire-offset": lambda x: chain().Offset(x),
    "offset-of-wires": lambda x: px.Offset([chain()], x),
    "face-offset": lambda x: face().Offset(x),
    "fillet": lambda x: square().Fillet((4, 0), x),
    "chamfer": lambda x: square().Chamfer((4, 0), x),
    "chamfer-d2": lambda x: square().Chamfer((4, 0), 1, d2=x),
}

SPLINES = {
    "spline-point": lambda x: px.Spline([(0, 0), (1, x), (2, 0)]),
    "spline-array": lambda x: px.Spline(np.array([[0.0, 0.0], [1.0, 1.0], [x, 0.0]])),
    "spline-tangent": lambda x: px.Spline([(0, 0), (1, 1), (2, 0)], tangents={0: (x, 1)}),
    "spline-tension": lambda x: px.Spline([(0, 0), (1, 1), (2, 0)], tangents={1: (1, 0)}, tensions={1: x}),
    "curve-range": lambda x: px.Curve(("t", "t**2"), (0, x)),
    "curve-callable": lambda x: px.Curve(lambda t: (t, x), (0, 1)),
}

SKETCH = {
    "start": lambda kind, x: kind((x, 0)),
    "move": lambda kind, x: kind((0, 0)).Line(1, 0).Move((x, 0)),
    "line-length": lambda kind, x: kind((0, 0)).Line(x),
    "line-point": lambda kind, x: kind((0, 0)).Line((x, 1)),
    "line-angle": lambda kind, x: kind((0, 0)).Line(1, angle=x),
    "arc-radius": lambda kind, x: kind((0, 0)).Line(1, 0).Arc(x, 90),
    "arc-sweep": lambda kind, x: kind((0, 0)).Line(1, 0).Arc(1, x),
    "circle-center": lambda kind, x: kind((0, 0)).Circle((x, 0), 1),
    "circle-radius": lambda kind, x: kind((0, 0)).Circle((0, 0), x),
    "rectangle": lambda kind, x: kind((0, 0)).Rectangle(x, 1),
    "spline-point": lambda kind, x: kind((0, 0)).Spline([(2, 1), (x, 2)]),
    "spline-tangent": lambda kind, x: kind((0, 0)).Spline([(2, 1), (3, 2)], tangents={0: (x, 1)}),
    "direction": lambda kind, x: kind((0, 0)).Direction(x, 1),
    "turn": lambda kind, x: kind((0, 0)).Turn(x),
    "vertex": lambda kind, x: kind((0, 0)).Vertex("p", (x, 0)),
}


@pytest.mark.parametrize("bad", BAD, ids=["nan", "inf", "-inf"])
@pytest.mark.parametrize("make", EDGES.values(), ids=EDGES.keys())
def test_an_edge_from_a_number_that_is_not_finite_raises(make, bad):
    with pytest.raises(POINT_ERRORS):
        make(bad)


@pytest.mark.parametrize("bad", BAD, ids=["nan", "inf", "-inf"])
@pytest.mark.parametrize("move", MOTIONS.values(), ids=MOTIONS.keys())
@pytest.mark.parametrize("thing", [chain, square, face], ids=["chain", "loop", "face"])
def test_a_motion_by_a_number_that_is_not_finite_raises(thing, move, bad):
    with pytest.raises(ValueError):
        move(thing(), bad)


@pytest.mark.parametrize("bad", BAD, ids=["nan", "inf", "-inf"])
@pytest.mark.parametrize("call", SIZES.values(), ids=SIZES.keys())
def test_a_size_that_is_not_finite_raises(call, bad):
    with pytest.raises(ValueError):
        call(bad)


@pytest.mark.parametrize("bad", BAD, ids=["nan", "inf", "-inf"])
@pytest.mark.parametrize("make", SPLINES.values(), ids=SPLINES.keys())
def test_a_spline_or_curve_from_a_number_that_is_not_finite_raises(make, bad):
    with pytest.raises(POINT_ERRORS):
        make(bad)


@pytest.mark.parametrize("bad", BAD, ids=["nan", "inf", "-inf"])
@pytest.mark.parametrize("draw", SKETCH.values(), ids=SKETCH.keys())
@pytest.mark.parametrize("kind", [px.QuickSketch, px.Sketch], ids=["QuickSketch", "Sketch"])
def test_a_sketch_call_with_a_number_that_is_not_finite_raises(kind, draw, bad):
    with pytest.raises(ValueError):
        draw(kind, bad)


def test_a_formula_that_gives_no_number_raises():
    with pytest.raises(ValueError):
        px.Curve(("t", "sqrt(-1 - t)"), (0, 1))
    with pytest.raises(ValueError):
        px.Curve(("t", "1 / (t - t)"), (0, 1))


def with_first_number_replaced(text, pattern, word):
    found = re.search(pattern, text)
    assert found, "the file has no number where one is expected"
    return text[:found.start(1)] + word + text[found.end(1):]


@pytest.mark.parametrize("word", ["inf", "nan", "1E999", "-inf"])
def test_a_step_file_with_a_number_that_is_not_finite_raises(tmp_path, word):
    path = tmp_path / "a.step"
    px.WriteStep(face(), path)
    text = with_first_number_replaced(path.read_text(), r"CARTESIAN_POINT\('',\(([^,]+),", word)
    path.write_text(text)
    with pytest.raises(ValueError):
        px.FromStep(path)


@pytest.mark.parametrize("word", ["inf", "nan", "1e999", "-inf"])
def test_a_brep_file_with_a_number_that_is_not_finite_raises(tmp_path, word):
    path = tmp_path / "a.brep"
    px.WriteBrep(face(), path)
    text = with_first_number_replaced(path.read_text(), r"\nVe\n[^\n]*\n(\S+) ", word)
    path.write_text(text)
    with pytest.raises(ValueError):
        px.FromBrep(path)


def test_an_svg_file_with_a_number_too_large_raises(tmp_path):
    pytest.importorskip("svgelements")
    path = tmp_path / "a.svg"
    path.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="10mm" height="10mm" viewBox="0 0 10 10">'
                    '<path d="M 0 0 L 4 0 L 4 1e999 L 0 3 Z" fill="none" stroke="black"/></svg>')
    with pytest.raises(POINT_ERRORS):
        px.FromSVG(path, painted=False)


@pytest.mark.parametrize("field, value", [("maxh", -1.0), ("maxh", 0.0), ("maxh", math.inf), ("maxh", math.nan), ("hpref", -1.0),
                                          ("hpref", math.nan)])
def test_a_mesh_size_of_a_vertex_that_is_no_size_raises(field, value):
    f = face()
    with pytest.raises(ValueError, match=field + " = .*: a mesh size is"):
        setattr(f.vertices[0], field, value)
    with pytest.raises(ValueError, match=field + " = .*: a mesh size is"):
        setattr(f.vertices, field, value)
    assert getattr(f.vertices, field) is None


def test_a_vertex_takes_a_mesh_size_and_a_refinement_of_zero():
    f = face()
    f.vertices[0].maxh = 0.25
    f.vertices[0].hpref = 0
    assert (f.vertices[0].maxh, f.vertices[0].hpref) == (0.25, 0)


# far from the origin the coordinates of a point on the plane round by more than the rounding of its place in the plane
def test_a_3d_point_on_a_plane_far_from_the_origin_is_on_it():
    plane = px.Plane(origin=(1e6, 2e6, 3e6), normal=(1, 2, 3), axis=(2, -1, 0))
    f = px.QuickSketch(plane).Move((0, 0)).Rectangle(1, 1).Face()
    inside, outside = plane.ToXYZ((0.5, 0.5)), plane.ToXYZ((2.5, 0.5))
    assert f.Contains(inside) and not f.Contains(outside)
    flat = px.Move((0, 0)).Rectangle(1, 1).Face()
    assert f.Relate(inside) == flat.Relate((0.5, 0.5)) and f.Relate(outside) == flat.Relate((2.5, 0.5))
    assert px.QuickSketch(plane).Move(inside).Line(outside).Measure()["length"] == pytest.approx(2)


def test_a_3d_point_off_the_plane_is_not_on_it():
    plane = px.Plane(origin=(1e6, 2e6, 3e6), normal=(1, 2, 3), axis=(2, -1, 0))
    f = px.QuickSketch(plane).Move((0, 0)).Rectangle(1, 1).Face()
    above = tuple(np.array(plane.ToXYZ((0.5, 0.5))) + 1e-6 * np.array((1, 2, 3)))
    assert not f.Contains(above)
    with pytest.raises(ValueError, match="off the plane"):
        f.Relate(above)
    with pytest.raises(ValueError, match="from the plane"):
        px.QuickSketch(plane).Move(above)
