// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/vertex.hpp"
#include "curves/curve.hpp"

namespace planex2d {

    // the ellipse c + cos t a + sin t b for t from t0 to t1 (radians, t0 < t1, at most one turn), exactly: the unit
    // circle's rational quadratic b-spline in pieces of at most 90 degrees, each middle pole where the piece's end
    // tangents meet with the cosine of half the piece's sweep as its weight, mapped by the axes a and b; a whole turn
    // ends on its first pole
    inline BSplineData EllipseData(const Vertex &c, const Vertex &a, const Vertex &b, double t0, double t1) {
        const double turn = 2.0 * std::numbers::pi;
        const bool whole = std::abs(t1 - t0 - turn) <= 1e-12 * turn;
        const size_t n = std::max<size_t>(1, (size_t)std::ceil((t1 - t0) / (turn / 4) - 1e-9));
        const double step = (t1 - t0) / (double)n, w = std::cos(step / 2);
        const auto at = [&](double u, double v) { return c + a * u + b * v; };
        BSplineData d;
        d.degree = 2;
        d.poles.push_back(at(std::cos(t0), std::sin(t0)));
        d.weights.push_back(1.0);
        d.knots = {0.0, 0.0, 0.0};
        for (size_t k = 0; k < n; ++k) {
            const double m = t0 + ((double)k + 0.5) * step, e = k + 1 == n ? t1 : t0 + (double)(k + 1) * step;
            d.poles.push_back(at(std::cos(m) / w, std::sin(m) / w));
            d.weights.push_back(w);
            d.poles.push_back(whole && k + 1 == n ? d.poles.front() : at(std::cos(e), std::sin(e)));
            d.weights.push_back(1.0);
            const double knot = (double)(k + 1);
            d.knots.insert(d.knots.end(), k + 1 == n ? 3 : 2, knot);
        }
        return d;
    }
}
