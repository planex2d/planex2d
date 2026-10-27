# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-12


def plate():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Rectangle(4, 2)
    sk.Constrain("Rectangle.1.Line.1", px.Horizontal)
    sk.Constrain("Rectangle.1.Line.1", px.Distance, 4, id="w")
    sk.Constrain("Rectangle.1.Line.2", px.Distance, 2, id="ht")
    return sk


def test_a_rectangle_has_right_angles_and_shares_its_corners():
    sk = px.Sketch()
    sk.Move((1, 1)).Rectangle(3, 2)
    assert sk.elements["Line"] == [f"Rectangle.1.Line.{k}" for k in range(1, 5)]
    assert list(sk.constraints["Perpendicular"]) == [f"Rectangle.1.Perpendicular.{k}" for k in (1, 2, 3)]
    # a corner each, free to move, turn and change its sides: 8 coordinates, 3 right angles
    assert (sk.status, sk.dof) == ("under", 5)
    sk.Constrain("Rectangle.1.Line.1", px.Distance, 5)
    corners = [sk._Location(f"Rectangle.1.Line.{k}.start") for k in range(1, 5)]
    sides = [(corners[(k + 1) % 4][0] - corners[k][0], corners[(k + 1) % 4][1] - corners[k][1]) for k in range(4)]
    assert math.hypot(*sides[0]) == pytest.approx(5, abs=TOL)
    for k in range(4):
        assert sides[k][0] * sides[(k + 1) % 4][0] + sides[k][1] * sides[(k + 1) % 4][1] == pytest.approx(0, abs=1e-12)
    assert sk.Face().mass == pytest.approx(5 * math.hypot(*sides[1]), rel=1e-12)


def test_a_rectangle_in_a_chain_closes_on_its_own_corner():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(1, 0).Rectangle(2, 1)
    assert sk._Location("Rectangle.1.Line.4.end") == pytest.approx((1, 0), abs=TOL)
    assert sk._Location("Rectangle.1.Line.4.end") == sk._Location("Rectangle.1.Line.1.start") == sk._Location("Line.1.end")
    assert sk.chain_start == (0, 0)


def test_named_points_and_their_vertex_properties():
    sk = plate()
    sk.Vertex("hole", (1, 0.5))
    assert sk.points == {"hole": (1, 0.5)}
    sk.Constrain("hole", "Rectangle.1.Line.4", px.Distance, 1.2)
    # the point moves to the distance (and, as little as the solver's steps let it, along the side)
    assert sk.points["hole"] == pytest.approx((1.2, 0.5), abs=1e-5)
    assert sk.points["hole"][0] == pytest.approx(1.2, abs=TOL)
    # construction: not exported
    assert len(sk.Wire().edges) == 4
    corner = sk("Rectangle.1.Line.1.end")
    corner.name, corner.maxh = "corner", 0.1
    sk(kind="point", near=(0, 2)).hpref = 1
    face = sk.ToOCCFace()
    named = {v.name: (v.p.x, v.p.y) for v in face.vertices if v.name}
    assert named == {"corner": pytest.approx((4, 0), abs=TOL)}
    sk["w"] = 5
    named = {v.name: (v.p.x, v.p.y) for v in sk.ToOCCFace().vertices if v.name}
    assert named == {"corner": pytest.approx((5, 0), abs=TOL)}


def test_an_element_as_a_wire_of_its_edges():
    sk = plate()
    sk.Circle((2, 1), 0.5, id="bore")
    sk.Move((0, 3)).Spline([(1, 3.5), (2, 3)], id="wave")
    assert len(sk.Wire("Rectangle.1.Line.1").edges) == 1
    bore = sk.Wire("bore")
    assert len(bore.edges) == 1 and bore.Measure()["length"] == pytest.approx(math.pi, rel=1e-12)
    assert len(sk.Wire("wave").splines) >= 1
    with pytest.raises(ValueError, match="no line, arc, circle or spline"):
        sk.Wire("Point.1")


