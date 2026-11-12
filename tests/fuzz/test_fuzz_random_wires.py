# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Loose curves through px.Wires: lines, arcs, circles and open splines, free or drawn from each other."""
import pytest

pytest.importorskip("hypothesis")
pytest.importorskip("shapely")

import fuzzlib  # noqa: E402


@pytest.mark.fuzz
def test_loose_curves_through_wires(fuzz_budget):
    fuzzlib.run(fuzzlib.check_loose_curves, fuzzlib.curve_sets(), fuzz_budget)


@pytest.mark.fuzz
def test_faces_cut_by_wires(fuzz_budget):
    fuzzlib.run(fuzzlib.check_wire_cuts, fuzzlib.cut_faces(), fuzz_budget)


FOUND_CUTS = [
    pytest.param((("holey", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0, 1.0, 1.0), (1.0, 1.0, 1.0, 1.0, 1.0), 1.0), 0.5),
                  ((0.5, 0.75), (0.0, 0.0), (1.0, 1.0)), ((0.0, 0.25), (1.0, 0.25))),
                 id="split-by-a-polyline-from-a-hole-returns-the-hole"),
]


@pytest.mark.parametrize("spec", FOUND_CUTS)
def test_found_cut_cases(spec):
    fuzzlib.check_wire_cuts(spec)


FOUND_CURVES = [
    pytest.param((("spline", "free", (0.125, 0.5, 1.0, 0.0, 0.25, -1.0, 0.0, 2.0), 0),
                  ("line", "free", (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0), 0),
                  ("line", "free", (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0), 0),
                  ("line", "free", (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0), 0),
                  ("spline", "free", (2.0, 0.0, 0.0, -2.0, 0.75, 0.0, 0.0625, 0.0), 0)),
                 id="two-splines-pieces-cut-again"),
    pytest.param((("line", "free", (1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 0),
                  ("line", "small angle", (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 11),
                  ("line", "end on", (0.0, -1.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 25)),
                 id="lines-at-a-small-angle-one-split-pieces-twice"),
]


@pytest.mark.parametrize("spec", FOUND_CURVES)
def test_found_curve_cases(spec):
    fuzzlib.check_loose_curves(spec)
