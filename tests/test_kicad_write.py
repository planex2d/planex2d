# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import os
import re
import warnings

import pytest

import planex as px

PIC = os.path.join(os.path.dirname(__file__), "data", "kicad", "pic_programmer.kicad_pcb")
# the file has 6 decimals of a millimetre: every coordinate moves by up to 0.5 nm, a point by up to 0.71 nm, and a
# three-point arc through moved points by at most 1.94 times that (the Lebesgue constant of 1, cos, sin at the arc's
# start, middle and end over 270 degrees, the longest arc written); a face's area by at most that times the length of
# its boundary
SHIFT = 2 * math.sqrt(2) * 0.5e-9
# FromKiCad's cleanup is not idempotent, so a written board is read back without it: what the writer wrote, exactly
NO_CLEANUP = {"remove_faces_smaller": 0, "remove_edges_smaller": 0, "close_gaps_smaller": 0,
              "merge_collinear_closer": 0, "remove_free_edges": False}


def rect(x0, y0, x1, y1):
    return px.QuickSketch((x0, y0)).Rectangle(x1 - x0, y1 - y0).Face()


def disc(x, y, r):
    return px.QuickSketch().Circle(x, y, r).Face()


OUTLINE = rect(0, 0, 0.05, 0.03)


def boundary(shape):
    return sum(e.mass for e in shape.edges)


def written(tmp_path, layers, **kwargs):
    path = str(tmp_path / ("w%d.kicad_pcb" % len(list(tmp_path.iterdir()))))
    px.WriteKiCad(path, layers, **kwargs)
    return path


def read(path):
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        return px.FromKiCad(path)


def test_a_face_with_two_holes_is_one_outline_with_bridges(tmp_path):
    plate = rect(0.005, 0.005, 0.025, 0.015) - disc(0.01, 0.01, 0.001) - rect(0.018, 0.008, 0.022, 0.012)
    plate.name = "GND"
    path = written(tmp_path, {"F.Cu": plate, "Edge.Cuts": OUTLINE})
    text = open(path).read()
    assert text.count("(gr_poly") == 1 and '(net 1 "GND")' in text and "(arc (start" in text
    back = read(path)
    assert list(back) == ["F.Cu", "B.Cu", "Edge.Cuts"]
    face, = back["F.Cu"].faces
    assert face.name == "GND" and len(face.inner) == 2
    assert face.mass == pytest.approx(plate.mass, abs=SHIFT * boundary(plate))
    assert back["Edge.Cuts"].mass == pytest.approx(0.05 * 0.03, abs=SHIFT * 0.16)


def test_bridges_never_run_along_an_edge(tmp_path):
    """the bridge from the right hole's lowest left corner would run along the top of the left hole, which lies a
    few ulps lower: a nanometre from the bridge's line counts as on it, so the bridge ends at that corner"""
    def box(x0, y0, x1, y1):
        return px.QuickSketch().Move((x0, y0)).Line((x1, y0)).Line((x1, y1)).Line((x0, y1)).Close().Face()

    lifted = 0.006 + 2e-18
    face = rect(0.001, 0.001, 0.02, 0.012) - box(0.002, 0.002, 0.006, 0.006) - box(0.008, lifted, 0.01, 0.008)
    face.name = "A"
    back = read(written(tmp_path, {"F.SilkS": face, "F.Cu": face, "Edge.Cuts": OUTLINE}))
    for layer in ("F.Cu", "F.SilkS"):
        got, = back[layer].faces
        assert len(got.inner) == 2 and got.mass == pytest.approx(face.mass, abs=SHIFT * boundary(face))


def test_arcs_exactly_and_splines_within_tol(tmp_path):
    rounded = rect(0.005, 0.006, 0.015, 0.011) + disc(0.015, 0.0085, 0.0035)
    rounded.name = "SIG"
    blob = px.Spline([(0.03, 0.01), (0.04, 0.012), (0.042, 0.02), (0.032, 0.022)], periodic=True).Face()
    blob.name = "BLOB"
    for tol in (1e-6, 1e-5):
        back = read(written(tmp_path, {"F.Cu": px.Compound([rounded, blob]), "Edge.Cuts": OUTLINE}, tol=tol))
        got = {f.name: f for f in back["F.Cu"].faces}
        assert got["SIG"].mass == pytest.approx(rounded.mass, abs=SHIFT * boundary(rounded))
        assert got["BLOB"].mass == pytest.approx(blob.mass, abs=(tol + SHIFT) * boundary(blob))
        assert got["SIG"].arcs and not got["BLOB"].arcs and not got["BLOB"].splines


