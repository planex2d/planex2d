# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import pytest

import planex as px


def plate():
    """40 x 20 mm, drawn in metres"""
    return px.Face([px.FromPolyline([(0, 0), (0.04, 0), (0.04, 0.02), (0, 0.02)], closed=True)])


AREA = 0.04 * 0.02


@pytest.mark.parametrize("unit, label", [(px.m, "SI_UNIT($,.METRE.)"), (px.mm, "SI_UNIT(.MILLI.,.METRE.)"),
                                         (px.cm, "SI_UNIT(.CENTI.,.METRE.)"), (px.um, "SI_UNIT(.MICRO.,.METRE.)"),
                                         (px.inch, "CONVERSION_BASED_UNIT('INCH'"), (px.mil, "CONVERSION_BASED_UNIT('MIL'")])
def test_step_names_its_unit_and_reads_back_in_metres(tmp_path, unit, label):
    plate().WriteStep(tmp_path / "a.step", unit=unit)
    assert label in (tmp_path / "a.step").read_text()
    assert px.FromStep(tmp_path / "a.step").mass == pytest.approx(AREA, rel=1e-12)
    assert px.FromStep(tmp_path / "a.step", scale=1).mass == pytest.approx(AREA / unit**2, rel=1e-12)


def test_step_scale_writes_the_numbers_as_they_are(tmp_path):
    millimetres = px.Face([px.FromPolyline([(0, 0), (40, 0), (40, 20), (0, 20)], closed=True)])
    millimetres.WriteStep(tmp_path / "a.step", unit=px.mm, scale=1)
    assert px.FromStep(tmp_path / "a.step").mass == pytest.approx(AREA, rel=1e-12)
    assert px.FromStep(tmp_path / "a.step", unit=px.m).mass == pytest.approx(800, rel=1e-12)


def test_units_a_format_cannot_name_raise():
    pytest.importorskip("ezdxf")
    with pytest.raises(ValueError, match="STEP names the meter with an SI prefix"):
        plate().WriteStep("never.step", unit=0.5)
    with pytest.raises(ValueError, match="DXF names lengths of its"):
        plate().WriteDXF("never.dxf", unit=0.5)
    with pytest.raises(ValueError, match="a positive number, or px.Auto"):
        plate().WriteStep("never.step", scale=0)
    with pytest.raises(TypeError, match="None is no value here"):
        plate().WriteStep("never.step", scale=None)


def test_dxf_and_svg_take_unit_and_scale(tmp_path):
    pytest.importorskip("ezdxf")
    pytest.importorskip("svgelements")
    plate().WriteDXF(tmp_path / "a.dxf", unit=px.mm)
    assert px.Face(px.FromDXF(tmp_path / "a.dxf")).mass == pytest.approx(AREA, rel=1e-9)
    assert px.Face(px.FromDXF(tmp_path / "a.dxf", scale=1)).mass == pytest.approx(800, rel=1e-9)
    plate().WriteDXF(tmp_path / "b.dxf", unit=px.mm, scale=1)
    assert px.Face(px.FromDXF(tmp_path / "b.dxf", scale=1)).mass == pytest.approx(AREA, rel=1e-9)
    with pytest.raises(TypeError, match=r"FromDXF\(unit=None\)"):
        px.FromDXF(tmp_path / "a.dxf", unit=None)
    plate().WriteSVG(tmp_path / "a.svg", unit=px.mm)
    assert px.Face(px.FromSVG(tmp_path / "a.svg")).mass == pytest.approx(AREA, rel=1e-9)
    assert px.Face(px.FromSVG(tmp_path / "a.svg", scale=1)).mass == pytest.approx(800, rel=1e-9)


def test_brep_takes_scale_only(tmp_path):
    plate().WriteBrep(tmp_path / "a.brep", scale=1000)
    assert px.FromBrep(tmp_path / "a.brep").mass == pytest.approx(800, rel=1e-12)
    assert px.FromBrep(tmp_path / "a.brep", scale=1e-3).mass == pytest.approx(AREA, rel=1e-12)


def test_mesh_sizes_and_identifications_scale_with_the_numbers(tmp_path):
    face = plate()
    face.maxh = 0.002
    face.edges[3].name, face.edges[1].name = "left", "right"
    face.Identify("left", "right", name="periodic")
    face.WriteStep(tmp_path / "a.step", unit=px.mm)
    back = px.FromStep(tmp_path / "a.step")
    assert back.maxh == pytest.approx(0.002, rel=1e-12)
    assert back.identifications["periodic"][2].vector == pytest.approx((0.04, 0), abs=1e-12)
    back = px.FromStep(tmp_path / "a.step", scale=1)
    assert back.maxh == pytest.approx(2, rel=1e-12)
    assert back.identifications["periodic"][2].vector == pytest.approx((40, 0), abs=1e-9)


@pytest.mark.occ
def test_netgen_reads_planex_files_in_millimetres(tmp_path):
    from netgen.occ import OCCGeometry
    face = plate()
    face.maxh = 0.002
    for unit in (px.m, px.mm, px.inch):
        face.WriteStep(tmp_path / "a.step", unit=unit)
        shape = OCCGeometry(str(tmp_path / "a.step")).shape
        assert shape.faces[0].mass == pytest.approx(800, rel=1e-9) and shape.faces[0].maxh == pytest.approx(2, rel=1e-9)
