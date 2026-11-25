// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/rankbasis.hpp"

#include <cmath>
#include <utility>

#include "planex/core/config.hpp"

namespace planex2d::cs {

    bool RankBasis::Joins(std::vector<double> v) {
        const auto length = [](const std::vector<double> &x) {
            double sum = 0.0;
            for (double e : x)
                sum += e * e;
            return std::sqrt(sum);
        };
        const double size = length(v);
        // twice: one Gram-Schmidt pass leaves v off orthogonal when it lies nearly in the span
        for (int pass = 0; pass < 2; ++pass)
            for (const std::vector<double> &b : vectors) {
                double d = 0.0;
                for (size_t i = 0; i < v.size(); ++i)
                    d += v[i] * b[i];
                for (size_t i = 0; i < v.size(); ++i)
                    v[i] -= d * b[i];
            }
        const double left = length(v);
        if (!(left > config::state->rank_tol * size))
            return false;
        for (double &e : v)
            e /= left;
        vectors.push_back(std::move(v));
        return true;
    }

}
