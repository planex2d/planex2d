# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""unit= and scale= of the writers and readers: unit the length the file counts in, scale the factor between the
model's numbers and the file's (px.Auto: between meters and unit)"""
import math

from ._auto import is_auto


def check_length(unit, call):
    if isinstance(unit, str):
        raise TypeError(f"{call}(unit={unit!r}): a unit is a length, e.g. px.mm, not a name")
    if not (isinstance(unit, (int, float)) and unit > 0 and math.isfinite(unit)):
        raise ValueError(f"{call}(unit={unit}): a unit is a positive length, e.g. px.mm")
    return float(unit)


def named(unit, lengths, call, names):
    """the format's name or code of unit, matched by its length; another length raises"""
    for length, name in lengths.items():
        if math.isclose(unit, length, rel_tol=1e-9):
            return name
    raise ValueError(f"{call}(unit={unit}): {names}")


def _factor(scale, call):
    if isinstance(scale, bool) or not (isinstance(scale, (int, float)) and scale > 0 and math.isfinite(scale)):
        raise ValueError(f"{call}(scale={scale!r}): a positive number, or px.Auto")
    return float(scale)


def write_factor(unit, scale, call):
    """what the model's numbers are multiplied by: scale, px.Auto from meters into unit"""
    return 1.0 / unit if is_auto(scale, call, "scale") else _factor(scale, call)


def read_factor(unit, scale, call):
    """what the file's numbers are multiplied by: scale, px.Auto from unit into meters"""
    return unit if is_auto(scale, call, "scale") else _factor(scale, call)
