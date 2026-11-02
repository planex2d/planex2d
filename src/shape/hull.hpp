// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "curves/edgelist.hpp"
#include "wire/wire.hpp"

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

    // the convex hull of edges, exact with arcs, as the support of a direction: points (every edge's ends) and arcs
    // (a spline by the lines and arcs of its approximation, a circle by its halves); an arc supports the outward
    // normals from its center to its points, the angles from `from` over `sweep`
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
            // the outward normal angles of the lines supporting both k and j, k first along the hull (counterclockwise);
            // the one where j's point of contact lies ahead of k's
            std::optional<double> Bridge(size_t k, size_t j) const {
                const Vertex ck = IsArc(k) ? ArcOf(k).center : points[k], cj = IsArc(j) ? ArcOf(j).center : points[j];
                const double rk = IsArc(k) ? ArcOf(k).radius : 0.0, rj = IsArc(j) ? ArcOf(j).radius : 0.0;
                const Vertex v = cj - ck;
                const double d = Norm(v);
                // an arc's ends lie on its circle to the rounding of its size (as the Arc constructor centers it)
                const double nearTol = 8.0 * Tol(NormInf(ck) + rk + NormInf(cj) + rj);
                // the supports are equal where the normal n has (cj - ck) . n = rk - rj; none for nested circles
                if (!(d > nearTol) || d < std::abs(rk - rj) - nearTol)
                    return std::nullopt;
                const bool touching = std::abs(d - std::abs(rk - rj)) <= nearTol;
                // touching: the two lines are one, along the line of the centers (acos is no good next to 1)
                const double base = std::atan2(v.y, v.x),
                             off = touching ? (rk > rj ? 0.0 : std::numbers::pi) : std::acos(std::clamp((rk - rj) / d, -1.0, 1.0));
                for (const double phi : {base - off, base + off}) {
                    if ((IsArc(k) && !ArcOf(k).Covers(phi)) || (IsArc(j) && !ArcOf(j).Covers(phi)))
                        continue;
                    // a point on an arc's circle: only an arc's last point, or a point where an arc starts, without a
                    // line between
                    if (touching) {
                        if ((IsArc(k) && !IsArc(j) && AngleFrom(phi, ArcOf(k).from) >= ArcOf(k).sweep - 1e-12) ||
                            (!IsArc(k) && IsArc(j) && AngleFrom(phi, ArcOf(j).from) <= 1e-12))
                            return phi;
                        continue;
                    }
                    const Vertex n = UnitVector(phi), along{-n.y, n.x};
                    if (Dot(TouchOf(j, phi) - TouchOf(k, phi), along) > tol)
                        return phi;
                }
                return std::nullopt;
            }
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
            // the hull's boundary, counterclockwise; raises when it has no area (nothing, a point, points on a line)
            Wire Boundary(const AxisSystem &axes) const {
                const size_t n = points.size() + arcs.size();
                const double down = -0.5 * std::numbers::pi;
                size_t start = 0;
                for (size_t k = 1; k < n; ++k) {
                    const double a = Support(k, down), b = Support(start, down);
                    if (a > b + tol || (a > b - tol && TouchOf(k, down).x < TouchOf(start, down).x))
                        start = k;
                }
                Wire w(axes);
                size_t k = start;
                double phi = down, turned = 0.0;
                for (size_t step = 0; step <= 4 * n && turned < 2.0 * std::numbers::pi - 1e-9; ++step) {
                    // the next element: the smallest turn of the normal, of equal turns the farthest along
                    size_t next = n;
                    double best = std::numeric_limits<double>::infinity(), farthest = 0.0, at = phi;
                    for (size_t j = 0; j < n; ++j) {
                        if (j == k)
                            continue;
                        const std::optional<double> b = Bridge(k, j);
                        if (!b)
                            continue;
                        const double turn = AngleFrom(*b, phi);
                        // the last step goes back to the start (not on along a line through it)
                        if (j != start && turned + turn > 2.0 * std::numbers::pi - 1e-9)
                            continue;
                        if (IsArc(k) && turn > AngleFrom(ArcOf(k).from + ArcOf(k).sweep, phi) + 1e-12)
                            continue;
                        const Vertex u{-std::sin(*b), std::cos(*b)};
                        const double reach = Dot(TouchOf(j, *b) - TouchOf(k, *b), u);
                        if (turn < best - 1e-12 || (turn < best + 1e-12 && reach > farthest)) {
                            best = turn;
                            farthest = reach;
                            next = j;
                            at = *b;
                        }
                    }
                    if (next == n)
                        break;
                    const Vertex from = TouchOf(k, phi), to = TouchOf(k, at), onto = TouchOf(next, at);
                    if (IsArc(k) && !from.IsClose(to, tol))
                        w.Add(Arc(from, to, ArcOf(k).center, true));
                    if (!to.IsClose(onto, tol))
                        w.Add(Line(to, onto));
                    turned += best;
                    phi = at;
                    k = next;
                    if (k == start && turned > std::numbers::pi)
                        break;
                }
                if (k == start && IsArc(k)) {
                    const Vertex from = TouchOf(k, phi), to = TouchOf(k, down);
                    if (!from.IsClose(to, tol))
                        w.Add(Arc(from, to, ArcOf(k).center, true));
                }
                double area = 0.0;
                for (const Edge &e : w)
                    area += ExactSignedArea(e, Vertex{});
                if (w.Size() < 2 || area <= tol * tol)
                    throw std::invalid_argument("the convex hull has no area: the geometry is a point or lies on a line");
                return w;
            }
    };

    // the farthest distance from q to a hull's boundary (lines and arcs): an end of each edge, or the point of an arc
    // straight across its center from q
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
    // the support of a hull's boundary along the direction at angle phi: the largest projection of its points
    inline double SupportOf(const Wire &hull, double phi) {
        const Vertex n = UnitVector(phi);
        double h = -std::numeric_limits<double>::infinity();
        for (const Edge &e : hull) {
            h = std::max({h, Dot(Wire::EndOf(e, false), n), Dot(Wire::EndOf(e, true), n)});
            if (const Arc *a = std::get_if<Arc>(&e))
                if (a->IsInSweep(a->Center() + n))
                    h = std::max(h, Dot(a->Center(), n) + a->Radius());
        }
        return h;
    }
    // the argument in [a, b] where f is least, f unimodal there (golden section)
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

    // the smallest circle around points (Welzl's, iteratively: a point outside the circle so far starts it again on
    // that point, in an order shuffled with a fixed seed so that it takes linear time on average); tol: the rounding
    // a point may lie outside
    inline std::pair<Vertex, double> CircleAround(std::vector<Vertex> p, double tol) {
        std::shuffle(p.begin(), p.end(), std::mt19937(1));
        const auto outside = [&](const Vertex &c, double r, const Vertex &q) { return Norm(q - c) > r + tol; };
        Vertex c = p[0];
        double r = 0.0;
        for (size_t i = 1; i < p.size(); ++i) {
            if (!outside(c, r, p[i]))
                continue;
            c = p[i];
            r = 0.0;
            for (size_t j = 0; j < i; ++j) {
                if (!outside(c, r, p[j]))
                    continue;
                c = (p[i] + p[j]) * 0.5;
                r = 0.5 * Norm(p[i] - p[j]);
                for (size_t k = 0; k < j; ++k)
                    if (outside(c, r, p[k])) {
                        // the circumcircle of p[i], p[j], p[k]
                        const Vertex a = p[j] - p[i], b = p[k] - p[i];
                        const double d = 2.0 * Cross(a, b);
                        const Vertex o{(b.y * Dot(a, a) - a.y * Dot(b, b)) / d, (a.x * Dot(b, b) - b.x * Dot(a, a)) / d};
                        c = p[i] + o;
                        r = Norm(o);
                    }
            }
        }
        return {c, r};
    }

    // the smallest circle around a convex hull's boundary: first where the farthest distance is least (a convex
    // function: least along v for each h, then along h), then exactly: the circle around the hull's vertices and the
    // points of its arcs farthest from that center, again from the new center until it stays; each center's radius
    // the farthest distance from it, so that the circle holds the arcs, the least of them
    inline std::pair<Vertex, double> EnclosingCircleOf(const Wire &hull) {
        const BBox box = hull.Box();
        const auto best = [&](double x) { return LeastAt(box.y0, box.y1, [&](double y) { return FarthestFrom(hull, {x, y}); }); };
        const double x = LeastAt(box.x0, box.x1, [&](double h) { return FarthestFrom(hull, {h, best(h)}); });
        Vertex c{x, best(x)}, at = c;
        double r = FarthestFrom(hull, c);
        for (int it = 0; it < 100; ++it) {
            std::vector<Vertex> p;
            for (const Edge &e : hull) {
                p.push_back(Wire::EndOf(e, false));
                if (const Arc *a = std::get_if<Arc>(&e))
                    if (const Vertex farthest = a->Center() + Normalized(a->Center() - at) * a->Radius(); a->IsInSweep(farthest))
                        p.push_back(farthest);
            }
            const Vertex q = CircleAround(p, hull.Tol()).first;
            if (q == at)
                break;
            at = q;
            if (const double reach = FarthestFrom(hull, q); reach <= r + hull.Tol()) {
                c = q;
                r = reach;
            }
        }
        return {c, r};
    }

    // the smallest-area rectangle around a convex hull's boundary: its center, the direction of its longer side
    // (radians, in (-pi/2, pi/2]) and its sides; between the directions where a side's support passes from one edge
    // to the next (a straight edge's direction, an arc's ends; there the area has a kink) the area is least by golden
    // section
    struct EnclosingRectangle {
        Vertex center;
        double angle, length, width;
    };
    inline EnclosingRectangle EnclosingRectangleOf(const Wire &hull) {
        const double quarter = 0.5 * std::numbers::pi;
        const auto extent = [&](double t) { return SupportOf(hull, t) + SupportOf(hull, t + std::numbers::pi); };
        const auto area = [&](double t) { return extent(t) * extent(t + quarter); };
        std::vector<double> breaks{0.0, quarter};
        const auto take = [&](const Vertex &d) { breaks.push_back(std::fmod(std::atan2(d.y, d.x) + 2.0 * std::numbers::pi, quarter)); };
        for (const Edge &e : hull)
            if (const Line *l = std::get_if<Line>(&e)) {
                take(l->End() - l->Start());
            } else if (const Arc *a = std::get_if<Arc>(&e)) {
                take(a->Start() - a->Center());
                take(a->End() - a->Center());
            }
        std::sort(breaks.begin(), breaks.end());
        double t = 0.0, least = area(0.0);
        for (size_t k = 0; k + 1 < breaks.size(); ++k) {
            // of equal areas (a disc: every angle) the first, from 0
            for (const double c : {breaks[k], LeastAt(breaks[k], breaks[k + 1], area)})
                if (const double a = area(c); a < least * (1.0 - 1e-12)) {
                    least = a;
                    t = c;
                }
        }
        const Vertex n1 = UnitVector(t), n2 = UnitVector(t + quarter);
        const double e1 = extent(t), e2 = extent(t + quarter);
        const Vertex c = n1 * (0.5 * (SupportOf(hull, t) - SupportOf(hull, t + std::numbers::pi))) +
                         n2 * (0.5 * (SupportOf(hull, t + quarter) - SupportOf(hull, t + 3.0 * quarter)));
        double angle = e1 >= e2 ? t : t + quarter;
        if (angle > quarter)
            angle -= std::numbers::pi;
        return {c, angle, std::max(e1, e2), std::min(e1, e2)};
    }

}
