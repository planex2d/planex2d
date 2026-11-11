# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Near-tangent sweeps: two faces that touch, from outside or from inside, moved into the gap or the overlap by 1e-3
... 1e-16, at the origin, turned, far from it and small. The booleans compare with the exact areas within what Tol(S)
allows; the glue keeps every edge, none shorter than Tol(S); px.Wires of a circle and a line ending at it leaves no
edge shorter than its tol."""
import math

import pytest

import planex as px

pytest.importorskip("hypothesis")

import fuzzlib  # noqa: E402

SHIFTS = [10.0 ** -k for k in range(3, 9)] + [m * 10.0 ** -k for k in range(9, 17) for m in (1.0, 3.0)]
SIGNED = [s for shift in SHIFTS for s in (shift, -shift)]

CIRCLES = {
    "equal": (1.0, 1.0, (0.0, 0.0), 0.0, False),
    "unequal-turned": (1.0, 0.3, (0.0, 0.0), 0.7, False),
    "large": (5.0, 2.0, (0.0, 0.0), 0.3, False),
    "small": (0.01, 0.01, (0.0, 0.0), 1.1, False),
    "far": (1.0, 0.3, (1000.0, -700.0), 2.1, False),
    "inside": (1.0, 0.3, (0.0, 0.0), 0.0, True),
    "inside-turned": (1.0, 0.3, (0.0, 0.0), 0.7, True),
    "inside-far": (1.0, 0.3, (1000.0, -700.0), 2.1, True),
}
LINES = {"line": (1.0, (0.0, 0.0)), "line-far": (1.0, (1000.0, -700.0))}


def segment(r, h):
    """the area of a circle's segment of height h; the half angle from its sine, which stays exact where h is tiny"""
    if h <= 0.0:
        return 0.0
    return 2 * r * r * math.asin(math.sqrt(h / (2 * r))) - (r - h) * math.sqrt(h * (2 * r - h))


def heights(r1, r2, d):
    """the heights of the two circles' segments beyond their common chord"""
    return (r2 - d + r1) * (r2 + d - r1) / (2 * d), (r1 - d + r2) * (r1 + d - r2) / (2 * d)


def circle_areas(r1, r2, d, inside):
    """the common area of discs r1 and r2 whose centres are d apart, and the area of the second outside the first"""
    b = math.pi * r2 * r2
    if inside:
        if d <= r1 - r2:
            return b, 0.0
        h1 = heights(r1, r2, d)[0]
        out = segment(r2, r2 - r1 + d + h1) - segment(r1, h1)
        return b - out, out
    if d >= r1 + r2:
        return 0.0, b
    h1, h2 = heights(r1, r2, d)
    common = segment(r1, h1) + segment(r2, h2)
    return common, b - common


def turned(c, angle, d):
    return (c[0] + d * math.cos(angle), c[1] + d * math.sin(angle))


def circle_pair(name, shift):
    r1, r2, c, angle, inside = CIRCLES[name]
    contact = r1 - r2 if inside else r1 + r2
    c2 = turned(c, angle, contact + (-shift if inside else shift))
    return px.Circle(c, r1).Face(), px.Circle(c2, r2).Face(), r1, r2, math.hypot(c2[0] - c[0], c2[1] - c[1])


def line_pair(name, shift):
    """a disc and a rectangle whose bottom side lies shift above the disc's top; the depth it reaches into the disc as
    the coordinates hold it"""
    r, c = LINES[name]
    bottom = c[1] + r + shift
    return px.Circle(c, r).Face(), px.Move((c[0] - 1.5 * r, bottom)).Rectangle(3 * r, r).Face(), r, (c[1] + r) - bottom


def chord(r, depth):
    return 2 * math.sqrt(max(0.0, depth * (2 * r - depth)))


def lens_chord(r1, r2, d, inside):
    """the distance between the two circles' crossings, 0 where they do not cross"""
    if (d <= r1 - r2) if inside else (d >= r1 + r2):
        return 0.0
    return chord(r1, heights(r1, r2, d)[0])


def near(value, expected, tol, what):
    assert abs(value - expected) <= tol, (what, value, expected, tol)


