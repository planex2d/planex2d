// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "planex/boolean/cutloop.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/circletouch.hpp"
#include "planex/curves/curve.hpp"
#include "planex/curves/edge.hpp"
#include "planex/wire/loops.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d {

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
                return curve.Curvature(u);
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

    struct CornerCut {
        Vertex first, second;
        std::optional<Vertex> center;

        bool IsCounterclockwise() const { return Cross(first - *center, second - *center) > 0.0; }
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
        std::optional<Vertex> TouchedBy(const PrimitiveEdge &piece, const Vertex &x, double r, double side) const {
            const std::optional<Vertex> t = TouchPoint(piece, x, r);
            if (!t || !IsOn(piece, *t))
                return std::nullopt;
            const Vertex away = piece.Tangent(*t) * (atEnd ? -1.0 : 1.0);
            return Cross(away, x - *t) * side > 0.0 ? t : std::nullopt;
        }
        std::optional<Vertex> Refined(const Vertex &p, const Locus &locus) const;
        std::vector<Vertex> Reaches(const Locus &locus) const;
        // angle in degrees to the arm towards the corner, on the side where o leaves
        Vertex ChamferDirection(const Vertex &p, const Arm &o, double angle) const {
            const Vertex t = TangentAt(p);
            const double rad = Radians(angle);
            return t * -std::cos(rad) + Vertex{-t.y, t.x} * (SideToward(o) * std::sin(rad));
        }
        Arm Shortened(const Vertex &p) const;
    };

    std::optional<CornerCut> ExactRounding(const Arm &a, const Arm &b, double r, double side, const CornerCut &seed);

    std::optional<CornerCut> RoundingOf(const Arm &a, const Arm &b, double r);

    std::optional<CornerCut> ChamferingOf(const Arm &a, const Arm &b, double d, double d2, const std::optional<double> &angle);

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
