// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/parametricfit.hpp"

namespace planex2d {

    // the quintic B-spline through jets[k] at the parameters u[k]: a knot of multiplicity 3 at each inner one, so the
    // point and both derivatives there fix the three poles both pieces share; C2 by construction
    Curve<double, Vertex> QuinticThrough(const std::vector<double> &u, const std::vector<CurveJet> &jets) {
        constexpr size_t degree = 5;
        const size_t n = u.size() - 1;
        Curve<double, Vertex> c;
        c.degree = degree;
        c.params = u;
        c.knots.assign(degree + 1, u.front());
        for (size_t j = 1; j < n; ++j)
            c.knots.insert(c.knots.end(), 3, u[j]);
        c.knots.insert(c.knots.end(), degree + 1, u.back());
        c.poles.assign(3 * n + 3, Vertex{});
        for (size_t j = 0; j <= n; ++j) {
            const size_t span = FindSpan(c.knots, degree, c.poles.size(), u[j]), offset = 3 * j - (span - degree);
            const std::vector<std::vector<double>> d = BasisDerivatives(c.knots, degree, span, u[j], 2);
            std::vector<std::vector<double>> a(3, std::vector<double>(3));
            for (size_t k = 0; k < 3; ++k)
                for (size_t m = 0; m < 3; ++m)
                    a[k][m] = d[k][offset + m];
            std::vector<Vertex> b{jets[j].p, jets[j].d, jets[j].dd};
            SolveDense(std::move(a), b);
            std::copy(b.begin(), b.end(), c.poles.begin() + (std::ptrdiff_t)(3 * j));
        }
        c.poles.front() = jets.front().p;
        c.poles.back() = jets.back().p;
        return c;
    }

    // the quintic of the piece between a and b, h long, at the 7 points x = k / 8 between its ends
    std::array<Vertex, 7> QuinticSamples(const CurveJet &a, const CurveJet &b, double h) {
        const std::array<Vertex, 6> q{a.p, a.p + a.d * (h / 5.0), a.p + a.d * (2.0 * h / 5.0) + a.dd * (h * h / 20.0),
                                      b.p - b.d * (2.0 * h / 5.0) + b.dd * (h * h / 20.0), b.p - b.d * (h / 5.0), b.p};
        constexpr std::array<double, 6> binomial{1.0, 5.0, 10.0, 10.0, 5.0, 1.0};
        std::array<Vertex, 7> samples;
        for (size_t k = 1; k < 8; ++k) {
            const double x = (double)k / 8.0;
            std::array<double, 6> xs{1.0}, ys{1.0};
            for (size_t i = 1; i < 6; ++i) {
                xs[i] = xs[i - 1] * x;
                ys[i] = ys[i - 1] * (1.0 - x);
            }
            Vertex at{};
            for (size_t i = 0; i < 6; ++i)
                at = at + q[i] * (binomial[i] * xs[i] * ys[5 - i]);
            samples[k - 1] = at;
        }
        return samples;
    }

}
