# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import netgen.occ as occ
import pytest

import planex as px


def plane(view, x):
    return occ.Axes(occ.Pnt(0, 0, 0), n=occ.Dir(*view), h=occ.Dir(*x))


def area(wires):
    return px.Shape(wires).Measure()["area"]


def kinds(wires):
    return sorted({type(e.edge).__name__ for w in wires for e in w.edges})


def cylinder():
    return occ.Cylinder(occ.Pnt(0, 0, 0), occ.X, r=1, h=4)


def torus(R=3.0, r=1.0):
    section = occ.WorkPlane(occ.Axes((0, 0, 0), n=occ.Y, h=occ.X)).MoveTo(R, 0).Circle(r).Face()
    return section.Revolve(occ.Axis((0, 0, 0), occ.Z), 360)


def test_a_cylinder_seen_side_on_casts_a_rectangle_bounded_by_its_fold_lines():
    wires = px.Project(cylinder(), silhouette=True)
    assert len(wires) == 1 and area(wires) == pytest.approx(8, rel=1e-12)
    assert kinds(wires) == ["LineEdge"]
    assert len(px.Project(cylinder())) > 1


def test_a_tilted_cylinder_casts_its_caps_and_the_band_between():
    phi = math.radians(60)
    wires = px.Project(cylinder(), plane((math.cos(phi), 0, math.sin(phi)), (0, 1, 0)), silhouette=True)
    assert area(wires) == pytest.approx(8 * math.sin(phi) + math.pi * math.cos(phi), rel=1e-12)


def test_a_sphere_casts_a_circle_from_any_side():
    wires = px.Project(occ.Sphere(occ.Pnt(1, 2, 3), 1.5), plane((1, 1, 1), (1, -1, 0)), silhouette=True)
    assert kinds(wires) == ["CircleEdge"] and area(wires) == pytest.approx(math.pi * 2.25, rel=1e-12)


def test_a_torus_casts_a_ring_along_its_axis_and_a_stadium_side_on():
    ring = px.Project(torus(), silhouette=True)
    assert len(ring) == 2 and area(ring) == pytest.approx(math.pi * (16 - 4), rel=1e-6)
    side = px.Project(torus(), plane((1, 0, 0), (0, 1, 0)), silhouette=True)
    assert len(side) == 1 and area(side) == pytest.approx(4 * 3 + math.pi, rel=1e-6)


def test_holes_one_sees_through_stay_holes():
    base = occ.Box(occ.Pnt(-40, -15, 0), occ.Pnt(40, 15, 14))
    bolts = occ.Cylinder(occ.Pnt(-32, 0, -1), occ.Z, r=4.5, h=16) + occ.Cylinder(occ.Pnt(32, 0, -1), occ.Z, r=4.5, h=16)
    boss = occ.Cylinder(occ.Pnt(0, -15, 34), occ.Y, r=24, h=30) + occ.Box(occ.Pnt(-24, -15, 0), occ.Pnt(24, 15, 34))
    housing = base + boss - bolts
    wires = px.Project(housing, silhouette=True)
    assert len(wires) == 3 and area(wires) == pytest.approx(80 * 30 - 2 * math.pi * 4.5 ** 2, rel=1e-12)


def test_the_methods_take_silhouette_as_px_project():
    target = px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True)
    for wires in (target.Project(cylinder(), silhouette=True), target.Face().Project(cylinder(), silhouette=True),
                  px.Shape([target]).Project(cylinder(), silhouette=True)):
        assert len(wires) == 1 and area(wires) == pytest.approx(8, rel=1e-12)


def test_a_sketch_takes_the_outline_as_construction_with_its_fold_lines_named():
    sk = px.Sketch()
    sk.Project(cylinder(), silhouette=True)
    folds = [x for x in sk.elements["Line"] if ".Fold." in x]
    assert len(folds) == 2 and all(x.startswith("Projection.1.Fold.") for x in folds)
    sk.Move((1, 3)).Line((3, 3), id="clamp")
    sk.Constrain("clamp", folds[0], px.Distance, 0.5)
    assert sk.Measure("clamp", to=folds[0])["distance"] == pytest.approx(0.5)


def test_a_shape_without_faces_casts_no_shadow():
    with pytest.raises(ValueError, match="the shape has no faces"):
        px.Project(occ.Segment(occ.Pnt(0, 0, 0), occ.Pnt(1, 1, 1)), silhouette=True)
