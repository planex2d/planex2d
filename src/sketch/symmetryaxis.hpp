// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/circlecarrier.hpp"
#include "curves/line.hpp"

namespace planex2d::sketch {

    using SymRef = std::variant<Vertex, Line, CircleCarrier>;

    inline Line Bisector(const Vertex &p, const Vertex &q, double half) {
        const Vertex m = (p + q) * 0.5, d = Normalized(q - p);
        const Vertex n{-d.y, d.x};
        return Line(m - n * half, m + n * half);
    }

    // along w through origin, long enough to cover a and b seen along it
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

    inline Line SymmetryAxis(const SymRef &a, const SymRef &b, bool reverse, const std::string &call) {
        if (const Vertex *p = std::get_if<Vertex>(&a), *q = std::get_if<Vertex>(&b); p && q) {
            if (*p == *q)
                throw std::invalid_argument(call + ": the two points coincide");
            return Bisector(*p, *q, 0.5 * Norm(*q - *p));
        }
        if (const CircleCarrier *p = std::get_if<CircleCarrier>(&a), *q = std::get_if<CircleCarrier>(&b); p && q) {
            const double tol = Tol(std::max(p->LengthScale(), q->LengthScale()));
            if (std::abs(p->radius - q->radius) > tol)
                throw std::invalid_argument(call + ": circles of different radii have no symmetry line");
            if (Norm(q->center - p->center) <= tol)
                throw std::invalid_argument(call + ": both lie on the same circle");
            return Bisector(p->center, q->center, p->radius);
        }
        const Line *la = std::get_if<Line>(&a), *lb = std::get_if<Line>(&b);
        if (!la || !lb)
            throw std::invalid_argument(call + ": a symmetry line lies between two points, two lines or two circles of the same radius");
        // the sine of the angle between the two directions
        if (std::abs(Cross(Normalized(la->End() - la->Start()), Normalized(lb->End() - lb->Start()))) > 1e-12)
            return AngleBisector(*la, *lb, reverse);
        if (reverse)
            Warning() << call << ": reverse found only one solution";
        return MidLine(*la, *lb);
    }

}
