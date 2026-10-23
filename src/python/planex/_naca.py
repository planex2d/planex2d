# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
from typing import TYPE_CHECKING

from . import _planex as px

if TYPE_CHECKING:
    from ._planex import Face
    from .types import Plane

# 5-digit mean lines for a design lift coefficient of 0.3, by the position digit: r and k1, reflex ones also k2/k1
FIVE_DIGIT = {1: (0.0580, 361.40), 2: (0.1260, 51.640), 3: (0.2025, 15.957), 4: (0.2900, 6.643), 5: (0.3910, 3.230)}
FIVE_DIGIT_REFLEX = {2: (0.1300, 51.990, 0.000764), 3: (0.2170, 15.793, 0.00677), 4: (0.3180, 6.520, 0.0303),
                     5: (0.4410, 3.191, 0.1355)}


def _mean_line(code):
    """the camber line of the code as x -> (yc, dyc/dx) on the unit chord, and the thickness ratio"""
    call = "FromNACA(%r)" % (code,)
    if not isinstance(code, str) or not code.isdigit() or len(code) not in (4, 5):
        raise ValueError(call + ": a 4- or 5-digit code such as '2412' or '23012' (the 6-series is not supported yet)")
    if len(code) == 4:
        m, p, t = int(code[0]) / 100, int(code[1]) / 10, int(code[2:]) / 100
        if m > 0 and p == 0:
            raise ValueError(call + ": a cambered 4-digit airfoil needs the position of the maximum camber")

        def camber(x):
            if m == 0:
                return 0.0, 0.0
            if x < p:
                return m / p**2 * (2 * p * x - x * x), 2 * m / p**2 * (p - x)
            return m / (1 - p) ** 2 * (1 - 2 * p + 2 * p * x - x * x), 2 * m / (1 - p) ** 2 * (p - x)

        return camber, t
    lift, position, reflex, t = int(code[0]), int(code[1]), int(code[2]), int(code[3:]) / 100
    scale = lift / 2
    if reflex == 0 and position in FIVE_DIGIT:
        r, k1 = FIVE_DIGIT[position]

        def camber(x):
            if x < r:
                return (scale * k1 / 6 * (x**3 - 3 * r * x * x + r * r * (3 - r) * x),
                        scale * k1 / 6 * (3 * x * x - 6 * r * x + r * r * (3 - r)))
            return scale * k1 * r**3 / 6 * (1 - x), -scale * k1 * r**3 / 6

        return camber, t
    if reflex == 1 and position in FIVE_DIGIT_REFLEX:
        r, k1, k21 = FIVE_DIGIT_REFLEX[position]

        def camber(x):
            a = k21 * (1 - r) ** 3 + r**3
            if x < r:
                return scale * k1 / 6 * ((x - r) ** 3 - a * x + r**3), scale * k1 / 6 * (3 * (x - r) ** 2 - a)
            return (scale * k1 / 6 * (k21 * (x - r) ** 3 - a * x + r**3),
                    scale * k1 / 6 * (3 * k21 * (x - r) ** 2 - a))

        return camber, t
    raise ValueError(call + ": no 5-digit mean line for the digits '%s'" % code[1:3])


def _sides(code, points, sharp):
    """the upper and lower side on the unit chord, both from the leading edge at (0, 0) to the trailing edge, at
    points cosine spaced stations, and the slope of the camber line at the leading edge"""
    camber, t = _mean_line(code)
    # the last coefficient closes the trailing edge (sharp) or leaves the classic 0.25% chord gap (blunt)
    a4 = -0.1036 if sharp else -0.1015
    upper, lower = [], []
    for i in range(points):
        x = (1 - math.cos(math.pi * i / (points - 1))) / 2
        yt = 5 * t * (0.2969 * math.sqrt(x) - 0.1260 * x - 0.3516 * x * x + 0.2843 * x**3 + a4 * x**4)
        yc, slope = camber(x)
        theta = math.atan(slope)
        upper.append((x - yt * math.sin(theta), yc + yt * math.cos(theta)))
        lower.append((x + yt * math.sin(theta), yc - yt * math.cos(theta)))
    upper[0] = lower[0] = (0.0, 0.0)
    if sharp:
        upper[-1] = lower[-1] = (1.0, 0.0)
    return upper, lower, camber(0.0)[1]


def FromNACA(code: str, /, chord: float = 1.0, samples: int = 100, trailing_edge: str = "sharp", angle: float = 0.0,
             pivot: float = 0.25, plane: Plane | None = None) -> Face:
    """A NACA 4-digit or 5-digit airfoil as a Face, the leading edge at the origin and the chord along the plane's u.

    Each side is one spline through cosine spaced stations of the exact contour, both with the same tangent at the
    leading edge.

    Args:
        code: the airfoil, 4 digits ('2412') or 5 digits ('23012', reflex '23112').
        chord: the chord length, > 0.
        samples: the number of stations per side, an integer >= 3.
        trailing_edge: 'sharp' closes the contour at the trailing edge, 'blunt' keeps the classic formula's gap and
            closes it with a line.
        angle: the angle of attack in degrees, positive nose up.
        pivot: the point the angle turns around, on the chord line as a fraction of the chord.
        plane: the plane of the face; None is the xy plane.

    Raises:
        ValueError: code is not a 4- or 5-digit code planex knows, trailing_edge is neither 'sharp' nor 'blunt',
            samples is not an integer >= 3, or chord is not positive.

    Examples:
        px.FromNACA("2412")
        px.FromNACA("23012", chord=0.3, angle=5, trailing_edge="blunt")
    """
    if trailing_edge not in ("sharp", "blunt"):
        raise ValueError("FromNACA: trailing_edge is 'sharp' or 'blunt'")
    if not isinstance(samples, int) or samples < 3:
        raise ValueError("FromNACA: samples is an integer >= 3")
    if not chord > 0:
        raise ValueError("FromNACA: chord must be positive")
    plane = px.AxesOf(plane, "FromNACA")
    upper, lower, slope = _sides(code, samples, trailing_edge == "sharp")
    c, s = math.cos(math.radians(-angle)), math.sin(math.radians(-angle))
    x0 = pivot * chord

    def placed(p):
        x, y = p[0] * chord - x0, p[1] * chord
        return (x0 + c * x - s * y, c * y + s * x)

    upper, lower = [placed(p) for p in upper], [placed(p) for p in lower]
    # along the contour from the upper to the lower side the leading edge tangent is normal to the camber line
    n = math.hypot(slope, 1.0)
    tangent = ((c * slope + s) / n, (s * slope - c) / n)
    w = px.Spline(upper[::-1], tangents={samples - 1: tangent}, plane=plane)
    w.Add(px.Spline(lower, tangents={0: tangent}, plane=plane))
    if trailing_edge == "blunt":
        w.Add(px.Line(lower[-1], upper[-1], plane=plane))
    return px.Face([w])
