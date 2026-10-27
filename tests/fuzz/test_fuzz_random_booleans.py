# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

pytest.importorskip("hypothesis")
pytest.importorskip("shapely")

import fuzzlib  # noqa: E402


@pytest.mark.fuzz
def test_boolean_identities(fuzz_budget):
    fuzzlib.run(fuzzlib.check_identities, fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_booleans_of_a_shape_with_itself(fuzz_budget):
    fuzzlib.run(fuzzlib.check_self, fuzzlib.shapes(), fuzz_budget)


@pytest.mark.fuzz
def test_booleans_against_shapely(fuzz_budget):
    fuzzlib.run(fuzzlib.check_against_shapely, fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_points_classified_like_their_operands(fuzz_budget):
    fuzzlib.run(fuzzlib.check_points, fuzzlib.pairs(), fuzz_budget)


FOUND = [
    pytest.param((("rounded", (0.0, 0.0, 4.6953125, (0.34375, 1.0, 1.0, 0.5, 0.5, 0.3125),
                               (0.5, 0.875, 0.875, 0.375, 0.25, 0.8125), 1.0), 0.5), "nudged shift",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 37),
                 id="rounded-shift-1e-11-common-not-commutative"),
    pytest.param((("rounded", (0.0, 0.0, 1.75, (0.5, 0.5, 0.5, 0.5), (0.5, 0.75, 0.875, 0.25), 1.0), 0.5),
                  "nudged shift", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 37),
                 id="rounded-shift-1e-11-common-empty"),
    pytest.param((("star", (-1.0, 1.0, 2.0, (1.0, 0.5, 1.0), (0.5, 1.0, 1.0), 0.5), 0.5), "nudged turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 24),
                 id="triangle-nudged-turn-union-empty-one-way"),
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 0.5, 1.0, 1.0, 1.0, 0.75), (0.5, 0.734375, 0.53125, 1.0, 0.203125, 0.5),
                               2.0), 0.5), "translated",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="spline-translated-onto-own-vertex-difference-empty"),
    pytest.param((("star", (0.0, 0.0, 4.6953125, (0.34375, 0.5, 0.5, 0.5, 0.5, 0.3125),
                            (0.5, 0.875, 0.875, 0.25, 0.375, 0.8125), 2.0), 0.5), "nudged shift",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 37),
                 id="star-shifted-by-tol-common-empty"),
    pytest.param((("spline", (0.0, -1.0, 4.46875, (1.0, 1.0, 1.0, 0.375, 1.0), (1.0, 1.0, 0.5, 1.0, 1.0), 1.0), 0.5),
                  "nudged turn", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 36),
                 id="spline-turned-1e-13-union-empty"),
]


@pytest.mark.parametrize("spec", FOUND)
def test_found_identity_cases(spec):
    fuzzlib.check_identities(spec)


FOUND_SHAPELY = [
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "half turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="spline-faces-touching-at-a-point-union-has-no-faces"),
]


@pytest.mark.parametrize("spec", FOUND_SHAPELY)
def test_found_shapely_cases(spec):
    fuzzlib.check_against_shapely(spec)


FOUND_POINTS = [
    pytest.param((("spline", (1.7282424896083262, 0.0, 3.9304120316336952,
                              (0.5396338481966652, 1.0, 0.6452288967655571, 0.8476371446829578, 0.34287552510799496,
                               0.8336868140710763, 0.365212169168572, 0.6203801200926138, 0.7515538268525586),
                              (0.8125010294290413, 0.8200093569715678, 0.3333333333333333, 0.5814814891657976,
                               0.6818018953316236, 0.3333333333333333, 0.6912878155230773, 0.4254702391557454,
                               0.9713657183527005), 0.5), 0.5), "half turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="spline-star-half-turn-union-misses-points"),
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.5), (1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0.5), 1.0), 0.5),
                  "nudged turn", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 10),
                 id="spline-turned-by-rounding-common-misses-points"),
    pytest.param((("spline", (0.0, -1.0, 4.46875, (1.0, 1.0, 1.0, 0.375, 1.0), (1.0, 1.0, 0.5, 1.0, 1.0), 1.0), 0.5),
                  "nudged turn", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 36),
                 id="spline-turned-1e-13-common-misses-points"),
]


@pytest.mark.parametrize("spec", FOUND_POINTS)
def test_found_point_cases(spec):
    fuzzlib.check_points(spec)
