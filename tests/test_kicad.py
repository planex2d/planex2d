# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import math
import os
import re
import shutil
import subprocess
import warnings

import pytest

import planex as px

BOARD = os.path.join(os.path.dirname(__file__), "data", "kicad", "small_board.kicad_pcb")
MM2 = 1e-6


def areas_by_name(shape):
    out = {}
    for f in shape.faces:
        out[f.name] = out.get(f.name, 0.0) + f.mass / MM2
    return out


def box_mm(face):
    (x0, y0), (x1, y1) = face.bounding_box
    return tuple(round(v * 1e3, 9) for v in (x0, y0, x1, y1))


def test_copper_layers_and_profile_in_metres():
    layers = px.FromKiCad(BOARD)
    fcu, bcu, profile = layers["F.Cu"], layers["B.Cu"], layers["Edge.Cuts"]
    quarter = math.pi / 4
    track, arc_track = 10 + quarter, (math.pi / 2) * (5.5**2 - 4.5**2) + quarter
    ring = math.pi * (1 - 0.25)
    rect, roundrect, oval = 2.0, 6 - (4 - math.pi) * 0.25, 2 + quarter
    got = areas_by_name(fcu)
    assert got["SIG"] == pytest.approx(track + arc_track + rect, rel=1e-12)
    assert got["GND"] == pytest.approx(2 * ring, rel=1e-12)
    assert got[None] == pytest.approx(roundrect + oval, rel=1e-12)
    # the zone's hole is joined to its outline by a slit, which cancels
    assert areas_by_name(bcu) == {"GND": pytest.approx(2 * ring + 36 - 4, rel=1e-12)}
    assert [f.name for f in profile.faces] == ["profile"]
    assert profile.mass / MM2 == pytest.approx(40 * 20 - 2 * math.pi * 0.25, rel=1e-12)
    for shape in (fcu, bcu, profile):
        assert sum(e.name == "pth" for e in shape.edges) >= 2


def test_footprint_rotation_and_kicad_y_down():
    fcu = px.FromKiCad(BOARD, "F.Cu")
    boxes = sorted(box_mm(f) for f in fcu.faces)
    # footprint at (10, 15) turned by 90 degrees: the pads' local x runs along -y in KiCad, up in planex
    assert (9.5, -18.0, 10.5, -16.0) in boxes
    assert (12.0, -16.5, 14.0, -13.5) in boxes
    assert (6.5, -16.5, 7.5, -13.5) in boxes
    assert (9.0, -14.0, 11.0, -12.0) in boxes


def test_layer_choice_and_errors():
    assert isinstance(px.FromKiCad(BOARD, "B.Cu"), px.Shape)
    assert [f.name for f in px.FromKiCad(BOARD, "Edge.Cuts").faces] == ["profile"]
    with pytest.raises(ValueError, match="F.Cu, B.Cu"):
        px.FromKiCad(BOARD, "In1.Cu")
    with pytest.raises(ValueError, match="2 copper layers need 1"):
        px.FromKiCad(BOARD, [0.8, 0.8])


def test_kicad_10_nets_by_name_and_unfilled_zones(tmp_path):
    # KiCad 10 names the net, even a numeric name, where KiCad 9 gives its number in the net table
    text = open(BOARD).read().replace("(net 2)", '(net "5")')
    path = tmp_path / "board.kicad_pcb"
    path.write_text(text)
    assert {"5", "SIG", "GND"} <= set(areas_by_name(px.FromKiCad(str(path), "F.Cu")))
    path.write_text(text.split("\t\t(filled_polygon")[0] + "\t)\n)\n")
    with pytest.warns(px.PlanexWarning, match="1 zone not filled"):
        px.FromKiCad(str(path), "F.Cu")


def test_stackup_in_3d():
    board = px.FromKiCad(BOARD, [1.6])
    assert [s.name for s in board.solids] == ["dielectric_1"]
    assert board.solids[0].mass == pytest.approx((40 * 20 - 2 * math.pi * 0.25) * 1.6 * 1e-9, rel=1e-6)
    names = {f.name for f in board.faces}
    assert {"pth", "SIG", "GND"} <= names
    heights = sorted({round(f.center.z * 1e3, 9) for f in board.faces if f.name in ("SIG", "GND")})
    assert heights == [0.0, 1.6]


def test_small_faces_are_thrown_away_or_merged():
    big = px.QuickSketch().Move((0, 0)).Rectangle(1, 1).Face()
    big.name = "big"
    strip = px.QuickSketch().Move((1, 0)).Rectangle(1e-4, 1).Face()
    strip.name = "strip"
    fleck = px.QuickSketch().Move((3, 3)).Rectangle(1e-3, 1e-3).Face()
    shape = px.Glue([big, strip, fleck])
    assert len(shape.faces) == 3
    thrown = shape.ToOCCShape(remove_faces_smaller=2e-4)
    assert len(thrown.faces) == 1 and thrown.mass == pytest.approx(1)
    merged = shape.ToOCCShape(remove_faces_smaller=2e-4, merge_small_faces_instead=True)
    assert len(merged.faces) == 1 and merged.mass == pytest.approx(1 + 1e-4) and merged.faces[0].name == "big"
    assert len(px.Glue([big, strip, fleck]).Simplify(remove_faces_smaller=2e-4).faces) == 1


def test_from_kicad_merges_tiny_faces_by_default():
    every = px.FromKiCad(BOARD, remove_faces_smaller=0)
    default = px.FromKiCad(BOARD)
    thrown = px.FromKiCad(BOARD, merge_small_faces_instead=False)
    assert list(every) == list(default) == list(thrown) == ["F.Cu", "B.Cu", "Edge.Cuts"]
    for a, b, c in zip(every.values(), default.values(), thrown.values()):
        assert sorted(f.mass for f in c.faces) == pytest.approx(sorted(f.mass for f in a.faces if f.mass >= 5e-8), rel=1e-9)
        assert sum(f.mass for f in b.faces) == pytest.approx(sum(f.mass for f in a.faces), rel=1e-12)
        assert all(f.mass >= 5e-8 for f in b.faces)


