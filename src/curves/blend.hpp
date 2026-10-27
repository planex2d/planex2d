// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "curves/curve.hpp"

namespace planex2d {

    // a[0] + a[1] x + ... + a[n - 1] x^(n - 1)
    template <class S>
    S Polynomial(const double *a, size_t n, const S &x) {
        S y(a[n - 1]);
        for (size_t k = n - 1; k-- > 0;)
            y = y * x + S(a[k]);
        return y;
    }

    // the speed |dB/du| at one end of a blend over the chord c, u running from 0 to 1: x the cosine of the angle between
    // the end's direction and the chord, m (other) this end's (the other end's) curvature times c times the sine of
    // that angle, > 0 bending toward the chord; fit: a quartic in x, then linear factors of m and other in x
    template <class S>
    S BlendSpeed(const std::array<double, 9> &fit, const S &c, const S &x, const S &m, const S &other) {
        return c * (Polynomial(fit.data(), 5, x) + Polynomial(fit.data() + 5, 2, x) * m + Polynomial(fit.data() + 7, 2, x) * other);
    }

    // a Bezier curve over the chord length c
    template <class S, class P>
    Curve<S, P> BezierOverChord(std::vector<P> poles, const S &c) {
        Curve<S, P> f;
        f.degree = poles.size() - 1;
        for (size_t k = 0; k < 2 * poles.size(); ++k)
            f.knots.push_back(k < poles.size() ? S(0.0) : c);
        f.params = {S(0.0), c};
        f.poles = std::move(poles);
        return f;
    }

    // the quintic from p0 (unit direction t0, curvature k0, > 0 turning left) to p1 (t1, k1) matching position,
    // direction and curvature at both ends, without acceleration along them there: the curvature-continuous blend
    template <class S, class P>
    Curve<S, P> G2Blend(const P &p0, const P &t0, const S &k0, const P &p1, const P &t1, const S &k1) {
        // the speeds whose curve changes its curvature most evenly (the smallest integral of (dk/ds)^2), fitted once
        // over corners, fillets beside arcs and gaps:
        // within 0.1 % of that integral's least at corners between lines, 17 % beside arcs
        constexpr std::array<double, 9> fit{2.788250273, -4.886411919, 6.524384366, -4.971188227, 1.545961913,
                                            0.017450238, 0.170956296, -0.546252915, 0.448503193};
        const P chord = p1 - p0;
        const S c = Norm(chord);
        const S m0 = k0 * Cross(t0, chord), m1 = k1 * Cross(chord, t1);
        const S a0 = BlendSpeed(fit, c, Dot(chord, t0) / c, m0, m1), a1 = BlendSpeed(fit, c, Dot(chord, t1) / c, m1, m0);
        const P n0{-t0.y, t0.x}, n1{-t1.y, t1.x};
        return BezierOverChord<S, P>({p0, p0 + t0 * (a0 * S(0.2)), p0 + t0 * (a0 * S(0.4)) + n0 * (a0 * a0 * k0 * S(0.05)),
                                      p1 - t1 * (a1 * S(0.4)) + n1 * (a1 * a1 * k1 * S(0.05)), p1 - t1 * (a1 * S(0.2)), p1},
                                     c);
    }

    // the cubic from p0 (unit direction t0) to p1 (t1) matching position and direction at both ends: the tangent blend
    template <class S, class P>
    Curve<S, P> G1Blend(const P &p0, const P &t0, const P &p1, const P &t1) {
        // fitted as G2Blend's, to corners between lines (within 6 % of the least) and gaps
        constexpr std::array<double, 9> fit{2.262589461, -3.485310282, 4.612503569, -3.319735248, 0.929952747, 0.0, 0.0, 0.0, 0.0};
        const P chord = p1 - p0;
        const S c = Norm(chord), zero(0.0);
        const S a0 = BlendSpeed(fit, c, Dot(chord, t0) / c, zero, zero), a1 = BlendSpeed(fit, c, Dot(chord, t1) / c, zero, zero);
        const S third(1.0 / 3.0);
        return BezierOverChord<S, P>({p0, p0 + t0 * (a0 * third), p1 - t1 * (a1 * third), p1}, c);
    }

}
