# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import netgen.occ as occ
import numpy as np
import pytest

import planex as px
from planex._drawing import _Lettering

pytest.importorskip("matplotlib")

OWN = ("center lines", "callouts", "cutting lines", "view name", "solid", "corner lines")


def block():
    """a 60 x 30 x 20 block with a vertical hole of radius 5 through it at (20, 15)"""
    return occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(60, 30, 20)) - occ.Cylinder(occ.Pnt(20, 15, -1), occ.Z, r=5, h=22)


def disc(holes=0):
    """a disc of diameter 80 and 10 thick about the z axis with a bore of diameter 20, holes of diameter 8 on a circle of
    diameter 50"""
    part = occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=40, h=10) - occ.Cylinder(occ.Pnt(0, 0, -1), occ.Z, r=10, h=12)
    for k in range(holes):
        t = 2 * math.pi * k / holes
        part = part - occ.Cylinder(occ.Pnt(25 * math.cos(t), 25 * math.sin(t), -1), occ.Z, r=4, h=12)
    return part


def chamfered(x=3.0, y=3.0):
    """a 60 x 20 plate, 10 thick, its upper right corner cut off x along its top edge and y down its right side"""
    face = occ.WorkPlane().MoveTo(0, 0).LineTo(60, 0).LineTo(60, 20 - y).LineTo(60 - x, 20).LineTo(0, 20).Close().Face()
    return face.Extrude(10)


def lever():
    """a fork lever's body 24 thick about the xy plane: a hub of radius 22 and an eye of radius 16 on centers 100 apart,
    joined by tangent lines, a boss of diameter 44 and 4 high on each side of the hub, a bore of diameter 20 through"""
    sk = px.Sketch(auto_constraint=True)
    sk.Move((0, -22), dir=(1, 0)).Line(100, id="lower").Arc(16, 180, id="eye").Line(100, id="upper").Arc(22, 180, id="hub")
    sk.Constrain("hub.end", "lower.start", px.Coincident)
    sk.Constrain("hub", "lower", px.Tangent)
    sk.Constrain("hub.center", "origin", px.Coincident)
    sk.Constrain("eye.center", "H", px.Coincident)
    sk.Constrain("hub.center", "eye.center", px.Distance, 100)
    sk.Constrain("hub", px.Radius, 22)
    sk.Constrain("eye", px.Radius, 16)
    part = px.Pad(sk, 12, length2=12)
    for face in (part.faces.Max(occ.Z), part.faces.Min(occ.Z)):
        boss = px.Sketch(face)
        boss.Circle(22, id="boss")
        boss.Constrain("boss.center", "origin", px.Coincident)
        boss.Constrain("boss", px.Radius, 22)
        part = part + px.Pad(boss, 4)
    bore = px.Sketch(part.faces.Max(occ.Z))
    bore.Circle(10, id="bore")
    bore.Constrain("bore.center", "origin", px.Coincident)
    bore.Constrain("bore", px.Radius, 10)
    return px.Pocket(part, bore, until=px.last)


def slanted():
    """a 60 x 30 x 20 block with a hole of diameter 10 at 45 degrees through it from its front face to its top"""
    return occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(60, 30, 20)) - occ.Cylinder(occ.Pnt(30, -20, 0), occ.Dir(0, 1, 1), r=5, h=80)


def placed_boxes(sh):
    """each view's box on the sheet in mm, its dimensions and name included"""
    from planex._drawing import _points

    a = sh._arrange()
    out = []
    for k, (d, _, _) in enumerate(sh._views):
        pts = _points(d._outline(), a.drawn[k], 1 / a.paper, a.lettering)
        out.append(((pts.min(axis=0) + a.places[k]) * a.paper + a.shift, (pts.max(axis=0) + a.places[k]) * a.paper + a.shift))
    return out


def values(strokes):
    return sorted(t for s in strokes if s.id not in OWN for _, _, t in s.texts)


