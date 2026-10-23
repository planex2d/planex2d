# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import warnings

import pytest

import planex as px
from netgen import occ


def cold_plate_body():
    plate = occ.Box(occ.Pnt(-60, -60, -2), occ.Pnt(60, 60, 0))
    points = [(0, -60, 4), (35, -35, 4), (35, 0, 4), (-35, 0, 4), (-35, 35, 4), (0, 60, 4)]
    spine = occ.Wire([occ.SplineApproximation([occ.Pnt(*p) for p in points])])
    profile = occ.WorkPlane(occ.Axes((0, -60, 4), n=occ.Y, h=occ.X))
    return plate + occ.Pipe(spine, profile.Circle(5).Face())


def slab_cut(body, y, thickness=0.01):
    plane = occ.Axes((0, y, 0), n=(0, -1, 0), h=(1, 0, 0))
    slab = occ.Box(occ.Pnt(-70, y - thickness / 2, -10), occ.Pnt(70, y + thickness / 2, 20))
    return body * slab, plane


@pytest.mark.parametrize("y", [12.2, 14.54, 44.96, 57.83])
def test_project_of_slab_cut_has_no_open_ends(y):
    cut, plane = slab_cut(cold_plate_body(), y)
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        shape = px.Shape(px.Project(cut, plane))
    assert not [w for w in caught if "open" in str(w.message)]
    assert len(shape.faces) == 1


@pytest.mark.parametrize("y", [12.2, 44.96])
def test_project_of_slab_cut_has_no_edge_shorter_than_occ_tolerance(y):
    cut, plane = slab_cut(cold_plate_body(), y)
    edges = [e for wire in px.Project(cut, plane) for e in wire.edges]
    assert min(e.mass for e in edges) > 1e-6
