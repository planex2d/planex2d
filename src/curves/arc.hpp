// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>


#include "core/config.hpp"
#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"

namespace planex2d {

    class Wire;

    class Arc {
        public:
            Properties props;
            Ends ends;
            bool fromSpline = false;

        private:
            bool ccw;
            Vertex start;
            Vertex end;
            Vertex center;
            double radius = 0.0;
            double sweep = 0.0;

            void Update() {
                radius = Norm(start - center);
                sweep = Param(end);
            }
            // the center onto the chord's bisector, the nearest center both ends lie on
            void Recenter() {
                Vertex chord = end - start;
                double l = Norm(chord);
                if (l > 0.0) {
                    Vertex m = (start + end) * 0.5;
                    Vertex nb = Vertex{-chord.y, chord.x} / l;
                    center = m + nb * Dot(center - m, nb);
                }
            }
            // v relative to the center lies on the arc: between a and b, or beyond pi not strictly between b and a
            bool Covers(const Vertex &v) const {
                Vertex a = start - center;
                Vertex b = end - center;
                double ca = ccw ? Cross(a, v) : Cross(v, a);
                double cb = ccw ? Cross(v, b) : Cross(b, v);
                return sweep <= std::numbers::pi ? ca >= 0.0 && cb >= 0.0 : ca >= 0.0 || cb >= 0.0;
            }

        public:
            // ends not equally far from the center beyond rounding (data from elsewhere, e.g. an OCC edge) move the
            // center onto the chord's bisector: else the arc's end is off its own circle, and so are its offsets' ends
            Arc(const Vertex &start, const Vertex &end, const Vertex &center, bool ccw)
                : ccw(ccw), start(start), end(end), center(center) {
                IsFinite("Arc", {start.x, start.y, end.x, end.y, center.x, center.y});
                const double rs = Norm(start - center), re = Norm(end - center);
                if (std::abs(re - rs) > Tol(NormInf(center) + std::max(rs, re)))
                    Recenter();
                Update();
            }
            const Vertex &Start() const { return start; }
            const Vertex &End() const { return end; }
            const Vertex &Center() const { return center; }
            double Radius() const { return radius; }
            bool CCW() const { return ccw; }
            // this arc and o together run once round one circle
            bool CompletesCircle(const Arc &o, double tol) const {
                return ccw == o.ccw && center.IsClose(o.center, tol) && std::abs(radius - o.radius) <= tol && end.IsClose(o.start, tol) &&
                       o.end.IsClose(start, tol);
            }
            BBox Box() const {
                BBox box = BBox::Of(start, end);
                for (const Vertex &v : {Vertex{radius, 0.0}, Vertex{-radius, 0.0}, Vertex{0.0, radius}, Vertex{0.0, -radius}})
                    if (Covers(v))
                        box.Grow(center + v);
                return box;
            }
            template <bool recomputeCenter = false>
            void Trim(const Vertex &a, const Vertex &b) {
                if constexpr (recomputeCenter) {
                    if (a == start && b == end)
                        return;
                    start = a;
                    end = b;
                    Recenter();
                }
                start = a;
                end = b;
                Update();
            }
            void Rotate(const Vertex &c, double cosVal, double sinVal) {
                start = Rotated(start, c, cosVal, sinVal);
                end = Rotated(end, c, cosVal, sinVal);
                center = Rotated(center, c, cosVal, sinVal);
                Update();
            }
            void Translate(const Vertex &d) {
                start = start + d;
                end = end + d;
                center = center + d;
                Update();
            }
            void Mirror(const Vertex &a, const Vertex &d) {
                start = Mirrored(start, a, d);
                end = Mirrored(end, a, d);
                center = Mirrored(center, a, d);
                ccw = !ccw;
                Update();
            }
            void Scale(const Vertex &c, double s) {
                start = Scaled(start, c, s);
                end = Scaled(end, c, s);
                center = Scaled(center, c, s);
                Update();
            }

            Arc Reversed() const {
                Arc r = *this;
                std::swap(r.start, r.end);
                r.ccw = !ccw;
                r.ends = Swapped(ends);
                r.Update();
                return r;
            }

            double Sweep() const { return sweep; }
            double Length() const { return radius * sweep; }
            std::array<Vertex, 2> RadialPoints(const Vertex &p) const {
                const Vertex u = Normalized(p - center) * radius;
                return {center + u, center - u};
            }
            Vertex NearestPoint(const Vertex &p) const {
                const Vertex q = p - center;
                if (const double l = Norm(q); l > 0.0 && IsInSweep(p))
                    return center + q * (radius / l);
                return Norm(p - start) <= Norm(p - end) ? start : end;
            }
            double Distance(const Vertex &q) const {
                if (IsInSweep(q))
                    return std::abs(Norm(q - center) - radius);
                return std::min(Norm(q - start), Norm(q - end));
            }
            double SignedArea(const Vertex &o) const {
                return 0.5 * (Cross(center - o, end - start) + radius * radius * (ccw ? sweep : -sweep));
            }
            double LengthFraction(const Vertex &p) const {
                Location at = Locate(p, Tol(std::max(LengthScale(), NormInf(p))));
                if (at == Location::AtStart)
                    return 0.0;
                if (at == Location::AtEnd)
                    return 1.0;
                return std::clamp(Param(p) / sweep, 0.0, 1.0);
            }
            Vertex PointAtFraction(double t) const { return PointAt(sweep * t); }
            Vertex TangentAtFraction(double t) const {
                Vertex r = PointAt(sweep * t) - center;
                return Normalized(ccw ? Vertex{-r.y, r.x} : Vertex{r.y, -r.x});
            }
            // b meets this arc at v: one arc over both if they lie on one circle in the same sense, never closing it
            std::optional<Arc> Joined(const Arc &b, const Vertex &v) const {
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
            // angle from start along the arc, in [0, 2pi)
            double Param(const Vertex &p) const {
                Vertex a = start - center;
                Vertex b = p - center;
                double c = Cross(a, b);
                double s = std::atan2(ccw ? c : -c, Dot(a, b));
                return s < 0.0 ? s + 2.0 * std::numbers::pi : s;
            }
            Vertex PointAt(double t) const { return Rotated(start, center, std::cos(t), ccw ? std::sin(t) : -std::sin(t)); }
            Vertex MidPoint() const { return PointAt(0.5 * sweep); }
            double LengthScale() const { return NormInf(center) + radius; }
            double CarrierDistance(const Vertex &p) const { return std::abs(Norm(p - center) - radius); }
            bool OnCarrier(const Vertex &p, double tol) const {
                Vertex r = p - center;
                double d2 = Dot(r, r);
                double hi = radius + tol;
                double lo = radius - tol;
                return d2 <= hi * hi && (lo <= 0.0 || d2 >= lo * lo);
            }
            Location Locate(const Vertex &p, double tol) const {
                if (p.IsClose(start, tol))
                    return Location::AtStart;
                if (p.IsClose(end, tol))
                    return Location::AtEnd;
                return OnCarrier(p, tol) && IsInSweep(p) ? Location::Inside : Location::Off;
            }
            bool IsInSweep(const Vertex &p) const { return Covers(p - center); }
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
                Vertex r = p - center;
                Vertex t = ccw ? Vertex{-r.y, r.x} : Vertex{r.y, -r.x};
                return t * (1.0 / Norm(t));
            }

            Wire Offset(double d) const;
    };

}
