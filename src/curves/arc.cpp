// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/arc.hpp"

namespace planex2d {

    // the center onto the chord's bisector, the nearest center both ends lie on. Beyond a half turn the far side
    // moves with the center, and near a full turn the short chord's bisector is ill-conditioned: there the center
    // stays where it would move farther than the ends did and they stay on its circle within rounding
    void Arc::Recenter(double moved, double rounding) {
        Vertex chord = end - start;
        double l = Norm(chord);
        if (l > 0.0) {
            Vertex m = (start + end) * 0.5;
            Vertex nb = Vertex{-chord.y, chord.x} / l;
            Vertex c = m + nb * Dot(center - m, nb);
            if (Param(end) <= std::numbers::pi || Norm(c - center) <= moved ||
                std::abs(Norm(end - center) - Norm(start - center)) > rounding)
                center = c;
        }
    }

    std::optional<Arc> Arc::Joined(const Arc &b, const Vertex &v) const {
        const double tol = Tol(std::max(LengthScale(), b.LengthScale()));
        if (fromSpline || b.fromSpline || Norm(center - b.center) > tol || std::abs(radius - b.radius) > tol)
            return std::nullopt;
        const Arc in = end == v ? *this : Reversed(), out = b.start == v ? b : b.Reversed();
        if (in.ccw != out.ccw || in.start == out.end)
            return std::nullopt;
        Arc r(in.start, out.end, center, in.ccw);
        r.props = props;
        r.ends = MakeEnds(PropsAtEnd(in.ends, false), PropsAtEnd(out.ends, true));
        return end == v ? r : r.Reversed();
    }

}
