# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import random

import netgen.occ as occ
import pytest

import planex as px
from helpers import (cell_boundary, circle, ends, is_empty, polygon_area, rect, seg, segments, square, stadium_area,
                     unit_boundary)

A = square(0, 0, 2)
B = square(1, 1, 2)
SQUARE = square(0, 0)
LOWER_HALF = rect(0, 0, 1, 0.5)


def test_union_of_overlapping_squares():
    p = A + B
    assert segments(p) == {
        seg((0, 0), (2, 0)), seg((2, 0), (2, 1)), seg((2, 1), (3, 1)), seg((3, 1), (3, 3)),
        seg((3, 3), (1, 3)), seg((1, 3), (1, 2)), seg((1, 2), (0, 2)), seg((0, 2), (0, 0)),
    }
    assert polygon_area(p) == pytest.approx(7)


def test_intersection_of_overlapping_squares():
    p = A * B
    assert segments(p) == {seg((1, 1), (2, 1)), seg((2, 1), (2, 2)), seg((2, 2), (1, 2)), seg((1, 2), (1, 1))}
    assert polygon_area(p) == pytest.approx(1)


def test_difference_of_overlapping_squares():
    p = A - B
    assert segments(p) == {
        seg((0, 0), (2, 0)), seg((2, 0), (2, 1)), seg((2, 1), (1, 1)),
        seg((1, 1), (1, 2)), seg((1, 2), (0, 2)), seg((0, 2), (0, 0)),
    }
    assert polygon_area(p) == pytest.approx(3)
    assert polygon_area(B - A) == pytest.approx(3)


def test_shared_edges_on_the_same_side_are_kept_once():
    # the split points on the sides separate nothing any more: the pieces join again
    assert segments(SQUARE + LOWER_HALF) == segments(SQUARE)
    assert segments(SQUARE * LOWER_HALF) == {
        seg((0, 0), (1, 0)), seg((1, 0), (1, 0.5)), seg((1, 0.5), (0, 0.5)), seg((0, 0.5), (0, 0)),
    }
    assert segments(SQUARE - LOWER_HALF) == {
        seg((0, 0.5), (1, 0.5)), seg((1, 0.5), (1, 1)), seg((1, 1), (0, 1)), seg((0, 1), (0, 0.5)),
    }
    assert is_empty(LOWER_HALF - SQUARE)


def test_shared_edges_on_opposite_sides():
    left, right = square(0, 0), square(1, 0)
    assert segments(left + right) == {seg((0, 0), (2, 0)), seg((2, 0), (2, 1)), seg((2, 1), (0, 1)), seg((0, 1), (0, 0))}
    assert is_empty(left * right)
    assert segments(left - right) == segments(left)


def test_squares_touching_at_a_corner():
    a, b = square(0, 0), square(1, 1)
    assert segments(a + b) == segments(a) | segments(b)
    assert polygon_area(a + b) == pytest.approx(2)
    assert is_empty(a * b)
    assert segments(a - b) == segments(a)


def test_polygons_far_apart():
    a, b = square(0, 0), square(5, 5)
    assert segments(a + b) == segments(a) | segments(b)
    assert is_empty(a * b)
    assert segments(a - b) == segments(a)


def test_polygon_with_itself():
    assert segments(A + A) == segments(A)
    assert segments(A * A) == segments(A)
    assert is_empty(A - A)
    p = A
    for _ in range(5):
        p = p + p
    assert len(p.lines) == 4


def test_empty_polygon():
    empty = px.Shape()
    assert segments(empty + A) == segments(A)
    assert segments(A + empty) == segments(A)
    assert is_empty(A * empty)
    assert segments(A - empty) == segments(A)
    assert is_empty(empty - A)


def test_circle_inside_a_square():
    s, c = rect(-3, -3, 6, 6), circle(0, 0, 2)
    union, inter, diff = s + c, s * c, s - c
    assert (len(union.lines), len(union.arcs), len(union.circles)) == (4, 0, 0)
    assert (len(inter.lines), len(inter.arcs), len(inter.circles)) == (0, 0, 1)
    assert (len(diff.lines), len(diff.arcs), len(diff.circles)) == (4, 0, 1)
    assert polygon_area(union) == pytest.approx(36)
    assert polygon_area(inter) == pytest.approx(4 * math.pi)
    assert polygon_area(diff) == pytest.approx(36 - 4 * math.pi)


