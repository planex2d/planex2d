# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import time

import pytest

import planex as px


def rect(x, y, w, h, name=None):
    f = px.QuickSketch().Move((x, y)).Rectangle(w, h).Face()
    f.name = name
    return f


def area(shape):
    return sum(f.mass for f in shape.faces)


def test_holes_of_less_area_are_filled_the_others_stay():
    plate = rect(0, 0, 10, 10, "plate") - rect(1, 1, 1, 1) - rect(5, 5, 3, 3)
    filled = plate.Simplified(remove_holes_smaller=2)
    assert area(filled) == pytest.approx(100 - 9, rel=1e-12)
    assert [f.name for f in filled.faces] == ["plate"]


def test_a_hole_with_a_face_in_it_stays():
    ring = rect(0, 0, 10, 10, "ring") - rect(3, 3, 4, 4)
    island = rect(4, 4, 2, 2, "island")
    shape = px.Glue([ring, island])
    filled = shape.Simplified(remove_holes_smaller=100)
    assert area(filled) == pytest.approx(100 - 16 + 4, rel=1e-12)
    assert sorted(f.name for f in filled.faces) == ["island", "ring"]


def test_holes_stay_by_default_and_the_export_fills_them():
    plate = rect(0, 0, 10, 10) - rect(1, 1, 1, 1)
    assert px.config.remove_holes_smaller == 0
    assert area(plate.Simplified()) == pytest.approx(99, rel=1e-12)
    pytest.importorskip("netgen.occ")
    assert plate.ToOCCShape(remove_holes_smaller=2).faces[0].mass == pytest.approx(100, rel=1e-12)


def test_a_slit_narrower_than_the_width_is_filled():
    slit = rect(0, 0, 10, 10, "copper") - rect(5, 2, 1e-6, 8)
    filled = slit.Simplified(merge_slivers_thinner=1e-3)
    assert area(filled) == pytest.approx(100, rel=1e-12)
    assert len(filled.faces) == 1
    assert all(min(abs(x), abs(x - 10), abs(y), abs(y - 10)) < 1e-12 for x, y in ((v.x, v.y) for v in filled.vertices))


def test_a_thin_face_goes_to_the_face_beside_it():
    shape = px.Glue([rect(0, 0, 1, 1, "copper"), rect(1, 0, 5e-4, 1, "board")])
    merged = shape.Simplified(merge_slivers_thinner=1e-3)
    assert [f.name for f in merged.faces] == ["copper"]
    assert area(merged) == pytest.approx(1.0005, rel=1e-12)


def test_a_strip_along_an_edge_goes_to_the_face_beside_it():
    board = rect(0, 0, 10, 10, "board")
    copper = rect(0, 0, 10, 10 - 5e-4, "copper")
    merged = px.Glue([copper, board]).Simplified(merge_slivers_thinner=1e-3)
    assert [f.name for f in merged.faces] == ["copper"]
    assert area(merged) == pytest.approx(100, rel=1e-12)


def test_a_gap_between_two_names_stays_with_a_warning():
    shape = px.Glue([rect(0, 0, 1, 1, "A"), rect(1 + 1e-4, 0, 1, 1, "B")])
    with pytest.warns(px.PlanexWarning, match="between faces of different properties kept"):
        kept = shape.Simplified(merge_slivers_thinner=1e-3)
    assert sorted(f.name for f in kept.faces) == ["A", "B"]
    assert area(kept) == pytest.approx(2, rel=1e-12)


def test_thin_faces_side_by_side_still_waiting_after_the_last_round_warn():
    strips = px.Glue([rect(0, 0, 10, 10)] + [rect(0.05 * k, 10, 0.05, 1) for k in range(6)])
    with pytest.warns(px.PlanexWarning, match="left after 4 rounds"):
        once = strips.Simplified(merge_slivers_thinner=0.2)
    assert len(once.faces) == 3
    again = once.Simplified(merge_slivers_thinner=0.2)
    assert len(again.faces) == 2 and area(again) == pytest.approx(100.3, rel=1e-12)


