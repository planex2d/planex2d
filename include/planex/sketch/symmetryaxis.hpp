// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <variant>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/circlecarrier.hpp"
#include "planex/curves/line.hpp"

namespace planex2d::sketch {

    using SymRef = std::variant<Vertex, Line, CircleCarrier>;

    inline Line Bisector(const Vertex &p, const Vertex &q, double half) {
        const Vertex m = (p + q) * 0.5, d = Normalized(q - p);
        const Vertex n{-d.y, d.x};
        return Line(m - n * half, m + n * half);
    }

    inline Line Covering(const Line &a, const Line &b, const Vertex &origin, const Vertex &w) {
        double t0 = std::numeric_limits<double>::infinity(), t1 = -t0;
        for (const Vertex &p : {a.Start(), a.End(), b.Start(), b.End()}) {
            t0 = std::min(t0, Dot(p - origin, w));
            t1 = std::max(t1, Dot(p - origin, w));
        }
        return Line(origin + w * t0, origin + w * t1);
    }

    inline Line MidLine(const Line &a, const Line &b) {
        return Covering(a, b, (a.Start() + b.Foot(a.Start())) * 0.5, Normalized(a.End() - a.Start()));
    }

    // of the two bisectors the one that maps a's midpoint closest to b's (the other one with reverse)
    inline Line AngleBisector(const Line &a, const Line &b, bool reverse) {
        const Vertex u = Normalized(a.End() - a.Start()), v = Normalized(b.End() - b.Start());
        const Vertex x = a.Start() + u * (Cross(b.Start() - a.Start(), v) / Cross(u, v));
        std::array<Vertex, 2> w{Normalized(u + v), Normalized(u - v)};
        const auto mismatch = [&](const Vertex &d) { return Norm(Mirrored(a.MidPoint(), x, d) - b.MidPoint()); };
        if (mismatch(w[1]) < mismatch(w[0]))
            std::swap(w[0], w[1]);
        return Covering(a, b, x, w[reverse ? 1 : 0]);
    }

    Line SymmetryAxis(const SymRef &a, const SymRef &b, bool reverse, const std::string &call);

}
