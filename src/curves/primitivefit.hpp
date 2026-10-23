// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/primitiveedge.hpp"

namespace planex2d {

    // a nearly straight arc has a far center whose rounding moves its points: a line where it stays within atol
    inline PrimitiveEdge TangentArc(const Vertex &p, const Vertex &t, const Vertex &q, double atol) {
        Vertex chord = q - p;
        Vertex n{-t.y, t.x};
        double nc = Dot(n, chord);
        // the sagitta is |chord|^2 / (8 r) with r = |chord|^2 / (2 |nc|), i.e. |nc| / 4
        if (std::abs(nc) <= 4.0 * atol)
            return Line(p, q);
        double k = Dot(chord, chord) / (2.0 * nc);
        return Arc(p, q, p + n * k, k > 0.0);
    }
    inline std::vector<PrimitiveEdge> Biarc(const Vertex &pa, const Vertex &ta, const Vertex &pb, const Vertex &tb, double atol) {
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
            double d = (-vt + std::sqrt(vt * vt + denom * vv)) / denom;
            pm = (pa + ta * d + pb - tb * d) * 0.5;
        }
        if (Norm(pm - pa) <= tol || Norm(pb - pm) <= tol)
            return {TangentArc(pa, ta, pb, atol)};
        PrimitiveEdge first = TangentArc(pa, ta, pm, atol);
        return {first, TangentArc(pm, first.EndTangent(), pb, atol)};
    }
    // coincident poles make the derivative vanish (below 1e-6 of the chord over the span): the secant over 1e-3 of the
    // span stands in
    template <class C>
    Vertex CurveTangent(const C &c, double u, double other, const Vertex &p, const Vertex &q) {
        Vertex d = c.Derivative(u);
        if (!(Norm(d) * std::abs(other - u) > 1e-6 * Norm(q - p))) {
            double h = 1e-3 * (other - u);
            d = (c.At(u + h) - p) * (h > 0.0 ? 1.0 : -1.0);
            if (Norm(d) == 0.0)
                d = q - p;
        }
        return Normalized(d);
    }
    template <class C>
    double SplineDistance(const C &c, const std::vector<PrimitiveEdge> &e, double u) {
        Vertex q = c.At(u);
        double d = e[0].Distance(q);
        for (size_t i = 1; i < e.size(); ++i)
            d = std::min(d, e[i].Distance(q));
        return d;
    }
    // 16 samples, then a ternary search for the maximum between the neighbours of the largest sample; the 40 steps
    // shrink that bracket to 1e-7 of the sample spacing
    template <class C>
    double FitError(const C &c, const std::vector<PrimitiveEdge> &e, double ua, double ub) {
        const int n = 16;
        double h = (ub - ua) / n;
        double err = 0.0;
        int kmax = 1;
        for (int k = 1; k < n; ++k) {
            double dk = SplineDistance(c, e, ua + h * k);
            if (dk > err) {
                err = dk;
                kmax = k;
            }
        }
        double lo = ua + h * (kmax - 1), hi = ua + h * (kmax + 1);
        for (int it = 0; it < 40; ++it) {
            double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
            double d1 = SplineDistance(c, e, m1), d2 = SplineDistance(c, e, m2);
            err = std::max(err, std::max(d1, d2));
            if (d1 < d2)
                lo = m1;
            else
                hi = m2;
        }
        return err;
    }
    template <class C>
    void FitPrimitiveEdges(const C &c, double ua, double ub, const Vertex &pa, const Vertex &pb, double atol,
                           std::vector<PrimitiveEdge> &out, std::vector<double> &us, int depth) {
        if (Norm(pb - pa) <= Tol(std::max(NormInf(pa), NormInf(pb))))
            return;
        std::vector<PrimitiveEdge> e = Biarc(pa, CurveTangent(c, ua, ub, pa, pb), pb, CurveTangent(c, ub, ua, pb, pa), atol);
        double err = FitError(c, e, ua, ub);
        // depth limit: 2^30 pieces per interval is far beyond any useful tolerance
        if (err <= atol || depth >= 30) {
            out.insert(out.end(), e.begin(), e.end());
            if (e.size() == 2)
                us.push_back(c.Project(e[0].End(), ua, ub));
            us.push_back(ub);
            return;
        }
        double um = 0.5 * (ua + ub);
        Vertex pm = c.At(um);
        FitPrimitiveEdges(c, ua, um, pa, pm, atol, out, us, depth + 1);
        FitPrimitiveEdges(c, um, ub, pm, pb, atol, out, us, depth + 1);
    }
    template <class C>
    std::vector<PrimitiveEdge> Approximated(const C &c, const std::vector<double> &u, const std::vector<Vertex> &nodes, double atol) {
        std::vector<PrimitiveEdge> out;
        std::vector<double> us;
        for (size_t i = 0; i + 1 < u.size(); ++i)
            FitPrimitiveEdges(c, u[i], u[i + 1], nodes[i], nodes[i + 1], atol, out, us, 0);
        if (out.empty())
            throw std::runtime_error("degenerate curve: zero length");
        return out;
    }

}
