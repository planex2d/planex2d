// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <tuple>
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
            // a reflex corner: its point, the edges meeting there and their inward normals at it, each away from the
            // other edge
            struct ReflexCorner {
                Vertex at;
                size_t edges[2];
                Vertex normals[2];
            };
            std::vector<ReflexCorner> reflex;

            bool IsPinched(size_t i, size_t j) const { return std::find(pinch[i].begin(), pinch[i].end(), j) != pinch[i].end(); }
            // where two edges meet: the point, the edges and their directions leaving it
            struct Corner {
                Vertex at;
                size_t edges[2];
                Vertex leaving[2];
            };
            std::vector<Corner> Corners() const {
                std::map<std::pair<double, double>, std::vector<std::pair<size_t, bool>>> at;
                for (size_t i = 0; i < n; ++i) {
                    const Vertex s = Wire::EndOf(region[i], false), e = Wire::EndOf(region[i], true);
                    at[{s.x, s.y}].push_back({i, true});
                    at[{e.x, e.y}].push_back({i, false});
                }
                std::vector<Corner> out;
                for (const auto &[p, meeting] : at) {
                    if (meeting.size() != 2)
                        continue;
                    Corner c{Vertex{p.first, p.second}, {meeting[0].first, meeting[1].first}, {}};
                    for (size_t k = 0; k < 2; ++k) {
                        const auto [i, start] = meeting[k];
                        const Vertex t = TangentAtFraction(region[i], start ? 0.0 : 1.0);
                        c.leaving[k] = start ? t : t * -1.0;
                    }
                    out.push_back(c);
                }
                return out;
            }
            // the angle inside the region between the edges
            double InsideAngle(const Corner &c) const {
                const double angle = Degrees(std::acos(std::clamp(Dot(c.leaving[0], c.leaving[1]), -1.0, 1.0)));
                const Vertex bisector = c.leaving[0] + c.leaving[1];
                if (Norm(bisector) > 1e-12 && !region.Contains(c.at + Normalized(bisector) * nudge))
                    return 360.0 - angle;
                return angle;
            }
            void FindPinchingCorners(double corner) {
                for (const Corner &c : Corners())
                    if (InsideAngle(c) < corner) {
                        pinch[c.edges[0]].push_back(c.edges[1]);
                        pinch[c.edges[1]].push_back(c.edges[0]);
                    }
            }
            // a reflex corner's inward normals at it, each away from the other edge: the disc touching there leans
            // anywhere between them
            void FindReflexCorners() {
                for (const Corner &c : Corners()) {
                    if (InsideAngle(c) <= 180.0 + 1e-9)
                        continue;
                    ReflexCorner r{c.at, {c.edges[0], c.edges[1]}, {}};
                    for (size_t k = 0; k < 2; ++k) {
                        const Vertex normal{-c.leaving[k].y, c.leaving[k].x};
                        r.normals[k] = Dot(normal, c.leaving[1 - k]) < 0.0 ? normal : normal * -1.0;
                    }
                    reflex.push_back(r);
                }
            }
            // from p along the inward direction: the ray to the wall, or the largest disc inside that touches at p;
            // nothing where it is pinched at a corner of the edges a or b there, or finds no wall
            std::optional<ThicknessSample> From(const Vertex &p, const Vertex &inward, size_t a, size_t b) {
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
                    return IsPinched(a, hit) || IsPinched(b, hit)
                               ? std::nullopt
                               : std::optional(ThicknessSample{p, where, (p + where) * 0.5, reach});
                // the largest disc touching at p: between 0 and half the ray's reach, 60 halvings at most
                double lo = 0.0, hi = 0.5 * reach;
                for (int it = 0; it < 60 && hi - lo > 1e-14 * size; ++it) {
                    const double r = 0.5 * (lo + hi);
                    (IsDiscInside(p + inward * r, r) ? lo : hi) = r;
                }
                const auto [other, j] = Touching(p + inward * lo, lo, p);
                if (j == n || IsPinched(a, j) || IsPinched(b, j))
                    return std::nullopt;
                return ThicknessSample{p, other.a, p + inward * lo, 2.0 * lo};
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
            // inside to rounding: the edge it touches at its sample is r away up to that (a relative slack let the largest
            // disc grow past the boundary by about 1e-9 of its radius)
            bool IsDiscInside(const Vertex &c, double r) {
                near.clear();
                grid.InBox(BBox::Of(c, c).Grown(r), near);
                for (uint32_t j : near)
                    if (NearestOnEdge(region[j], c).d < r - tol)
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
                if (ball)
                    FindReflexCorners();
            }
            // at the fraction t of edge i, nothing where the disc or ray is pinched or finds no wall
            std::optional<ThicknessSample> At(size_t i, double t) {
                const Vertex p = PointAtFraction(region[i], t), tangent = TangentAtFraction(region[i], t);
                Vertex inward{-tangent.y, tangent.x};
                if (!region.Contains(p + inward * nudge))
                    inward = inward * -1.0;
                return From(p, inward, i, i);
            }
            // the largest disc touching reflex corner k, its center along the direction s of the way from one edge's
            // inward normal there to the other's (0 and 1: the limits of the edges' own discs)
            std::optional<ThicknessSample> AtCorner(size_t k, double s) {
                const ReflexCorner &c = reflex[k];
                return From(c.at, Normalized(c.normals[0] * (1.0 - s) + c.normals[1] * s), c.edges[0], c.edges[1]);
            }
            // the direction s at reflex corner k (as AtCorner takes it) toward c
            double CornerDirectionTo(size_t k, const Vertex &c) const {
                const ReflexCorner &r = reflex[k];
                const double a = Cross(r.normals[0], c - r.at), b = Cross(r.normals[1], c - r.at);
                return a == b ? 0.5 : std::clamp(a / (a - b), 0.0, 1.0);
            }
            // the reflex corner at p, if any
            std::optional<size_t> ReflexCornerAt(const Vertex &p) const {
                for (size_t k = 0; k < reflex.size(); ++k)
                    if (reflex[k].at.IsClose(p, tol))
                        return k;
                return std::nullopt;
            }
            // the edge nearest p and the fraction of its length where it comes nearest
            std::pair<size_t, double> EdgeAt(const Vertex &p) {
                near.clear();
                grid.InBox(BBox::Of(p, p).Grown(tol), near);
                std::pair<size_t, double> best{0, 0.0};
                double d = std::numeric_limits<double>::infinity();
                for (uint32_t j : near)
                    if (const Nearest m = NearestOnEdge(region[j], p); m.d < d) {
                        d = m.d;
                        best = {j, LengthFraction(region[j], m.a)};
                    }
                return best;
            }
            // the distance from c to the boundary: the nearest edge within a box grown until one lies within it
            double Clearance(const Vertex &c) {
                double d = std::numeric_limits<double>::infinity();
                for (double r = 0.01 * size; d > r && r < 8.0 * size; r *= 4.0) {
                    near.clear();
                    grid.InBox(BBox::Of(c, c).Grown(r), near);
                    for (uint32_t j : near)
                        d = std::min(d, NearestOnEdge(region[j], c).d);
                }
                return d;
            }
            // the largest disc inside, up to 1e-3 of the size: its center, the point inside farthest from the boundary
            // (found as polylabel finds the pole of inaccessibility: cells over the box, split while a point in one
            // could lie farther than the best found by more than that), and where it touches the boundary; nothing for
            // a region without inside
            std::optional<std::pair<Vertex, Vertex>> Pole() {
                struct Cell {
                    Vertex c;
                    double h, d;
                    double Bound() const { return d + h * std::numbers::sqrt2; }
                };
                const auto cell = [&](const Vertex &c, double h) {
                    return Cell{c, h, region.Contains(c) ? Clearance(c) : -Clearance(c)};
                };
                const auto lower = [](const Cell &a, const Cell &b) { return a.Bound() < b.Bound(); };
                std::priority_queue<Cell, std::vector<Cell>, decltype(lower)> cells(lower);
                const BBox box = region.Box();
                const Vertex extent = box.Size();
                const double side = std::min(extent.x, extent.y);
                if (!(side > 0.0))
                    return std::nullopt;
                for (double x = box.x0; x < box.x1; x += side)
                    for (double y = box.y0; y < box.y1; y += side)
                        cells.push(cell({x + 0.5 * side, y + 0.5 * side}, 0.5 * side));
                Cell best = cell(box.Center(), 0.0);
                while (!cells.empty()) {
                    const Cell c = cells.top();
                    cells.pop();
                    if (c.d > best.d)
                        best = c;
                    if (c.Bound() - best.d <= 1e-3 * size)
                        continue;
                    const double h = 0.5 * c.h;
                    for (const Vertex &o : {Vertex{-h, -h}, Vertex{h, -h}, Vertex{-h, h}, Vertex{h, h}})
                        cells.push(cell(c.c + o, h));
                }
                if (!(best.d > 0.0))
                    return std::nullopt;
                near.clear();
                grid.InBox(BBox::Of(best.c, best.c).Grown(best.d + tol), near);
                Nearest touch;
                for (uint32_t j : near)
                    if (const Nearest m = NearestOnEdge(region[j], best.c); m.d < touch.d)
                        touch = m;
                return std::pair{best.c, touch.a};
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
        auto count = [&](size_t i) {
            return std::max<size_t>(1, (size_t)std::lround((double)samples * ExactLengthOf(region[i]) / total));
        };
        size_t best = 0, bestEdge = 0;
        double bestT = 0.0;
        auto better = [&](double a, double b) { return refine < 0 ? a < b : a > b; };
        for (size_t i = 0; i < region.Size(); ++i)
            for (size_t k = 0, n = count(i); k < n; ++k) {
                const double t = ((double)k + 0.5) / (double)n;
                const std::optional<ThicknessSample> s = probe.At(i, t);
                if (!s)
                    continue;
                if (refine != 0 && (out.empty() || better(s->thickness, out[best].thickness))) {
                    best = out.size();
                    bestEdge = i;
                    bestT = t;
                }
                out.push_back(*s);
            }
        if (refine == 0 || out.empty())
            return out;
        // golden section within a sample spacing either side of the best sample, on its edge
        std::optional<size_t> atCorner;
        size_t edge = bestEdge;
        double a = std::max(0.0, bestT - 1.0 / (double)count(edge));
        double b = std::min(1.0, bestT + 1.0 / (double)count(edge));
        // the thickest: where the largest disc inside touches, which a sample can miss (a disc that touches reflex
        // corners only leans anywhere between their inward normals, which no sample inside an edge reaches); refined
        // across the directions at such a corner around the one toward the disc's center (its size over all of them
        // peaks sharply there, and higher again toward the edges), else along its edge
        if (ball && refine > 0)
            if (const std::optional<std::pair<Vertex, Vertex>> pole = probe.Pole()) {
                const auto &[centre, touch] = *pole;
                atCorner = probe.ReflexCornerAt(touch);
                if (atCorner) {
                    const double toward = probe.CornerDirectionTo(*atCorner, centre);
                    a = std::max(0.0, toward - 0.05);
                    b = std::min(1.0, toward + 0.05);
                } else {
                    double t = 0.0;
                    std::tie(edge, t) = probe.EdgeAt(touch);
                    a = std::max(0.0, t - 1.0 / (double)count(edge));
                    b = std::min(1.0, t + 1.0 / (double)count(edge));
                }
            }
        auto sample = [&](double t) { return atCorner ? probe.AtCorner(*atCorner, t) : probe.At(edge, t); };
        const double g = (std::sqrt(5.0) - 1.0) / 2.0;
        auto value = [&](double t) {
            const std::optional<ThicknessSample> s = sample(t);
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
        const std::optional<ThicknessSample> s = sample(0.5 * (a + b));
        out.push_back(s && better(s->thickness, out[best].thickness) ? *s : out[best]);
        return out;
    }

}
