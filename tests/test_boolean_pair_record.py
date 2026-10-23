# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Near-coincidences decided once per edge pair"""
import math

import pytest

import planex as px
from planex._planex import CutResult
from helpers import circle, rect


def polygon(points):
    s = px.Move(tuple(points[0]))
    for p in points[1:]:
        s.Line(p)
    return s.Close().Face()


# the half chord h from (r - d/2)(r + d/2): acos(d / 2r) and 4r^2 - d^2 lose all digits near tangency
def lens(r, d):
    h = math.sqrt((r - d / 2) * (r + d / 2))
    return 2 * r * r * math.atan2(h, d / 2) - d * h


def test_circle_times_its_copy_turned_by_5e_7_about_the_origin():
    a = circle(0.4, 0, 0.5)
    b = a.Rotated((0, 0), math.degrees(5e-7))
    shift = 0.4 * 5e-7
    assert (a * b).mass == pytest.approx(math.pi / 4 - shift, abs=1e-12)
    assert (b * a).mass == pytest.approx(math.pi / 4 - shift, abs=1e-12)
    assert (a + b).mass == pytest.approx(math.pi / 4 + shift, abs=1e-12)
    assert (a - b).mass == pytest.approx(shift, rel=1e-6)


@pytest.mark.parametrize("e", [1e-6, 1e-9, 1e-12])
def test_lines_crossing_at_a_near_tangent_angle(e):
    a = rect(0, 0, 1, 1)
    b = polygon([(-0.5, -1), (1.5, -1), (1.5, 1 + e), (-0.5, 1 - e)])
    assert (a * b).mass == pytest.approx(1 - e / 8, abs=1e-15)
    assert (a - b).mass == pytest.approx(e / 8, rel=1e-3, abs=1e-15)
    assert (a + b).mass == pytest.approx(4 + e / 8, abs=1e-14)
    assert (b - a).mass == pytest.approx(3 + e / 8, abs=1e-14)


@pytest.mark.parametrize("g", [1e-4, 1e-6, 1e-8])
def test_arcs_crossing_at_a_near_tangent_angle(g):
    a = circle(0, 0, 1)
    b = circle(2 - g, 0, 1)
    common = lens(1, 2 - g)
    assert (a * b).mass == pytest.approx(common, rel=1e-6, abs=1e-14)
    assert (a + b).mass == pytest.approx(2 * math.pi - common, abs=1e-13)
    assert (a - b).mass == pytest.approx(math.pi - common, abs=1e-13)


def test_arcs_whose_centres_differ_by_less_than_tol_overlap():
    a = circle(0.4, 0, 0.5)
    b = circle(0.4 + 3e-15, 0, 0.5)
    assert (a * b).mass == pytest.approx(math.pi / 4, abs=1e-13)
    assert (a + b).mass == pytest.approx(math.pi / 4, abs=1e-13)
    assert (a - b).mass == pytest.approx(0, abs=1e-13)
    half = b * rect(-1, 0, 3, 1)
    assert (a * half).mass == pytest.approx(math.pi / 8, abs=1e-13)
    assert (a + half).mass == pytest.approx(math.pi / 4, abs=1e-13)
    assert (a - half).mass == pytest.approx(math.pi / 8, abs=1e-13)
    assert (half - a).mass == pytest.approx(0, abs=1e-13)


def strip(angle, at):
    c, s = math.cos(angle), math.sin(angle)
    x, y = at
    return polygon([(x - 2 * c, y - 2 * s), (x + 2 * c, y + 2 * s), (x + 2 * c - 0.1 * s, y + 2 * s + 0.1 * c),
                    (x - 2 * c - 0.1 * s, y - 2 * s + 0.1 * c)])


def test_three_lines_crossing_within_tol_of_one_point_meet_in_one_vertex():
    strips = [strip(0, (0, 0)), strip(math.pi / 3, (3e-15, 0)), strip(2 * math.pi / 3, (0, 3e-15))]
    g = px.Glue(strips)
    near = {(p.x, p.y) for e in g.edges for p in (e.start, e.end) if math.hypot(p.x, p.y) < 1e-9}
    assert len(near) == 1
    assert sum(f.mass for f in g.faces) == pytest.approx(px.Unify(strips).mass, abs=1e-12)