def test_net_names_are_unescaped(tmp_path):
    # KiCad writes "/" in a net name as {slash}
    text = open(BOARD).read().replace('"SIG"', '"/VPP{slash}MCLR"')
    path = tmp_path / "board.kicad_pcb"
    path.write_text(text)
    names = set(areas_by_name(px.FromKiCad(str(path), "F.Cu")))
    assert "/VPP/MCLR" in names and not any("{" in n for n in names if n)


def test_removing_a_small_face_keeps_the_edges_the_others_share():
    """two glued squares and a tiny one: removing the tiny face must not give the two squares separate copies of
    their common side (a crack in the mesh)"""
    def rect(x0, y0, x1, y1):
        return px.QuickSketch((x0, y0)).Rectangle(x1 - x0, y1 - y0).Face()

    g = px.Glue([rect(0, 0, 1, 1), rect(1, 0, 2, 1), rect(2, 0, 2.001, 0.001)])
    s = g.Simplified(remove_faces_smaller=1e-5)
    ends = [tuple(sorted([(round(e.start.x, 12), round(e.start.y, 12)), (round(e.end.x, 12), round(e.end.y, 12))])) for e in s.edges]
    assert len(s.faces) == 2 and len(ends) == len(set(ends)) == 7


GOLD, GREEN = (0.831, 0.686, 0.216), (0.106, 0.369, 0.125)


def rgb(c):
    return tuple(round(v, 3) for v in list(c)[:3])


def test_copper_is_gold_and_the_board_green():
    layers = px.FromKiCad(BOARD)
    copper, profile = [layers["F.Cu"], layers["B.Cu"]], layers["Edge.Cuts"]
    for layer in copper:
        assert {rgb(f.col) for f in layer.faces} == {GOLD} and {rgb(e.col) for e in layer.edges} == {GOLD}
    assert rgb(profile.faces[0].col) == GREEN
    assert {rgb(e.col) for e in profile.edges if e.name == "pth"} == {GOLD}
    assert {rgb(e.col) for e in profile.edges if e.name != "pth"} == {GREEN}
    board = px.FromKiCad(BOARD, [1.6])
    for f in board.faces:
        assert rgb(f.col) in (GOLD, GREEN) and (f.name not in ("pth", "SIG", "GND") or rgb(f.col) == GOLD), f.name
    # copper without a net has no name: gold all the same
    unnamed = sorted(round(f.mass / MM2, 6) for f in board.faces if f.name is None and rgb(f.col) == GOLD)
    assert unnamed and unnamed == sorted(round(f.mass / MM2, 6) for layer in copper for f in layer.faces if f.name is None)


def test_blind_vias_keep_the_colours(tmp_path):
    path = tmp_path / "blind.kicad_pcb"
    path.write_text('''(kicad_pcb (version 20241229) (generator "t")
 (layers (0 "F.Cu" signal) (1 "In1.Cu" signal) (2 "B.Cu" signal) (25 "Edge.Cuts" user))
 (net 0 "") (net 1 "SIG")
 (gr_rect (start 0 0) (end 10 10) (stroke (width 0.05) (type default)) (fill no) (layer "Edge.Cuts"))
 (via blind (at 5 5) (size 1) (drill 0.4) (layers "F.Cu" "In1.Cu") (net 1))
)''')
    board = px.FromKiCad(str(path), [0.5, 1.0])
    assert [s.name for s in board.solids] == ["dielectric_1", "dielectric_2"]
    walls = [f for f in board.faces if f.name == "pth"]
    assert walls and {rgb(f.col) for f in walls} == {GOLD}
    assert {rgb(f.col) for f in board.faces if f.name not in ("pth", "SIG")} == {GREEN}
    assert {rgb(f.col) for f in board.faces if f.name == "SIG"} == {GOLD}


LAYERS = '''(layers (0 "F.Cu" signal) %s(2 "B.Cu" signal)
 (9 "F.Adhes" user "F.Adhesive") (11 "B.Adhes" user "B.Adhesive") (13 "F.Paste" user) (15 "B.Paste" user)
 (5 "F.SilkS" user "F.Silkscreen") (7 "B.SilkS" user "B.Silkscreen") (1 "F.Mask" user) (3 "B.Mask" user)
 (17 "Dwgs.User" user "User.Drawings") (19 "Cmts.User" user "User.Comments") (21 "Eco1.User" user "User.Eco1")
 (23 "Eco2.User" user "User.Eco2") (25 "Edge.Cuts" user) (27 "Margin" user)
 (31 "F.CrtYd" user "F.Courtyard") (29 "B.CrtYd" user "B.Courtyard") (35 "F.Fab" user) (33 "B.Fab" user))'''


def kicad_board(tmp_path, body, version=20241229, inner=0, setup=""):
    """a board with KiCad 9's whole layer table (inner copper layers In1.Cu, ... too) and a 60 x 40 mm outline"""
    copper = "".join('(%d "In%d.Cu" signal) ' % (2 * k + 2, k) for k in range(1, inner + 1))
    path = tmp_path / ("board_%d.kicad_pcb" % len(list(tmp_path.iterdir())))
    outline = '(gr_rect (start 0 0) (end 60 40) (stroke (width 0.05) (type default)) (fill no) (layer "Edge.Cuts"))'
    path.write_text('(kicad_pcb (version %d) (generator "t")\n %s\n (setup %s)\n (net 0 "") (net 1 "A")\n %s\n %s\n)' %
                    (version, LAYERS % copper, setup, outline, body))
    return str(path)


def areas_mm2(shape):
    return sorted(f.mass / MM2 for f in shape.faces)