def test_measure_by_id_name_point_and_to_occ_shapes():
    from netgen import occ

    sk = plate()
    sk.Circle((2, 1), 0.5, id="bore", name="bores")
    assert sk.Measure("Rectangle.1.Line.1")["length"] == pytest.approx(4)
    assert sk.Measure("bores")["length"] == pytest.approx(math.pi)
    assert sk.Measure()["area"] == pytest.approx(8 - math.pi * 0.25)
    assert sk.Measure("Rectangle.1.Line.1.start", to="Rectangle.1.Line.2.end")["distance"] == pytest.approx(math.hypot(4, 2))
    m = sk.Measure("Rectangle.1.Line.2", to=occ.Box(occ.Pnt(6, 0, 1), occ.Pnt(7, 1, 2)))
    assert (m["distance"], m["dx"], m["dy"], m["dz"]) == pytest.approx((math.hypot(2, 1), 2, 0, 1))


def test_a_copy_is_independent():
    sk = plate()
    copy = sk.Copy()
    copy["w"] = 7
    assert sk["w"] == 4 and copy["w"] == 7
    assert sk._Location("Rectangle.1.Line.1.end") == pytest.approx((4, 0), abs=TOL)


def test_motions_move_the_plane_and_keep_the_drawing():
    sk = plate()
    moved = sk.Translated((1, 2)).Rotated((0, 0), 90)
    assert moved._Location("Rectangle.1.Line.1.end") == pytest.approx((4, 0), abs=TOL)
    # the plane moved to (1, 2) and turned there by 90 degrees: (4, 0) of it is (1, 6) in 3d
    assert moved.ToXYZ((4, 0)) == pytest.approx((1, 6, 0), abs=TOL)
    assert moved.status == "full"
    moved["w"] = 5
    assert moved.ToXYZ(moved._Location("Rectangle.1.Line.1.end")) == pytest.approx((1, 7, 0), abs=TOL)
    lifted = sk.Translated((0, 0, 3)).Rotated((0, 0, 0), (1, 0, 0), 90)
    assert lifted.Normal() == pytest.approx((0, -1, 0), abs=TOL)
    assert lifted.ToXYZ((4, 2)) == pytest.approx((4, -3, 2), abs=TOL)
    assert sk.ToHV((4, 2, 0)) == (4, 2)
    assert sk.bounding_box == ((0, 0), (4, 2))


def test_scaling_scales_every_length_and_keeps_the_angles():
    sk = plate()
    sk.Move((4, 2)).Line(1, 1, id="arm")
    sk.Constrain("arm", px.Distance, 1, id="reach")
    sk.Constrain(px.H, "arm", px.Angle, 30, id="tilt")
    big = sk.Scaled((1, 1), 2)
    assert (big["w"], big["ht"], big["reach"], big["tilt"]) == pytest.approx((8, 4, 2, 30))
    assert big.status == sk.status
    # (1, 1) keeps its place in 3d
    assert big.ToXYZ((2, 2)) == pytest.approx(sk.ToXYZ((1, 1)), abs=TOL)
    assert big.Measure("Rectangle.1.Line.1")["length"] == pytest.approx(8)
    big["w"] = 9
    assert big._Location("Rectangle.1.Line.1.end") == pytest.approx((9, 0), abs=TOL)
    around = sk.Scaled((0, 0, 0), 0.5)
    assert around["w"] == pytest.approx(2) and around.ToXYZ((2, 0)) == pytest.approx((2, 0, 0), abs=TOL)
    with pytest.raises(ValueError):
        sk.Scaled((0, 0), 0)


def test_writers_and_conversions(tmp_path, monkeypatch):
    pytest.importorskip("ezdxf")
    pytest.importorskip("shapely")
    sk = plate()
    sk.WriteStep(tmp_path / "p.step")
    sk.WriteBrep(tmp_path / "p.brep")
    sk.WriteDXF(tmp_path / "p.dxf")
    assert all((tmp_path / n).stat().st_size > 0 for n in ("p.step", "p.brep", "p.dxf"))
    assert sk.ToShapely().area == pytest.approx(8)
    monkeypatch.setattr("netgen.webgui.Draw", lambda obj, **kwargs: obj)
    assert len(sk.Plot3d(backend="webgui").edges) == 4
