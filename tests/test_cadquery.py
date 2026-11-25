# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

cq = pytest.importorskip("cadquery")

import planex as px  # noqa: E402
from helpers import square  # noqa: E402


def approx3(t):
    return pytest.approx(t, abs=1e-12)


def test_to_cadquery_face_wire_and_shape():
    face = px.Circle((0, 0), 2).Face() - px.Circle((0.5, 0), 0.5).Face()
    c = face.ToCadQuery()
    assert isinstance(c, cq.Face) and c.isValid()
    assert c.Area() == pytest.approx(face.mass, rel=1e-12)
    w = px.ToCadQuery(square(0, 0, 3).Wire())
    assert isinstance(w, cq.Wire) and w.Length() == pytest.approx(12)
    assert w.IsClosed()
    hook = px.ToCadQuery(px.FromPolyline([(0, 0), (1, 0), (1, 1)]))
    assert isinstance(hook, cq.Wire) and not hook.IsClosed() and hook.Length() == pytest.approx(2)
    two = px.Glue([square(0, 0), square(1, 0)])
    c2 = two.ToCadQuery()
    assert isinstance(c2, cq.Compound) and len(c2.Faces()) == 2 and len(c2.Edges()) == 7


def test_to_cadquery_scale_and_plane():
    assert square(0, 0, 2).ToCadQuery(scale=1000).Area() == pytest.approx(4e6)
    side = px.QuickSketch(px.XZ).Rectangle(2, 1).Face()
    c = side.ToCadQuery()
    n = c.normalAt()
    assert abs(n.y) == pytest.approx(1) and c.Center().y == pytest.approx(0, abs=1e-12)
    assert c.Area() == pytest.approx(2)


def test_to_cadquery_extrudes():
    solid = cq.Solid.extrudeLinear(square(0, 0, 2).ToCadQuery(), cq.Vector(0, 0, 3))
    assert solid.isValid() and solid.Volume() == pytest.approx(12)


def test_from_cadquery_box_faces_on_the_sketch_plane():
    box = cq.Workplane().box(10, 20, 5)
    expected = {">Z": ((0, 0, 2.5), (1, 0, 0), (0, 1, 0), 200), "<Z": ((0, 0, -2.5), (1, 0, 0), (0, -1, 0), 200),
                ">X": ((5, 0, 0), (0, 1, 0), (0, 0, 1), 100)}
    for selector, (origin, h, v, area) in expected.items():
        face = px.FromCadQuery(box.faces(selector).val())
        assert isinstance(face, px.Face) and face.mass == pytest.approx(area)
        plane = px.Plane(face)
        assert plane.origin == approx3(origin) and plane.h == approx3(h) and plane.v == approx3(v)
        sketch = px.QuickSketch(face)
        assert px.Plane(sketch).origin == approx3(origin) and px.Plane(sketch).h == approx3(h)
    assert px.FromCadQuery(box.faces(">Z")).mass == pytest.approx(200)


def test_from_cadquery_hole_wire_edge_and_scale():
    plate = cq.Workplane().rect(10, 10).circle(2).extrude(1)
    top = px.FromCadQuery(plate.faces(">Z").val())
    assert top.mass == pytest.approx(100 - 4 * math.pi) and len(top.circles) == 1 and len(top.edges) == 5
    outline = px.FromCadQuery(plate.faces(">Z").val().outerWire())
    assert isinstance(outline, px.Wire) and outline.mass == pytest.approx(40) and len(outline.edges) == 4
    assert px.FromCadQuery(plate.faces(">Z").val(), scale=0.001).mass == pytest.approx((100 - 4 * math.pi) * 1e-6)
    hole = next(e for e in plate.faces(">Z").val().Edges() if e.geomType() == "CIRCLE")
    circle = px.FromCadQuery(hole)
    assert isinstance(circle, px.Wire) and len(circle.circles) == 1 and circle.mass == pytest.approx(4 * math.pi)
    line = px.FromCadQuery(next(e for e in plate.faces(">Z").val().Edges() if e.geomType() == "LINE"))
    assert isinstance(line, px.Wire) and line.mass == pytest.approx(10)


def test_cadquery_round_trip():
    face = square(0, 0, 4) - px.Circle((1, 1), 0.5).Face()
    back = px.FromCadQuery(face.ToCadQuery())
    assert back.mass == pytest.approx(face.mass, rel=1e-12)
    assert px.Plane(back).origin == approx3((0, 0, 0)) and px.Plane(back).h == approx3((1, 0, 0))


def test_from_cadquery_rejects():
    box = cq.Workplane().box(1, 1, 1)
    with pytest.raises(ValueError, match="one plane"):
        px.FromCadQuery(box.val())
    with pytest.raises(ValueError):
        px.FromCadQuery(cq.Workplane().cylinder(2, 1).faces("not %PLANE").val())
    with pytest.raises(TypeError, match="no CadQuery shape"):
        px.FromCadQuery(5)
    with pytest.raises(ValueError, match="no shapes"):
        px.FromCadQuery(cq.Workplane())


def test_from_cadquery_id():
    face = px.FromCadQuery(cq.Workplane().rect(2, 3).extrude(1).faces(">Z").val(), id="lid")
    assert face.mass == pytest.approx(6) and face.id == "lid"
