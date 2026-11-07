# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import pytest

import planex as px

pytestmark = pytest.mark.constraints


def coincidences(sk):
    return sorted(repr(c) for c in sk.constraints.get("Coincident", {}).values())


def tee():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0, id="bar")
    sk.Line((2, 0), (2, 3), id="stem")
    return sk


def test_an_end_at_a_line_s_middle_gets_its_midpoint():
    sk = tee()
    sk.AutoConstrain(geometric={"midpoint": True}, dimensional=False)
    assert coincidences(sk) == ['"Point.3", "bar.mid", px.Coincident']


def test_without_midpoints_the_end_lies_on_the_line():
    sk = tee()
    sk.AutoConstrain(dimensional=False)
    assert coincidences(sk) == ['"Point.3", "bar", px.Coincident']


def test_a_hole_at_an_edge_s_middle_gets_its_midpoint():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0, id="edge")
    sk.Circle((2, 0), 0.5, id="hole")
    sk.AutoConstrain(geometric={"midpoint": True}, dimensional=False)
    assert coincidences(sk) == ['"Center.1", "edge.mid", px.Coincident']
    sk2 = px.Sketch((0, 0))
    sk2.Line(4, 0, id="edge")
    sk2.Circle((2, 0), 0.5, id="hole")
    assert sk2.AutoConstrain(dimensional=False) == []


def test_a_point_off_the_middle_is_no_midpoint():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0, id="bar")
    sk.Line((1, 0), (1, 3), id="stem")
    sk.AutoConstrain(geometric={"midpoint": True}, dimensional=False)
    assert coincidences(sk) == ['"Point.3", "bar", px.Coincident']


def trapezoid(left=0.0):
    sk = px.Sketch((left, 0))
    sk.Line(6, 0, id="bottom").Line((left + 5, 3), id="right").Line((left + 1, 3), id="top").Close(id="left")
    return sk


def symmetric(sk):
    return sk.AutoConstrain(geometric={"symmetric": True}, dimensional=False)


def counts(sk):
    return {kind: len(ids) for kind, ids in sk.constraints.items()}


def test_a_symmetric_outline_gets_its_axis_as_a_construction_line():
    sk = trapezoid()
    added = symmetric(sk)
    axis = added[0]
    assert axis.startswith("Line.") and sk(axis).construction
    assert sk.Location(axis + ".start") == pytest.approx((3, 0)) and sk.Location(axis + ".end") == pytest.approx((3, 3))
    assert counts(sk) == {"Coincident": 2, "Perpendicular": 2}
    assert (sk.dof, sk.redundant) == (6, [])


def test_the_outline_stays_symmetric_when_a_side_changes():
    sk = trapezoid()
    symmetric(sk)
    sk.Constrain("bottom", px.Distance, 8)
    (a, b), (c, d) = sk.Location("bottom.start"), sk.Location("bottom.end")
    (p, q), (r, t) = sk.Location("bottom.mid"), sk.Location("top.mid")
    assert (r - p) * (c - a) + (t - q) * (d - b) == pytest.approx(0, abs=1e-9)


def test_an_axis_on_v_is_used_instead_of_a_new_line():
    sk = trapezoid(left=-3.0)
    added = symmetric(sk)
    assert not any(a.startswith("Line.") for a in added)
    assert counts(sk) == {"Coincident": 2, "Perpendicular": 2}
    assert (sk.dof, sk.redundant) == (4, [])


def plate_with_holes():
    sk = px.Sketch((0, 0))
    sk.Line(8, 0, id="bottom").Line(0, 4, id="right").Line(-8, 0, id="top").Close(id="left")
    sk.Circle((2, 2), 0.5, id="a")
    sk.Circle((6, 2), 0.5, id="b")
    return sk