def test_copper_layers_are_there_even_when_empty(tmp_path):
    track = '(segment (start 5 5) (end 15 5) (width 1) (layer "F.Cu") (net 1))'
    layers = px.FromKiCad(kicad_board(tmp_path, track, inner=2))
    assert list(layers) == ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "Edge.Cuts"]
    assert [len(layers[n].faces) for n in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")] == [1, 0, 0, 0]


def test_drawings_on_the_other_layers_as_kicad_prints_them(tmp_path):
    body = '''(gr_line (start 5 5) (end 15 5) (stroke (width 0.2) (type solid)) (layer "F.SilkS"))
 (gr_poly (pts (xy 20 5) (xy 22 5) (xy 22 7) (xy 20 7)) (stroke (width 0) (type solid)) (fill yes) (layer "F.SilkS"))
 (footprint "t:a" (layer "F.Cu") (at 30 10 90)
  (fp_rect (start -1 -1) (end 1 1) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))
  (fp_circle (center 5 0) (end 6 0) (stroke (width 0.2) (type solid)) (fill no) (layer "B.SilkS")))'''
    layers = px.FromKiCad(kicad_board(tmp_path, body))
    assert list(layers) == ["F.Cu", "B.Cu", "Edge.Cuts", "F.SilkS", "B.SilkS", "F.Fab"]
    assert areas_mm2(layers["F.SilkS"]) == pytest.approx([2 + 0.01 * math.pi, 4.0], rel=1e-9)
    # the rectangle's outline 0.1 wide: a 2.1 mm square with corners of radius 0.05 around a 1.9 mm square
    assert areas_mm2(layers["F.Fab"]) == pytest.approx([0.79 + 0.0025 * math.pi], rel=1e-9)
    ring, = layers["B.SilkS"].faces
    assert ring.mass / MM2 == pytest.approx(0.4 * math.pi, rel=1e-9)
    # (5, 0) in the footprint turned by 90 degrees is (30, 5) in KiCad, y down
    assert box_mm(ring) == pytest.approx((28.9, -6.1, 31.1, -3.9), abs=1e-9)
    assert all(f.name is None for n in ("F.SilkS", "B.SilkS", "F.Fab") for f in layers[n].faces)


MASK_BODY = '''(footprint "t:a" (layer "F.Cu") (at 10 10)
  (solder_mask_margin 0.1)
  (pad "1" smd rect (at -4 0) (size 2 1) (layers "F.Cu" "F.Mask" "F.Paste") (solder_mask_margin 0.2))
  (pad "2" smd circle (at 0 0) (size 1 1) (layers "F.Cu" "F.Mask"))
  (pad "3" smd roundrect (at 4 0) (size 2 1) (layers "F.Cu" "F.Mask") (roundrect_rratio 0.25))
  (pad "4" smd oval (at 8 0) (size 2 1) (layers "F.Cu" "F.Mask") (solder_mask_margin -0.1)))
 (footprint "t:b" (layer "F.Cu") (at 10 20)
  (pad "5" thru_hole circle (at 0 0) (size 2 2) (drill 1) (layers "*.Cu" "*.Mask") (net 1 "A"))
  (pad "" np_thru_hole circle (at 5 0) (size 3 3) (drill 3) (layers "*.Cu" "*.Mask"))
  (pad "7" smd rect (at 10 0) (size 1 1) (layers "F.Mask"))
  (pad "8" smd roundrect (at 15 0) (size 2 1) (layers "F.Cu" "F.Paste") (roundrect_rratio 0.25)
   (solder_paste_margin 0.1) (solder_paste_margin_ratio 0)))
 (via (at 30 10) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net 1))
 (via (at 35 10) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (tenting none) (net 1))
 (segment (start 5 30) (end 15 30) (width 0.5) (layers "F.Cu" "F.Mask") (solder_mask_margin 0.1) (net 1))
 (gr_rect (start 20 29) (end 22 31) (stroke (width 0.1) (type solid)) (fill yes) (layers "F.Cu" "F.Mask") (net 1))
 (gr_circle (center 30 30) (end 31 30) (stroke (width 0) (type solid)) (fill yes) (layer "F.Mask"))'''


def test_mask_and_paste_layers_are_the_openings(tmp_path):
    """KiCad 9's margins, the same as the Gerbers kicad-cli 9.0.5 plots of this board: the pad's margin, else the
    footprint's, else the board's setup; none for a pad on the mask alone; a rectangle grown gets round corners, a round
    rectangle keeps its corner ratio; paste: margin plus ratio times the size; vias open where not tented"""
    setup = ("(pad_to_mask_clearance 0.05) (pad_to_paste_clearance -0.05) (pad_to_paste_clearance_ratio -0.1) "
             "(tenting front)")
    layers = px.FromKiCad(kicad_board(tmp_path, MASK_BODY, setup=setup))
    assert list(layers) == ["F.Cu", "B.Cu", "Edge.Cuts", "F.Paste", "F.Mask", "B.Mask"]
    pi = math.pi
    pads = [3.2 + 0.04 * pi,    # 2 x 1 rectangle, its own margin 0.2: corners of radius 0.2
            0.36 * pi,          # circle 1, the footprint's margin 0.1
            2.28 + 0.09 * pi,   # 2 x 1 round rectangle (ratio 0.25) grown to 2.2 x 1.2, radius 0.25 * 1.2
            0.8 + 0.16 * pi,    # 2 x 1 oval, its own margin -0.1: 1.8 x 0.8
            1.1025 * pi,        # plated pad 2, the board's margin 0.05
            2.4025 * pi,        # unplated pad 3, the board's margin too
            1.0]                # pad on F.Mask alone: no margin
    drawn = [0.2025 * pi,       # the via with (tenting none), 0.8 + 2 * 0.05
             7 + 0.1225 * pi,   # track 0.5 wide with its mask margin 0.1: 0.7 wide
             4.8 + 0.01 * pi,   # filled 2 x 2 copper rectangle, outline 0.1: grown by 0.05 + the board's 0.05
             pi]                # disc drawn on F.Mask
    assert areas_mm2(layers["F.Mask"]) == pytest.approx(sorted(pads + drawn), rel=1e-9)
    # the board tents the vias on the front only
    back = [1.1025 * pi, 2.4025 * pi, 0.2025 * pi, 0.2025 * pi]
    assert areas_mm2(layers["B.Mask"]) == pytest.approx(sorted(back), rel=1e-9)
    # 2 x 1: -0.05 - 0.1 * 2 and -0.05 - 0.1 * 1 a side; the round rectangle's own margin 0.1 and ratio 0
    assert areas_mm2(layers["F.Paste"]) == pytest.approx(sorted([1.05, 2.28 + 0.09 * pi]), rel=1e-9)
    assert all(f.name is None for n in ("F.Paste", "F.Mask", "B.Mask") for f in layers[n].faces)


def test_kicad_10_tenting(tmp_path):
    body = '''(via (at 10 10) (size 1) (drill 0.5) (layers "F.Cu" "B.Cu") (net 1))
 (via (at 20 10) (size 1) (drill 0.5) (layers "F.Cu" "B.Cu") (tenting (front no) (back none)) (net 1))'''
    layers = px.FromKiCad(kicad_board(tmp_path, body, version=20260206, setup="(tenting (front yes) (back no))"))
    assert areas_mm2(layers["F.Mask"]) == pytest.approx([0.25 * math.pi], rel=1e-9)
    assert areas_mm2(layers["B.Mask"]) == pytest.approx([0.25 * math.pi] * 2, rel=1e-9)


def test_text_is_left_out_with_a_count(tmp_path):
    font = "(effects (font (size 1 1) (thickness 0.15)))"
    body = '''(gr_text "x" (at 5 5) (layer "F.SilkS") %s)
 (gr_text "y" (at 5 9) (layer "F.Cu") %s)
 (footprint "t:a" (layer "F.Cu") (at 20 20)
  (property "Reference" "R1" (at 0 -2) (layer "F.SilkS") %s)
  (property "Value" "10k" (at 0 2) (layer "F.Fab") (hide yes) %s)
  (fp_text user "${REFERENCE}" (at 0 0) (layer "F.Fab") %s)
  (fp_line (start -1 -1) (end 1 -1) (stroke (width 0.12) (type solid)) (layer "F.SilkS")))''' % ((font,) * 5)
    path = kicad_board(tmp_path, body)
    with pytest.warns(px.PlanexWarning, match=r"FromKiCad: 4 texts left out \(not supported yet\)"):
        layers = px.FromKiCad(path)
    assert "F.SilkS" in layers and "F.Fab" not in layers
    with pytest.warns(px.PlanexWarning, match="FromKiCad: 1 text left out"):
        px.FromKiCad(path, layer="F.Cu")


def test_version_check(tmp_path):
    older = r"version 20240108 is older than KiCad 9 \(20241229\); open the board in KiCad 9 or later and save it"
    with pytest.raises(ValueError, match=older):
        px.FromKiCad(kicad_board(tmp_path, "", version=20240108))
    with pytest.warns(px.PlanexWarning, match=r"file version 20270101 is newer than the newest tested \(20260206\)"):
        px.FromKiCad(kicad_board(tmp_path, "", version=20270101))
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        assert list(px.FromKiCad(kicad_board(tmp_path, "", version=20260206))) == ["F.Cu", "B.Cu", "Edge.Cuts"]


def test_plated_and_unplated_holes_are_named_pth_and_npth(tmp_path):
    body = '''(via (at 10 10) (size 1) (drill 0.5) (layers "F.Cu" "B.Cu") (net 1))
 (footprint "t:a" (layer "F.Cu") (at 20 10)
  (pad "1" thru_hole circle (at 0 0) (size 2 2) (drill 1) (layers "*.Cu" "*.Mask") (net 1 "A"))
  (pad "" np_thru_hole circle (at 10 0) (size 3 3) (drill 3) (layers "*.Cu" "*.Mask")))
 (zone (net 1) (net_name "A") (layer "B.Cu") (polygon (pts (xy 25 5) (xy 35 5) (xy 35 15) (xy 25 15)))
  (filled_polygon (layer "B.Cu") (pts (xy 25 5) (xy 35 5) (xy 35 15) (xy 25 15))))'''
    path = kicad_board(tmp_path, body)
    layers = px.FromKiCad(path)

    def names(shape):
        return {e.name for e in shape.edges}

    assert "pth" in names(layers["F.Cu"]) and "npth" not in names(layers["F.Cu"])
    assert {"pth", "npth"} <= names(layers["B.Cu"])
    assert {"pth", "npth"} <= names(layers["Edge.Cuts"])
    assert not any("via" in names(s) for s in layers.values())
    assert layers["Edge.Cuts"].mass / MM2 == pytest.approx(60 * 40 - math.pi * (0.25**2 + 0.5**2 + 1.5**2), rel=1e-12)
    board = px.FromKiCad(path, [1.6])
    walls = [f for f in board.faces if f.name in ("pth", "npth")]
    assert {f.name for f in walls} == {"pth", "npth"} and {rgb(f.col) for f in walls} == {GOLD}


def test_one_layer_or_an_unknown_one(tmp_path):
    path = kicad_board(tmp_path, '(gr_line (start 5 5) (end 15 5) (stroke (width 0.2) (type solid)) (layer "F.SilkS"))')
    silk = px.FromKiCad(path, layer="F.SilkS")
    assert isinstance(silk, px.Shape) and len(silk.faces) == 1
    margin = px.FromKiCad(path, layer="Margin")
    assert isinstance(margin, px.Shape) and not margin.faces
    with pytest.raises(ValueError, match=r"layer='User.9'\): the board's layers are F.Cu, B.Cu, F.Adhes, B.Adhes"):
        px.FromKiCad(path, layer="User.9")


EXAMPLES = os.path.join(os.path.dirname(__file__), "..", "notebooks", "examples", "kicad")
PIC = os.path.join(os.path.dirname(__file__), "data", "kicad", "pic_programmer.kicad_pcb")


@pytest.fixture(scope="module")
def pic():
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        layers = px.FromKiCad(PIC)
    return layers, [str(w.message) for w in caught]


def test_kicad_10_demo_board_has_every_layer_with_something_in_kicad_order(pic):
    layers, caught = pic
    assert list(layers) == ["F.Cu", "B.Cu", "Edge.Cuts", "F.SilkS", "B.SilkS", "F.Mask", "B.Mask", "Cmts.User",
                            "F.CrtYd", "B.CrtYd", "F.Fab"]
    assert "FromKiCad: 223 texts left out (not supported yet)" in caught
    assert not [w for w in caught if "version" in w]


def test_kicad_10_demo_board_silkscreen_and_fab_are_faces(pic):
    """the drawings lie on the board, the fab drawings up to a centimetre beyond its edge; the ZIF socket P3 draws its
    lever on the silkscreen past the top edge (KiCad's DRC flags it): the lever's arc through (-5.080051, -22.86),
    (-3.168, -24.765038), (-1.27, -22.846) in the footprint at (175.26, 50.8), 0.381 wide, reaches KiCad y 25.8444 mm"""
    layers, _ = pic
    (x0, y0), (x1, y1) = layers["Edge.Cuts"].faces[0].bounding_box
    for name in ("F.SilkS", "B.SilkS", "F.Fab"):
        faces = layers[name].faces
        assert faces and all(f.name is None and f.mass > 0 for f in faces), name
        for f in faces:
            (a, b), (c, d) = f.bounding_box
            assert x0 - 0.01 < a < c < x1 + 0.01 and y0 - 0.01 < b < d, name
            assert d < y1 + 0.01 or name == "F.SilkS", name
    top = max(f.bounding_box[1][1] for f in layers["F.SilkS"].faces)
    assert top == pytest.approx(-0.0258444491402, abs=1e-12)


def test_kicad_10_demo_board_own_stackup():
    from planex._kicad import _Board

    assert _Board(PIC).dielectrics() == [1.51]


def test_ids_of_the_layers():
    assert all(s.id.startswith("KiCad.") for s in px.FromKiCad(BOARD).values())
    named = px.FromKiCad(BOARD, id="board")
    assert [s.id for s in named.values()] == ["board.F.Cu", "board.B.Cu", "board.Edge.Cuts"]
    assert px.Find("board.Edge.Cuts") is named["Edge.Cuts"]


def test_every_stackup_has_a_thickness_per_dielectric():
    assert px.stackups.jlc7628 == {4: [0.2104, 1.065, 0.2104], 6: [0.2104, 0.4, 0.2028, 0.4, 0.2104]}
    names = [n for n in px.stackups.__all__ if n != "kicad"]
    assert len(names) == 23
    for name in names:
        stackup = getattr(px.stackups, name)
        assert stackup and set(stackup) <= {4, 6}, name
        for n, thicknesses in stackup.items():
            assert len(thicknesses) == n - 1 and all(0 < t < 1.3 for t in thicknesses), name
    assert repr(px.stackups.kicad) == "px.stackups.kicad"


def solid_volumes_mm3(board):
    return {s.name: s.mass * 1e9 for s in board.solids}


def test_a_stackup_gives_the_entry_for_the_boards_copper_layers(tmp_path):
    path = kicad_board(tmp_path, '(via (at 10 10) (size 1) (drill 0.5) (layers "F.Cu" "B.Cu") (net 1))', inner=2)
    area = 60 * 40 - math.pi * 0.25**2
    expected = {"dielectric_1": area * 0.2104, "dielectric_2": area * 1.065, "dielectric_3": area * 0.2104}
    assert solid_volumes_mm3(px.FromKiCad(path, px.stackups.jlc7628)) == pytest.approx(expected, rel=1e-6)
    assert solid_volumes_mm3(px.FromKiCad(path, [0.2104, 1.065, 0.2104])) == pytest.approx(expected, rel=1e-6)
    six = kicad_board(tmp_path, "", inner=4)
    volumes = solid_volumes_mm3(px.FromKiCad(six, px.stackups.jlc7628))
    assert [volumes["dielectric_%d" % k] for k in range(1, 6)] == pytest.approx(
        [2400 * t for t in (0.2104, 0.4, 0.2028, 0.4, 0.2104)], rel=1e-6)


def test_a_stackup_without_an_entry_for_the_board_raises():
    with pytest.raises(ValueError, match="the stackup has entries for 4 and 6 copper layers, the board has 2"):
        px.FromKiCad(BOARD, px.stackups.jlc7628)
    with pytest.raises(ValueError, match="the stackup has entries for 6 copper layers, the board has 2"):
        px.FromKiCad(BOARD, px.stackups.jlc7628d)


def test_the_stackup_in_the_board_file(tmp_path):
    stackup = ('(stackup (layer "F.Mask" (type "Top Solder Mask") (thickness 0.01)) '
               '(layer "F.Cu" (type "copper") (thickness 0.035)) '
               '(layer "dielectric 1" (type "prepreg") (thickness 0.1) (material "FR4") (epsilon_r 4.5) '
               'addsublayer (thickness 0.2 locked) (material "FR4") (epsilon_r 4.5)) '
               '(layer "B.Cu" (type "copper") (thickness 0.035)) (layer "B.Mask" (type "Bottom Solder Mask") '
               '(thickness 0.01)))')
    board = px.FromKiCad(kicad_board(tmp_path, "", setup=stackup), px.stackups.kicad)
    assert solid_volumes_mm3(board) == pytest.approx({"dielectric_1": 2400 * 0.3}, rel=1e-6)
    with pytest.raises(ValueError, match=r"px.stackups.kicad\): the board file has no stackup"):
        px.FromKiCad(BOARD, px.stackups.kicad)


def test_video_board_own_stackup_is_its_dielectrics():
    """the dielectrics px.stackups.kicad takes from the file, without building the board in 3d (one small 3d build with
    the board's own stackup is test_the_stackup_in_the_board_file)"""
    from planex._kicad import _Board

    path = os.path.join(EXAMPLES, "video.kicad_pcb")
    if not os.path.exists(path):
        pytest.skip("notebooks/examples/kicad/video.kicad_pcb is not there")
    assert _Board(path).dielectrics() == [0.480066] * 3


def test_stackups_has_only_the_stackups_and_kicad():
    public = [n for n in dir(px.stackups) if not n.startswith("_")]
    assert sorted(public) == sorted(px.stackups.__all__) and "kicad" in public and len(public) == 24

def test_what_is_left_out_is_named_with_counts(tmp_path):
    font = "(effects (font (size 1 1) (thickness 0.15)))"
    body = '''(gr_text "x" (at 5 5) (layer "F.SilkS") %s)
 (dimension (type aligned) (layer "Dwgs.User") (pts (xy 5 30) (xy 25 30)) (height 2) (gr_text "20 mm" (at 15 27)
  (layer "Dwgs.User") %s))
 (dimension (type aligned) (layer "Dwgs.User") (pts (xy 5 35) (xy 25 35)) (height 2))
 (table (column_count 1) (layer "F.Fab"))
 (barcode (at 40 30) (layer "F.SilkS") (size 5 5) (text "planex"))
 (gr_rect (start 30 5) (end 40 10) (stroke (width 0.2) (type solid)) (fill hatch) (layer "F.SilkS"))''' % (font, font)
    left_out = r"FromKiCad: 1 text, 2 dimensions, 1 table, 1 barcode, 1 hatched fill left out \(not supported yet\)"
    with pytest.warns(px.PlanexWarning, match=left_out):
        layers = px.FromKiCad(kicad_board(tmp_path, body, version=20260206))
    # the hatched rectangle's outline is printed, its hatch left out
    w = 0.2
    outline = (10 + w) * (5 + w) - (4 - math.pi) * (w / 2) ** 2 - (10 - w) * (5 - w)
    assert areas_mm2(layers["F.SilkS"]) == pytest.approx([outline], rel=1e-9)
    assert "Dwgs.User" not in layers and "F.Fab" not in layers


def bezier_length(p, n=20000):
    """the length of a cubic Bezier curve, by the midpoint rule"""
    total = 0.0
    for k in range(n):
        t = (k + 0.5) / n
        d = [3 * (1 - t) ** 2 * (p[1][i] - p[0][i]) + 6 * (1 - t) * t * (p[2][i] - p[1][i])
             + 3 * t * t * (p[3][i] - p[2][i]) for i in (0, 1)]
        total += math.hypot(*d) / n
    return total


def test_bezier_curves_and_rounded_rectangles(tmp_path):
    """the Bezier curve stroked as KiCad 9.0.5 plots it (its Gerber has 4.982890 mm2 here): width times length plus the
    round ends, its radius of curvature (3.9 mm at least) far above half the width; the spline's own approximation
    (planex's spline_tol) bounds the difference"""
    pts = [(5, 20), (10, 15), (15, 25), (20, 20)]
    body = '''(gr_curve (pts (xy 5 20) (xy 10 15) (xy 15 25) (xy 20 20)) (stroke (width 0.3) (type solid))
  (layer "F.SilkS"))
 (gr_rect (start 30 5) (end 40 10) (radius 1) (stroke (width 0) (type solid)) (fill yes) (layer "F.Fab"))
 (gr_rect (start 30 15) (end 40 20) (radius 4) (stroke (width 0) (type solid)) (fill yes) (layer "F.Fab"))'''
    layers = px.FromKiCad(kicad_board(tmp_path, body, version=20260206))
    assert areas_mm2(layers["F.SilkS"]) == pytest.approx([0.3 * bezier_length(pts) + math.pi * 0.15 ** 2], rel=2e-4)
    # KiCad 10 clamps the radius to half the shorter side
    assert areas_mm2(layers["F.Fab"]) == pytest.approx(sorted([50 - (4 - math.pi), 50 - (4 - math.pi) * 2.5 ** 2]),
                                                       rel=1e-9)


def test_kicad_10_grows_round_rectangles_exactly(tmp_path):
    """KiCad 10 plots a round rectangle grown alike in x and y with its radius grown by the margin (KiCad 9 keeps the
    corner ratio, test_mask_and_paste_layers_are_the_openings)"""
    setup = ("(pad_to_mask_clearance 0.05) (pad_to_paste_clearance -0.05) (pad_to_paste_clearance_ratio -0.1) "
             "(tenting front)")
    layers = px.FromKiCad(kicad_board(tmp_path, MASK_BODY, version=20260206, setup=setup))
    pi = math.pi
    exact = 2.15 + 0.1225 * pi  # 2 x 1, radius 0.25 grown by 0.1: 2.2 x 1.2 with radius 0.35
    front = [3.2 + 0.04 * pi, 0.36 * pi, exact, 0.8 + 0.16 * pi, 1.1025 * pi, 2.4025 * pi, 1.0,
             0.2025 * pi, 7 + 0.1225 * pi, 4.8 + 0.01 * pi, pi]
    assert areas_mm2(layers["F.Mask"]) == pytest.approx(sorted(front), rel=1e-9)
    assert areas_mm2(layers["F.Paste"]) == pytest.approx(sorted([1.05, exact]), rel=1e-9)


def test_solder_mask_minimum_width_closes_narrow_gaps(tmp_path):
    """KiCad's mask with a minimum width (plot_board_layers.cpp, GenerateLayerPoly): the openings grown by half the
    width less a nanometre, united, shrunk back, the exact openings added; the pads' openings then grow exactly (a round
    rectangle's radius by its margin). kicad-cli 9.0.5's Gerber of this board: 2.1817 and 5.5069 mm2 (its corners and
    arcs approximated), 1 and 1"""
    body = '''(footprint "t:a" (layer "F.Cu") (at 10 10)
  (pad "1" smd rect (at 0 0) (size 1 1) (layers "F.Cu" "F.Mask") (solder_mask_margin 0))
  (pad "2" smd rect (at 1.2 0) (size 1 1) (layers "F.Cu" "F.Mask") (solder_mask_margin 0))
  (pad "3" smd roundrect (at 6 0) (size 2 1) (layers "F.Cu" "F.Mask") (roundrect_rratio 0.25)
   (solder_mask_margin 0.5))
  (pad "4" smd rect (at 12 0) (size 1 1) (layers "F.Cu" "F.Mask") (solder_mask_margin 0))
  (pad "5" smd rect (at 13.3 0) (size 1 1) (layers "F.Cu" "F.Mask") (solder_mask_margin 0)))'''
    layers = px.FromKiCad(kicad_board(tmp_path, body, setup="(pad_to_mask_clearance 0) (solder_mask_min_width 0.25)"))
    h = 0.125 - 1e-6
    angle = 2 * math.asin(0.1 / h)
    # two squares 0.2 apart: joined, the gap filled but for two circular segments of radius h on a chord of 0.2
    joined = 2.2 - h * h * (angle - math.sin(angle))
    expected = [1.0, 1.0, joined, 3.75 + 0.5625 * math.pi]
    assert areas_mm2(layers["F.Mask"]) == pytest.approx(sorted(expected), rel=1e-9)


def test_only_copper_and_the_outline_are_cleaned(tmp_path):
    """a silkscreen dot smaller than remove_faces_smaller stays: the other layers keep what KiCad prints"""
    path = kicad_board(tmp_path, '(gr_circle (center 5 5) (end 5.1 5) (stroke (width 0) (type solid)) (fill yes) '
                                 '(layer "F.SilkS"))')
    for small in (5e-8, 1e-5):
        layers = px.FromKiCad(path, remove_faces_smaller=small, merge_small_faces_instead=False)
        assert areas_mm2(layers["F.SilkS"]) == pytest.approx([0.01 * math.pi], rel=1e-9)
        assert areas_mm2(px.FromKiCad(path, "F.SilkS", remove_faces_smaller=small)) == pytest.approx([0.01 * math.pi])


MAC_KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
WINDOWS_KICAD_CLI = r"C:\Program Files\KiCad\%s\bin\kicad-cli.exe"
ISFILE = os.path.isfile


class FakeKiCadCli:
    """subprocess.run as kicad-cli answers it: its version, and a conversion that writes BOARD or fails with stderr"""

    def __init__(self, version="10.0.0", stderr=None):
        self.version, self.stderr, self.calls = version, stderr, []

    def __call__(self, args, **kwargs):
        self.calls.append(list(args))
        if args[1] == "version":
            return subprocess.CompletedProcess(args, 0, self.version + "\n", "")
        if self.stderr is not None:
            return subprocess.CompletedProcess(args, 1, "", self.stderr)
        shutil.copyfile(BOARD, args[args.index("-o") + 1])
        return subprocess.CompletedProcess(args, 0, "", "")


@pytest.fixture
def no_kicad(monkeypatch):
    """no kicad-cli anywhere: not in $PLANEX_KICAD_CLI, not on the PATH, not where KiCad installs it"""
    monkeypatch.delenv("PLANEX_KICAD_CLI", raising=False)
    monkeypatch.setattr(shutil, "which", lambda name: None)
    monkeypatch.setattr(os.path, "isfile", lambda p: p != MAC_KICAD_CLI and ISFILE(p))
    monkeypatch.setattr(glob, "glob", lambda pattern: [])
    return monkeypatch


@pytest.fixture
def eagle(tmp_path):
    path = tmp_path / "board.brd"
    path.write_text('<?xml version="1.0" encoding="utf-8"?>\n<eagle version="9.6.2"></eagle>\n')
    return str(path)


def test_a_board_of_another_program_needs_kicad_10(no_kicad, eagle, tmp_path):
    """kicad-cli is mocked: the real conversion is untested (no KiCad on the test server, KiCad 9 has no pcb import)"""
    assert px.FromKiCad(BOARD, "Edge.Cuts").faces
    missing = "FromKiCad(%r): KiCad >= 10 (kicad-cli) is not installed; it converts Eagle boards" % eagle
    with pytest.raises(RuntimeError, match=re.escape(missing) + "$"):
        px.FromKiCad(eagle)
    gone = str(tmp_path / "missing" / "kicad-cli")
    no_kicad.setenv("PLANEX_KICAD_CLI", gone)
    with pytest.raises(RuntimeError, match=re.escape("%s (%s cannot be run)" % (missing, gone)) + "$"):
        px.FromKiCad(eagle)
    no_kicad.delenv("PLANEX_KICAD_CLI")
    no_kicad.setattr(shutil, "which", lambda name: "/usr/bin/kicad-cli")
    old = FakeKiCadCli("9.0.5")
    no_kicad.setattr(subprocess, "run", old)
    with pytest.raises(RuntimeError, match=r"it converts Eagle boards \(found kicad-cli 9\.0\.5\)$"):
        px.FromKiCad(eagle)
    assert old.calls == [["/usr/bin/kicad-cli", "version"]]


def test_a_board_of_another_program_is_converted_by_kicad_cli(no_kicad, eagle):
    no_kicad.setattr(shutil, "which", lambda name: "/usr/bin/kicad-cli")
    cli = FakeKiCadCli()
    no_kicad.setattr(subprocess, "run", cli)
    layers = px.FromKiCad(eagle)
    version, convert = cli.calls
    assert version == ["/usr/bin/kicad-cli", "version"]
    assert convert[:6] == ["/usr/bin/kicad-cli", "pcb", "import", "--format", "auto", "-o"] and convert[7:] == [eagle]
    assert convert[6].endswith(".kicad_pcb") and not os.path.exists(os.path.dirname(convert[6]))
    native = px.FromKiCad(BOARD)
    assert list(layers) == list(native)
    for name in native:
        assert areas_by_name(layers[name]) == pytest.approx(areas_by_name(native[name]), rel=1e-12)
    with pytest.raises(ValueError, match=re.escape("FromKiCad(%r, stackup): 2 copper layers need 1" % eagle)):
        px.FromKiCad(eagle, [0.8, 0.8])


def test_kicad_cli_is_taken_from_the_variable_the_path_then_where_kicad_installs_it(no_kicad, eagle):
    def used():
        cli = FakeKiCadCli()
        no_kicad.setattr(subprocess, "run", cli)
        px.FromKiCad(eagle, "Edge.Cuts")
        return cli.calls[1][0]

    no_kicad.setattr(glob, "glob", lambda pattern: [WINDOWS_KICAD_CLI % v for v in ("9.0", "10.0", "8.0")]
                     if pattern == WINDOWS_KICAD_CLI % "*" else [])
    assert used() == WINDOWS_KICAD_CLI % "10.0"
    no_kicad.setattr(os.path, "isfile", lambda p: p == MAC_KICAD_CLI or ISFILE(p))
    assert used() == MAC_KICAD_CLI
    no_kicad.setattr(shutil, "which", lambda name: "/usr/bin/kicad-cli" if name == "kicad-cli" else None)
    assert used() == "/usr/bin/kicad-cli"
    no_kicad.setenv("PLANEX_KICAD_CLI", "/opt/kicad/bin/kicad-cli")
    assert used() == "/opt/kicad/bin/kicad-cli"


def test_a_failed_conversion_raises_with_kicad_clis_message(no_kicad, eagle, tmp_path):
    no_kicad.setattr(shutil, "which", lambda name: "/usr/bin/kicad-cli")
    no_kicad.setattr(subprocess, "run", FakeKiCadCli(stderr="Failed to load board\n"))
    with pytest.raises(ValueError, match=re.escape("FromKiCad(%r): kicad-cli could not convert the Eagle board: "
                                                   "Failed to load board" % eagle) + "$"):
        px.FromKiCad(eagle)
    other = tmp_path / "board.xyz"
    other.write_text("not a board\n")
    with pytest.raises(ValueError, match="could not convert the non-KiCad board: Failed to load board$"):
        px.FromKiCad(str(other))


def test_a_legacy_kicad_board_is_not_converted(no_kicad, tmp_path):
    cli = FakeKiCadCli()
    no_kicad.setattr(subprocess, "run", cli)
    no_kicad.setattr(shutil, "which", lambda name: "/usr/bin/kicad-cli")
    for name in ("old.brd", "old.kicad_pcb"):
        path = tmp_path / name
        path.write_text("PCBNEW-BOARD Version 1 date 01/01/2015 12:00:00\n\n# Created by Pcbnew(2013-07-07)\n")
        legacy = "FromKiCad(%r): a legacy KiCad board (before KiCad 6); open it in KiCad 9 or later and save it"
        with pytest.raises(ValueError, match=re.escape(legacy % str(path)) + "$"):
            px.FromKiCad(str(path))
    assert cli.calls == []


def test_a_drill_offset_moves_the_copper_not_the_hole(tmp_path):
    body = '''(footprint "t:a" (layer "F.Cu") (at 20 10)
  (pad "1" thru_hole rect (at 0 0) (size 2 2) (drill 0.5 (offset 0.3 0)) (layers "*.Cu") (net 1 "A")))'''
    copper = px.FromKiCad(kicad_board(tmp_path, body), "F.Cu", remove_faces_smaller=0)
    (face,) = copper.faces
    x0, _, x1, _ = box_mm(face)
    assert (x0 + x1) / 2 == pytest.approx(20.3, abs=1e-9)
    centres = [e.center[0] for e in copper.circles] + [e.center[0] for e in copper.arcs]
    assert centres and all(x * 1e3 == pytest.approx(20, abs=1e-9) for x in centres)


def test_the_inner_copper_wildcard_is_every_inner_layer(tmp_path):
    body = '''(footprint "t:a" (layer "F.Cu") (at 20 10)
  (pad "1" thru_hole circle (at 0 0) (size 2 2) (drill 1) (layers "F.Cu" "*In.Cu") (net 1 "A")))'''
    layers = px.FromKiCad(kicad_board(tmp_path, body, inner=2), remove_faces_smaller=0)
    assert [len(layers[n].faces) for n in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")] == [1, 1, 1, 0]


def test_a_chamfered_round_rectangle_pad_keeps_the_rounding(tmp_path):
    body = '''(footprint "t:a" (layer "F.Cu") (at 20 10)
  (pad "1" smd roundrect (at 0 0) (size 2 1) (roundrect_rratio 0.25) (chamfer_ratio 0.25) (chamfer top_left)
   (layers "F.Cu") (net 1 "A")))'''
    copper = px.FromKiCad(kicad_board(tmp_path, body), "F.Cu", remove_faces_smaller=0)
    r, c = 0.25, 0.25
    right = (1 - math.pi / 4) * r * r
    obtuse = (1 / math.tan(3 * math.pi / 8) - math.pi / 8) * r * r
    assert copper.mass / MM2 == pytest.approx(2 - c * c / 2 - 3 * right - 2 * obtuse, rel=1e-9)
    assert len(copper.arcs) == 5


def test_a_kicad_10_development_build_is_taken(no_kicad, eagle):
    no_kicad.setattr(shutil, "which", lambda name: "/usr/bin/kicad-cli")
    no_kicad.setattr(subprocess, "run", FakeKiCadCli(version="9.99.0-rc1"))
    assert list(px.FromKiCad(eagle)) == list(px.FromKiCad(BOARD))
