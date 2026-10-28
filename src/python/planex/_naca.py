# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
from typing import TYPE_CHECKING

from . import _planex as px

if TYPE_CHECKING:
    from ._planex import Face, OffsetForm
    from .types import Plane

# 5-digit mean lines for a design lift coefficient of 0.3, by the position digit: r and k1, reflex ones also k2/k1
FIVE_DIGIT = {1: (0.0580, 361.40), 2: (0.1260, 51.640), 3: (0.2025, 15.957), 4: (0.2900, 6.643), 5: (0.3910, 3.230)}
FIVE_DIGIT_REFLEX = {2: (0.1300, 51.990, 0.000764), 3: (0.2170, 15.793, 0.00677), 4: (0.3180, 6.520, 0.0303),
                     5: (0.4410, 3.191, 0.1355)}
# the station x along the unit chord at the parameter t in degrees: cosine spacing, smooth in t at the leading edge
STATION = "((1 - cos(t))/2)"


def _mean_line(code):
    """the camber line of the code on the unit chord in pieces from the leading edge, each (the station where it ends,
    the camber and its slope as expressions in the station X), and the thickness ratio"""
    call = "FromNACA(%r)" % (code,)
    if not isinstance(code, str) or not code.isdigit() or len(code) not in (4, 5):
        raise ValueError(call + ": a 4- or 5-digit code such as '2412' or '23012' (the 6-series is not supported yet)")
    if len(code) == 4:
        m, p, t = int(code[0]) / 100, int(code[1]) / 10, int(code[2:]) / 100
        if m > 0 and p == 0:
            raise ValueError(call + ": a cambered 4-digit airfoil needs the position of the maximum camber")
        if m == 0:
            return [(1.0, "0", "0")], t
        a, b = m / p**2, m / (1 - p) ** 2
        return [(p, f"{a!r}*(2*{p!r}*X - X^2)", f"{2 * a!r}*({p!r} - X)"),
                (1.0, f"{b!r}*({1 - 2 * p!r} + 2*{p!r}*X - X^2)", f"{2 * b!r}*({p!r} - X)")], t
    lift, position, reflex, t = int(code[0]), int(code[1]), int(code[2]), int(code[3:]) / 100
    scale = lift / 2
    if reflex == 0 and position in FIVE_DIGIT:
        r, k1 = FIVE_DIGIT[position]
        k = scale * k1 / 6
        return [(r, f"{k!r}*(X^3 - {3 * r!r}*X^2 + {r * r * (3 - r)!r}*X)", f"{k!r}*(3*X^2 - {6 * r!r}*X + {r * r * (3 - r)!r})"),
                (1.0, f"{k * r**3!r}*(1 - X)", f"{-k * r**3!r}")], t
    if reflex == 1 and position in FIVE_DIGIT_REFLEX:
        r, k1, k21 = FIVE_DIGIT_REFLEX[position]
        k, a = scale * k1 / 6, k21 * (1 - r) ** 3 + r**3
        return [(r, f"{k!r}*((X - {r!r})^3 - {a!r}*X + {r**3!r})", f"{k!r}*(3*(X - {r!r})^2 - {a!r})"),
                (1.0, f"{k!r}*({k21!r}*(X - {r!r})^3 - {a!r}*X + {r**3!r})", f"{k!r}*({3 * k21!r}*(X - {r!r})^2 - {a!r})")], t
    raise ValueError(call + ": no 5-digit mean line for the digits '%s'" % code[1:3])


def _surface(camber, slope, t, a4, side):
    """a side (+1 upper, -1 lower) at the station X = (1 - cos t)/2 as the expressions (x, y) in t: the thickness normal
    to the camber line, its sqrt(X) written sin(t/2)"""
    x = STATION
    camber, slope = camber.replace("X", x), slope.replace("X", x)
    thickness = f"{5 * t!r}*(0.2969*sin(t/2) - 0.126*{x} - 0.3516*{x}^2 + 0.2843*{x}^3 + {a4!r}*{x}^4)"
    norm = f"sqrt(1 + ({slope})^2)"
    return f"{x} - {side}*({thickness})*({slope})/{norm}", f"{camber} + {side}*({thickness})/{norm}"


def _placed(point, chord, angle, pivot):
    """(x, y) on the unit chord scaled to the chord and turned by the angle (nose up) around the pivot"""
    c, s = math.cos(math.radians(-angle)), math.sin(math.radians(-angle))
    x0 = pivot * chord
    h, v = f"(({point[0]})*{chord!r} - {x0!r})", f"(({point[1]})*{chord!r})"
    return f"{x0!r} + {c!r}*{h} - {s!r}*{v}", f"{c!r}*{v} + {s!r}*{h}"


# id= is the id wrapper's (_ids.with_id), which names the result; declared here so that type checkers see it
def FromNACA(code: str, /, chord: float = 1.0, trailing_edge: OffsetForm = px.OffsetForm.Sharp, angle: float = 0.0,
             pivot: float = 0.25, plane: Plane | None = None, *, id: str | None = None) -> Face:
    """A NACA 4-digit or 5-digit airfoil as a Face, the leading edge at the origin and the chord along the plane's u.

    Each side is the exact contour of the series' formulas, as parametric curves fitted to about 1e-12 of the chord, a
    side in two where its camber line changes formula (a cambered airfoil has four curves, a symmetric one two); the
    sides meet at the leading edge with one tangent and curvature.

    Args:
        code: the airfoil, 4 digits ('2412') or 5 digits ('23012', reflex '23112').
        chord: the chord length, > 0.
        trailing_edge: px.Sharp closes the contour at the trailing edge, px.Flat keeps the classic formula's gap and
            closes it with a line.
        angle: the angle of attack in degrees, positive nose up.
        pivot: the point the angle turns around, on the chord line as a fraction of the chord.
        plane: the plane of the face; None is the xy plane.

    Raises:
        ValueError: code is not a 4- or 5-digit code planex knows, trailing_edge is neither px.Sharp nor px.Flat, or
            chord is not positive.

    Examples:
        px.FromNACA("2412")
        px.FromNACA("23012", chord=0.3, angle=5, trailing_edge=px.Flat)
    """
    sharp, flat = px.OffsetForm.Sharp, px.OffsetForm.Flat
    if not (isinstance(trailing_edge, px.OffsetForm) and trailing_edge in (sharp, flat)):
        raise ValueError("FromNACA: trailing_edge is px.Sharp or px.Flat, not %r" % (trailing_edge,))
    if not chord > 0:
        raise ValueError("FromNACA: chord must be positive")
    plane = px.AxesOf(plane, "FromNACA")
    pieces, t = _mean_line(code)
    # the last coefficient closes the trailing edge (sharp) or leaves the classic 0.25% chord gap (flat)
    a4 = -0.1036 if trailing_edge == sharp else -0.1015
    stations = [0.0] + [x for x, _, _ in pieces]
    angles = [math.degrees(math.acos(1 - 2 * x)) for x in stations]
    # around the contour: the upper side from the trailing edge to the leading edge, the lower side back
    runs = [(1, k, angles[k + 1], angles[k]) for k in reversed(range(len(pieces)))]
    runs += [(-1, k, angles[k], angles[k + 1]) for k in range(len(pieces))]
    sk = px.QuickSketch(plane)
    for side, k, t0, t1 in runs:
        _, camber, slope = pieces[k]
        sk.Curve(_placed(_surface(camber, slope, t, a4, side), chord, angle, pivot), (t0, t1))
    if trailing_edge == flat:
        sk.Close()
    return sk.Face()