def test_the_axis_mirroring_the_most_edges_wins_and_mirrored_holes_follow_it():
    sk = plate_with_holes()
    added = symmetric(sk)
    lines = [a for a in added if a.startswith("Line.")]
    assert len(lines) == 1
    assert sk.Location(lines[0] + ".start")[0] == pytest.approx(4) and sk.Location(lines[0] + ".end")[0] == pytest.approx(4)
    assert counts(sk)["Symmetric"] == 1 and counts(sk)["Equal"] == 1
    assert sk.redundant == []


def test_a_single_line_or_two_holes_alone_get_no_axis():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0)
    assert symmetric(sk) == []
    holes = px.Sketch()
    holes.Circle((0, 0), 1)
    holes.Circle((4, 0), 1)
    assert symmetric(holes) == []


def test_suggestions_take_symmetry_about_lines_the_sketch_has():
    assert len(trapezoid(left=-3.0).SuggestConstraints(geometric={"symmetric": True}, dimensional=False)) == 4
    assert trapezoid().SuggestConstraints(geometric={"symmetric": True}, dimensional=False) == []


def two_holes():
    sk = px.Sketch()
    sk.Circle((2, 1), 0.5, id="a")
    sk.Circle((6, 3), 0.5, id="b")
    return sk


def test_two_points_have_a_horizontal_distance_across_v_and_a_vertical_one_across_h():
    sk = two_holes()
    sk.Constrain("a.center", "b.center", px.V, px.Distance, 10, id="dx")
    sk.Constrain("a.center", "b.center", px.H, px.Distance, 5, id="dy")
    (ah, av), (bh, bv) = sk.Location("a.center"), sk.Location("b.center")
    assert (bh - ah, bv - av) == pytest.approx((10, 5))
    assert repr(sk.constraints["Distance"]["dx"]) == '"a.center", "b.center", px.V, px.Distance, 10'
    sk["dx"] = 6
    assert sk.Location("b.center")[0] - sk.Location("a.center")[0] == pytest.approx(6)


def test_a_distance_across_a_line_is_measured_square_to_it():
    sk = two_holes()
    sk.Line((0, 0), (1, 1), id="diag")
    sk.Constrain("diag", px.Fix)
    sk.Constrain("a.center", "b.center", "diag", px.Distance, 2)
    (ah, av), (bh, bv) = sk.Location("a.center"), sk.Location("b.center")
    assert abs((bv - av) - (bh - ah)) / 2 ** 0.5 == pytest.approx(2)


def test_the_distance_measures_as_a_reference_and_is_drawn_horizontal():
    from planex import _dimensions
    sk = two_holes()
    sk.Constrain("a.center", "b.center", px.V, px.Distance, reference=True, id="r")
    assert sk["r"] == pytest.approx(4)
    sk.Constrain("a.center", "b.center", px.V, px.Distance, 4, id="dx")
    (d,) = [d for d in _dimensions.Dimensions(sk) if d.id == "dx"]
    assert d.kind == "linear" and tuple(d.u) == pytest.approx((1, 0)) and d.span == pytest.approx(4)


def row():
    sk = px.Sketch((0, 0))
    sk.Line(80, 0, px.Horizontal, id="bottom").Line(0, 30, px.Vertical, id="right")
    sk.Line(-80, 0, px.Horizontal, id="top").Close(px.Vertical, id="left")
    for i, x in enumerate((10, 30, 50)):
        sk.Circle((x, 15), 2, id=f"h{i + 1}")
    return sk


def test_a_row_of_holes_gets_its_largest_spans_first_then_its_distance_from_the_datum_edge():
    calls = [g["call"] for g in row().SuggestConstraints(geometric={"equal_radius": True})]
    assert [c for c in calls if '.center"' in c and "px.V, px.Distance" in c] == [
        'sk.Constrain("h1.center", "h3.center", px.V, px.Distance, 40)',
        'sk.Constrain("h1.center", "h2.center", px.V, px.Distance, 20)',
        'sk.Constrain("h1.center", px.V, px.Distance, 10)']
    assert calls.index('sk.Constrain("left", "right", px.Distance, 80)') < calls.index(
        'sk.Constrain("h1.center", "h3.center", px.V, px.Distance, 40)')


