// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "planex/curves/edgelist.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d {

    // the angle a - b in [0, 2 pi), a value within rounding below 2 pi as 0
    inline double AngleFrom(double a, double b) {
        const double t = 2.0 * std::numbers::pi;
        double d = std::fmod(a - b, t);
        if (d < 0.0)
            d += t;
        return d > t - 1e-12 ? 0.0 : d;
    }
    inline Vertex UnitVector(double angle) { return {std::cos(angle), std::sin(angle)}; }

    // exact with arcs (a spline by its approximation, a circle by its halves); an arc supports the outward normals from
    // its center to its points, the angles from `from` over `sweep`
    class ConvexHullOf {
        private:
            struct HullArc {
                Vertex center;
                double radius, from, sweep;

                bool Covers(double phi) const { return AngleFrom(phi, from) <= sweep + 1e-12; }
                Vertex At(double phi) const { return center + UnitVector(phi) * radius; }
            };
            std::vector<Vertex> points;
            std::vector<HullArc> arcs;
            double tol;

            // element k: a point below points.size(), else an arc
            bool IsArc(size_t k) const { return k >= points.size(); }
            const HullArc &ArcOf(size_t k) const { return arcs[k - points.size()]; }
            Vertex TouchOf(size_t k, double phi) const { return IsArc(k) ? ArcOf(k).At(phi) : points[k]; }
            void Take(const Line &l) {
                points.push_back(l.Start());
                points.push_back(l.End());
            }
            void Take(const Arc &a) {
                points.push_back(a.Start());
                points.push_back(a.End());
                const Vertex lowest = a.CCW() ? a.Start() : a.End();
                arcs.push_back({a.Center(), a.Radius(), std::atan2(lowest.y - a.Center().y, lowest.x - a.Center().x), a.Sweep()});
            }
            std::optional<double> Bridge(size_t k, size_t j) const;
            double Support(size_t k, double phi) const {
                if (!IsArc(k))
                    return Dot(points[k], UnitVector(phi));
                return ArcOf(k).Covers(phi) ? Dot(ArcOf(k).center, UnitVector(phi)) + ArcOf(k).radius
                                           : -std::numeric_limits<double>::infinity();
            }

        public:
            explicit ConvexHullOf(const EdgeList &edges) {
                for (const Edge &e : edges)
                    std::visit([&](const auto &x) { ForEachPart(x, [&](const auto &q) { Take(q); }); }, e);
                tol = edges.JoinTol(std::nullopt);
            }
            Wire Boundary(const AxisSystem &axes) const;
    };

    inline double FarthestFrom(const Wire &hull, const Vertex &q) {
        double d = 0.0;
        for (const Edge &e : hull) {
            d = std::max({d, Norm(Wire::EndOf(e, false) - q), Norm(Wire::EndOf(e, true) - q)});
            if (const Arc *a = std::get_if<Arc>(&e))
                if (a->IsInSweep(a->Center() * 2.0 - q))
                    d = std::max(d, Norm(a->Center() - q) + a->Radius());
        }
        return d;
    }
    double SupportOf(const Wire &hull, double phi);
    // f is unimodal on [a, b] (golden section)
    template <class F>
    double LeastAt(double a, double b, F f) {
        const double g = (std::sqrt(5.0) - 1.0) / 2.0, end = 1e-15 * std::max({std::abs(a), std::abs(b), b - a});
        double x = b - g * (b - a), y = a + g * (b - a), fx = f(x), fy = f(y);
        for (int it = 0; it < 120 && b - a > end; ++it)
            if (fx < fy) {
                b = y;
                y = x;
                fy = fx;
                x = b - g * (b - a);
                fx = f(x);
            } else {
                a = x;
                x = y;
                fx = fy;
                y = a + g * (b - a);
                fy = f(y);
            }
        return 0.5 * (a + b);
    }

    std::pair<Vertex, double> CircleAround(std::vector<Vertex> p, double tol);

    std::pair<Vertex, double> EnclosingCircleOf(const Wire &hull);

    // direction: of the longer side, in radians, in (-pi/2, pi/2]. The area has a kink where a side's support passes from
    // one edge to the next; between the kinks it is least by golden section
    struct EnclosingRectangle {
        Vertex center;
        double angle, length, width;

        Wire Boundary(const AxisSystem &axes) const;
    };
    EnclosingRectangle EnclosingRectangleOf(const Wire &hull);

}