def test_faces_without_a_name_get_nets_of_their_own(tmp_path):
    a, b, c = rect(0.005, 0.005, 0.01, 0.01), rect(0.02, 0.005, 0.025, 0.01), rect(0.03, 0.005, 0.04, 0.01)
    named = rect(0.005, 0.02, 0.01, 0.025)
    named.name = "net1"
    back = read(written(tmp_path, {"F.Cu": px.Compound([named, a, b]), "B.Cu": c, "Edge.Cuts": OUTLINE}))
    top = {f.name: f.mass for f in back["F.Cu"].faces}
    assert top == pytest.approx({"net1": named.mass, "net2": a.mass, "net3": b.mass}, abs=1e-15)
    assert [f.name for f in back["B.Cu"].faces] == ["net4"]


def test_edge_cuts_with_cutouts(tmp_path):
    board = OUTLINE - rect(0.01, 0.01, 0.02, 0.02) - disc(0.035, 0.015, 0.004)
    path = written(tmp_path, {"Edge.Cuts": board})
    text = open(path).read()
    assert text.count("(gr_circle") == 1 and text.count("(gr_line") == 8
    assert re.search(r"\(stroke \(width 0.05\) \(type solid\)\)\s+\(layer \"Edge.Cuts\"\)", text)
    profile = read(path)["Edge.Cuts"]
    assert profile.mass == pytest.approx(board.mass, abs=SHIFT * boundary(board))


def test_edges_on_no_face_are_lines_except_on_copper(tmp_path):
    line = px.Line((0.005, 0.005), (0.015, 0.005))
    arc = px.Arc((0.02, 0.005), (0.03, 0.005), (0.025, 0.005))
    path = str(tmp_path / "free.kicad_pcb")
    with pytest.warns(px.PlanexWarning, match=r"1 edge on no face left out on F.Cu \(copper needs an area\)"):
        px.WriteKiCad(path, {"F.Cu": px.Compound([line, rect(0.005, 0.01, 0.01, 0.015)]),
                             "F.SilkS": px.Compound([line, arc]), "Edge.Cuts": OUTLINE}, line_width=2e-4)
    back = read(path)
    w = 2e-4
    assert back["F.Cu"].mass == pytest.approx(0.005 * 0.005, abs=1e-14)
    stroked = sorted(f.mass for f in back["F.SilkS"].faces)
    half_ring = math.pi / 2 * ((0.005 + w / 2) ** 2 - (0.005 - w / 2) ** 2) + math.pi * (w / 2) ** 2
    assert stroked == pytest.approx(sorted([0.01 * w + math.pi * (w / 2) ** 2, half_ring]), rel=1e-6)


def test_the_same_layers_give_the_same_file(tmp_path):
    plate = rect(0.005, 0.005, 0.025, 0.015) - disc(0.01, 0.01, 0.001)
    twins = px.Compound([rect(0.03, 0.005, 0.035, 0.01), rect(0.03, 0.005, 0.035, 0.01)])
    layers = {"F.Cu": plate, "F.SilkS": twins, "Edge.Cuts": OUTLINE}
    first, second = written(tmp_path, layers), written(tmp_path, layers)
    text = open(first).read()
    assert text == open(second).read()
    uuids = re.findall(r'\(uuid "([0-9a-f-]+)"\)', text)
    assert len(uuids) == len(set(uuids)) == text.count("(gr_")


def test_unit_says_what_the_geometry_counts_in(tmp_path):
    in_m = written(tmp_path, {"F.Cu": rect(0.005, 0.005, 0.025, 0.015), "Edge.Cuts": OUTLINE})
    in_mm = written(tmp_path, {"F.Cu": rect(5, 5, 25, 15), "Edge.Cuts": rect(0, 0, 50, 30)}, unit=px.mm, tol=1e-3,
                    thickness=1.6, line_width=0.1)
    assert open(in_m).read() == open(in_mm).read()
    assert "(thickness 1.6)" in open(in_m).read()


