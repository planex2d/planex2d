# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def plate():
    return px.QuickSketch().Rectangle(0.04, 0.02).Face()


@pytest.mark.parametrize("write, extension", [
    (lambda f, p: f.WriteStep(p), ".step"),
    (lambda f, p: px.WriteBrep(f, p), ".brep"),
    (lambda f, p: f.WriteSVG(p), ".svg"),
    (lambda f, p: px.WriteGeo(f, p), ".geo"),
])
def test_a_path_without_an_extension_gets_the_format_s(tmp_path, write, extension):
    # a folder's dot is no extension of the file
    (tmp_path / "out.d").mkdir()
    write(plate(), tmp_path / "part")
    write(plate(), str(tmp_path / "out.d" / "part"))
    assert (tmp_path / ("part" + extension)).is_file() and (tmp_path / "out.d" / ("part" + extension)).is_file()
    assert not (tmp_path / "part").exists()


def test_another_extension_stays_as_given(tmp_path):
    plate().WriteStep(tmp_path / "part.txt")
    plate().WriteStep(tmp_path / "part.STP")
    assert (tmp_path / "part.txt").is_file() and (tmp_path / "part.STP").is_file()
    assert sorted(p.name for p in tmp_path.iterdir()) == ["part.STP", "part.txt"]


def test_dxf_kicad_and_drawings_add_theirs(tmp_path):
    pytest.importorskip("ezdxf")
    pytest.importorskip("matplotlib")
    plate().WriteDXF(tmp_path / "part")
    px.WriteKiCad(tmp_path / "board", {"Edge.Cuts": plate().wires[0], "F.Cu": plate()})
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((40, 0), px.Horizontal, id="l")
    sk.Constrain("l", px.Distance, 40)
    dw = px.Drawing(sk, unit=px.mm)
    dw.WriteSVG(tmp_path / "view")
    dw.WritePDF(tmp_path / "view")
    px.Sheet(dw).WriteDXF(tmp_path / "sheet")
    assert {p.name for p in tmp_path.iterdir()} == {"part.dxf", "board.kicad_pcb", "view.svg", "view.pdf", "sheet.dxf"}