def test_ring():
    ring = circle(0, 0, 5) - circle(0, 0, 2)
    assert len(ring.circles) == 2 and not ring.arcs
    face = ring.ToOCCFace()
    assert len(face.faces) == 1
    assert len(face.wires) == 2
    assert polygon_area(ring) == pytest.approx(21 * math.pi)


def test_lens_of_two_circles():
    lens = 2 * math.pi / 3 - math.sqrt(3) / 2
    a, b = circle(0, 0, 1), circle(1, 0, 1)
    assert polygon_area(a * b) == pytest.approx(lens)
    assert polygon_area(a + b) == pytest.approx(2 * math.pi - lens)
    assert polygon_area(a - b) == pytest.approx(math.pi - lens)


def test_rotated_squares():
    s = rect(-1, -1, 2, 2)
    octagon = 8 * (math.sqrt(2) - 1)
    assert polygon_area(s * s.Rotated((0, 0), 45)) == pytest.approx(octagon)
    assert polygon_area(s + s.Rotated((0, 0), 45)) == pytest.approx(8 - octagon)


def test_cross_from_a_rectangle_rotated_by_90_degrees():
    bar = rect(-1, -0.5, 2, 1)
    cross = bar + bar.Rotated((0, 0), 90)
    assert len(cross.lines) == 12
    assert polygon_area(cross) == pytest.approx(3)


def test_sum_of_a_list():
    row = sum(square(i, 0) for i in range(3))
    assert len(row.lines) == 4
    assert polygon_area(row) == pytest.approx(3)


def test_in_place_operators():
    p = square(0, 0, 2)
    p += square(1, 1, 2)
    assert polygon_area(p) == pytest.approx(7)
    p -= square(0, 0)
    assert polygon_area(p) == pytest.approx(6)
    p *= square(0, 0, 2)
    assert polygon_area(p) == pytest.approx(3)


def test_workplane_face_takes_part_in_booleans_on_either_side():
    wp = px.Move((0, 0)).Rectangle(2, 2)
    face = wp.Face()
    assert polygon_area(face + B) == pytest.approx(7)
    assert polygon_area(face * B) == pytest.approx(1)
    assert polygon_area(face - B) == pytest.approx(3)
    assert polygon_area(B - face) == pytest.approx(3)
    with pytest.raises(TypeError):
        wp + B


def test_polygon_rotate_returns_a_copy():
    r = A.Rotated((0, 0), 90)
    assert segments(A) == {seg((0, 0), (2, 0)), seg((2, 0), (2, 2)), seg((2, 2), (0, 2)), seg((0, 2), (0, 0))}
    assert polygon_area(r) == pytest.approx(4)


def test_polygons_on_different_axes_cannot_be_combined():
    lifted = px.QuickSketch(occ.Axes((0, 0, 1), occ.Z, occ.X)).Rectangle(1, 1).Face()
    with pytest.raises(ValueError, match="different planes"):
        lifted + SQUARE


@pytest.mark.parametrize("seed", range(20))
def test_random_grid_booleans_match_the_cell_sets(seed):
    rng = random.Random(seed)
    p, cells = px.Shape(), set()
    for _ in range(25):
        x, y, w, h = rng.randrange(8), rng.randrange(8), rng.randint(1, 3), rng.randint(1, 3)
        q = rect(x, y, w, h)
        qcells = {(x + i, y + j) for i in range(w) for j in range(h)}
        op = rng.choice("+++-*")
        if op == "+":
            p, cells = p + q, cells | qcells
        elif op == "-":
            p, cells = p - q, cells - qcells
        else:
            p, cells = p * q, cells & qcells
        assert unit_boundary(p) == cell_boundary(cells)
    assert polygon_area(p) == pytest.approx(len(cells))


def random_shape(rng):
    kind = rng.choice(["rect", "rotated", "circle", "stadium"])
    x, y = rng.uniform(0, 4), rng.uniform(0, 4)
    if kind == "rect":
        w, h = rng.uniform(0.5, 3), rng.uniform(0.5, 3)
        return rect(x, y, w, h), w * h
    if kind == "rotated":
        w, h = rng.uniform(0.5, 3), rng.uniform(0.5, 3)
        return rect(x, y, w, h).Rotated((x, y), rng.uniform(0, 360)), w * h
    if kind == "circle":
        r = rng.uniform(0.3, 2)
        return circle(x, y, r), math.pi * r * r
    length, d = rng.uniform(0.5, 4), rng.uniform(0.1, 0.8)
    angle = rng.uniform(0, 2 * math.pi)
    return px.Move((x, y)).Line(length * math.cos(angle), length * math.sin(angle)).Wire().Offset(d), stadium_area(length, d)


