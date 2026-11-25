// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/primitivefit.hpp"

namespace planex2d {

    std::vector<PrimitiveEdge> Biarc(const Vertex &pa, const Vertex &ta, const Vertex &pb, const Vertex &tb, double atol) {
        double tol = Tol(std::max(NormInf(pa), NormInf(pb)));
        Vertex v = pb - pa;
        double vv = Dot(v, v);
        if (std::abs(Cross(ta, v)) <= 4.0 * atol && std::abs(Cross(tb, v)) <= 4.0 * atol && Dot(ta, v) > 0.0)
            return {Line(pa, pb)};
        Vertex pm;
        if (std::abs(Cross(ta, tb)) * std::sqrt(vv) <= tol && Dot(ta, tb) > 0.0) {
            double vtb = Dot(v, tb);
            pm = std::abs(vtb) <= tol ? pa + v * 0.5 : (pa + ta * (vv / (4.0 * vtb)) + pb - tb * (vv / (4.0 * vtb))) * 0.5;
        } else {
            double denom = 2.0 * (1.0 - Dot(ta, tb));
            double vt = Dot(v, ta + tb);
            // the root of denom d^2 + 2 vt d = vv without cancellation: with tangents nearly alike denom is rounding
            // and (-vt + s) / denom is 0 / 0
            double s = std::sqrt(vt * vt + denom * vv);
            double d = vt >= 0.0 ? vv / (vt + s) : (s - vt) / denom;
            // tangents alike and across the chord: no biarc of equal tangent lengths; one arc, which the fit splits
            if (!std::isfinite(d))
                return {TangentArc(pa, ta, pb, atol)};
            pm = (pa + ta * d + pb - tb * d) * 0.5;
        }
        if (Norm(pm - pa) <= tol || Norm(pb - pm) <= tol)
            return {TangentArc(pa, ta, pb, atol)};
        PrimitiveEdge first = TangentArc(pa, ta, pm, atol);
        return {first, TangentArc(pm, first.EndTangent(), pb, atol)};
    }

}
