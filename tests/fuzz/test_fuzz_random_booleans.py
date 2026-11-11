# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

pytest.importorskip("hypothesis")
pytest.importorskip("shapely")

import fuzzlib  # noqa: E402


@pytest.mark.fuzz
def test_boolean_identities(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_identities), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_booleans_of_a_shape_with_itself(fuzz_budget):
    fuzzlib.run(fuzzlib.check_self, fuzzlib.shapes(), fuzz_budget)


@pytest.mark.fuzz
def test_booleans_against_shapely(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_against_shapely), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_points_classified_like_their_operands(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_points), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_relations_follow_their_matrix_and_shapely(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_relate), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_boolean_results_are_clean(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_clean), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_chains_of_booleans(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_chains, fuzzlib.pairs_in_triple), fuzzlib.triples(),
                fuzz_budget)


@pytest.mark.fuzz
def test_booleans_of_a_moved_turned_mirrored_scaled_pair(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_moved, fuzzlib.pair_of_moved, fuzzlib.known_moved), fuzzlib.moved_pairs(),
                fuzz_budget)


@pytest.mark.fuzz
def test_unify_and_glue_of_many_shapes(fuzz_budget):
    fuzzlib.run(fuzzlib.check_crowd, fuzzlib.crowds(), fuzz_budget)


@pytest.mark.fuzz
def test_the_same_answer_every_time(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_same_answer), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_random_programs_of_operations(fuzz_budget):
    def body(spec):
        if not fuzzlib.known_program(spec):
            fuzzlib.check_program(spec)

    fuzzlib.run(body, fuzzlib.programs(), fuzz_budget)


@pytest.mark.fuzz
def test_shapes_on_a_grid(fuzz_budget):
    fuzzlib.run(fuzzlib.check_grid, fuzzlib.grid_pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_a_disc_and_its_polygons(fuzz_budget):
    fuzzlib.run(fuzzlib.check_circle_polygon, fuzzlib.circle_polygons(), fuzz_budget)


@pytest.mark.fuzz
def test_measurements_agree_with_the_booleans(fuzz_budget):
    fuzzlib.run(fuzzlib.unless_known(fuzzlib.check_measures), fuzzlib.pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_shapes_from_solved_sketches(fuzz_budget):
    fuzzlib.run(fuzzlib.check_sketch_pair, fuzzlib.sketch_pairs(), fuzz_budget)


@pytest.mark.fuzz
def test_a_shape_against_its_offset(fuzz_budget):
    fuzzlib.run(fuzzlib.check_offset_inputs, fuzzlib.offset_inputs(), fuzz_budget)


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
    pytest.param((("spline", (1.9375, 0.0, 4.5, (0.3125, 0.328125, 0.9375, 0.5), (0.322265625, 0.25, 0.5, 0.5),
                              1.890625), 0.5), "nudged turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 49),
                 id="quintic-turned-1e-13-union-empty-one-way"),
    pytest.param((("spline", (1.9375, 0.0, 4.5, (0.375, 0.328125, 0.9375, 0.5), (0.32421875, 0.25, 0.71875, 0.5),
                              1.890625), 0.5), "nudged turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 49),
                 id="quintic-turned-1e-13-common-empty"),
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "tangent",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 46),
                 id="spline-and-disc-overlapping-1e-7-lens-counted-the-wrong-way",
                 marks=pytest.mark.xfail(reason="below spline_tol: union and differences add the lens")),
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "offset",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 4),
                 id="spline-and-its-offset-by-1e-7-parts-do-not-add-up",
                 marks=pytest.mark.xfail(reason="below spline_tol: the parts do not add up")),
    pytest.param((("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 0.5, 0.5), 1.0), 0.5), "tangent",
                  ("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 0.5), 1.0), 0.5), 46),
                 id="triangle-and-spline-overlapping-1e-7-lens-counted-the-wrong-way",
                 marks=pytest.mark.xfail(reason="below spline_tol: union and differences add the lens")),
    pytest.param((("disc", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "corner on corner",
                  ("mixed", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0, 1.0, 1.0), (1.0, 0.75, 0.5, 0.5, 1.0), 1.0), 0.5), 22),
                 id="disc-at-a-corner-of-a-large-face-common-outside"),
    pytest.param((("holey", (0.0, 0.0, 0.0, (0.5, 1.0, 1.0), (1.0, 1.0, 1.0), 0.5), 0.5), "corner on corner",
                  ("mixed", (0.0, 0.0, 0.78125, (0.75, 0.5625, 0.75, 1.0, 1.0), (0.9375, 1.0, 0.5, 0.5, 0.75), 2.0), 0.5),
                  22),
                 id="holey-face-at-a-corner-of-a-large-face-common-outside"),
    pytest.param((("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "corner on edge",
                  ("holey", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0, 1.0, 1.0, 1.0), (1.0, 1.0, 1.0, 1.0, 1.0, 1.0), 1.0), 0.46875), 5),
                 id="triangle-and-holey-face-corner-off-an-edge-union-nearly-empty"),
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
    pytest.param((("spline", (1.9271819658796048, 1.0, 4.75,
                              (0.32112479120335163, 0.333984375, 0.9497670531365341, 0.5),
                              (0.3225978347009676, 0.43786302742188715, 0.6965602508857187, 0.25), 1.9), 0.5),
                  "nudged turn", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 49),
                 id="quintic-turned-1e-13-common-misses-points"),
]