def view_values(dw):
    return values(dw._strokes(1.0, _Lettering()))


def sheet_values(*views):
    return [values(drawn) for drawn in px.Sheet(*views)._arrange().drawn]


def test_a_view_of_a_solid_dimensions_itself_fully():
    top = px.Drawing(block(), px.Top, unit=px.mm).AutoDimension()
    assert top._auto.dof == 0 and top._auto.redundant == []
    assert view_values(top) == ["20", "30", "60", "⌀10"]
    # the hole on the block's middle: its distance from the edge follows from the symmetry
    assert "15" not in view_values(top)


def test_a_sheet_shows_each_size_once_front_first():
    views = [px.Drawing(block(), plane, unit=px.mm).AutoDimension() for plane in (px.Front, px.Top, px.Left)]
    front, top, left = sheet_values(*views)
    assert front == ["20", "60"] and top == ["20", "30", "⌀10"] and left == []
    # a view on its own keeps all of its own
    assert view_values(views[2]) == ["20", "30"]


def test_a_turned_part_s_diameter_goes_to_the_view_along_its_axis_not_where_its_circle_is_round():
    front, top = sheet_values(*(px.Drawing(disc(), plane, unit=px.mm).AutoDimension() for plane in (px.Front, px.Top)))
    assert "⌀80" in front and "⌀80" not in top and "10" in front


def test_a_bolt_circle_is_one_hole_and_a_pattern_on_its_pitch_circle():
    top = px.Drawing(disc(6), px.Top, unit=px.mm).AutoDimension()
    assert top._auto.dof == 0 and top._auto.redundant == []
    assert "6× ⌀8" in view_values(top) and "⌀50" in view_values(top)


def test_a_view_s_own_sketch_comes_first_and_the_generated_dimensions_complete_it():
    sk = px.Sketch(px.Plane(px.Top))
    sk.Circle((20, 15), 5, id="hole")
    sk.Constrain("hole", px.Diameter, 10, id="d")
    sk.Constrain("hole.center", px.V, px.Distance, 20, id="x")
    sk.Constrain("hole.center", px.H, px.Distance, 15, id="y")
    top = px.Drawing(block(), px.Top, unit=px.mm).Add(sk).AutoDimension()
    assert top._auto.dof == 0
    shown = {d.id for d in top._shown()}
    assert {"d", "x", "y"} <= shown
    # the sketch's sizes once, and nothing that follows from them: with the hole's 15 on the block's middle, no 30
    assert view_values(top) == ["15", "20", "60", "⌀10"]


def test_a_section_through_the_middle_measures_nothing_from_its_plane_s_axes():
    middle = px.Plane(origin=(30, 15, 10), normal=(0, -1, 0), axis=(1, 0, 0))
    section = px.Drawing(block(), middle, unit=px.mm, section=True).AutoDimension()
    assert section._auto.dof == 0
    assert not [d for d in section._shown() if {"H", "V"} & set(d.refs)]
    assert view_values(section) == ["15", "20", "35", "60"]


def test_a_turned_part_s_axis_is_drawn_as_a_center_line():
    # a disc with a hub: its section symmetric about the axis only
    hub = disc() + occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=20, h=25) - occ.Cylinder(occ.Pnt(0, 0, -1), occ.Z, r=10, h=27)
    middle = px.Plane(origin=(0, 0, 12.5), normal=(0, -1, 0), axis=(1, 0, 0))
    section = px.Drawing(hub, middle, unit=px.mm, section=True).AutoDimension()
    assert section._auto_axes == {0}
    centres = next(s for s in section._strokes(1.0, _Lettering()) if s.id == "center lines")
    assert any(abs(a[0]) < 1e-6 and abs(b[0]) < 1e-6 and abs(b[1] - a[1]) > 10 for a, b, _ in centres.lines)


