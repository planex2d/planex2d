// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

#include "boolean/cutloop.hpp"
#include "core/vertex.hpp"
#include "curves/circletouch.hpp"
#include "curves/curve.hpp"
#include "curves/edge.hpp"
#include "wire/loops.hpp"
#include "wire/wire.hpp"

namespace planex2d {

    // an edge as its exact curve with a parameter: the point there and the unit direction along the edge
    class ExactCurve {
        private:
            const Edge &edge;
            Curve<double, Vertex> curve;

        public:
            explicit ExactCurve(const Edge &e) : edge(e) {
                if (const Spline *s = std::get_if<Spline>(&e))
                    curve = s->Segment();
            }
            double First() const { return std::holds_alternative<Spline>(edge) ? curve.First() : 0.0; }
            double Last() const {
                if (const Arc *a = std::get_if<Arc>(&edge))
                    return a->Sweep();
                return std::holds_alternative<Line>(edge) ? 1.0 : curve.Last();
            }
            Vertex At(double u) const {
                if (const Line *l = std::get_if<Line>(&edge))
                    return l->PointAtFraction(u);
                if (const Arc *a = std::get_if<Arc>(&edge))
                    return a->PointAt(u);
                return curve.At(u);
            }
            Vertex Direction(double u) const {
                if (const Line *l = std::get_if<Line>(&edge))
                    return Normalized(l->End() - l->Start());
                if (const Arc *a = std::get_if<Arc>(&edge))
                    return a->Tangent<false>(a->PointAt(u));
                return curve.Direction(u);
            }
            // signed along the edge's direction, a left turn positive
            double Curvature(double u) const {
                if (std::holds_alternative<Line>(edge))
                    return 0.0;
                if (const Arc *a = std::get_if<Arc>(&edge))
                    return (a->CCW() ? 1.0 : -1.0) / a->Radius();
                const Vertex d1 = curve.At(u, 1), d2 = curve.At(u, 2);
                const double speed = Norm(d1);
                return Cross(d1, d2) / (speed * speed * speed);
            }
            double ParamOf(const Vertex &p) const {
                if (const Line *l = std::get_if<Line>(&edge))
                    return l->LengthFraction(p);
                if (const Arc *a = std::get_if<Arc>(&edge))
                    return a->Param(p);
                return curve.Project(p, First(), Last());
            }
            // left of the direction by side (+1) or right of it (-1) times distance
            Vertex Offset(double u, double side, double distance) const {
                const Vertex t = Direction(u);
                return At(u) + Vertex{-t.y, t.x} * (side * distance);
            }
            // d f / d u by a difference over 2e-6 of the parameter range (about the square root of the rounding of
            // the curve's evaluation), cut off at the ends of the range
            template <class F>
            auto Slope(F f, double u) const {
                const double h = 1e-6 * (Last() - First());
                const double lo = std::max(First(), u - h), hi = std::min(Last(), u + h);
                return (f(hi) - f(lo)) / (hi - lo);
            }
            double Clamped(double u) const { return std::clamp(u, First(), Last()); }
    };

    // where a corner is cut: the cut point on each edge, and for a fillet the center of its arc
    struct CornerCut {
        Vertex first, second;
        std::optional<Vertex> center;

        // the arc between the cut points runs counterclockwise
        bool IsCounterclockwise() const { return Cross(first - *center, second - *center) > 0.0; }
        // the new edge between the cut points
        Edge Joining() const {
            if (!center)
                return Line(first, second);
            return Arc(first, second, *center, IsCounterclockwise());
        }
    };

    // an edge at a corner, seen from the corner outwards
    struct Arm {
        Edge edge;
        bool atEnd;