@pytest.mark.parametrize("spec", FOUND_POINTS)
def test_found_point_cases(spec):
    fuzzlib.check_points(spec)


FOUND_RELATIONS = [
    pytest.param((("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "free",
                  ("holed", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="triangle-and-itself-with-a-hole-relate-as-equal"),
    pytest.param((("rounded", (-2.0, 1.875, 0.984375, (0.5, 0.75, 1.0), (0.875, 0.67578125, 0.75), 1.3125), 0.1875),
                  "nudged turn", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 50),
                 id="rounded-turned-1e-14-relate-by-the-order"),
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 0.5), (1.0, 1.0, 1.0), 2.0), 0.5), "tangent",
                  ("rounded", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 2.0), 0.125), 64),
                 id="spline-and-rounded-overlapping-1e-11-relate-by-the-order"),
    pytest.param((("rounded", (0.0, 0.0, 1.0, (1.0, 1.0, 1.0, 1.0), (1.0, 1.0, 1.0, 1.0), 1.0), 0.125), "tangent",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 2.0), 0.5), 2),
                 id="rounded-and-disc-overlapping-1e-5-crossings-made-twice"),
    pytest.param((("spline", (0.0, 0.0, 1.03125, (0.8125, 0.9375, 0.59375), (1.0, 0.4296875, 0.3359375), 1.8359375),
                   0.5), "one point moved", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 22),
                 id="spline-and-its-reshaped-copy-relate-by-the-order"),
    pytest.param((("spline", (0.0, 0.0, 6.0, (1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 1.0), (1.0, 0.5, 0.5, 1.0, 1.0, 1.0, 0.5), 1.0),
                   0.5), "corner on edge",
                  ("rounded", (0.0, 0.0, 0.0, (1.0, 1.0, 0.75, 0.375), (1.0, 0.75, 0.5, 1.0), 1.5), 0.25), 4),
                 id="spline-and-a-corner-1e-7-into-it-relate-by-the-order"),
]


@pytest.mark.parametrize("spec", FOUND_RELATIONS)
def test_found_relation_cases(spec):
    fuzzlib.check_relate(spec)


