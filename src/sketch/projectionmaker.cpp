// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/projectionmaker.hpp"

namespace planex2d::sketch {

    std::vector<double> InterpolationParams(const Curve<double, Vertex> &c, bool closed, std::array<double, 2> endAngles, double tol) {
        const double u0 = c.First(), u1 = c.Last();
        std::vector<double> u{u0};
        for (double k : c.knots)
            if (k > u.back() && k < u1)
                u.push_back(k);
        u.push_back(u1);
        while (u.size() < (closed ? 4 : 3))
            for (size_t k = u.size() - 1; k > 0; --k)
                u.insert(u.begin() + (std::ptrdiff_t)k, (u[k - 1] + u[k]) / 2.0);
        // a bound on the work: each round interpolates all samples densely
        constexpr size_t most = 200;
        std::vector<size_t> tangentAt;
        if (!closed)
            tangentAt = {0, u.size() - 1};
        for (bool finer = true; finer && u.size() < most;) {
            std::vector<Vertex> q, t;
            for (size_t k = 0; k + (closed ? 1 : 0) < u.size(); ++k)
                q.push_back(c.At(u[k]));
            if (!closed) {
                tangentAt = {0, q.size() - 1};
                t = {{std::cos(endAngles[0]), std::sin(endAngles[0])}, {std::cos(endAngles[1]), std::sin(endAngles[1])}};
            }
            const Curve<double, Vertex> f = Interpolate(q, tangentAt, t, closed);
            std::vector<double> more;
            for (size_t k = 0; k + 1 < u.size(); ++k) {
                const double half = (u[k] + u[k + 1]) / 2.0;
                const Vertex x = c.At(half);
                if (Norm(f.At(f.Project(x, (f.params[k] + f.params[k + 1]) / 2.0), 0) - x) > tol)
                    more.push_back(half);
            }
            finer = !more.empty();
            u.insert(u.end(), more.begin(), more.end());
            std::sort(u.begin(), u.end());
        }
        return u;
    }

}