# a's start lies 2.7e-13 from b, b's end 2.6e-13 from a (tol 6.5e-13); they share 2.8e-7 between the two
A0, A1 = (65.1159010791497, 61.93992268170584), (64.97355107914969, 61.408662681705835)
B0, B1 = (65.13531100630003, 62.012362409824895), (65.11590100630004, 61.939922409824895)


def side_strip(p, q, w):
    dx, dy = q[0] - p[0], q[1] - p[1]
    n = (-dy * w / math.hypot(dx, dy), dx * w / math.hypot(dx, dy))
    return polygon([p, q, (q[0] + n[0], q[1] + n[1]), (p[0] + n[0], p[1] + n[1])])


@pytest.mark.parametrize("w", [1e-3, 1e-6])
def test_thin_faces_on_both_sides_of_lines_at_a_tiny_angle_touch_along_their_stretch(w):
    fa = side_strip(A0, A1, w)
    fb = side_strip(B0, B1, -w)
    la = math.dist(A0, A1) * w
    lb = math.dist(B0, B1) * w
    assert fa.mass == pytest.approx(la, rel=1e-9)
    assert fb.mass == pytest.approx(lb, rel=1e-9)
    assert (fa * fb).mass == pytest.approx(0, abs=1e-12)
    assert (fa + fb).mass == pytest.approx(la + lb, abs=1e-12)
    assert (fa - fb).mass == pytest.approx(la, abs=1e-12)
    assert (fb - fa).mass == pytest.approx(lb, abs=1e-12)


def xy(p):
    return (p.x, p.y)


def ends(e):
    return (xy(e.start), xy(e.end))


# the same for arcs: an end of each edge within tol of the other, 6e-8 to 0.4 apart
TOUCH = 5e-8
E = (-math.sin(TOUCH), math.cos(TOUCH))


def test_line_nearly_tangent_to_an_arc_shares_the_stretch_between_their_ends():
    a = px.LineEdge((TOUCH, 1), (-1, 1))
    b = px.ArcEdge((1, 0), E, (0, 0))
    result, pieces = a.Cut(b)
    assert result == CutResult.Overlap
    assert ends(a) == ((TOUCH, 1), E)
    assert ends(b) == ((1, 0), (TOUCH, 1))
    assert [ends(e) for e in pieces] == [(E, (-1, 1))]


def test_disc_touching_a_line_with_ends_near_the_touch_point():
    disc = px.Wire([px.Arc(E, (-E[0], -E[1]), (0, 0)), px.Arc((-E[0], -E[1]), E, (0, 0))]).Face()
    box = polygon([(TOUCH, 1), (2, 1), (2, 2), (-1, 2), (-1, 1)])
    assert (disc * box).mass == pytest.approx(0, abs=1e-12)
    assert (disc + box).mass == pytest.approx(math.pi + 3, abs=1e-12)
    assert (disc - box).mass == pytest.approx(math.pi, abs=1e-12)
    assert (box - disc).mass == pytest.approx(3, abs=1e-12)


P = (math.cos(3e-8), math.sin(3e-8))
R = (2 - math.cos(3e-8), -math.sin(3e-8))


def test_nearly_tangent_arcs_share_the_stretch_between_their_ends():
    a = px.ArcEdge((-P[0], -P[1]), P, (0, 0))
    b = px.ArcEdge((4 - R[0], -R[1]), R, (2, 0))
    result, pieces = a.Cut(b)
    assert result == CutResult.Overlap
    assert ends(a) == ((-P[0], -P[1]), R)
    assert ends(b) == ((4 - R[0], -R[1]), P)
    assert [ends(e) for e in pieces] == [(R, P)]


def test_touching_discs_with_ends_near_the_touch_point():
    a = px.Wire([px.Arc(P, (-P[0], -P[1]), (0, 0)), px.Arc((-P[0], -P[1]), P, (0, 0))]).Face()
    b = px.Wire([px.Arc(R, (4 - R[0], -R[1]), (2, 0)), px.Arc((4 - R[0], -R[1]), R, (2, 0))]).Face()
    assert (a * b).mass == pytest.approx(0, abs=1e-12)
    assert (a + b).mass == pytest.approx(2 * math.pi, abs=1e-12)
    assert (a - b).mass == pytest.approx(math.pi, abs=1e-12)
    assert (b - a).mass == pytest.approx(math.pi, abs=1e-12)


