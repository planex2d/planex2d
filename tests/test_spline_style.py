# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import netgen.occ as occ
import pytest

import planex as px
from helpers import polygon_area

TOL = 1e-12

P2 = [(0, 0), (2, 1)]
P3 = [(0, 0), (1, 1), (2, 0)]
P4 = [(0, 0), (1, 1), (2, 0), (3, 1)]
P5 = [(0, 0), (1, 0.8), (2.3, 0.5), (3, -0.4), (4.2, 0.2)]
P8 = [(0, 0), (0.2, 0.3), (1, 1), (1.1, 1.05), (3, 0), (3.5, -1), (5, -1.2), (5.1, 0)]
C3 = [(1, 0), (-0.5, 0.8), (-0.5, -0.8)]
C4 = [(1, 0), (0, 1), (-1, 0), (0, -1)]
C5 = [(1.2, 0), (0.4, 1.1), (-0.9, 0.7), (-0.8, -0.6), (0.3, -1.0)]
C7 = [(1.2, 0), (0.8, 0.97), (-0.27, 1.24), (-1.1, 0.54), (-1.0, -0.56), (-0.29, -1.02), (0.76, -0.95)]
CASES = [
    (P2, False, {}), (P3, False, {}), (P4, False, {}), (P5, False, {}), (P8, False, {}),
    (P2, False, {0: (1, 0)}), (P2, False, {0: (1, 0), 1: (0, 1)}),
    (P3, False, {0: (0, 1)}), (P3, False, {2: (1, -1)}), (P3, False, {1: (1, 0)}),
    (P5, False, {0: (1, 0), 4: (0, 1)}), (P5, False, {0: (3, 0), 4: (0, 3)}), (P5, False, {0: (0.1, 0), 4: (0, 0.1)}),
    (P5, False, {1: (1, -0.2), 3: (1, 1)}), (P5, False, {0: (1, 1), 1: (1, 0), 2: (1, -0.5), 3: (1, 0.3), 4: (0, 1)}),
    (P8, False, {0: (1, 1), 2: (1, 0), 7: (0, 1)}),
    (C3, True, {}), (C4, True, {}), (C5, True, {}), (C7, True, {}),
    (C4, True, {0: (0, 1)}), (C4, True, {3: (1, 0)}), (C5, True, {2: (-1, -0.3)}),
    (C5, True, {0: (0, 1), 3: (0.5, -1)}),
    (C7, True, {0: (0, 2), 2: (-2, -0.2), 5: (2, 0)}),
]
IDS = ["%d points%s%s" % (len(p), " periodic" if c else "", "".join(" t%d" % k for k in t)) for p, c, t in CASES]


@pytest.fixture
def style():
    before = px.config.spline_style
    yield
    px.config.spline_style = before


def netgen(pts, periodic, tangents):
    return occ.SplineInterpolation(pts, periodic, 1e-8, tangents)


def chord(pts, periodic):
    loop = pts + pts[:1] if periodic else pts
    return sum(math.dist(a, b) for a, b in zip(loop, loop[1:]))


def from_netgen(wire, curve):
    """the largest distance of the exported edges' points from netgen's curve at the same parameters"""
    worst = 0.0
    for e in wire.ToOCCWire().edges:
        s0, s1 = e.parameter_interval
        for k in range(41):
            s = s0 + (s1 - s0) * k / 40
            p, q = e.Value(s), curve.Value(s)
            worst = max(worst, math.hypot(p.x - q.x, p.y - q.y))
    return worst


def unit(t):
    n = math.hypot(*t)
    return (t[0] / n, t[1] / n)


@pytest.mark.parametrize("pts, periodic, tangents", CASES, ids=IDS)
def test_clamped_style_is_netgens_spline(pts, periodic, tangents):
    curve = netgen(pts, periodic, tangents)
    end = chord(pts, periodic)
    ours = [px._planex._InterpolatedAt(pts, tangents, periodic, end * k / 200, 0, style="clamped") for k in range(201)]
    assert max(math.dist(p, (q.x, q.y)) for p, q in zip(ours, (curve.Value(end * k / 200) for k in range(201)))) < TOL
    assert from_netgen(px.Spline(pts, periodic=periodic, tangents=tangents, style="clamped"), curve) < TOL


@pytest.mark.parametrize("pts, periodic, tangents", CASES, ids=IDS)
def test_clamped_style_in_a_sketch_is_netgens_spline(pts, periodic, tangents):
    # a sketch keeps tangent directions: two points with one tangent take its unit vector for the other end too
    sketched = {k: unit(t) for k, t in tangents.items()} if len(pts) == 2 and len(tangents) == 1 else tangents
    sk = px.QuickSketch()
    sk.Spline(pts, periodic=periodic, tangents=tangents, start_from_localpos=False, style="clamped")
    assert sk.Geometry("Spline.1")["style"] == "clamped"
    assert from_netgen(sk.Wire("Spline.1"), netgen(pts, periodic, sketched)) < TOL


