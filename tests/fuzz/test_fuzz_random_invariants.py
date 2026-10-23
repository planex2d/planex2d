# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Invariants beyond the booleans: offsets there and back, export round trips, solved sketches at random dimensions."""
import math

import pytest

pytest.importorskip("hypothesis")
pytest.importorskip("shapely")

import fuzzlib  # noqa: E402

SQUARE = (0.0, 0.0, math.pi / 4, (1.0,) * 4, (1.0,) * 4, 1.0)
FRACTIONS = [0.9] + [10.0 ** -k for k in range(1, 9)]


@pytest.mark.fuzz
def test_convex_shapes_offset_there_and_back(fuzz_budget):
    fuzzlib.run(fuzzlib.check_offset_convex, fuzzlib.offsets(fuzzlib.convex_shapes()), fuzz_budget)


@pytest.mark.fuzz
def test_offsets_there_and_back_are_closing_and_opening(fuzz_budget):
    fuzzlib.run(fuzzlib.check_offset_any, fuzzlib.offsets(fuzzlib.shapes()), fuzz_budget)


@pytest.mark.fuzz
def test_occ_round_trip(fuzz_budget):
    fuzzlib.run(fuzzlib.check_occ_round_trip, fuzzlib.shapes(), fuzz_budget)


@pytest.mark.fuzz
def test_shapely_round_trip_of_straight_edged_faces(fuzz_budget):
    fuzzlib.run(fuzzlib.check_shapely_round_trip, fuzzlib.shapes(fuzzlib.STRAIGHT), fuzz_budget)


@pytest.mark.fuzz
@pytest.mark.constraints
def test_solved_sketches_meet_their_dimensions(fuzz_budget):
    fuzzlib.run(fuzzlib.check_sketch, fuzzlib.sketch_specs(), fuzz_budget)


@pytest.mark.parametrize("kind", ["star", "disc", "rounded"])
@pytest.mark.parametrize("frac", FRACTIONS, ids=lambda f: f"{f:.0e}")
def test_a_square_disc_and_rounded_square_offset_there_and_back(kind, frac):
    fuzzlib.check_offset_convex(((kind, SQUARE, 0.3), frac))