def test_auto_dimension_again_makes_them_anew_and_place_and_hide_take_them():
    top = px.Drawing(block(), px.Top, unit=px.mm).AutoDimension()
    first = [(d.id, d.value) for d in top._shown()]
    assert [(d.id, d.value) for d in top.AutoDimension()._shown()] == first
    assert sum(s.sketch is top._auto for s in top._sketched) == 1
    big = next(d.id for d in top._shown() if d.value == pytest.approx(60))
    assert "60" not in view_values(top.Place(big, 15).Hide(big))


def test_auto_dimension_needs_a_view_of_a_solid_along_a_plane():
    sk = px.Sketch()
    sk.Circle((0, 0), 5)
    with pytest.raises(ValueError, match="a view of an OCC solid along a plane"):
        px.Drawing(sk).AutoDimension()
    with pytest.raises(ValueError, match="a view of an OCC solid along a plane"):
        px.Drawing(block(), px.Isometric).AutoDimension()


def test_a_45_degree_chamfer_is_one_callout_of_its_leg_and_another_keeps_its_two_dimensions():
    top = px.Drawing(chamfered(2.0, 2.0), px.Top, unit=px.mm).AutoDimension()
    assert top._auto.dof == 0
    # the sizes to the sharp corner, the chamfer on one callout
    assert view_values(top) == ["2 × 45°", "20", "60"]
    callout = next(s for s in top._strokes(1.0, _Lettering()) if any("×" in t for _, _, t in s.texts))
    (tip, bend, _), (_, end, _) = callout.lines
    # from the chamfer's middle out of the part, at 45 degrees, then along to the value
    assert np.allclose(tip, (59, 19)) and np.allclose(bend - tip, (bend - tip)[0] * np.ones(2)) and (bend - tip)[0] > 0
    # 3 by 3 on a plate 20 high is no chamfer (over 2 mm and a tenth of the side it cuts): its angle and length
    assert view_values(px.Drawing(chamfered(), px.Top, unit=px.mm).AutoDimension()) == ["20", "45°", "57", "60"]
    assert abs(end[1] - bend[1]) < 1e-9 and callout.arrows and callout.entity is None
    # 5 along the top, 3 down the side: the lengths of the edges it cuts, no callout
    assert view_values(px.Drawing(chamfered(5.0, 3.0), px.Top, unit=px.mm).AutoDimension()) == ["17", "20", "55", "60"]


def test_place_and_hide_take_a_chamfer_s_callout_and_a_sheet_shows_it_once():
    top = px.Drawing(chamfered(2.0, 2.0), px.Top, unit=px.mm).AutoDimension()
    chamfer = next(d.id for d in top._shown() if d.kind == "chamfer")
    callout = next(s for s in top.Place(chamfer, "n")._strokes(1.0, _Lettering()) if s.id == chamfer)
    (tip, bend, _), _ = callout.lines
    assert abs(bend[0] - tip[0]) < 1e-9 and bend[1] > tip[1]
    assert "2 × 45°" not in view_values(top.Hide(chamfer))
    bottom = px.Drawing(chamfered(2.0, 2.0), px.Plane(origin=(0, 0, 0), normal=(0, 0, -1), axis=(1, 0, 0)), unit=px.mm)
    shown = sheet_values(px.Drawing(chamfered(2.0, 2.0), px.Top, unit=px.mm).AutoDimension(), bottom.AutoDimension())
    assert sum("2 × 45°" in v for v in shown) == 1


def test_an_isometric_goes_into_the_free_corner_between_the_side_and_the_top_view():
    part = block()
    for standard, side in (("DIN", px.Left), ("ASME", px.Right)):
        views = [px.Drawing(part, plane, unit=px.mm) for plane in (px.Front, px.Top, side, px.Isometric)]
        front, top, beside, iso = placed_boxes(px.Sheet(*views, standard=standard))
        assert iso[0][0] > max(front[1][0], top[1][0]) and iso[0][0] < beside[1][0]
        if standard == "DIN":
            assert iso[1][1] < min(front[0][1], beside[0][1]) and iso[0][1] < top[1][1]
        else:
            assert iso[0][1] > max(front[1][1], beside[1][1]) and iso[1][1] > top[0][1]
    # without a side view: beside the top view
    front, top, iso = placed_boxes(px.Sheet(*(px.Drawing(part, plane, unit=px.mm) for plane in (px.Front, px.Top, px.Isometric))))
    assert iso[0][0] > max(front[1][0], top[1][0]) and iso[1][1] < front[0][1]