# centres 3e-14 apart (tol 1e-14): the circles within tol for |angle| < 1/3 around angle 0
SHIFT = 3e-14


def at(c, r, angle):
    return (c[0] + r * math.cos(angle), c[1] + r * math.sin(angle))


def test_nearly_concentric_arcs_share_the_stretch_between_their_ends():
    a = px.ArcEdge(at((0, 0), 1, -0.5), at((0, 0), 1, 0.2), (0, 0))
    b = px.ArcEdge(at((0, SHIFT), 1, -0.2), at((0, SHIFT), 1, 0.5), (0, SHIFT))
    result, pieces = a.Cut(b)
    assert result == CutResult.Overlap
    assert ends(a) == (at((0, 0), 1, -0.5), at((0, SHIFT), 1, -0.2))
    assert ends(b) == (at((0, 0), 1, 0.2), at((0, SHIFT), 1, 0.5))
    assert [ends(e) for e in pieces] == [(at((0, SHIFT), 1, -0.2), at((0, 0), 1, 0.2))]


def ring_piece(c, r0, r1, t0, t1):
    return px.Wire([px.Arc(at(c, r1, t0), at(c, r1, t1), c), px.Line(at(c, r1, t1), at(c, r0, t1)),
                    px.Arc(at(c, r0, t1), at(c, r0, t0), c, reverse=True), px.Line(at(c, r0, t0), at(c, r1, t0))]).Face()


@pytest.mark.parametrize("w", [1e-3, 1e-6])
def test_thin_ring_pieces_on_both_sides_of_nearly_concentric_arcs(w):
    a = ring_piece((0, 0), 1 - w, 1, -0.5, 0.2)
    b = ring_piece((0, SHIFT), 1, 1 + w, -0.2, 0.5)
    ma = 0.35 * (2 - w) * w
    mb = 0.35 * (2 + w) * w
    assert a.mass == pytest.approx(ma, rel=1e-9)
    assert b.mass == pytest.approx(mb, rel=1e-9)
    assert (a * b).mass == pytest.approx(0, abs=1e-12)
    assert (a + b).mass == pytest.approx(ma + mb, abs=1e-12)
    assert (a - b).mass == pytest.approx(ma, abs=1e-12)
    assert (b - a).mass == pytest.approx(mb, abs=1e-12)


# a chord 2 sin(1e-3) long and its arc (sagitta 5e-7): one end of each on the other, but no common stretch
CHORD = 1e-3
Q = (math.sin(CHORD), math.cos(CHORD))
Q1 = (-math.sin(CHORD), math.cos(CHORD))


def test_a_chord_and_its_arc_share_no_stretch():
    a = px.LineEdge(Q1, Q)
    b = px.ArcEdge(Q, Q1, (0, 0))
    result, pieces = a.Cut(b)
    assert result == CutResult.Untouched
    assert ends(a) == (Q1, Q)
    assert ends(b) == (Q, Q1)
    assert pieces == []


def test_the_face_of_a_chord_and_its_arc_keeps_its_segment():
    x = 2 * CHORD
    segment = (x ** 3 / 6 - x ** 5 / 120 + x ** 7 / 5040) / 2
    face = px.Wire([px.Line(Q1, Q), px.Arc(Q, Q1, (0, 0))]).Face()
    assert face.mass == pytest.approx(segment, rel=1e-6)


# fuzz case: a closed spline and its copy nudged by 1e-13 (below tol): the approximations' arcs are copies within tol
def test_closed_spline_and_its_copy_nudged_below_tol():
    a = px.QuickSketch().Move((1.75, 0.0)).Spline([(-1.3098938092994266, 1.1604646519213921),
                                                   (-0.6205585523244378, -1.6362784246994757)], periodic=True).Face()
    b = a.Translated((7.071067811865474e-14, -7.071067811865477e-14))
    assert (a + b).mass == pytest.approx(a.mass, abs=1e-12)
    assert (a * b).mass == pytest.approx(a.mass, abs=1e-12)
    assert (a - b).mass == pytest.approx(0, abs=1e-12)
    assert (b - a).mass == pytest.approx(0, abs=1e-12)
