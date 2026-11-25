// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "planex/curves/curve.hpp"

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

    template <class S, class P>
    Curve<S, P> G2Blend(const P &p0, const P &t0, const S &k0, const P &p1, const P &t1, const S &k1);

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
