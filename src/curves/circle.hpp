// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "curves/arc.hpp"

namespace planex2d {

    class Wire;

    // a full turn as one edge: no vertex, its start and end are the parameter origin, a plain point; the core cuts
    // only lines and arcs: where something meets the circle it is cut into arcs (Arcs), Halves() is the circle as
    // sampling code and the export take it
    class Circle {
        public:
            Properties props;
            Ends ends;

        friend class PlanexWriter;
        friend class PlanexReader;

        private:
            bool ccw;
            Vertex center;
            Vertex origin;
            double radius = 0.0;

            void Update() { radius = Norm(origin - center); }

        public:
            Circle(const Vertex &center, const Vertex &origin, bool ccw) : ccw(ccw), center(center), origin(origin) {
                IsFinite("Circle", {center.x, center.y, origin.x, origin.y});
                Update();
            }
            Circle(const Vertex &center, double radius, bool ccw = true) : Circle(center, center + Vertex{radius, 0.0}, ccw) {}
            const Vertex &Start() const { return origin; }
            const Vertex &End() const { return origin; }
            const Vertex &Origin() const { return origin; }
            const Vertex &Center() const { return center; }
            double Radius() const { return radius; }
            bool CCW() const { return ccw; }
            BBox Box() const { return BBox::Of(center - Vertex{radius, radius}, center + Vertex{radius, radius}); }
            void Rotate(const Vertex &c, double cosVal, double sinVal) {
                origin = Rotated(origin, c, cosVal, sinVal);
                center = Rotated(center, c, cosVal, sinVal);
                Update();
            }
            void Translate(const Vertex &d) {
                origin = origin + d;
                center = center + d;
                Update();
            }
            void Mirror(const Vertex &a, const Vertex &d) {
                origin = Mirrored(origin, a, d);
                center = Mirrored(center, a, d);
                ccw = !ccw;
                Update();
            }
            void Scale(const Vertex &c, double s) {
                origin = Scaled(origin, c, s);
                center = Scaled(center, c, s);
                Update();
            }
            Circle Reversed() const {
                Circle r = *this;
                r.ccw = !ccw;
                return r;
            }

            Vertex Opposite() const { return center * 2.0 - origin; }
            std::array<Arc, 2> Halves() const {
                std::array<Arc, 2> h{Arc(origin, Opposite(), center, ccw), Arc(Opposite(), origin, center, ccw)};
                h[0].props = props;
                h[1].props = props;
                return h;
            }

            // the arcs between the points on the circle, along it: one point gives the two halves from it
            std::vector<Arc> Arcs(std::vector<Vertex> at, double tol) const {
                std::sort(at.begin(), at.end(), [&](const Vertex &p, const Vertex &q) { return Param(p) < Param(q); });
                at.erase(std::unique(at.begin(), at.end(), [&](const Vertex &p, const Vertex &q) { return p.IsClose(q, tol); }), at.end());
                if (at.size() > 1 && at.front().IsClose(at.back(), tol))
                    at.pop_back();
                if (at.size() == 1)
                    at.push_back(center * 2.0 - at.front());
                std::vector<Arc> arcs;
                for (size_t k = 0; k < at.size(); ++k) {
                    arcs.emplace_back(at[k], at[(k + 1) % at.size()], center, ccw);
                    arcs.back().props = props;
                }
                return arcs;
            }
            bool IsCongruent(const Circle &o, double tol) const {
                return center.IsClose(o.center, tol) && std::abs(radius - o.radius) <= tol;
            }

            double Length() const { return 2.0 * std::numbers::pi * radius; }
            Vertex NearestPoint(const Vertex &p) const {
                const Vertex q = p - center;
                const double l = Norm(q);
                return l > 0.0 ? center + q * (radius / l) : origin;
            }
            double Distance(const Vertex &q) const { return CarrierDistance(q); }
            double SignedArea(const Vertex &) const { return (ccw ? std::numbers::pi : -std::numbers::pi) * radius * radius; }
            double LengthFraction(const Vertex &p) const { return std::clamp(Param(p) / (2.0 * std::numbers::pi), 0.0, 1.0); }
            Vertex PointAtFraction(double t) const { return PointAt(2.0 * std::numbers::pi * t); }
            Vertex TangentAtFraction(double t) const { return Tangent<false>(PointAtFraction(t)); }
            // angle from the origin along the circle, in [0, 2pi)
            double Param(const Vertex &p) const {
                Vertex a = origin - center;
                Vertex b = p - center;
                double c = Cross(a, b);
                double s = std::atan2(ccw ? c : -c, Dot(a, b));
                return s < 0.0 ? s + 2.0 * std::numbers::pi : s;
            }
            Vertex PointAt(double t) const { return Rotated(origin, center, std::cos(t), ccw ? std::sin(t) : -std::sin(t)); }
            Vertex MidPoint() const { return Opposite(); }
            double LengthScale() const { return NormInf(center) + radius; }
            double CarrierDistance(const Vertex &p) const { return std::abs(Norm(p - center) - radius); }
            bool OnCarrier(const Vertex &p, double tol) const {
                Vertex r = p - center;
                double d2 = Dot(r, r);
                double hi = radius + tol;
                double lo = radius - tol;
                return d2 <= hi * hi && (lo <= 0.0 || d2 >= lo * lo);
            }
            Location Locate(const Vertex &p, double tol) const { return OnCarrier(p, tol) ? Location::Inside : Location::Off; }
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