def test_a_second_view_on_the_front_plane_goes_beside_the_front_view():
    middle = px.Plane(origin=(30, 15, 10), normal=(0, -1, 0), axis=(1, 0, 0))
    front = px.Drawing(block(), px.Front, unit=px.mm)
    section = px.Drawing(block(), middle, unit=px.mm, section=True)
    places = px.Sheet(front, section)._arrange().places
    assert places[1][0] > 60


def test_a_view_s_arcs_on_one_circle_join_and_slivers_and_other_curves_stay_out():
    from planex._autodim import _merged

    c, d = (10.0, 5.0), (30.0, 5.0)

    def at(o, r, deg):
        return (o[0] + r * math.cos(math.radians(deg)), o[1] + r * math.sin(math.radians(deg)))

    loose = [e for w in (px.Arc(at(c, 4, 300), at(c, 4, 30), c), px.Arc(at(c, 4, 20), at(c, 4, 100), c),
                         px.Arc(at(c, 4, 150), at(c, 4, 200), c), px.Arc(at(c, 4, 200), at(c, 4, 160), c, reverse=True),
                         px.Arc(at(d, 2, 0), at(d, 2, 180), d), px.Arc(at(d, 2, 180), at(d, 2, 360), d),
                         px.Line((0, 0), (0, 1e-9)), px.Spline([(0, 0), (5, 3), (10, 0)])) for e in w.edges]
    out = [e.edge for w in px.Wires(_merged(loose, px.XY, 1e-6), 1e-6) for e in w.edges]
    arcs = sorted((round(math.degrees(math.atan2(a.start[1] - 5, a.start[0] - 10)) % 360),
                   round(math.degrees(math.atan2(a.end[1] - 5, a.end[0] - 10)) % 360), a.ccw)
                  for a in out if type(a).__name__ == "ArcEdge")
    circles = [(tuple(e.center), e.radius) for e in out if type(e).__name__ == "CircleEdge"]
    assert len(out) == 3 and arcs == [(150, 200, True), (300, 100, True)]
    assert circles == [(pytest.approx(d), pytest.approx(2))]


def test_a_boss_seen_along_its_axis_is_one_circle_however_many_edges_fall_on_it():
    top = px.Drawing(lever(), px.Top, unit=px.mm).AutoDimension()
    assert top._auto.dof == 0 and top._auto.redundant == []
    values = view_values(top)
    assert values.count("⌀44") == 1 and "R22" not in values and {"R16", "⌀20"} <= set(values)


def test_a_hole_seen_at_a_slant_gets_no_dimensions_on_its_outline():
    top = px.Drawing(slanted(), px.Top, unit=px.mm).AutoDimension()
    assert view_values(top) == ["30", "60"]


def test_a_distance_a_chamfer_s_leg_cannot_replace_comes_back_under_a_new_id():
    from planex import _autodim

    sk = px.Sketch(auto_constraint=False)
    sk.Line((0, 0), (60, 0), id="a").Line((60, 0), (60, 20), id="b").Line((60, 20), (0, 20), id="c").Line((0, 20), (0, 0), id="d")
    sk.AutoConstrain()

    def distances():
        return sorted((c["refs"], c["value"]) for c in sk._Constraints() if c["kind"] == "Distance" and not c.get("derived"))

    before = distances()
    c = next(c for c in sk._Constraints() if c["id"] == "Distance.1")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", px.PlanexWarning)
        assert _autodim._swap(sk, [c], ("a.start", "a"), 1.0, "chamfer1") is None
    assert distances() == before and sk.dof == 0


