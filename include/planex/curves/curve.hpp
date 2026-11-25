// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"

namespace planex2d {

    inline double ValueOf(double v) { return v; }
    inline bool ExactlyZero(double v) { return v == 0.0; }

    template <class S>
    size_t FindSpan(const std::vector<S> &t, size_t p, size_t n, const S &u);

    template <class S>
    std::vector<std::vector<S>> BasisDerivatives(const std::vector<S> &t, size_t p, size_t span, const S &u, size_t kmax);

    template <class S, class P>
    void InsertKnot(std::vector<S> &t, std::vector<P> &q, size_t p, const S &u);

    struct BSplineData {
        std::vector<Vertex> poles;
        std::vector<double> knots;
        std::vector<double> weights;  // empty if not rational
        int degree = 0;
    };

    // S: double, or dual numbers that carry the derivatives with respect to the points and tangents the curve was made
    // from; P: its points (with +, - and * S)
    template <class S, class P>
    struct Curve {
        size_t degree = 3;
        bool closed = false;
        std::vector<S> knots;
        // of the points, a closed curve's end (back at the first point) last
        std::vector<S> params;
        std::vector<size_t> tangentAt;
        std::vector<P> poles;
        // one per pole, or none if not rational
        std::vector<S> weights;

        S First() const { return knots[degree]; }
        S Last() const { return knots[poles.size()]; }
        P At(const S &u, size_t k = 0) const;
        P Derivative(const S &u) const { return At(u, 1); }
        P Direction(const S &u) const;
        // signed along the curve's direction, a left turn positive
        double Curvature(const S &u) const {
            const P d1 = At(u, 1), d2 = At(u, 2);
            const double speed = Norm(d1);
            return Cross(d1, d2) / (speed * speed * speed);
        }
        S Project(const P &p, std::optional<S> guess = std::nullopt) const;
        S Project(const P &p, const S &ua, const S &ub) const;
        Curve Reversed() const;
        Curve Segment(S a, S b) const;
        Curve Shifted(const S &d) const {
            Curve r = *this;
            for (S &t : r.knots)
                t = t + d;
            for (S &t : r.params)
                t = t + d;
            return r;
        }
        Curve Joined(const Curve &next) const;
    };

    std::vector<double> UniformKnots(size_t n, size_t p, bool periodic);

    template <class S, class P>
    Curve<S, P> PoleCurve(const std::vector<P> &poles, const std::vector<double> &knots, size_t p, bool periodic);

    Curve<double, Vertex> BSplineCurve(const BSplineData &b, bool periodic);

}