        Vertex Corner() const { return atEnd ? EndOf(edge) : StartOf(edge); }
        Vertex Far() const { return atEnd ? StartOf(edge) : EndOf(edge); }
        // unit, away from the corner
        Vertex Direction() const { return Leaving(edge, !atEnd).dir; }
        double Tolerance() const { return Tol(LengthScaleOf(edge)); }
        // +1 if o leaves to the left of this arm, else -1
        double SideToward(const Arm &o) const { return Cross(Direction(), o.Direction()) > 0.0 ? 1.0 : -1.0; }
        // directions closer than 1e-9 rad count as equal, as in BuildLoops
        bool IsTangentTo(const Arm &o) const { return std::abs(Cross(Direction(), o.Direction())) <= 1e-9; }
        // 0 at the corner, 1 at the far end
        double Along(const Vertex &p) const {
            const double f = LengthFraction(edge, p);
            return atEnd ? 1.0 - f : f;
        }
        bool IsSpline() const { return std::holds_alternative<Spline>(edge); }
        // past the corner, up to the far end: a cut there uses the edge up
        bool IsInterior(const Vertex &p) const { return !p.IsClose(Corner(), Tolerance()); }
        bool IsUsedUpBy(const Vertex &p) const { return p.IsClose(Far(), Tolerance()); }
        bool IsOn(const PrimitiveEdge &piece, const Vertex &p) const {
            return piece.Locate(p, Tolerance()) != Location::Off && IsInterior(p);
        }
        Vertex TangentAt(const Vertex &p) const {
            const ExactCurve curve(edge);
            return curve.Direction(curve.ParamOf(p)) * (atEnd ? -1.0 : 1.0);
        }
        // signed along TangentAt, away from the corner
        double CurvatureAt(const Vertex &p) const {
            const ExactCurve curve(edge);
            return curve.Curvature(curve.ParamOf(p)) * (atEnd ? -1.0 : 1.0);
        }
        // where the circle around x of radius r touches the piece of this arm, with its center on the given side
        std::optional<Vertex> TouchedBy(const PrimitiveEdge &piece, const Vertex &x, double r, double side) const {
            const std::optional<Vertex> t = TouchPoint(piece, x, r);
            if (!t || !IsOn(piece, *t))
                return std::nullopt;
            const Vertex away = piece.Tangent(*t) * (atEnd ? -1.0 : 1.0);
            return Cross(away, x - *t) * side > 0.0 ? t : std::nullopt;
        }
        // a spline: the point near p on the locus, on the exact curve
        std::optional<Vertex> Refined(const Vertex &p, const Locus &locus) const {
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
        // the points of the arm on the locus, from the corner outwards
        std::vector<Vertex> Reaches(const Locus &locus) const {
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
        // the direction of a chamfer line from p on this arm, at the angle (degrees) to the arm towards the corner,
        // turned to the side where o leaves
        Vertex ChamferDirection(const Vertex &p, const Arm &o, double angle) const {
            const Vertex t = TangentAt(p);
            const double rad = Radians(angle);
            return t * -std::cos(rad) + Vertex{-t.y, t.x} * (SideToward(o) * std::sin(rad));
        }
        // the edge with its corner end moved to p, the vertex properties there gone, a partition remapped
        Arm Shortened(const Vertex &p) const {
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
    };

    // the circle of radius r tangent to both arms on the sides facing each other, the one nearest to the corner; its
    // touch points lie inside the arms. Lines and arcs exact, a spline refined on its exact curve
    inline std::optional<CornerCut> ExactRounding(const Arm &a, const Arm &b, double r, double side, const CornerCut &seed) {
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

    inline std::optional<CornerCut> RoundingOf(const Arm &a, const Arm &b, double r) {
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

    // d on a, then d2 on b, or the line at the angle (degrees) to a that d is cut off with; the cut points are at the
    // straight distance from the corner
    inline std::optional<CornerCut> ChamferingOf(const Arm &a, const Arm &b, double d, double d2, const std::optional<double> &angle) {
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

    // the largest size up to wanted for which fits holds, by halving; 0 if none does
    template <class Fits>
    double LargestFit(Fits fits, double wanted) {
        double lo = 0.0, hi = wanted;
        for (int k = 0; k < 60; ++k) {
            const double mid = 0.5 * (lo + hi);
            (fits(mid) ? lo : hi) = mid;
        }
        return lo;
    }

}
