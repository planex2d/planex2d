# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""a constraint sketch's solutions in a spreadsheet: a row per solution, read back as rows of driving dimensions
"""
from __future__ import annotations

import csv
import os
from typing import Any


def _file(path, call):
    """the path and its format: .csv or .xlsx; without an extension the default .csv is added, as the write calls do"""
    p = os.fspath(path)
    extension = os.path.splitext(p)[1].lower()
    if not extension:
        return p + ".csv", "csv"
    if extension in (".csv", ".xlsx"):
        return p, extension[1:]
    raise ValueError("%s(%r): a .csv or an .xlsx file (no extension: .csv)" % (call, p))


def _openpyxl(call):
    try:
        import openpyxl
    except ImportError as e:
        raise ImportError("%s of an .xlsx file needs openpyxl (pip install openpyxl)" % call) from e
    return openpyxl


def _rows_of(path, kind, call):
    """the file's rows, each a list of cells; none for a missing or empty file"""
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return []
    if kind == "csv":
        with open(path, newline="", encoding="utf-8") as f:
            return [row for row in csv.reader(f) if row]
    sheet = _openpyxl(call).load_workbook(path, read_only=True).active
    return [["" if c is None else c for c in row] for row in sheet.iter_rows(values_only=True) if any(c is not None for c in row)]


def AddToSpreadsheet(sketch: Any, path: str | os.PathLike, /, name: str | None = None, values: dict | None = None) -> None:
    """Appends the current solution as a row of a spreadsheet: its name, every dimension by id and the values given.

    A missing or empty file gets the header row first; an existing file's header must have the same columns in the same
    order. CSV is written with Python's csv module (comma, decimal point, UTF-8); .xlsx needs openpyxl.

    Args:
        path: a .csv or an .xlsx file; without an extension .csv is added.
        name: the row's name, its first column (empty if None).
        values: more columns after the dimensions, {column: value}, e.g. {"mass": face.mass}.

    Raises:
        ValueError: the file's header has other columns (the message names the missing and the unexpected ones), or
            path has another extension.
        ImportError: an .xlsx file without openpyxl.

    Examples:
        sk.AddToSpreadsheet("designs.csv", name="v1")
        sk.AddToSpreadsheet("designs.xlsx", values={"area": sk.Measure()["area"]})
    """
    path, kind = _file(path, "AddToSpreadsheet")
    dimensions = sketch.dimensions
    extra = dict(values or {})
    header = ["name"] + list(dimensions) + list(extra)
    row = ["" if name is None else str(name)] + list(dimensions.values()) + list(extra.values())
    found = _rows_of(path, kind, "AddToSpreadsheet")
    if found and [str(c) for c in found[0]] != header:
        missing = [c for c in header if c not in found[0]]
        unexpected = [c for c in found[0] if c not in header]
        raise ValueError("AddToSpreadsheet(%r): the file's columns differ from this row's: missing %s, unexpected %s%s"
                         % (path, missing, unexpected, "" if missing or unexpected else ", the same columns in another order"))
    if kind == "csv":
        with open(path, "a", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            if not found:
                writer.writerow(header)
            writer.writerow(row)
        return
    openpyxl = _openpyxl("AddToSpreadsheet")
    book = openpyxl.load_workbook(path) if found else openpyxl.Workbook()
    if not found:
        book.active.append(header)
    book.active.append(row)
    book.save(path)


def ReadSpreadsheet(sketch: Any, path: str | os.PathLike, /) -> dict:
    """The rows of a spreadsheet as this sketch's driving dimensions, ready for sk.Update(row) or sk.Animate(rows).

    Args:
        path: a .csv or an .xlsx file (as AddToSpreadsheet writes them); without an extension .csv is added.

    Returns:
        {row name (its row number, from 1, where it has none): {driving dimension: value}}.

    Raises:
        ValueError: the file has no header row, a driving dimension of the sketch is not a column, a row name
            repeats, or path has another extension.
        ImportError: an .xlsx file without openpyxl.

    Examples:
        designs = sk.ReadSpreadsheet("designs.csv")
        sk.Update(designs["v1"])
    """
    path, kind = _file(path, "ReadSpreadsheet")
    found = _rows_of(path, kind, "ReadSpreadsheet")
    if not found:
        raise ValueError("ReadSpreadsheet(%r): no header row" % path)
    header = [str(c) for c in found[0]]
    current = sketch.dimensions
    driven = set(sketch._DrivenDimensions())
    driving = [d for d in current if d not in driven]
    missing = [d for d in driving if d not in header]
    if missing:
        raise ValueError("ReadSpreadsheet(%r): the driving dimension%s %s %s no column" % (
            path, "s" if len(missing) > 1 else "", ", ".join(missing), "have" if len(missing) > 1 else "has"))
    at = {d: header.index(d) for d in driving}
    out = {}
    for number, cells in enumerate(found[1:], 1):
        name = cells[0] if header[0] == "name" and str(cells[0]) != "" else number
        if name in out:
            raise ValueError("ReadSpreadsheet(%r): the row name %r repeats" % (path, name))
        out[name] = {d: (int(float(cells[i])) if isinstance(current[d], int) else float(cells[i])) for d, i in at.items()}
    return out
