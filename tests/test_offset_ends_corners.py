# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import time

import pytest

import planex as px

D = 1.0


def ell():
    """the open chain (0, 0) to (10, 0) to (10, 10), turning left by 90 degrees"""
    return px.Move((0, 0)).Line((10, 0)).Line((10, 10)).Wire()


# the two side bands of the L overlap in a square d x d inside the corner; then per form what the outside of the corner
# and the two ends add
BANDS = 2 * D * 10 * 2 - D * D


@pytest.mark.parametrize("ends, end_area", [(px.Round, math.pi * D * D), (px.Flat, 0.0), (px.Square, 4 * D * D)])
@pytest.mark.parametrize("corners, corner_area", [(px.Round, math.pi * D * D / 4), (px.Sharp, D * D), (px.Chamfer, D * D / 2)])
def test_an_open_chain_s_band_by_its_ends_and_corners(ends, end_area, corners, corner_area):
    band = ell().Offset(D, ends=ends, corners=corners)
    assert len(band.faces) == 1
    assert band.mass == pytest.approx(BANDS + end_area + corner_area, rel=1e-12)


def test_the_default_is_round_ends_and_corners():
    assert ell().Offset(D).mass == pytest.approx(ell().Offset(D, ends=px.Round, corners=px.Round).mass, rel=1e-15)
    assert ell().Offset(D).mass == pytest.approx(BANDS + math.pi * D * D * 1.25, rel=1e-12)


def test_flat_ends_end_square_on_the_chain_s_ends():
    band = px.Move((0, 0)).Line((10, 0)).Wire().Offset(D, ends=px.Flat)
    assert band.mass == pytest.approx(20 * D, rel=1e-12)
    xs = [v.x for v in band.vertices]
    assert (min(xs), max(xs)) == pytest.approx((0, 10), abs=1e-12)
    square = px.Move((0, 0)).Line((10, 0)).Wire().Offset(D, ends=px.Square)
    xs = [v.x for v in square.vertices]
    assert (min(xs), max(xs)) == pytest.approx((-1, 11), abs=1e-12)


@pytest.mark.parametrize("corners, area", [(px.Round, 32 + math.pi), (px.Sharp, 36.0), (px.Chamfer, 34.0)])
def test_a_face_grows_with_the_corner_form(corners, area):
    f = px.Move((0, 0)).Rectangle(4, 4).Face()
    assert f.Offset(1, corners=corners).mass == pytest.approx(area, rel=1e-12)


def test_shrinking_keeps_convex_corners_sharp_and_forms_the_reflex_one():
    def l_face():
        return px.FromPolyline([(0, 0), (4, 0), (4, 2), (2, 2), (2, 4), (0, 4)], closed=True).Face()

    sharp = l_face().Offset(-0.5, corners=px.Sharp)
    rounded = l_face().Offset(-0.5)
    chamfered = l_face().Offset(-0.5, corners=px.Chamfer)
    # an L of arm width 2 shrunk by 0.5 on every side: arms 1 wide (3 + 2), the corner at the reflex one sharp; round,
    # the points 0.5 from that corner come in (a 0.5 x 0.5 square less a quarter circle), chamfered half that square
    assert sharp.mass == pytest.approx(5.0, rel=1e-12)
    assert rounded.mass == pytest.approx(5.0 + 0.25 - math.pi * 0.25 / 4, rel=1e-12)
    assert chamfered.mass == pytest.approx(5.0 + 0.25 / 2, rel=1e-12)


def test_a_sharp_corner_has_no_limit():
    theta = math.radians(10)
    tip = (10 - 10 * math.cos(theta), 10 * math.sin(theta))
    band = px.Move((0, 0)).Line((10, 0)).Line(tip).Wire().Offset(D, corners=px.Sharp)
    # the outside of the corner at (10, 0) reaches d / sin(theta / 2) beyond it along the bisector
    far = max(math.dist((v.x, v.y), (10, 0)) for v in band.vertices)
    assert far == pytest.approx(D / math.sin(theta / 2), rel=1e-9)


def test_an_arc_offset_beyond_its_radius_covers_its_centre():
    r, d = 1.0, 2.0
    band = px.Arc(px.Vertex((r, 0)), px.Vertex((0, r)), px.Vertex((0, 0))).Offset(d)
    sector = px.Wires([px.Line((0, 0), (r + d, 0)), px.Arc((r + d, 0), (0, r + d), (0, 0)), px.Line((0, r + d), (0, 0))])[0].Face()
    caps = [px.QuickSketch().Circle((r, 0), d).Face(), px.QuickSketch().Circle((0, r), d).Face()]
    assert band.mass == pytest.approx(px.Unify([sector] + caps).mass, rel=1e-12)


def test_a_spline_s_offset_is_one_spline_per_side():
    w = px.Move((0, 3)).Line(1).Spline([(2, 3.5), (3, 3)], tangents={0: (1, 0)}).Line(1).Wire()
    trace = w.Offset(0.1)
    assert len(trace.splines) == 2 and len(trace.faces) == 1
    # a band 2 d wide along the wire, with the caps and the corner at the spline's end within d^2 terms
    assert abs(trace.mass - 0.2 * w.mass) < 0.05


def test_a_spline_offset_beyond_its_tightest_bend_is_still_one_region():
    hairpin = px.Spline([(0, 0), (2, 0.4), (2.2, 0.5), (2, 0.6), (0, 1)])
    band = hairpin.Offset(0.5)
    assert len(band.faces) == 1 and band.mass > 0


def test_a_face_with_a_hole_grows_and_its_hole_shrinks():
    f = px.Move((0, 0)).Rectangle(10, 10).Face() - px.Move((3, 3)).Rectangle(4, 4).Face()
    g = f.Offset(1, corners=px.Sharp)
    assert g.mass == pytest.approx(12 * 12 - 2 * 2, rel=1e-12)


def test_wrong_forms_raise():
    with pytest.raises(ValueError, match="ends= is px.Round, px.Flat or px.Square"):
        ell().Offset(D, ends=px.Sharp)
    with pytest.raises(ValueError, match="corners= is px.Round, px.Sharp or px.Chamfer"):
        ell().Offset(D, corners=px.Flat)
    with pytest.raises(ValueError, match="corners= is px.Round, px.Sharp or px.Chamfer"):
        px.Move((0, 0)).Rectangle(4, 4).Face().Offset(1, corners=px.Square)


def test_a_long_wire_offsets_in_one_cut():
    pts = [(i, (i % 7) * 0.3 + (i % 3) * 0.2) for i in range(501)]
    w = px.FromPolyline(pts)
    start = time.perf_counter()
    band = w.Offset(0.1, corners=px.Sharp)
    assert time.perf_counter() - start < 20
    assert len(band.faces) >= 1


def test_a_spline_offset_shares_its_joints_and_exports_plainly():
    trace = px.Move((0, 3)).Line(1).Spline([(2, 3.5), (3, 3)], tangents={0: (1, 0)}).Line(1).Wire().Offset(0.1)
    assert not any(e.from_spline for e in trace.lines + trace.arcs)
    assert min(math.dist((e.start.x, e.start.y), (e.end.x, e.end.y)) for e in trace.edges) > 1e-3
    face = trace.ToOCCFace()
    assert len(face.faces) == 1
