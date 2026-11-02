# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The quintic spline at random points and settings, and quintic splines in solved sketches at random dimensions."""
import pytest

pytest.importorskip("hypothesis")

import fuzzlib  # noqa: E402

import planex as px  # noqa: E402


@pytest.mark.fuzz
def test_quintic_splines_keep_their_points_settings_and_symmetries(fuzz_budget):
    pytest.importorskip("shapely")
    fuzzlib.run(fuzzlib.check_spline, fuzzlib.spline_specs(), fuzz_budget)


def test_a_closed_spline_with_tangents_crossing_itself_says_so():
    with pytest.raises(ValueError, match="crosses or touches itself"):
        px.Spline([(0, 1), (0, 0), (0, 2), (0, 0.5)], periodic=True, tangents={2: (1, 0), 3: (1, 0)}, style="quintic")


def test_a_closed_spline_with_tangents_through_points_on_one_line_is_built():
    w = px.Spline([(0, 0.96875), (0, 2), (0, 0)], periodic=True, tangents={0: (1, 0), 2: (1, 0)}, style="quintic")
    assert w.Measure(at=(0, 2))["distance"] < 1e-12


@pytest.mark.fuzz
@pytest.mark.constraints
def test_solved_sketch_splines_meet_their_constraints(fuzz_budget):
    fuzzlib.run(fuzzlib.check_sketch_spline, fuzzlib.sketch_spline_specs(), fuzz_budget)


FOUND_SKETCH_SPLINES = [
    pytest.param((((0.0, 1.0), (0.0, 1.0)), -1.0, 2.0, 1.0, 90.0, 1.0), id="curvature-radius-to-the-other-sign"),
]


@pytest.mark.constraints
@pytest.mark.parametrize("spec", FOUND_SKETCH_SPLINES)
def test_found_sketch_spline_cases(spec):
    fuzzlib.check_sketch_spline(spec)