def test_corners_are_no_slivers():
    square = rect(0, 0, 10, 10, "plate") - rect(2, 2, 2, 2)
    same = square.Simplified(merge_slivers_thinner=0.5)
    assert area(same) == pytest.approx(96, rel=1e-12)
    assert len(same.edges) == 8 and same.arcs == []


def test_a_thin_neck_beside_nothing_is_thrown_away():
    dumbbell = px.Unify([rect(0, 0, 4, 4), rect(4, 2, 1, 1e-3), rect(5, 0, 4, 4)])
    assert len(dumbbell.faces) == 1
    split = dumbbell.Simplified(merge_slivers_thinner=1e-2)
    assert len(split.faces) == 2
    # each square keeps the segment of a circle of radius 0.005 (half the width) over the neck's mouth
    r, half = 5e-3, 5e-4
    segment = r * r * math.asin(half / r) - half * math.sqrt(r * r - half * half)
    assert area(split) == pytest.approx(32 + 2 * segment, rel=1e-12)


def test_slivers_stay_by_default():
    slit = rect(0, 0, 10, 10) - rect(5, 2, 1e-6, 8)
    assert px.config.merge_slivers_thinner == 0
    assert area(slit.Simplified()) == pytest.approx(100 - 8e-6, rel=1e-12)


def test_a_filled_gap_is_glued_into_the_edge_it_lies_along():
    # the gap's side is part of the taller face's edge; shared only, the drill hole came back filled in the cut
    hole = px.QuickSketch().Circle((5, 5), 1).Face()
    copper = px.Glue([rect(0, 0, 10, 10, "A") - hole, rect(10 + 1e-4, -5, 10, 20, "A")])
    merged = copper.Simplified(merge_slivers_thinner=1e-3)
    cut = merged * (rect(-1, -6, 30, 22) - hole)
    assert not any(f.Contains((5, 5)) for f in cut.faces)
    assert area(cut) == pytest.approx(300 - math.pi + 1e-3, rel=1e-6)


def stroked_boxes(width):
    """two 5 x 5 squares drawn with a round stroke of that width (Ucamco's gerber example 2.13.1, in mm)"""
    boxes = [px.FromPolyline([(x, 0), (x + 5, 0), (x + 5, 5), (x, 5)], closed=True) for x in (0, 10)]
    return px.Unify([px.Shape([b.Offset(width / 2, True, px.Round, px.Round)]) for b in boxes])


def test_strokes_as_wide_as_the_width_go_and_the_specks_they_leave_go_with_the_small_faces():
    # the strokes are 0.01 wide; at their inner corners a disc 12.1e-3 across fits, so the merge keeps a speck there
    boxes = stroked_boxes(0.01)
    assert area(boxes.Simplified(merge_slivers_thinner=0.005)) == pytest.approx(area(boxes), rel=1e-9)
    specks = boxes.Simplified(merge_slivers_thinner=0.01)
    assert len(specks.faces) == 8 and area(specks) < 2e-3
    assert specks.faces[0].mass < 2e-4
    assert len(boxes.Simplified(merge_slivers_thinner=0.01, remove_faces_smaller=1e-3).faces) == 0


def test_a_plate_without_thin_parts_is_left_as_it_is_quickly():
    # 400 holes of radius 0.2 every 1 in a 21 x 21 plate: nothing no wider than 0.1, so no face is opened
    plate = rect(0, 0, 21, 21, "plate") - px.Unify(
        [px.QuickSketch().Circle((1 + i, 1 + j), 0.2).Face() for i in range(20) for j in range(20)])
    start = time.perf_counter()
    merged = plate.Simplified(merge_slivers_thinner=0.1)
    assert time.perf_counter() - start < 0.5
    assert len(merged.edges) == len(plate.edges)
    assert area(merged) == pytest.approx(area(plate), rel=1e-12)