def test_copper_layers_from_a_list_or_the_highest_given(tmp_path):
    faces = [rect(0.005, 0.005, 0.01, 0.01), rect(0.015, 0.005, 0.02, 0.01), rect(0.025, 0.005, 0.03, 0.01)]
    back = read(written(tmp_path, faces, outline=OUTLINE))
    assert [n for n in back if n.endswith(".Cu")] == ["F.Cu", "In1.Cu", "B.Cu"]
    assert [len(back[n].faces) for n in ("F.Cu", "In1.Cu", "B.Cu")] == [1, 1, 1]
    back = read(written(tmp_path, {"In2.Cu": faces[0], "Edge.Cuts": OUTLINE}))
    assert [n for n in back if n.endswith(".Cu")] == ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]
    assert [len(back[n].faces) for n in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")] == [0, 0, 1, 0]


def test_what_cannot_be_written_raises(tmp_path):
    face = rect(0.005, 0.005, 0.01, 0.01)
    with pytest.raises(ValueError, match="'F.Silk' is no KiCad layer"):
        written(tmp_path, {"F.Silk": face})
    with pytest.raises(ValueError, match="given twice"):
        written(tmp_path, {"Edge.Cuts": OUTLINE}, outline=OUTLINE)
    with pytest.raises(ValueError, match="layer 'B.Cu' lies in another plane than layer 'F.Cu'"):
        written(tmp_path, {"F.Cu": face, "B.Cu": face.Translated((0.0, 0.0, 1e-3))})
    with pytest.raises(ValueError, match="another plane"):
        written(tmp_path, {"F.Cu": px.Compound([face, face.Translated((0.0, 0.0, 1e-3))])})
    with pytest.raises(TypeError, match="layer 'F.Cu' holds a str"):
        written(tmp_path, {"F.Cu": "copper"})
    with pytest.raises(ValueError, match="a unit is a positive length"):
        written(tmp_path, {"F.Cu": face}, unit=0)


def round_trip(path, tmp_path):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", px.PlanexWarning)
        layers = px.FromKiCad(path)
    out = written(tmp_path, layers)
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        back = px.FromKiCad(out, **NO_CLEANUP)
    names = {f.name for s in layers.values() for f in s.faces}
    assert list(back)[:len([n for n in layers if n.endswith(".Cu")]) + 1] == \
        [n for n in layers if n.endswith(".Cu")] + ["Edge.Cuts"]
    for layer, shape in layers.items():
        got = back[layer]
        if layer.endswith(".Cu"):
            assert len(got.faces) == len(shape.faces), layer
            renamed = [None if f.name not in names and re.fullmatch(r"net\d+", f.name or "") else f.name
                       for f in got.faces]
            assert sorted(map(str, renamed)) == sorted(str(f.name) for f in shape.faces), layer
            for name in set(renamed):
                a = sum(f.mass for f, n in zip(got.faces, renamed) if n == name)
                b = sum(f.mass for f in shape.faces if f.name == name)
                length = sum(boundary(f) for f in shape.faces if f.name == name)
                assert a == pytest.approx(b, abs=SHIFT * length), (layer, name)
        else:
            assert got.mass == pytest.approx(shape.mass, abs=SHIFT * boundary(shape)), layer


def test_round_trip_of_the_kicad_10_demo_board(tmp_path):
    round_trip(PIC, tmp_path)


def test_round_plated_holes_become_vias_and_unplated_ones_mounting_holes(tmp_path):
    plate = rect(0.005, 0.005, 0.015, 0.015)
    plate.name = "SIG"
    path = written(tmp_path, {"F.Cu": plate, "B.Cu": plate, "Edge.Cuts": OUTLINE},
                   pth=disc(0.01, 0.01, 0.0005), npth=disc(0.03, 0.01, 0.0016))
    text = open(path).read()
    # the via's pad as wide as the copper around the hole allows: 5 mm to the plate's sides
    assert re.search(r'\(via\s+\(at 10 -10\)\s+\(size 10\)\s+\(drill 1\)\s+\(layers "F.Cu" "B.Cu"\)\s+\(net 1\)', text)
    assert '(footprint "MountingHole"' in text and \
        '(pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask")' in text
    assert "(gr_circle" not in text
    back = read(path)
    for layer in ("F.Cu", "B.Cu"):
        face, = back[layer].faces
        assert face.name == "SIG" and face.mass == pytest.approx(1e-4 - math.pi * 0.0005 ** 2, rel=1e-9)
        assert {e.name for e in face.edges} >= {"pth"}
    profile = back["Edge.Cuts"]
    assert profile.mass == pytest.approx(0.05 * 0.03 - math.pi * (0.0005 ** 2 + 0.0016 ** 2), rel=1e-9)
    assert {"pth", "npth"} <= {e.name for e in profile.edges}


def test_holes_named_pth_and_npth_are_drills(tmp_path):
    """the holes as FromKiCad names them: written as drills again, read back with the same names"""
    hole = disc(0.01, 0.01, 0.0005)
    for e in hole.edges:
        e.name = "pth"
    plate = rect(0.005, 0.005, 0.015, 0.015) - hole
    plate.name = "SIG"
    board = OUTLINE - hole
    text = open(written(tmp_path, {"F.Cu": plate, "B.Cu": plate, "Edge.Cuts": board})).read()
    assert text.count("(via") == 1 and "(gr_circle" not in text


def test_non_round_holes_are_cutouts_of_the_outline(tmp_path):
    slot = px.Line((0.009, 0.01), (0.011, 0.01)).Offset(0.0005)
    plate = rect(0.005, 0.005, 0.015, 0.015)
    plate.name = "SIG"
    path = written(tmp_path, {"F.Cu": plate, "Edge.Cuts": OUTLINE}, pth=slot)
    text = open(path).read()
    assert "(via" not in text and text.count("(gr_arc") == 2
    back = read(path)
    assert back["Edge.Cuts"].mass == pytest.approx(0.05 * 0.03 - slot.mass, rel=1e-9)
    assert back["F.Cu"].mass == pytest.approx(1e-4 - slot.mass, rel=1e-9)


def test_a_plated_hole_joins_the_faces_it_goes_through_into_one_net(tmp_path):
    hole = disc(0.01, 0.01, 0.0005)
    top, bottom = rect(0.005, 0.005, 0.015, 0.015), rect(0.005, 0.005, 0.015, 0.015)
    top.name = "A"
    back = read(written(tmp_path, {"F.Cu": top, "B.Cu": bottom, "Edge.Cuts": OUTLINE}, pth=hole))
    assert [f.name for f in back["B.Cu"].faces] == ["A"]
    # two names on one net: the top layer's wins, with a warning
    bottom.name = "VCC"
    top.name = "GND"
    path = str(tmp_path / "short.kicad_pcb")
    with pytest.warns(px.PlanexWarning, match=r"faces 'GND' and 'VCC' are connected through a plated hole at "
                                              r"\(0.01, 0.01\); their net is 'GND'"):
        px.WriteKiCad(path, {"F.Cu": top, "B.Cu": bottom, "Edge.Cuts": OUTLINE}, pth=hole)
    assert [f.name for f in read(path)["B.Cu"].faces] == ["GND"]
    # faces without a name joined by a hole: one net, net1
    top.name = bottom.name = None
    back = read(written(tmp_path, {"F.Cu": top, "B.Cu": bottom, "Edge.Cuts": OUTLINE}, pth=hole))
    assert [f.name for f in back["F.Cu"].faces] == [f.name for f in back["B.Cu"].faces] == ["net1"]


def test_a_plated_hole_needs_copper_on_every_layer(tmp_path):
    plate = rect(0.005, 0.005, 0.015, 0.015)
    with pytest.raises(ValueError, match=r"the plated hole at \(0.01, 0.01\) has no copper around it on In1.Cu"):
        written(tmp_path, {"F.Cu": plate, "In2.Cu": plate, "B.Cu": plate, "Edge.Cuts": OUTLINE},
                pth=disc(0.01, 0.01, 0.0005))
    with pytest.raises(ValueError, match="the pth holes are given twice"):
        written(tmp_path, {"F.Cu": plate, "pth": disc(0.01, 0.01, 0.0005)}, pth=disc(0.01, 0.01, 0.0005))