def test_a_line_ending_a_hair_inside_a_circle_it_touches_leaves_no_stub():
    from planex._autodim import _wires

    a = math.asin(6 / 100)
    n = (math.sin(a), -math.cos(a))
    touch, far = (22 * n[0] - 2e-12 * n[0], 22 * n[1] - 2e-12 * n[1]), (100 + 16 * n[0], 16 * n[1])
    loose = [e for w in (px.Circle((0, 0), 22), px.Line(touch, far)) for e in w.edges]
    assert sorted(round(e.mass, 1) for w in _wires(loose, px.XY, 1e-4) for e in w.edges) == [69.1, 69.1, 99.8]


def shaft():
    """a shaft along x: 20 long of diameter 20 with a 1 x 45 degree chamfer at its free end, then 10 of diameter 30"""
    part = occ.Cylinder(occ.Pnt(0, 0, 0), occ.X, r=10, h=21) + occ.Cylinder(occ.Pnt(20, 0, 0), occ.X, r=15, h=10)
    return part.MakeChamfer([part.edges.Min(occ.X)], 1)


def keyed():
    """a shaft of diameter 25 and 60 long along x with a keyway 8 wide and 32 long, round ended, 4 deep on top"""
    key = occ.Box(occ.Pnt(18, -4, 8.5), occ.Pnt(42, 4, 20))
    for x in (18, 42):
        key = key + occ.Cylinder(occ.Pnt(x, 0, 8.5), occ.Z, r=4, h=12)
    return occ.Cylinder(occ.Pnt(0, 0, 0), occ.X, r=12.5, h=60) - key


def counterbored():
    """a disc of diameter 100 and 10 thick with four holes of diameter 6.6, counterbored 11 and 5 deep, 40 degrees
    apart on a circle of diameter 70"""
    part = occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=50, h=10)
    for k in range(4):
        x, y = 35 * math.cos(math.radians(40 * k)), 35 * math.sin(math.radians(40 * k))
        part = part - occ.Cylinder(occ.Pnt(x, y, -1), occ.Z, r=3.3, h=12) - occ.Cylinder(occ.Pnt(x, y, 5), occ.Z, r=5.5, h=6)
    return part


def test_a_turned_part_s_diameters_are_given_along_its_axis_and_its_chamfer_once():
    front = px.Drawing(shaft(), px.Front, unit=px.mm).AutoDimension()
    assert front._auto.dof == 0
    shown = view_values(front)
    assert {"⌀20", "⌀30", "1 × 45°"} <= set(shown)
    # no radial step 5 between the diameters, no end face taken for a chamfer of the corner its two chamfers make
    assert "5" not in shown and not any(v.startswith("12,") for v in shown)
    front, left = sheet_values(*(px.Drawing(shaft(), plane, unit=px.mm).AutoDimension() for plane in (px.Front, px.Left)))
    assert {"⌀20", "⌀30"} <= set(front) and not {"⌀20", "⌀30"} & set(left)


def test_lines_along_a_turned_axis_that_are_no_fold_lines_get_no_dimensions():
    front = px.Drawing(keyed(), px.Front, unit=px.mm).AutoDimension()
    assert front._auto.dof == 0
    assert view_values(front) == ["60", "⌀25"]


def test_a_counterbored_hole_is_one_instance_of_its_pattern_and_its_view_comes_first():
    from planex import _autodim
    top = px.Drawing(counterbored(), px.Top, unit=px.mm).AutoDimension()
    assert top._auto.dof == 0
    assert [v for v in view_values(top) if v.endswith("°")] == ["3× 40°"]
    assert _autodim._circular(top) and not _autodim._circular(px.Drawing(counterbored(), px.Front, unit=px.mm).AutoDimension())
