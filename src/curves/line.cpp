// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/line.hpp"

namespace planex2d {

    std::optional<Line> Line::Joined(const Line &b, const Vertex &v) const {
        if (fromSpline || b.fromSpline)
            return std::nullopt;
        const Vertex p = start == v ? end : start, q = b.start == v ? b.end : b.start;
        const Vertex pq = q - p;
        const double l = Norm(pq);
        if (!(l > 0.0) || Dot(p - v, q - v) >= 0.0 || std::abs(Cross(pq, v - p)) > Tol(std::max(LengthScale(), b.LengthScale())) * l)
            return std::nullopt;
        Line r = end == v ? Line(p, q) : Line(q, p);
        r.props = props;
        if (ends || b.ends)
            r.ends = end == v ? MakeEnds(PropsAwayFrom(*this, v), PropsAwayFrom(b, v)) : MakeEnds(PropsAwayFrom(b, v), PropsAwayFrom(*this, v));
        return r;
    }

}