def same_areas(a, b, common, b_only):
    """a + b, a * b, a - b and b - a against the exact common area and the area of b outside a"""
    tol = fuzzlib.area_tol(a, b)
    A = fuzzlib.area(a)
    near(fuzzlib.area(a + b), A + b_only, tol, "union")
    near(fuzzlib.area(a * b), common, tol, "common")
    near(fuzzlib.area(a - b), A - common, tol, "a - b")
    near(fuzzlib.area(b - a), b_only, tol, "b - a")


def glued(a, b, lens):
    """the glue: every edge of both outlines, a shared stretch once (the lens, about as long as its chord), and no
    edge shorter than Tol(S)"""
    tol = px.config.k * px.config.eps * fuzzlib.size(a, b)
    g = px.Glue([a, b])
    lengths = [e.mass for e in g.edges]
    assert min(lengths) > tol, ("an edge under tol", min(lengths), tol)
    whole = fuzzlib.perimeter(a) + fuzzlib.perimeter(b)
    assert whole - 2 * lens - 8 * tol <= sum(lengths) <= whole + 8 * tol, ("edges lost", sum(lengths), whole, lens)


def shift_param(shift):
    return pytest.param(shift, id=("gap" if shift > 0 else "overlap") + f"-{abs(shift):.0e}")


@pytest.mark.parametrize("shift", [shift_param(s) for s in SIGNED])
@pytest.mark.parametrize("name", list(CIRCLES))
def test_touching_circles(name, shift):
    a, b, r1, r2, d = circle_pair(name, shift)
    same_areas(a, b, *circle_areas(r1, r2, d, CIRCLES[name][4]))


@pytest.mark.parametrize("shift", [shift_param(s) for s in SIGNED])
@pytest.mark.parametrize("name", list(LINES))
def test_line_touching_a_circle(name, shift):
    a, b, r, depth = line_pair(name, shift)
    common = segment(r, depth)
    same_areas(a, b, common, fuzzlib.area(b) - common)


@pytest.mark.parametrize("shift", [shift_param(s) for s in SIGNED])
@pytest.mark.parametrize("name", list(CIRCLES))
def test_glue_of_touching_circles(name, shift):
    a, b, r1, r2, d = circle_pair(name, shift)
    glued(a, b, lens_chord(r1, r2, d, CIRCLES[name][4]))


@pytest.mark.parametrize("shift", [shift_param(s) for s in SIGNED])
@pytest.mark.parametrize("name", list(LINES))
def test_glue_of_a_line_touching_a_circle(name, shift):
    a, b, r, depth = line_pair(name, shift)
    glued(a, b, chord(r, max(0.0, depth)))


WIRES_TOL = 1.5e-4


def wires_param(shift):
    """a line ending inside the circle by more than rounding keeps a stub shorter than tol
    up to sqrt(2 r shift) = tol; whether px.Wires joins it is open. At 1e-13 the end is within 10 % of the rounding,
    where the pair may be decided tangent on another platform"""
    depth = -shift
    if not 5e-14 < depth < 5e-10:
        return shift_param(shift)
    reason = "px.Wires keeps a stub under its tol where a line ends a hair inside a circle (open)"
    return pytest.param(shift, id=shift_param(shift).id,
                        marks=pytest.mark.xfail(strict=depth > 2e-13, reason=reason))


@pytest.mark.parametrize("shift", [wires_param(s) for s in SIGNED])
def test_wires_of_a_circle_and_a_line_ending_at_it(shift):
    """a line tangent to a circle r 22 whose end lies shift outside it (inside where negative), px.Wires with a tol
    well above the rounding: as long as before, no edge shorter than tol"""
    r, angle, length = 22.0, 1.3, 100.0
    n = (math.cos(angle), math.sin(angle))
    end = ((r + shift) * n[0], (r + shift) * n[1])
    far = (end[0] - length * n[1], end[1] + length * n[0])
    ws = px.Wires([px.Circle((0.0, 0.0), r), px.Line(end, far)], WIRES_TOL)
    lengths = [e.mass for w in ws for e in w.edges]
    assert min(lengths) > WIRES_TOL, ("an edge under tol", sorted(lengths)[:3])
    near(sum(lengths), 2 * math.pi * r + length, 4 * WIRES_TOL, "length")