@pytest.mark.parametrize("seed", range(40))
def test_random_booleans_satisfy_the_area_identities(seed):
    rng = random.Random(seed)
    (a, area_a), (b, area_b) = random_shape(rng), random_shape(rng)
    union, inter = polygon_area(a + b), polygon_area(a * b)
    assert union + inter == pytest.approx(area_a + area_b)
    assert polygon_area(a - b) == pytest.approx(area_a - inter, rel=1e-6, abs=1e-9)
    assert polygon_area(b - a) == pytest.approx(area_b - inter, rel=1e-6, abs=1e-9)


def test_unify_of_a_row_of_overlapping_squares():
    assert polygon_area(px.Unify([square(i, 0, 2) for i in range(5)])) == pytest.approx(12)


def test_unify_accepts_generators():
    p = px.Unify(x for x in [px.Move((0, 0)).Rectangle(1, 1).Face(), square(1, 0)])
    assert polygon_area(p) == pytest.approx(2)


def test_intersect_of_three_squares():
    assert polygon_area(px.Intersect([A, B, square(1.5, 1.5, 2)])) == pytest.approx(0.25)


def test_subtract_removes_all_others_from_the_first():
    p = px.Subtract([rect(0, 0, 4, 1), square(0, 0), square(2, 0)])
    assert polygon_area(p) == pytest.approx(2)


def test_list_booleans_of_nothing_are_empty():
    assert is_empty(px.Unify([])) and is_empty(px.Intersect([])) and is_empty(px.Subtract([]))


@pytest.mark.parametrize("op", [px.Unify, px.Intersect, px.Subtract])
def test_multithreaded_list_booleans_match_single_threaded(op):
    rng = random.Random(3)
    polys = [circle(rng.uniform(4.5, 5.5), rng.uniform(4.5, 5.5), rng.uniform(1, 4)) for _ in range(64)]
    single, multi = op(polys, multithreaded=False), op(polys)
    assert segments(single) == segments(multi)
    assert sorted(map(ends, single.arcs)) == sorted(map(ends, multi.arcs))


def test_unify_keeps_voids_enclosed_by_touching_translated_cells():
    # re-entrant honeycomb cells only touch; translated copies differ by an ulp where they meet, which once let the
    # balanced union fill a void
    t, w, h, phi = 0.5, 3.5, 6.0, math.radians(30)
    d, a = t / math.cos(phi), w * math.tan(phi)
    pts = [(w - t / 2, a + d / 2), (t / 2, d / 2), (t / 2, h - d / 2), (w - t / 2, h - a - d / 2), (w - t / 2, h - a + d / 2),
           (t / 2, h + d / 2), (-t / 2, h + d / 2), (-t / 2, -d / 2), (t / 2, -d / 2), (w - t / 2, a - d / 2)]
    s = px.Move(tuple(pts[0]))
    for p in pts[1:]:
        s.Line(p)
    cell = s.Close().Face()
    cells = [cell.Translated(((2 * i + k) * w, (2 * j + k) * (h - a))) for j in range(3) for k in (0, 1) for i in range(4)]
    for u in (px.Unify(cells), px.Unify(cells, multithreaded=False)):
        assert u.mass == pytest.approx(24 * cell.mass, rel=1e-12)
        assert sum(len(f.inner) for f in u.faces) == 12


def _coupler_band(d, mirror):
    """a waveguide band: a line tilted by d at each end, then a tangent arc of radius 10 over 30 degrees; mirror: the
    V mirror, so the two lines lie within 2d of each other and cross in the middle"""
    s = -1 if mirror else 1
    p0, p1 = (-6.0 * s, 0.325 + d), (6.0 * s, 0.325 - d)
    w = px.Line(p0, p1)
    t = math.atan2(p1[1] - p0[1], (p1[0] - p0[0]) * s)
    c = (p1[0] - s * 10 * math.sin(t), p1[1] + 10 * math.cos(t))
    a = math.radians(30)
    w.Add(px.Arc(p1, (c[0] + s * 10 * math.sin(t + a), c[1] - 10 * math.cos(t + a)), c, reverse=mirror))
    return w.Offset(0.25)