def test_angles_come_first_then_sizes_then_distances_and_no_line_by_its_length():
    sk = px.Sketch((0, 0))
    sk.Line(6, 0, id="base").Line((4, 2), id="flank")
    sk.Circle((2, 1), 0.5, id="hole")
    found = [g for g in sk.SuggestConstraints(geometric=False) if g["kind"] != "Coincident"]
    kinds, calls = [g["kind"] for g in found], [g["call"] for g in found]
    assert kinds[0] == "Angle" and kinds.index("Diameter") < kinds.index("Distance")
    assert all(k != "Angle" for k in kinds[kinds.index("Diameter"):])
    assert not any('"base", px.Distance' in c or '"flank", px.Distance' in c for c in calls)
    sk.AutoConstrain(geometric=False)
    assert (sk.dof, sk.redundant) == (0, [])


def test_a_right_angle_is_no_dimension():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0, id="a").Line(0, 3, id="b")
    assert not any("px.Angle" in g["call"] for g in sk.SuggestConstraints(geometric=False))
    sk.AutoConstrain(geometric=False)
    assert (sk.dof, sk.redundant) == (0, [])


def test_a_datum_on_the_origin_is_held_there_first():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Vertical).Line(-4, 0, px.Horizontal).Close(px.Vertical)
    calls = [g["call"] for g in sk.SuggestConstraints(geometric=False)]
    assert calls[:2] == ['sk.Constrain("bottom.start", px.V, px.Coincident)', 'sk.Constrain("bottom.start", px.H, px.Coincident)']


def test_a_round_part_is_measured_from_its_largest_circle():
    sk = px.Sketch()
    sk.Circle((5, 5), 20, id="disc")
    sk.Circle((15, 5), 2, id="a")
    sk.Circle((5, 17), 2, id="b")
    calls = [g["call"] for g in sk.SuggestConstraints(geometric=False)]
    assert 'sk.Constrain("disc.center", "a.center", px.V, px.Distance, 10)' in calls
    assert 'sk.Constrain("disc.center", "b.center", px.H, px.Distance, 12)' in calls
    positions = [c for c in calls if c.count('"') == 2 and "px.Distance" in c]
    assert positions[0] == 'sk.Constrain("disc.center", px.V, px.Distance, 5)'


def test_a_fillet_is_sized_by_its_radius_and_its_centre_gets_no_dimension():
    sk = px.Sketch((0, 0))
    sk.Line(60, 0).Line(0, 8).Line(-47, 0).Arc(5, -90, id="fillet").Line(0, 27).Line(-8, 0).Close()
    relations = {"horizontal": True, "vertical": True, "perpendicular": True, "parallel": True, "equal_length": True}
    calls = [g["call"] for g in sk.SuggestConstraints(geometric=relations)]
    assert 'sk.Constrain("fillet", px.Radius, 5)' in calls
    assert not any("fillet.center" in c for c in calls)
    sk.AutoConstrain(geometric=relations)
    assert (sk.dof, sk.redundant) == (0, [])


def plate_with_four_holes():
    sk = px.Sketch((0, 0))
    sk.Line(80, 0, id="bottom").Line(0, 50, id="right").Line(-80, 0, id="top").Close(id="left")
    for i, (x, y) in enumerate(((8, 8), (72, 8), (8, 42), (72, 42))):
        sk.Circle((x, y), 4, id=f"h{i + 1}")
    return sk


def test_hole_centres_in_rows_and_columns_are_level_and_in_line():
    sk = plate_with_four_holes()
    sk.AutoConstrain(geometric={"horizontal": True, "vertical": True}, dimensional=False)
    assert counts(sk)["Horizontal"] == 4 and counts(sk)["Vertical"] == 4
    assert sk.redundant == []


def test_a_hole_pattern_in_rows_and_columns_gets_its_spacing_once():
    hv = {"horizontal": True, "vertical": True, "equal_radius": True}
    values = [g["args"][-1] for g in plate_with_four_holes().SuggestConstraints(geometric=hv) if g["kind"] == "Distance"]
    assert sum(v == pytest.approx(64) for v in values) == 1 and sum(v == pytest.approx(34) for v in values) == 1


