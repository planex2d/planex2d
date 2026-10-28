# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""BREP files (OCC's text format) without OCC: px.WriteBrep writes what OCC reads, px.FromBrep reads planar files back,
each part on its own."""
import pytest

import planex as px


def square(x0, y0, x1, y1, plane=None):
    return px.FromPolyline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], closed=True, plane=plane)


def plate():
    return (px.Face([square(0, 0, 40, 20)]) - px.Face([px.Circle((10, 10), 3)])).faces[0].Copy()


@pytest.mark.parametrize("make", [plate,
                                  lambda: px.Spline([(0, 0), (10, 8), (20, 0), (30, 6)]),
                                  lambda: px.Face([px.Ellipse((0, 0), 30, 12, angle=20)]),
                                  lambda: px.Move((0, 0)).Line(50, 0).Line(0, 20).Arc(10, 90).Line(-40, 0).Close().Face(),
                                  lambda: px.Face([square(0, 0, 1, 1, plane=px.XZ)])])
def test_objects_come_back_exactly(make, tmp_path):
    obj = make()
    px.WriteBrep(obj, tmp_path / "a.brep")
    back = px.FromBrep(tmp_path / "a.brep")
    assert type(back) is type(obj) and back.mass == pytest.approx(obj.mass, rel=1e-12)
    assert back.Normal() == pytest.approx(obj.Normal(), abs=1e-12)


def test_a_compound_keeps_its_overlapping_parts_apart(tmp_path):
    a, b = plate(), px.Face([square(0, 0, 10, 10)])
    px.Compound([a, px.Compound([b, px.FromPolyline([(0, 0), (5, 5)])])]).WriteBrep(tmp_path / "c.brep")
    back = px.FromBrep(tmp_path / "c.brep")
    assert isinstance(back, px.Compound) and len(back.parts) == 2 and back.parts[0].mass == pytest.approx(a.mass, rel=1e-12)
    assert [type(p).__name__ for p in back.parts[1].parts] == ["Face", "Wire"]


@pytest.mark.occ
def test_occ_reads_planexs_files_and_planex_reads_occs(tmp_path):
    from netgen.occ import OCCGeometry
    face = plate()
    face.WriteBrep(tmp_path / "own.brep")
    assert OCCGeometry(str(tmp_path / "own.brep")).shape.faces[0].mass == pytest.approx(face.mass, rel=1e-12)
    face.ToOCCShape().Move((1, 2, 3)).WriteBrep(str(tmp_path / "occ.brep"))
    assert px.FromBrep(tmp_path / "occ.brep").mass == pytest.approx(face.mass, rel=1e-12)


@pytest.mark.occ
def test_faces_with_curves_on_surfaces_and_curved_faces(tmp_path):
    from netgen.occ import Box, Cylinder, Pnt, Z
    Box(Pnt(0, 0, 0), Pnt(1, 2, 3)).faces[0].WriteBrep(str(tmp_path / "boxface.brep"))
    assert px.FromBrep(tmp_path / "boxface.brep").mass == pytest.approx(6, rel=1e-12)
    Cylinder(Pnt(0, 0, 0), Z, r=1, h=2).WriteBrep(str(tmp_path / "cyl.brep"))
    with pytest.raises(ValueError, match=r"curved surface; netgen\.occ reads the file"):
        px.FromBrep(tmp_path / "cyl.brep")
