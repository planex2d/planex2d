# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest
from netgen import occ

import planex as px
from helpers import area, ends, point, polygon_area, seg, square


def test_mass_is_length_for_edges_and_wires_and_area_for_faces():
    p = square(0, 0, 3) - square(1, 1, 1)
    assert p.mass == pytest.approx(8)
    assert p.wires[0].mass == pytest.approx(12) or p.wires[1].mass == pytest.approx(12)
    assert [e.mass for e in square(0, 0, 2).edges] == [2, 2, 2, 2]
    disc = px.QuickSketch().Circle((0, 0), 1).Face()
    assert disc.mass == pytest.approx(math.pi)
    assert disc.Wire().mass == pytest.approx(2 * math.pi)
    assert px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Wire().mass == pytest.approx(2)
    ring = px.QuickSketch().Circle((0, 0), 2).Face() - px.QuickSketch().Circle((0, 0), 1).Face()
    assert ring.mass == pytest.approx(3 * math.pi)
    curved = px.Move((0, 0)).Line((2, 0)).Line((2, 1))
    curved.Spline([(1, 1.5), (0, 1)])
    curved.Close()
    assert curved.Face().mass == pytest.approx(area(curved.ToOCCFace()), rel=1e-5)


def test_write_step_and_brep(tmp_path):
    p = square(0, 0, 2) - square(0.5, 0.5, 1)
    p.WriteStep(tmp_path / "p.step", unit=px.mm, scale=1)
    p.WriteBrep(tmp_path / "p.brep")
    for name in ("p.step", "p.brep"):
        shape = occ.OCCGeometry(str(tmp_path / name)).shape
        assert len(shape.faces) == 1
        assert area(shape) == pytest.approx(3)
    c = px.Compound([square(0, 0, 2), px.Line((-1, 1), (3, 1))])
    c.WriteStep(tmp_path / "c.step")
    shape = occ.OCCGeometry(str(tmp_path / "c.step")).shape
    assert len(shape.faces) == 1 and len(shape.edges) == 5
    px.Move((0, 0)).Line((1, 0)).Wire().WriteStep(tmp_path / "w.step")
    assert len(occ.OCCGeometry(str(tmp_path / "w.step")).shape.edges) == 1


def test_bspline_round_trip_through_its_data():
    pytest.importorskip("shapely")
    s = px.Spline([(0, 0), (1, 1), (2, 0), (3, 1)])
    b = s.edges[0].edge.bspline
    assert b["degree"] == 3 and b["weights"] is None
    assert len(b["knots"]) == len(b["poles"]) + b["degree"] + 1
    back = px.BSpline(b["poles"], b["knots"], b["degree"])
    assert len(back.edges) == 1
    assert point(back.edges[0].edge.start) == pytest.approx((0, 0))
    assert point(back.edges[0].edge.end) == pytest.approx((3, 1))
    assert back.ToShapely(1e-4).hausdorff_distance(s.ToShapely(1e-4)) < 1e-3
    with pytest.raises(ValueError, match="invalid b-spline"):
        px.BSpline([(0, 0), (1, 1)], [0, 0, 1, 1], 3)