def flange():
    import math
    sk = px.Sketch()
    sk.Circle((0, 0), 50, id="rim")
    sk.Circle((0, 0), 12.5, id="bore")
    for k in range(6):
        t = math.radians(60 * k + 30)
        sk.Circle((37.5 * math.cos(t), 37.5 * math.sin(t)), 5, id=f"h{k + 1}")
    return sk


def test_equal_holes_about_a_centre_get_a_pitch_circle_and_their_steps():
    sk = flange()
    added = sk.AutoConstrain(geometric={"pitch_circle": True, "equal_radius": True})
    circles = [a for a in added if a.startswith("Circle.")]
    assert len(circles) == 1 and sk(circles[0]).construction and sum(a.startswith("Line.") for a in added) == 6
    assert sorted(round(sk[i], 9) for i in sk.constraints["Angle"]) == [30.0] + [60.0] * 5
    assert 75.0 in [round(sk[i], 9) for i in sk.constraints["Diameter"]]
    assert (sk.dof, sk.redundant) == (0, [])


def test_a_chamfer_gets_its_angle_from_the_edge_it_meets():
    sk = px.Sketch((0, 0))
    sk.Line(40, 0, id="bottom").Line(0, 25, id="right").Line(-35, 0, id="top").Line((0, 20), id="chamfer").Close(id="left")
    hv = {"horizontal": True, "vertical": True}
    angles = [g["call"] for g in sk.SuggestConstraints(geometric=hv) if g["kind"] == "Angle"]
    assert len(angles) == 1 and '"chamfer"' in angles[0] and "px.H" not in angles[0]


def test_a_drawn_pattern_gets_no_second_pitch_circle():
    sk = px.Sketch()
    sk.Circle((0, 0), 12.5, id="bore")
    sk.Circle((37.5, 0), 5, id="h1")
    sk.CircularPattern(["h1"], "bore.center", 6)
    assert sk.AutoConstrain(geometric={"pitch_circle": True}, dimensional=False) == []


def test_of_two_relations_that_say_the_same_the_earlier_stays():
    sk = px.Sketch((0, 0))
    sk.Line(4, 0).Line(0, 2).Line(-4, 0).Close()
    lines = {"horizontal": True, "vertical": True, "perpendicular": True, "parallel": True}
    sk.AutoConstrain(geometric=lines, dimensional=False)
    assert counts(sk) == {"Horizontal": 2, "Vertical": 2}
    assert sk.redundant == []


def corner_block(scale=1.0):
    sk = px.Sketch((0, 0))
    sk.Line(80 * scale, 0).Line(0, 25 * scale).Line((50 * scale, 40 * scale), id="cut").Line((0, 40 * scale)).Close()
    return sk


HV = {"horizontal": True, "vertical": True}


def test_round_lengths_come_before_an_angle_that_is_not_round():
    found = corner_block().SuggestConstraints(geometric=HV)
    assert not any(g["kind"] == "Angle" for g in found)
    sk = corner_block()
    sk.AutoConstrain(geometric=HV)
    assert (sk.dof, sk.redundant) == (0, [])


def test_lengths_are_round_relative_to_the_sketch_s_size():
    for scale in (1.0, 1e-3, 1e3):
        sk = corner_block(scale)
        sk.AutoConstrain(geometric=HV)
        assert "Angle" not in sk.constraints and sk.dof == 0


def test_an_angle_that_nothing_round_replaces_stays():
    sk = px.Sketch((0, 0))
    sk.Line((3.3127, 1.7139), id="slant")
    found = [g for g in sk.SuggestConstraints(geometric=False) if g["kind"] != "Coincident"]
    assert found[0]["kind"] == "Angle"


