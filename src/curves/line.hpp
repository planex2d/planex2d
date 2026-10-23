// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <optional>
#include <utility>


#include "core/config.hpp"
#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"

namespace planex2d {

    class Wire;

    class Line {
        private:
            Vertex start;
            Vertex end;

        public:
            Properties props;
            Ends ends;
            bool fromSpline = false;

            Line(const Vertex &start, const Vertex &end) : start(start), end(end) {
                IsFinite("Line", {start.x, start.y, end.x, end.y});
            }
            const Vertex &Start() const { return start; }
            const Vertex &End() const { return end; }
            BBox Box() const { return BBox::Of(start, end); }
            template <bool recomputeCenter = true>
            void Trim(const Vertex &a, const Vertex &b) {
                start = a;
                end = b;
            }
            void Rotate(const Vertex &c, double cosVal, double sinVal) {
                start = Rotated(start, c, cosVal, sinVal);
                end = Rotated(end, c, cosVal, sinVal);
            }
            void Translate(const Vertex &d) {
                start = start + d;
                end = end + d;
            }
            void Mirror(const Vertex &a, const Vertex &d) {
                start = Mirrored(start, a, d);
                end = Mirrored(end, a, d);
            }
            void Scale(const Vertex &c, double s) {
                start = Scaled(start, c, s);
                end = Scaled(end, c, s);
            }
            Line Reversed() const {
                Line r = *this;
                std::swap(r.start, r.end);
                r.ends = Swapped(ends);
                return r;
            }
            Vertex MidPoint() const { return (start + end) * 0.5; }
            double Length() const { return Norm(end - start); }
            Vertex NearestPoint(const Vertex &p) const {
                const Vertex r = end - start;
                return start + r * std::clamp(Dot(p - start, r) / Dot(r, r), 0.0, 1.0);
            }
            Vertex Foot(const Vertex &p) const {
                const Vertex r = end - start;
                return start + r * (Dot(p - start, r) / Dot(r, r));
            }
            double Distance(const Vertex &q) const {
                Vertex r = end - start;
                double t = std::clamp(Dot(q - start, r) / Dot(r, r), 0.0, 1.0);
                return Norm(q - (start + r * t));
            }
            // the term of a loop's area about o: o near the loop keeps large coordinates from cancelling
            double SignedArea(const Vertex &o) const { return 0.5 * Cross(start - o, end - o); }
            double LengthFraction(const Vertex &p) const {
                Vertex r = end - start;
                return std::clamp(Dot(p - start, r) / Dot(r, r), 0.0, 1.0);
            }
            Vertex PointAtFraction(double t) const { return start + (end - start) * t; }
            Vertex TangentAtFraction(double) const { return Normalized(end - start); }
            // b meets this line at v: one line over both if they lie on one line and do not fold back; pieces of spline
            // offsets stay apart
            std::optional<Line> Joined(const Line &b, const Vertex &v) const {
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
            double LengthScale() const {
                double a = NormInf(start);
                double b = NormInf(end);
                return a > b ? a : b;
            }
            double CarrierDistance(const Vertex &p) const {
                const Vertex r = end - start;
                return std::abs(Cross(p - start, r)) / Norm(r);
            }
            bool OnCarrier(const Vertex &p, double tol) const {
                Vertex r = end - start;
                double c = Cross(p - start, r);
                return c * c <= tol * tol * Dot(r, r);
            }
            Location Locate(const Vertex &p, double tol) const {
                if (p.IsClose(start, tol))
                    return Location::AtStart;
                if (p.IsClose(end, tol))
                    return Location::AtEnd;
                Vertex r = end - start;
                double s = Dot(p - start, r);
                return OnCarrier(p, tol) && s > 0.0 && s < Dot(r, r) ? Location::Inside : Location::Off;
            }
            bool IsInside(const Vertex &p) const {
                double s = LengthScale();
                double q = NormInf(p);
                return Locate(p, Tol(s > q ? s : q)) != Location::Off;
            }
            template <bool check = true>
            Vertex Tangent(const Vertex &p) const {
                if constexpr (check) {
                    if (!IsInside(p))
                        return {0.0, 0.0};
                }
                Vertex r = end - start;
                return r * (1.0 / Norm(r));
            }
            // position along the line, only for ordering: 0 at start, |end - start|^2 at end
            double Param(const Vertex &p) const { return Dot(p - start, end - start); }

            Wire Offset(double d) const;
    };

}
