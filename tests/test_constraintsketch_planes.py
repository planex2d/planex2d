# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-13


def test_projected_edges_are_fixed_references():
    from netgen import occ

    rect = occ.WorkPlane().MoveTo(1, 1).Rectangle(2, 1).Face()
    sk = px.Sketch()
    sk.Project(rect)
    lines = sk.elements["Line"]
    assert len(lines) == 4 and len(sk.Construction().edges) == 4
    assert all(sk._States()[i] == "full" for i in lines)
    sk.Move((0, 0)).Line(0.5, 0.4, id="arm")
    sk.Constrain("arm.end", lines[0] + ".start", px.Coincident)
    assert sk._Location("arm.end") == pytest.approx((1, 1), abs=TOL)
    assert sk._Geometry(lines[0])["start"] == pytest.approx((1, 1), abs=TOL)


def test_projected_circles_and_names():
    from netgen import occ

    disc = occ.WorkPlane().Circle(0, 0, 1).Face()
    sk = px.Sketch()
    sk.Project(disc, construction=False, name="rim")
    assert len(sk.elements["Circle"]) == 1 and "Arc" not in sk.elements
    assert {e.name for e in sk.Face().edges} == {"rim"}
    assert sk.Face().mass == pytest.approx(math.pi, rel=1e-12)
    assert sk.status == "full"


def test_set_origin_moves_the_axes_not_the_drawing():
    sk = px.Sketch()
    sk.Move((1, 1)).Line(2, 0, id="l")
    before = [p for v in sk.ToOCCWire().vertices for p in (v.p.x, v.p.y, v.p.z)] if hasattr(sk.ToOCCWire(), "vertices") else None
    sk.SetOrigin((1, 2))
    assert sk._Geometry("l")["start"] == pytest.approx((0, -1), abs=TOL)
    assert sk._Location(px.origin) == pytest.approx((-1, -2), abs=TOL)
    assert sk._Location(sk.origin) == pytest.approx((0, 0), abs=TOL)
    if before is not None:
        assert [p for v in sk.ToOCCWire().vertices for p in (v.p.x, v.p.y, v.p.z)] == pytest.approx(before, abs=TOL)


def test_constraints_to_the_axes_follow_them():
    sk = px.Sketch()
    sk.Move((1, 0)).Line(2, 0, id="l")
    sk.Constrain("l.start", px.H, px.Coincident)
    sk.Constrain("l.end", px.H, px.Coincident)
    sk.SetOrigin((0, 0.5))
    assert [sk._Location(e)[1] for e in ("l.start", "l.end")] == pytest.approx([0, 0], abs=TOL)


def test_the_global_origin_as_a_reference():
    sk = px.Sketch()
    sk.SetOrigin((2, 0))
    sk.Move(px.origin, px.Coincident).Line(1, 0, id="l")
    assert sk._Location("l.start") == pytest.approx((-2, 0), abs=TOL)
    assert repr(sk.constraints["Coincident"]["Coincident.1"]) == '"Point.1", px.origin, px.Coincident'


def test_a_constraint_sketch_as_axes_and_on_a_plane():
    from netgen import occ

    sk = px.Sketch(occ.Axes((0, 0, 2), n=occ.Z, h=occ.X))
    sk.Move((0, 0)).Line(1, 0)
    assert sk.Wire().ToXYZ((1, 0)) == pytest.approx((1, 0, 2))
    assert px.QuickSketch(sk).Move((0, 0)).Line((1, 0)).Wire().ToXYZ((0, 0)) == pytest.approx((0, 0, 2))
    assert px.Plane(sk).ToXYZ((0, 0)) == pytest.approx((0, 0, 2))


def test_projected_splines_follow_the_curve_and_hold():
    from netgen import occ

    plate = occ.WorkPlane().MoveTo(0, 0).Line(3, 0).Line(0, 1).Spline([(1.5, 1.8), (0, 1)]).Close().Face()
    sk = px.Sketch()
    sk.Project(plate, construction=False, name="plate")
    assert len(sk.elements["Spline"]) == 1 and len(sk.elements["Line"]) == 3
    assert sk.status == "full"
    # the samples keep it within the spline tolerance of the curve: the areas agree
    assert sk.Face().mass == pytest.approx(plate.mass, rel=1e-6)
    curve = sk.elements["Spline"][0]
    g = sk._Geometry(curve)
    assert g["start"] == pytest.approx((3, 1), abs=TOL) and g["end"] == pytest.approx((0, 1), abs=TOL)
    # its ends are the lines' ends, tangent where the curve is
    assert sk._Location(sk.elements["Line"][1] + ".end") == pytest.approx(g["start"], abs=TOL)
    # a fixed curve keeps its shape: a line made tangent to it turns, a point on it slides
    sk.Move((0.4, 2.2)).Line(2.2, 0.2, id="ruler")
    sk.Constrain("ruler", curve, px.Tangent)
    sk.Constrain("ruler.start", curve, px.Coincident)
    assert sk._Geometry(curve)["points"] == g["points"]
    assert sk.status == "under"


def test_projected_closed_splines():
    from netgen import occ

    blob = occ.WorkPlane().Spline([(2, 0), (1.5, 1.5), (0, 1), (-0.8, 0.2)], periodic=True).Face()
    sk = px.Sketch()
    sk.Project(blob, construction=False)
    assert len(sk.elements["Spline"]) == 1 and sk._Geometry(sk.elements["Spline"][0])["closed"]
    assert sk.Face().mass == pytest.approx(blob.mass, rel=1e-6)


def _rect_fix_line():
    sk = px.Sketch()
    sk.Move((1, 1)).Rectangle(2, 1)
    sk.Constrain("Rectangle.1.Line.1", px.Fix)
    return sk


def _rect_fix_point():
    sk = px.Sketch()
    sk.Move((1, 1), px.Fix).Rectangle(2, 1)
    return sk


def _rect_rigid():
    sk = px.Sketch()
    sk.Move((1, 1)).Rectangle(2, 1)
    sk.RigidGroup("Rectangle.1", id="G")
    return sk


def _rect_full():
    sk = px.Sketch()
    sk.Move((1, 1), px.Fix).Rectangle(2, 1)
    sk.Constrain("Rectangle.1.Line.1", px.Horizontal)
    sk.Constrain("Rectangle.1.Line.1", px.Distance, 2)
    sk.Constrain("Rectangle.1.Line.2", px.Distance, 1)
    return sk


@pytest.mark.parametrize("build", [_rect_fix_line, _rect_fix_point, _rect_rigid, _rect_full])
def test_set_origin_keeps_the_drawing_where_it_is(build):
    sk = build()
    assert sk.Measure()["center"][:2] == pytest.approx((2, 1.5), abs=1e-14)
    sk.SetOrigin((1, 1))
    assert sk.Measure()["center"][:2] == pytest.approx((2, 1.5), abs=1e-12)
    assert sk.Face().mass == pytest.approx(2.0, rel=1e-12)
    assert sk._Location("Rectangle.1.Line.1.start") == pytest.approx((0, 0), abs=1e-12)


def test_set_origin_keeps_a_fixed_arc():
    sk = px.Sketch()
    sk.Move((1, 1)).Line(2, 0).Arc(0.5, 180, id="a").Line(-2, 0).Arc(0.5, 180).Close()
    sk.Constrain("a", px.Fix)
    area = sk.Face().mass
    sk.SetOrigin((1, 1))
    assert sk.Face().mass == pytest.approx(area, rel=1e-12)
    assert sk._Geometry("a")["center"] == pytest.approx((2, 0.5), abs=1e-12)