def test_holes_equally_spaced_on_part_of_a_circle_get_their_pitch_circle():
    import math
    sk = px.Sketch()
    sk.Circle((0, 0), 19.25, id="bore")
    for k in range(5):
        t = math.radians(180 + (k - 2) * 360 / 7)
        sk.Circle((27.25 * math.cos(t), 27.25 * math.sin(t)), 3.3, id=f"h{k + 1}")
    added = sk.AutoConstrain(geometric={"pitch_circle": True, "equal_radius": True})
    assert sum(a.startswith("Circle.") for a in added) == 1 and sum(a.startswith("Line.") for a in added) == 5
    assert 54.5 in [round(sk[i], 9) for i in sk.constraints["Diameter"]]
    steps = [round(sk[i], 6) for i in sk.constraints["Angle"]]
    assert steps.count(round(360 / 7, 6)) == 4
    assert (sk.dof, sk.redundant) == (0, [])


def test_a_grid_of_equal_holes_is_no_pitch_circle():
    sk = px.Sketch()
    for i in range(3):
        for j in range(3):
            sk.Circle((10 * i, 10 * j), 2)
    added = sk.AutoConstrain(geometric={"pitch_circle": True}, dimensional=False)
    assert not any(a.startswith("Circle.") for a in added)


def step_texts(sk):
    from planex import _dimensions
    return {d.id: _dimensions.Text(d, None, False) for d in _dimensions.Dimensions(sk) if "Pattern" in d.id}


def test_a_grid_shows_each_step_with_its_count_and_total():
    sk = px.Sketch()
    sk.Circle((2, 3), 0.5, id="hole")
    sk.LinearPattern(["hole"], px.H, 4, 2.0, dir2=px.V, count2=3, spacing2=1.5)
    assert step_texts(sk) == {"LinearPattern.1.spacing": "3× 2 (= 6)", "LinearPattern.1.spacing2": "2× 1,5 (= 3)"}


def test_a_partial_circular_pattern_shows_its_step_and_a_full_one_none():
    sk = px.Sketch()
    sk.Circle((0, 0), 5, id="bore")
    sk.Circle((8, 0), 0.5, id="hole")
    sk.CircularPattern(["hole"], "bore.center", 5, angle=40)
    assert step_texts(sk) == {"CircularPattern.1.angle": "4× 40° (= 160°)"}
    full = px.Sketch()
    full.Circle((0, 0), 5, id="bore")
    full.Circle((8, 0), 0.5, id="hole")
    full.CircularPattern(["hole"], "bore.center", 6)
    assert step_texts(full) == {}


def test_a_step_s_total_is_written_in_the_drawing_s_millimeters():
    from planex import _dimensions, _drawing
    sk = px.Sketch()
    sk.Circle((0.002, 0.003), 0.0005, id="hole")
    sk.LinearPattern(["hole"], px.H, 7, 0.012)
    (d,) = [d for d in _dimensions.Dimensions(sk) if d.id == "LinearPattern.1.spacing"]
    assert _dimensions.Text(_drawing._in_mm(d, 1e3), None, False) == "6× 12 (= 72)"


def test_a_point_within_tol_of_an_axis_is_put_on_it_not_dimensioned_from_it():
    sk = px.Sketch()
    sk.Circle((10, 1e-9), 2, id="c")
    sk.AutoConstrain(tol=1e-6)
    assert sk.dof == 0
    on_h = [g for g in sk._Constraints() if set(g["refs"]) == {"c.center", "H"}]
    assert [g["kind"] for g in on_h] == ["Coincident"]


def test_a_spline_too_long_for_the_solver_is_joined_without_a_tangent():
    for n, tangent in ((10, True), (80, False)):
        sk = px.Sketch(auto_constraint=False)
        sk.Spline([(float(x), 0.0) for x in range(1, n)], id="s")
        sk.Line((n - 1.0, 0.0), (n + 9.0, 0.0), id="l")
        sk.AutoConstrain(dimensional=False)
        assert bool(sk.constraints.get("Tangent")) == tangent
        assert sk.Measure("s.end", "l.start")["distance"] == 0.0
