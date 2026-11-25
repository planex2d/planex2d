// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/corner.hpp"

#include <limits>

namespace planex2d {

    std::optional<Vertex> Arm::Refined(const Vertex &p, const Locus &locus) const {
        const ExactCurve curve(edge);
        double u = curve.ParamOf(p);
        auto value = [&](double t) { return locus.Value(curve.At(t)); };
        for (int k = 0; k < 50; ++k) {
            if (std::abs(value(u)) <= Tolerance())
                return IsInterior(curve.At(u)) ? std::optional<Vertex>(curve.At(u)) : std::nullopt;
            const double slope = curve.Slope(value, u);
            if (slope == 0.0)
                return std::nullopt;
            u = curve.Clamped(u - value(u) / slope);
        }
        return std::nullopt;
    }

    std::vector<Vertex> Arm::Reaches(const Locus &locus) const {
        std::vector<Vertex> found;
        for (const Piece &piece : PiecesOf(edge))
            for (const Vertex &p : Meet(locus, Locus::Of(piece.e)))
                if (IsOn(piece.e, p))
                    found.push_back(p);
        if (IsSpline()) {
            std::vector<Vertex> exact;
            for (const Vertex &p : found)
                if (const std::optional<Vertex> q = Refined(p, locus))
                    exact.push_back(*q);
            found = std::move(exact);
        }
        std::sort(found.begin(), found.end(), [&](const Vertex &p, const Vertex &q) { return Along(p) < Along(q); });
        return found;
    }

    Arm Arm::Shortened(const Vertex &p) const {
        Edge e = edge;
        if (const Spline *s = std::get_if<Spline>(&edge)) {
            const size_t k = s->NearestPieceIndex(p);
            // p lies on the exact curve, up to the approximation's distance from the piece
            const std::vector<Spline> pieces = s->SplitAt({{k, p}}, s->Approximation()[k].Distance(p) * (1.0 + 1e-9));
            e = atEnd ? pieces.front() : pieces.back();
        } else {
            Wire::MoveEnd(e, atEnd, p);
        }
        const Ends &old = EndsOf(edge);
        SetEnds(e, atEnd ? MakeEnds(PropsAtEnd(old, false), nullptr) : MakeEnds(nullptr, PropsAtEnd(old, true)));
        RemapPartition(e, edge);
        return {std::move(e), atEnd};
    }

    std::optional<CornerCut> ExactRounding(const Arm &a, const Arm &b, double r, double side, const CornerCut &seed) {
        const ExactCurve ca(a.edge), cb(b.edge);
        const double sa = a.atEnd ? -side : side, sb = b.atEnd ? side : -side;
        auto offsetA = [&](double u) { return ca.Offset(u, sa, r); };
        auto offsetB = [&](double v) { return cb.Offset(v, sb, r); };
        double u = ca.ParamOf(seed.first), v = cb.ParamOf(seed.second);
        for (int k = 0; k < 50; ++k) {
            const Vertex f = offsetA(u) - offsetB(v);
            if (NormInf(f) <= std::max(a.Tolerance(), b.Tolerance())) {
                const Vertex ta = ca.At(u), tb = cb.At(v);
                if (!a.IsInterior(ta) || !b.IsInterior(tb))
                    return std::nullopt;
                return CornerCut{ta, tb, offsetA(u)};
            }
            const Vertex du = ca.Slope(offsetA, u), dv = cb.Slope(offsetB, v) * -1.0;
            const double det = Cross(du, dv);
            if (det == 0.0)
                return std::nullopt;
            u = ca.Clamped(u + Cross(f * -1.0, dv) / det);
            v = cb.Clamped(v + Cross(du, f * -1.0) / det);
        }
        return std::nullopt;
    }

    std::optional<CornerCut> RoundingOf(const Arm &a, const Arm &b, double r) {
        const double side = a.SideToward(b);
        std::optional<CornerCut> best;
        double nearest = std::numeric_limits<double>::infinity();
        ForEachCenter(PiecesOf(a.edge), PiecesOf(b.edge), r, [&](const Piece &pa, const Piece &pb, const Locus &, const Locus &, const Vertex &x) {
            const std::optional<Vertex> ta = a.TouchedBy(pa.e, x, r, side), tb = b.TouchedBy(pb.e, x, r, -side);
            if (!ta || !tb)
                return;
            const double reach = Norm(*ta - a.Corner()) + Norm(*tb - b.Corner());
            if (reach < nearest) {
                nearest = reach;
                best = CornerCut{*ta, *tb, x};
            }
        });
        if (best && (a.IsSpline() || b.IsSpline()))
            return ExactRounding(a, b, r, side, *best);
        return best;
    }

    // angle in degrees; the cut points are at the straight distance from the corner
    std::optional<CornerCut> ChamferingOf(const Arm &a, const Arm &b, double d, double d2, const std::optional<double> &angle) {
        const std::vector<Vertex> on = a.Reaches(Locus::Circle(a.Corner(), d));
        if (on.empty())
            return std::nullopt;
        const Vertex first = on.front();
        if (!angle) {
            const std::vector<Vertex> reach = b.Reaches(Locus::Circle(b.Corner(), d2));
            if (reach.empty())
                return std::nullopt;
            return CornerCut{first, reach.front(), std::nullopt};
        }
        const Vertex dir = a.ChamferDirection(first, b, *angle);
        for (const Vertex &p : b.Reaches(Locus::Through(first, dir)))
            if (Dot(p - first, dir) > 0.0)
                return CornerCut{first, p, std::nullopt};
        return std::nullopt;
    }

}
