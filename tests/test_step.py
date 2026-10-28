# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""STEP files without OCC: px.WriteStep writes what netgen and OCC read (names, colours, maxh, hpref), px.FromStep reads
planar files back, each part on its own."""
import pytest

import planex as px


def square(x0, y0, x1, y1, plane=None):
    return px.FromPolyline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], closed=True, plane=plane)


def plate():
    face = (px.Face([square(0, 0, 40, 20)]) - px.Face([px.Circle((10, 10), 3)])).faces[0].Copy()
    face.name = "plate"
    face.col = (0.2, 0.4, 0.8)
    face.maxh = 2.0
    face.edges[0].name = "bottom"
    face.edges[0].hpref = 1.0
    return face


def test_a_face_comes_back_with_its_names_colour_and_mesh_sizes(tmp_path):
    face = plate()
    face.WriteStep(tmp_path / "a.step")
    back = px.FromStep(tmp_path / "a.step")
    assert isinstance(back, px.Face) and back.mass == pytest.approx(face.mass, rel=1e-12)
    assert (back.name, back.maxh, back.col) == ("plate", 2.0, pytest.approx((0.2, 0.4, 0.8, 1.0), abs=1e-12))
    bottom = [e for e in back.edges if e.name == "bottom"]
    assert len(bottom) == 1 and bottom[0].hpref == 1.0


@pytest.mark.parametrize("make", [lambda: px.Spline([(0, 0), (10, 8), (20, 0), (30, 6)]),
                                  lambda: px.Face([px.Ellipse((0, 0), 30, 12, angle=20)]),
                                  lambda: px.Move((0, 0)).Line(50, 0).Line(0, 20).Arc(10, 90).Line(-40, 0).Close().Face(),
                                  lambda: px.Face([square(0, 0, 1, 1, plane=px.XZ)])])
def test_curves_and_planes_come_back_exactly(make, tmp_path):
    obj = make()
    px.WriteStep(obj, tmp_path / "a.step")
    back = px.FromStep(tmp_path / "a.step")
    assert type(back) is type(obj) and back.mass == pytest.approx(obj.mass, rel=1e-12)
    assert back.Normal() == pytest.approx(obj.Normal(), abs=1e-12)


def test_a_compound_keeps_its_overlapping_parts_apart(tmp_path):
    a, b = plate(), px.Face([square(0, 0, 10, 10)])
    px.Compound([a, px.Compound([b, px.FromPolyline([(0, 0), (5, 5)])])]).WriteStep(tmp_path / "c.step")
    back = px.FromStep(tmp_path / "c.step")
    assert isinstance(back, px.Compound) and len(back.parts) == 2
    assert back.parts[0].mass == pytest.approx(a.mass, rel=1e-12)
    inner = back.parts[1]
    assert isinstance(inner, px.Compound) and [type(p).__name__ for p in inner.parts] == ["Face", "Wire"]
    assert inner.parts[0].mass == pytest.approx(100, rel=1e-12)


def test_free_wires_keep_their_names(tmp_path):
    shape = px.Glue([px.Face([square(0, 0, 4, 2)]), px.FromPolyline([(1, 1), (3, 1)], name="weld")])
    shape.WriteStep(tmp_path / "w.step")
    back = px.FromStep(tmp_path / "w.step")
    assert isinstance(back, px.Shape) and back.mass == pytest.approx(8)
    assert [e.name for e in back.edges if e.name] == ["weld"]


@pytest.mark.occ
def test_netgen_reads_the_names_colours_and_mesh_sizes(tmp_path):
    from netgen.occ import OCCGeometry
    plate().WriteStep(tmp_path / "a.step", unit=px.mm, scale=1)
    face = OCCGeometry(str(tmp_path / "a.step")).shape.faces[0]
    assert (face.name, face.maxh) == ("plate", 2.0) and tuple(face.col)[:3] == pytest.approx((0.2, 0.4, 0.8), abs=1e-6)
    assert sorted(e.name for e in OCCGeometry(str(tmp_path / "a.step")).shape.edges if e.name) == ["bottom"]


@pytest.mark.occ
def test_netgen_reads_a_model_in_metres_in_millimetres(tmp_path):
    from netgen.occ import OCCGeometry
    plate().WriteStep(tmp_path / "m.step")
    face = OCCGeometry(str(tmp_path / "m.step")).shape.faces[0]
    assert face.maxh == pytest.approx(2000.0, rel=1e-12) and face.mass == pytest.approx(plate().mass * 1e6, rel=1e-9)


@pytest.mark.occ
def test_a_planar_file_of_occ_reads_as_from_occ_does(tmp_path):
    plate().ToOCCShape().WriteStep(str(tmp_path / "occ.step"))
    assert px.FromStep(tmp_path / "occ.step", scale=1).mass == pytest.approx(px.FromOCC(tmp_path / "occ.step").mass, rel=1e-12)


@pytest.mark.occ
def test_curved_and_3d_shapes_name_netgen_occ(tmp_path):
    from netgen.occ import Box, Cylinder, X, Z
    Cylinder((0, 0, 0), Z, r=1, h=2).WriteStep(str(tmp_path / "cyl.step"))
    with pytest.raises(ValueError, match=r"netgen\.occ reads the file \(OCCGeometry\(path\)\.shape\)"):
        px.FromStep(tmp_path / "cyl.step")
    Box((0, 0, 0), (1, 1, 1)).WriteStep(str(tmp_path / "box.step"))
    with pytest.raises(ValueError, match=r"several planes; netgen\.occ .*px\.Intersect3d\(plane, shape\)"):
        px.FromStep(tmp_path / "box.step")
    Box((0, 0, 0), (1, 1, 1)).WriteBrep(str(tmp_path / "box.brep"))
    with pytest.raises(ValueError, match=r"several planes; netgen\.occ"):
        px.FromBrep(tmp_path / "box.brep")


def strip():
    face = px.Face([square(0, 0, 2, 1)])
    face.edges[3].name, face.edges[1].name = "left", "right"
    return face.Identify("left", "right", name="periodic")


@pytest.mark.occ
def test_identifications_come_back(tmp_path):
    face = strip()
    face.WriteStep(tmp_path / "p.step")
    back = px.FromStep(tmp_path / "p.step")
    name, (a, b, trafo, kind) = next(iter(back.identifications.items()))
    want = face.identifications["periodic"]
    assert (name, a, b, kind) == ("periodic", "left", "right", want[3])
    assert [x for row in trafo.matrix for x in row] == pytest.approx([1, 0, 0, 1], abs=1e-12)
    assert trafo.vector == pytest.approx((2, 0), abs=1e-12)


@pytest.mark.occ
def test_identifications_of_netgens_files_come_back(tmp_path):
    face = strip()
    face.ToOCCShape().WriteStep(str(tmp_path / "occ.step"))
    back = px.FromStep(tmp_path / "occ.step")
    assert list(back.identifications) == ["periodic"] and back.identifications["periodic"][:2] == ("left", "right")
