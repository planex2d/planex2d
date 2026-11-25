// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/blend.hpp"

#ifdef PLANEX_CONSTRAINTS
#include "planex/constraint_sketch/dual.hpp"
#endif

namespace planex2d {

    // k0, k1 > 0 turning left; no acceleration along the directions at the ends
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

    template Curve<double, Vertex> G2Blend<double, Vertex>(const Vertex &p0, const Vertex &t0, const double &k0,
                                                           const Vertex &p1, const Vertex &t1, const double &k1);

#ifdef PLANEX_CONSTRAINTS
    template Curve<cs::Dual, cs::DualVector> G2Blend<cs::Dual, cs::DualVector>(const cs::DualVector &p0,
                                                                               const cs::DualVector &t0,
                                                                               const cs::Dual &k0,
                                                                               const cs::DualVector &p1,
                                                                               const cs::DualVector &t1,
                                                                               const cs::Dual &k1);
#endif

}