def test_averaged_style_is_planexs_own():
    w = px.Spline(P5, style="averaged")
    assert from_netgen(px.Spline(P4, style="averaged"), netgen(P4, False, {})) < TOL
    assert from_netgen(w, netgen(P5, False, {})) > 1e-3
    assert w.mass == px.Spline(P5).mass
    sk = px.QuickSketch()
    sk.Spline(P5, start_from_localpos=False)
    assert sk.Geometry("Spline.1")["style"] == "averaged"
    assert from_netgen(sk.Wire("Spline.1"), netgen(P5, False, {})) > 1e-3


@pytest.mark.parametrize("pts", [C4, C5, C7], ids=["4 points", "5 points", "7 points"])
@pytest.mark.constraints
def test_averaged_periodic_is_the_c2_periodic_cubic_from_every_start(pts):
    areas = []
    for k in range(len(pts)):
        turned = pts[k:] + pts[:k]
        areas.append(polygon_area(px.Face([px.Spline(turned, periodic=True)])))
        for make in (px.QuickSketch, px.Sketch):
            sk = make()
            sk.Spline(turned, periodic=True, start_from_localpos=False)
            areas.append(polygon_area(sk.Face()))
    assert max(areas) - min(areas) <= 1e-12 * max(areas)
    if pts is C4:
        assert areas[0] == pytest.approx(61 / 20, rel=TOL)


def test_averaged_periodic_spline_and_sketch_spline_are_one_curve():
    end = chord(C5, True)
    w = px.Spline(C5, periodic=True)
    for e in w.ToOCCWire().edges:
        s0, s1 = e.parameter_interval
        for k in range(21):
            s = s0 + (s1 - s0) * k / 20
            p = e.Value(s)
            assert px._planex._InterpolatedAt(C5, {}, True, s, 0) == pytest.approx((p.x, p.y), abs=TOL)
    assert max(e.parameter_interval[1] for e in w.ToOCCWire().edges) == pytest.approx(end, rel=TOL)


def test_config_spline_style(style):
    assert px.config.spline_style == "averaged"
    px.config.spline_style = "clamped"
    assert px.config.spline_style == "clamped"
    assert from_netgen(px.Spline(P5), netgen(P5, False, {})) < TOL
    sk = px.QuickSketch()
    sk.Spline(P5, start_from_localpos=False)
    assert sk.Geometry("Spline.1")["style"] == "clamped"
    sk.Spline([(5, 1), (6, 2)], style="averaged")
    assert sk.Geometry("Spline.2")["style"] == "averaged"
    for wrong in ("OCC", "netgen", 1, None):
        with pytest.raises(ValueError, match="'averaged' or 'clamped'"):
            px.config.spline_style = wrong
    assert px.config.spline_style == "clamped"
    with pytest.raises(ValueError, match=r"Spline: style 'b'; it is 'averaged' or 'clamped'"):
        px.Spline(P3, style="b")
    with pytest.raises(ValueError, match="'averaged' or 'clamped'"):
        px.QuickSketch().Spline(P3, style="b")


@pytest.mark.parametrize("old, new", [("px", "averaged"), ("occ", "clamped")])
def test_the_old_style_names_raise_naming_the_new(style, old, new):
    message = rf"style '{old}' is now '{new}'"
    with pytest.raises(ValueError, match=r"^Spline: " + message):
        px.Spline(P3, style=old)
    with pytest.raises(ValueError, match=message):
        px.QuickSketch().Spline(P3, style=old)
    with pytest.raises(ValueError, match=message):
        px.Sketch().Spline(P3, style=old)
    with pytest.raises(ValueError, match=r"^config.spline_style: " + message):
        px.config.spline_style = old
    assert px.config.spline_style == "averaged"


@pytest.mark.constraints
def test_a_clamped_spline_with_tangents_warns_in_a_sketch_with_constraints():
    sk = px.Sketch()
    with pytest.warns(px.PlanexWarning, match=r"Spline.1: style 'clamped' with given tangents is not smooth for "):
        sk.Move((0, 0)).Spline([(1, 1), (2, 0), (3, 1)], tangents={0: (1, 0)}, style="clamped")
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        sk.Move((0, 2)).Spline([(1, 3), (2, 2), (3, 3)], style="clamped")
        sk.Move((0, 4)).Spline([(1, 5), (2, 4), (3, 5)], tangents={0: (1, 0)})
        px.QuickSketch().Move((0, 0)).Spline([(1, 1), (2, 0), (3, 1)], tangents={0: (1, 0)}, style="clamped")


@pytest.mark.parametrize("style_", ["averaged", "clamped"])
@pytest.mark.constraints
def test_constraints_move_a_spline_with_tangents_in_either_style(style_):
    sk = px.Sketch()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", px.PlanexWarning)
        sk.Move((0, 0), px.Fix).Spline([(1, 0.8), (2.3, 0.5), (3, -0.4), (4.2, 0.2)], tangents={0: (1, 0), 4: (0, 1)},
                                       style=style_)
        sk.Vertex("q", (2.3, 0.4))
        sk.Constrain("q", "Spline.1", px.Coincident)
        sk.Constrain("Spline.1.end", px.H, px.Distance, 1.5)
        sk.Constrain("origin", "q", px.Distance, 2.0)
    assert sk.Location("Spline.1.end")[1] == pytest.approx(1.5, abs=1e-12)
    assert math.dist(sk.Location("origin"), sk.Location("q")) == pytest.approx(2.0, abs=1e-12)