FOUND_MOVED = [
    pytest.param(((("mixed", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0), (0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0), 1.0),
                    0.5), "same circle", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 20),
                  0, 0.0, False, 5, 5.5),
                 id="mixed-minus-a-star-on-its-circle-at-1e5-glued-again-changes",
                 marks=pytest.mark.xfail(strict=True, reason="a - b glued again loses two edges")),
    pytest.param(((("disc", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "nudged shift",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 21), 1, 0.0, False, 4, 0.0),
                 id="discs-a-tol-apart-at-1e4-union-keeps-an-edge-under-tol"),
    pytest.param(((("star", (1.0, 0.0, 1.1015625, (1.0, 1.0, 1.0, 0.5, 0.8046875, 1.0, 1.0),
                             (1.0, 0.5, 0.5, 0.20946528082377702, 0.5, 0.75, 0.25), 1.0625), 0.5), "corner on corner",
                   ("rounded", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 4), -3, 0.0, False, 4, 0.0),
                 id="corner-about-tol-from-a-corner-at-1e4-union-empty"),
    pytest.param(((("rounded", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "free",
                   ("sharp", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.3828125), 0), 0, 0.0, False, 3, 0.0),
                 id="needle-thinner-than-tol-at-1e3-union-empty"),
    pytest.param(((("spline", (0.0, 0.0, 1.0, (1.0, 1.0, 1.0), (0.5, 1.0, 0.5), 1.0), 0.5), "nudged turn",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 21), 3, 0.0, False, 6, 0.0),
                 id="spline-turned-1e-11-scaled-at-1e6-union-empty",
                 marks=pytest.mark.xfail(reason="below spline_tol: a spline's near copy loses the region")),
    pytest.param(((("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "free",
                   ("slotted", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.421875), 0), 0, 0.0, False, 3, 0.0),
                 id="slot-of-tol-at-1e3-common-part-glued-again-changes"),
]


@pytest.mark.parametrize("spec", FOUND_MOVED)
def test_found_moved_cases(spec):
    fuzzlib.check_moved(spec)


FOUND_OFFSETS = [
    pytest.param((("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 14, False),
                 id="triangle-offset-by-tol-raises"),
    pytest.param((("star", (0.0, 0.0, 0.0, (0.5625, 0.5, 0.5, 0.5, 1.0, 0.875, 1.0, 1.0),
                            (1.0, 0.5, 0.5, 0.875, 1.0, 1.0, 0.5, 1.0), 1.0), 0.5), 14, False),
                 id="star-and-its-offset-by-tol-common-empty"),
    pytest.param((("star", (0.25, 0.0, 0.0, (0.375, 0.375, 1.0), (0.375, 0.5, 0.5), 0.5), 0.5), 14, False),
                 id="triangle-and-its-offset-by-tol-union-empty"),
]


@pytest.mark.parametrize("spec", FOUND_OFFSETS)
def test_found_offset_cases(spec):
    fuzzlib.check_offset_inputs(spec)


FOUND_CLEAN = [
    pytest.param((("spline", (0.0, 1.625, 0.671875, (0.34375, 0.34375, 1.0, 0.625, 0.3951824352639679, 0.76953125, 0.75, 1.0),
                              (0.5625, 0.21875, 0.8125, 0.8125, 0.28125, 0.25, 0.625, 1.0), 0.8125), 0.5), "nudged shift",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 23),
                 id="spline-and-its-nudged-copy-difference-glued-again-changes",
                 marks=pytest.mark.xfail(reason="below spline_tol: a - b glued again changes")),
    pytest.param((("spline", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 0.5), 1.0), 0.5), "one point moved",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 22),
                 id="spline-and-its-reshaped-copy-common-part-edges-without-a-face",
                 marks=pytest.mark.xfail(reason="below spline_tol: a * b has edges and no face")),
    pytest.param((("slotted", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 0.5), 0.296875), "free",
                  ("mixed", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="slot-thinner-than-the-large-face-s-tol-face-without-area"),
]


@pytest.mark.parametrize("spec", FOUND_CLEAN)
def test_found_clean_cases(spec):
    fuzzlib.check_clean(spec)


FOUND_CHAINS = [
    pytest.param(((("holey", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.4284677294328184), "same circle",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 8), "nudged shift",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 32),
                 id="sliver-along-a-hole-united-with-a-face-with-that-hole-empty"),
    pytest.param(((("disc", (0.0, -1.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), "half turn",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0), "nudged turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 24),
                 id="discs-touching-at-a-point-and-a-near-copy-lose-a-disc"),
    pytest.param(((("holed", (0.0, 0.0, 0.0, (1.0, 1.0, 0.5), (1.0, 1.0, 0.25), 0.5), 0.5), "nudged turn",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 23), "free",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="near-copies-of-a-holed-face-united-fill-the-hole"),
    pytest.param(((("star", (0.0, 0.0, 1.875, (1.0, 1.0, 1.0, 0.34375, 0.5625, 1.0), (0.75, 0.625, 0.375, 0.25, 0.375, 0.4375),
                             1.0), 0.5), "nudged shift", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 49),
                  "free", ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="star-and-its-copy-moved-1e-13-split-and-joined-glued-again-changes"),
    pytest.param(((("holey", (-1.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.50390625), "translated",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0), "nudged turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 30),
                 id="faces-with-a-hole-touching-the-outline-union-smaller-than-one"),
    pytest.param(((("mixed", (0.0, 1.0, 1.0, (1.0, 0.5, 1.0), (0.5, 0.5, 1.0), 1.0), 0.5), "one point moved",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 46), "free",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="large-polygon-and-its-copy-with-a-corner-moved-union-again-empty"),
    pytest.param(((("rounded", (0.0, 1.0, 0.1875, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 0.5), 0.5), "corner on corner",
                   ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 38), "free",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 0),
                 id="corner-1e-13-off-a-corner-union-with-a-third-empty"),
]


@pytest.mark.parametrize("spec", FOUND_CHAINS)
def test_found_chain_cases(spec):
    fuzzlib.check_chains(spec)


FOUND_CROWDS = [
    pytest.param(("grid", 5, "rounded", 53, 1.0), id="rounded-squares-within-tol-hole-as-a-face"),
]


@pytest.mark.parametrize("spec", FOUND_CROWDS)
def test_found_crowd_cases(spec):
    fuzzlib.check_crowd(spec)


FOUND_SAME = [
    pytest.param((("rounded", (0.0, 0.0, 0.0, (0.5, 1.0, 0.5), (1.0, 1.0, 0.25), 1.0), 0.5), "nudged turn",
                  ("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5), 49),
                 id="rounded-turned-1e-13-sum-edges-by-the-order"),
    pytest.param((("rounded", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5625), "tangent",
                  ("rounded", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 0.5), 1.0), 0.05), 4),
                 id="rounded-faces-near-tangent-sum-outline-six-tol-off"),
    pytest.param((("sharp", (0.0, 0.0, 0.0, (0.5, 1.0, 1.0, 1.0, 1.0, 1.0), (1.0, 1.0, 1.0, 1.0, 1.0, 0.5), 1.0), 0.4375),
                  "corner on edge", ("rounded", (0.0, 0.0, 3.0, (1.0, 1.0, 1.0, 1.0), (1.0, 1.0, 1.0, 1.0), 1.0), 0.5), 57),
                 id="chamfered-star-and-rounded-square-common-edges-by-the-order"),
]


@pytest.mark.parametrize("spec", FOUND_SAME)
def test_found_same_answer_cases(spec):
    fuzzlib.check_same_answer(spec)


FOUND_PROGRAMS = [
    pytest.param(((("star", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.5),
                   ("sharp", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), 1.0), 0.171875),
                   ("mixed", (0.0, 0.0, 0.0, (1.0, 1.0, 1.0), (1.0, 0.5, 0.25), 1.0), 0.5)),
                  (("+", 0, 0, 0), ("+", 0, 0, 0), ("+", 0, 0, 0), ("+", 0, 0, 0), ("+", 1, 2, 0))),
                 id="needle-united-with-a-large-face-glue-removes-it"),
]


@pytest.mark.parametrize("spec", FOUND_PROGRAMS)
def test_found_program_cases(spec):
    fuzzlib.check_program(spec)


FOUND_SKETCHES = [
    pytest.param((("sas", 5.0, 6.0, 15.0), ("sas", 5.0, 6.0, 15.0), "corner on edge", 38),
                 id="triangles-from-two-sketches-collinear-edges-glued-again-join"),
    pytest.param((("triangle", 1.0, 1.0, 1.0), ("sas", 1.0, 1.0, 15.0), "corner on edge", 10),
                 id="corner-on-an-edge-every-boolean-empty"),
    pytest.param((("rectangle", 8.375, 9.78125), ("rectangle", 1.0, 1.0), "nudged shift", 62),
                 id="rectangle-and-its-copy-moved-by-tol-two-ends-within-tol"),
    pytest.param((("rectangle", 1.0, 1.0), ("sas", 0.5, 4.0, 116.0), "corner on corner", 38),
                 id="rectangle-and-triangle-corner-1e-13-off-a-corner-union-loses-the-triangle"),
]


@pytest.mark.parametrize("spec", FOUND_SKETCHES)
def test_found_sketch_cases(spec):
    fuzzlib.check_sketch_pair(spec)
