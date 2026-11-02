# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import csv

import pytest

import planex as px


def plate():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), id="a").Line((10, 4), id="b").Line((0, 4), id="top_line").Close()
    sk.Constrain("a", px.Horizontal)
    sk.Constrain("b", px.Vertical)
    sk.Constrain("a", px.Distance, 10, id="width")
    sk.Constrain("b", px.Distance, 4, id="height")
    sk.Constrain("top_line", px.Distance, id="top", reference=True)
    return sk


def test_rows_go_to_a_csv_with_a_header_and_come_back_as_driving_dimensions(tmp_path):
    sk = plate()
    path = tmp_path / "designs.csv"
    sk.AddToSpreadsheet(path, name="small", values={"area": sk.Measure()["area"]})
    sk.Update({"width": 12.5})
    sk.AddToSpreadsheet(path, name="wide", values={"area": sk.Measure()["area"]})
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    assert rows[0] == ["name", "width", "height", "top", "area"]
    assert rows[1] == ["small", "10.0", "4.0", "10.0", "40.0"] and rows[2][0] == "wide" and rows[2][1] == "12.5"
    designs = sk.ReadSpreadsheet(path)
    assert designs == {"small": {"width": 10.0, "height": 4.0}, "wide": {"width": 12.5, "height": 4.0}}
    sk.Update(designs["small"])
    assert sk.dimensions["width"] == pytest.approx(10)


def test_a_path_without_an_extension_is_a_csv_and_rows_without_a_name_are_numbered(tmp_path):
    sk = plate()
    sk.AddToSpreadsheet(tmp_path / "runs")
    sk.AddToSpreadsheet(tmp_path / "runs")
    assert (tmp_path / "runs.csv").exists()
    assert sk.ReadSpreadsheet(tmp_path / "runs") == {1: {"width": 10.0, "height": 4.0}, 2: {"width": 10.0, "height": 4.0}}


def test_the_rows_drive_an_animation(tmp_path):
    pytest.importorskip("matplotlib").use("Agg")
    sk = plate()
    for w in (8, 10, 12):
        sk.Update({"width": w})
        sk.AddToSpreadsheet(tmp_path / "sweep.csv", name="w%d" % w)
    anim = sk.Animate(sk.ReadSpreadsheet(tmp_path / "sweep.csv"))
    anim._draw_frame(2)
    assert anim._fig.axes[0].get_title() == "w12"


def test_the_header_must_fit_and_hold_the_driving_dimensions(tmp_path):
    sk = plate()
    path = tmp_path / "designs.csv"
    sk.AddToSpreadsheet(path, values={"area": 40})
    with pytest.raises(ValueError, match=r"columns differ from this row's: missing \['mass'\], unexpected \['area'\]"):
        sk.AddToSpreadsheet(path, values={"mass": 1})
    other = tmp_path / "other.csv"
    other.write_text("name,width\nx,3\n", encoding="utf-8")
    with pytest.raises(ValueError, match="the driving dimension height has no column"):
        sk.ReadSpreadsheet(other)
    with pytest.raises(ValueError, match=r"a \.csv or an \.xlsx file"):
        sk.AddToSpreadsheet(tmp_path / "designs.ods")


def test_an_xlsx_file_takes_the_same_rows(tmp_path):
    pytest.importorskip("openpyxl")
    sk = plate()
    path = tmp_path / "designs.xlsx"
    sk.AddToSpreadsheet(path, name="small")
    sk.Update({"height": 5})
    sk.AddToSpreadsheet(path, name="tall")
    assert sk.ReadSpreadsheet(path) == {"small": {"width": 10.0, "height": 4.0}, "tall": {"width": 10.0, "height": 5.0}}
