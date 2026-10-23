# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""measurements to call or store: obj.Measure(f), obj.Measure(f, id="gap") and obj.measures.

A measurement is a callable f(obj, **params) returning its value. One that can be drawn also takes plot=True (and
samples=n) and then returns {"points": [(h, v), ...], "values": [...], "label": ..., "value": ..., "marks": [...]}
for px.PlotField(f, obj); with "discs": [(h, v, r, value), ...] a point inside takes the largest value of the
discs covering it instead of the interpolation."""
from ._measurements import Clearance, Hausdorff, HydraulicDiameter, MaxThickness, MinThickness, TorsionConstant

__all__ = ["Clearance", "Hausdorff", "HydraulicDiameter", "MaxThickness", "MinThickness", "TorsionConstant"]