def test_bands_on_lines_closer_than_the_arcs_tolerance_unite():
    """the lines' ends lie 2d apart: the snap (with the tolerance of the arcs' scale) merges them, so the cut must
    already see the lines as one stretch, else both copies are classified inside the other band and dropped"""
    exact = (_coupler_band(0.0, False) + _coupler_band(0.0, True)).mass
    for d in (1e-14, 5e-14, 1e-13, 1.5e-13, 1e-12):
        a, b = _coupler_band(d, False), _coupler_band(d, True)
        for r in (a + b, px.Unify([a, b])):
            assert r.mass == pytest.approx(exact, abs=1e-9)
        assert (a * b).mass == pytest.approx(2 * a.mass - exact, abs=1e-9)
        assert (a - b).mass == pytest.approx(exact - b.mass, abs=1e-9)


def test_holes_touching_two_rotated_cutters_are_cut():
    """each hole touches two cutters (rotated copies, so the touching edges are off by rounding); a cutter's neck
    opens to the plate's edge, so a hole's corner piece hangs on only at the two touch points: there a line and an
    arc leave in the same direction, also leftwards, where the angles meet at pi"""
    hole = math.pi * 2.75**2
    plate = px.QuickSketch((-20, -20)).Rectangle(40, 40).Face()
    holes = [px.QuickSketch().Circle(x, y, 2.75).Face() for x, y in [(14.5, 14.5), (-14.5, 14.5), (-14.5, -14.5), (14.5, -14.5)]]
    for open_top in (False, True):
        pts = [(-11.75, 13.5), (11.75, 13.5), (11.75, 17.5)]
        pts += [(4.1, 17.5), (4.1, 20), (-4.1, 20), (-4.1, 17.5)] if open_top else []
        s = px.QuickSketch().Move(tuple(pts[0]))
        for p in pts[1:] + [(-11.75, 17.5)]:
            s.Line(p)
        cutter = s.Close().Face()
        cutters = [cutter.Rotated((0, 0), 90 * k) for k in range(4)]
        r = px.Subtract([plate, *cutters, *holes])
        assert r.mass == pytest.approx(1600 - 4 * cutter.mass - 4 * hole, abs=1e-9)
        assert sum(f.mass for f in r.faces) == pytest.approx(r.mass, abs=1e-9)


def test_an_edge_in_the_big_list_and_in_cells_is_counted_once():
    """a gear generated by 2880 rack cuts has 25000 tiny edges, so a disc's half arc covers more than the grid's cell
    limit; its first piece after the cut is re-inserted with a small box after the snap: a ray met it twice and the
    edges just above the axis were classified outside (g * disc was 0)"""
    m, alpha = 2.0, math.radians(20)
    p, r = math.pi * m, 24.0
    s = px.QuickSketch().Move((-3.5 * p, 3 * m))
    top, tip = p / 4 + 1.25 * m * math.tan(alpha), p / 4 - 1.25 * m * math.tan(alpha)
    for n in range(-3, 4):
        s.Line((n * p - top, 1.25 * m)).Line((n * p - tip, -1.25 * m)).Line((n * p + tip, -1.25 * m)).Line((n * p + top, 1.25 * m))
    rack = s.Line((3.5 * p, 1.25 * m)).Line((3.5 * p, 3 * m)).Close().Face()
    steps = 2880
    cuts = [rack.Translated(((r * phi) % p, r)).Rotated((0, 0), math.degrees(phi)) for phi in (2 * math.pi * n / steps for n in range(steps))]
    g = px.Subtract([px.QuickSketch().Circle(r + m).Face(), *cuts])
    for radius in (23.0, 24.0):
        disc = px.QuickSketch().Circle(radius).Face()
        assert (g * disc).mass + (g - disc).mass == pytest.approx(g.mass, abs=1e-6)
    ring = px.QuickSketch().Circle(r + 0.005).Face() - px.QuickSketch().Circle(r - 0.005).Face()
    assert len((g * ring).faces) == 24


def test_union_of_spline_faces_touching_at_one_point_keeps_both_faces():
    third = 2 * math.pi / 3
    a = px.QuickSketch().Move((1.0, 0.0)).Spline([(math.cos(third), math.sin(third)),
                                                  (math.cos(2 * third), math.sin(2 * third))], periodic=True).Face()
    b = a.Rotated((a.edges[0].start.x, a.edges[0].start.y), 180)
    for u in (a + b, b + a):
        assert sorted(f.mass for f in u.faces) == pytest.approx([a.mass] * 2, rel=1e-12)
        assert len(u.ToOCCShape().faces) == 2
