// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "shape/measure.hpp"

namespace planex2d {

    // at: on the boundary, other: where the disc or ray ends, centre: of the disc (the ray's midpoint), in the middle
    // of the wall
    struct ThicknessSample {
        Vertex at, other, centre;
        double thickness;
    };

    // the wall thickness at a point of a region's boundary: the diameter of the largest disc inside it that touches
    // there (ball), or the distance along the inward normal to the boundary it meets (ray). A disc or ray that ends on
    // an edge meeting the sample's edge at a convex corner sharper than corner degrees is pinched by that corner
    class ThicknessProbe {
        private:
            const Wire &region;
            bool ball;
            size_t n;
            double size, tol, nudge;
            Grid grid;
            std::vector<uint32_t> near;
            std::vector<std::vector<size_t>> pinch;

            bool IsPinched(size_t i, size_t j) const { return std::find(pinch[i].begin(), pinch[i].end(), j) != pinch[i].end(); }
            void FindPinchingCorners(double corner) {
                std::map<std::pair<double, double>, std::vector<std::pair<size_t, bool>>> at;
                for (size_t i = 0; i < n; ++i) {
                    const Vertex s = Wire::EndOf(region[i], false), e = Wire::EndOf(region[i], true);
                    at[{s.x, s.y}].push_back({i, true});
                    at[{e.x, e.y}].push_back({i, false});
                }
                for (const auto &[p, meeting] : at) {
                    if (meeting.size() != 2)
                        continue;
                    Vertex d[2];
                    for (size_t k = 0; k < 2; ++k) {
                        const auto [i, start] = meeting[k];
                        const Vertex t = TangentAtFraction(region[i], start ? 0.0 : 1.0);
                        d[k] = start ? t : t * -1.0;
                    }
                    double angle = Degrees(std::acos(std::clamp(Dot(d[0], d[1]), -1.0, 1.0)));
                    const Vertex bisector = d[0] + d[1];
                    if (Norm(bisector) > 1e-12 && !region.Contains(Vertex{p.first, p.second} + Normalized(bisector) * nudge))
                        angle = 360.0 - angle;
                    if (angle < corner) {
                        pinch[meeting[0].first].push_back(meeting[1].first);
                        pinch[meeting[1].first].push_back(meeting[0].first);
                    }
                }
            }
            // the boundary point nearest c that is not near p, with its edge
            std::pair<Nearest, size_t> Touching(const Vertex &c, double r, const Vertex &p) {
                near.clear();
                grid.InBox(BBox::Of(c, c).Grown(r * (1.0 + 1e-6) + tol), near);
                std::pair<Nearest, size_t> best{Nearest{}, n};
                for (uint32_t j : near) {
                    const Nearest m = NearestOnEdge(region[j], c);
                    if (Norm(m.a - p) > 0.1 * r && m.d < best.first.d)
                        best = {m, j};
                }
                return best;
            }
            bool IsDiscInside(const Vertex &c, double r) {
                near.clear();
                grid.InBox(BBox::Of(c, c).Grown(r), near);
                for (uint32_t j : near)
                    if (NearestOnEdge(region[j], c).d < r * (1.0 - 1e-9) - tol)
                        return false;
                return true;
            }

        public:
            ThicknessProbe(const Wire &region, bool ball, double corner)
                : region(region), ball(ball), n(region.Size()), grid(region.EdgeGrid()), pinch(region.Size()) {
                const BBox box = region.Box();
                size = NormInf(box.Size());
                tol = Tol(std::max(box.NormInf(), size));
                nudge = 1e-9 * size;
                if (corner > 0.0)
                    FindPinchingCorners(corner);
            }
            // at the fraction t of edge i, nothing where the disc or ray is pinched or finds no wall
            std::optional<ThicknessSample> At(size_t i, double t) {
                const Vertex p = PointAtFraction(region[i], t), tangent = TangentAtFraction(region[i], t);
                Vertex inward{-tangent.y, tangent.x};
                if (!region.Contains(p + inward * nudge))
                    inward = inward * -1.0;
                const Line ray(p + inward * nudge, p + inward * (4.0 * size));
                double reach = 4.0 * size;
                size_t hit = n;
                Vertex where = p;
                near.clear();
                grid.InBox(ray.Box(), near);
                for (uint32_t j : near)
                    std::visit([&](const auto &x) {
                        ForEachPart(x, [&](const auto &q) {
                            for (const Vertex &c : CommonPoints(ray, q, tol))
                                if (const double s = Dot(c - p, inward); s > 10.0 * tol && s < reach) {
                                    reach = s;
                                    hit = j;
                                    where = c;
                                }
                        });
                    }, region[j]);
                if (hit == n)
                    return std::nullopt;
                if (!ball)
                    return IsPinched(i, hit) ? std::nullopt : std::optional(ThicknessSample{p, where, (p + where) * 0.5, reach});
                // the largest disc touching at p: between 0 and half the ray's reach, 60 halvings at most
                double lo = 0.0, hi = 0.5 * reach;
                for (int it = 0; it < 60 && hi - lo > 1e-14 * size; ++it) {
                    const double r = 0.5 * (lo + hi);
                    (IsDiscInside(p + inward * r, r) ? lo : hi) = r;
                }
                const auto [other, j] = Touching(p + inward * lo, lo, p);
                if (j == n || IsPinched(i, j))
                    return std::nullopt;
                return ThicknessSample{p, other.a, p + inward * lo, 2.0 * lo};
            }
    };

    // samples spread along the boundary by length; refine: -1 the thinnest sample, 1 the thickest, refined along its
    // edge by golden section (appended last)
    inline std::vector<ThicknessSample> Thickness(const Wire &region, size_t samples, bool ball, double corner, int refine = 0) {
        std::vector<ThicknessSample> out;
        if (region.Empty())
            return out;
        ThicknessProbe probe(region, ball, corner);
        const double total = region.Length();
        size_t best = 0, bestEdge = 0;
        double bestT = 0.0, step = 0.0;
        auto better = [&](double a, double b) { return refine < 0 ? a < b : a > b; };
        for (size_t i = 0; i < region.Size(); ++i) {
            const size_t count = std::max<size_t>(1, (size_t)std::lround((double)samples * LengthOf(region[i]) / total));
            for (size_t k = 0; k < count; ++k) {
                const double t = ((double)k + 0.5) / (double)count;
                const std::optional<ThicknessSample> s = probe.At(i, t);
                if (!s)
                    continue;
                if (refine != 0 && (out.empty() || better(s->thickness, out[best].thickness))) {
                    best = out.size();
                    bestEdge = i;
                    bestT = t;
                    step = 1.0 / (double)count;
                }
                out.push_back(*s);
            }
        }
        if (refine == 0 || out.empty())
            return out;
        // golden section within a sample spacing either side, on the same edge
        const double g = (std::sqrt(5.0) - 1.0) / 2.0;
        double a = std::max(0.0, bestT - step), b = std::min(1.0, bestT + step);
        auto value = [&](double t) {
            const std::optional<ThicknessSample> s = probe.At(bestEdge, t);
            const double v = s ? s->thickness : out[best].thickness;
            return refine < 0 ? v : -v;
        };
        for (int it = 0; it < 80 && b - a > 1e-15; ++it) {
            const double c = b - g * (b - a), d = a + g * (b - a);
            if (value(c) < value(d))
                b = d;
            else
                a = c;
        }
        const std::optional<ThicknessSample> s = probe.At(bestEdge, 0.5 * (a + b));
        out.push_back(s && better(s->thickness, out[best].thickness) ? *s : out[best]);
        return out;
    }

}
