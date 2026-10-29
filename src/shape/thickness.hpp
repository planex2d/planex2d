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
    // there (ball), or the distance along the inward normal to the boundary it meets (ray). A convex corner sharper
    // than corner degrees holds a disc (a ray: as the disc of its half length around its middle) in its wedge when
    // every point where it touches the boundary lies within 2.2 r / tan(angle / 2) of the corner; such a sample counts
    // as the corner's, not as a wall's
    class ThicknessProbe {
        private:
            const Wire &region;
            bool ball, exact = false;
            size_t n;
            double size, tol, nudge, slack;
            Grid grid;
            std::vector<uint32_t> nearby;
            // a convex corner sharper than corner degrees: its point and the distance from it, in disc radii, within
            // which a disc its wedge holds touches its sides: 2.2 / tan of half its angle. A disc in the wedge touches
            // at 1 / tan, a disc cut short at the corner's point (by the next edge, to rounding) at about 2 / tan; a
            // plate's wall disc touches the far side at least sqrt(5) / 2 = 2.24 radii from a right angle (a square)
            struct SharpCorner {
                Vertex at;
                double reach;
            };
            std::vector<SharpCorner> sharp;
            // a reflex corner: its point, the edges meeting there and their inward normals at it, each away from the
            // other edge
            struct ReflexCorner {
                Vertex at;
                size_t edges[2];
                Vertex normals[2];
            };
            std::vector<ReflexCorner> reflex;

            // the point of edge j nearest p: on its lines and arcs (a spline's approximation, where the samples lie),
            // or on the exact curves for the final sample
            Nearest Near(size_t j, const Vertex &p) const {
                if (exact)
                    return NearestOnEdge(region[j], p);
                Nearest m;
                std::visit([&](const auto &x) { ForEachPart(x, [&](const auto &q) { m.Take(q.NearestPoint(p), p); }); },
                           region[j]);
                return m;
            }
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
            void FindSharpCorners(double corner) {
                for (const Corner &c : Corners())
                    if (const double angle = InsideAngle(c); angle < corner)
                        sharp.push_back({c.at, 2.2 / std::tan(0.5 * angle * std::numbers::pi / 180.0)});
            }
            // a disc of radius r touching the boundary at the points touch sits in a sharp corner's wedge: all of them
            // within the corner's reach (a disc that also touches a wall farther away is that wall's)
            bool IsHeld(const std::vector<Vertex> &touch, double r) const {
                if (touch.size() < 2)
                    return false;
                for (const SharpCorner &s : sharp)
                    if (std::all_of(touch.begin(), touch.end(),
                                    [&](const Vertex &q) { return Norm(q - s.at) <= s.reach * r; }))
                        return true;
                return false;
            }
            // the points where the disc of radius r around c touches the boundary, on the pieces Near measures
            std::vector<Vertex> Contacts(const Vertex &c, double r) {
                nearby.clear();
                grid.InBox(BBox::Of(c, c).Grown(r + 2.0 * slack), nearby);
                std::vector<Vertex> out;
                for (uint32_t j : nearby)
                    if (const Nearest m = Near(j, c); m.d <= r + slack)
                        out.push_back(m.a);
                return out;
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
            // nothing where a sharp corner holds it, or where it finds no wall
            std::optional<ThicknessSample> From(const Vertex &p, const Vertex &inward) {
                const Line ray(p + inward * nudge, p + inward * (4.0 * size));
                double reach = 4.0 * size;
                size_t hit = n;
                Vertex where = p;
                nearby.clear();
                grid.InBox(ray.Box(), nearby);
                for (uint32_t j : nearby)
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
                    return !sharp.empty() && IsHeld({p, where}, 0.5 * reach)
                               ? std::nullopt
                               : std::optional(ThicknessSample{p, where, (p + where) * 0.5, reach});
                // the largest disc touching at p: between 0 and half the ray's reach, 60 halvings at most
                double lo = 0.0, hi = 0.5 * reach;
                for (int it = 0; it < 60 && hi - lo > 1e-14 * size; ++it) {
                    const double r = 0.5 * (lo + hi);
                    (IsDiscInside(p + inward * r, r) ? lo : hi) = r;
                }
                const auto [other, j] = Touching(p + inward * lo, lo, p);
                if (j == n || (!sharp.empty() && IsHeld(Contacts(p + inward * lo, lo), lo)))
                    return std::nullopt;
                return ThicknessSample{p, other.a, p + inward * lo, 2.0 * lo};
            }
            // the boundary point nearest c that is not near p, with its edge
            std::pair<Nearest, size_t> Touching(const Vertex &c, double r, const Vertex &p) {
                nearby.clear();
                grid.InBox(BBox::Of(c, c).Grown(r * (1.0 + 1e-6) + tol), nearby);
                std::pair<Nearest, size_t> best{Nearest{}, n};
                for (uint32_t j : nearby) {
                    const Nearest m = Near(j, c);
                    if (Norm(m.a - p) > 0.1 * r && m.d < best.first.d)
                        best = {m, j};
                }
                return best;
            }
            // inside to rounding: the edge it touches at its sample is r away up to that (a relative slack let the largest
            // disc grow past the boundary by about 1e-9 of its radius)
            bool IsDiscInside(const Vertex &c, double r) {
                nearby.clear();
                grid.InBox(BBox::Of(c, c).Grown(r), nearby);
                for (uint32_t j : nearby)
                    if (Near(j, c).d < r - tol)
                        return false;
                return true;
            }

        public:
            ThicknessProbe(const Wire &region, bool ball, double corner)
                : region(region), ball(ball), n(region.Size()), grid(region.EdgeGrid()) {
                const BBox box = region.Box();
                size = NormInf(box.Size());
                tol = Tol(std::max(box.NormInf(), size));
                nudge = 1e-9 * size;
                // 1e-9: a disc's radius from the halving in From is good to 1e-14 of the size; touching within this
                slack = 1e-9 * size;
                if (corner > 0.0)
                    FindSharpCorners(corner);
                if (ball)
                    FindReflexCorners();
            }
            // at the fraction t of edge i, nothing where a sharp corner holds the disc or ray, or it finds no wall
            std::optional<ThicknessSample> At(size_t i, double t) {
                return Along(i, PointAtFraction(region[i], t), TangentAtFraction(region[i], t));
            }
            // the sample at the fraction t of edge i on the exact curves: its point and normal on a spline's exact
            // curve, the disc against the exact curves (the samples follow the approximation, within spline_tol of
            // them)
            std::optional<ThicknessSample> ExactAt(size_t i, double t) {
                Vertex p = PointAtFraction(region[i], t), tangent = TangentAtFraction(region[i], t);
                if (const Spline *s = std::get_if<Spline>(&region[i])) {
                    tangent = s->ExactTangent(p);
                    p = s->ExactNearestPoint(p);
                }
                exact = true;
                const std::optional<ThicknessSample> out = Along(i, p, tangent);
                exact = false;
                return out;
            }
            // at p on edge i with the tangent there
            std::optional<ThicknessSample> Along(size_t i, const Vertex &p, const Vertex &tangent) {
                Vertex inward{-tangent.y, tangent.x};
                if (!region.Contains(p + inward * nudge))
                    inward = inward * -1.0;
                return From(p, inward);
            }
            // the largest disc touching reflex corner k, its center along the direction s of the way from one edge's
            // inward normal there to the other's (0 and 1: the limits of the edges' own discs)
            std::optional<ThicknessSample> AtCorner(size_t k, double s) {
                const ReflexCorner &c = reflex[k];
                return From(c.at, Normalized(c.normals[0] * (1.0 - s) + c.normals[1] * s));
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
                nearby.clear();
                grid.InBox(BBox::Of(p, p).Grown(tol), nearby);
                std::pair<size_t, double> best{0, 0.0};
                double d = std::numeric_limits<double>::infinity();
                for (uint32_t j : nearby)
                    if (const Nearest m = Near(j, p); m.d < d) {
                        d = m.d;
                        best = {j, LengthFraction(region[j], m.a)};
                    }
                return best;
            }
            // the distance from c to the boundary: the nearest edge within a box grown until one lies within it
            double Clearance(const Vertex &c) {
                double d = std::numeric_limits<double>::infinity();
                for (double r = 0.01 * size; d > r && r < 8.0 * size; r *= 4.0) {
                    nearby.clear();
                    grid.InBox(BBox::Of(c, c).Grown(r), nearby);
                    for (uint32_t j : nearby)
                        d = std::min(d, Near(j, c).d);
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
                nearby.clear();
                grid.InBox(BBox::Of(best.c, best.c).Grown(best.d + tol), nearby);
                Nearest touch;
                for (uint32_t j : nearby)
                    if (const Nearest m = Near(j, best.c); m.d < touch.d)
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
        const bool refined = s && better(s->thickness, out[best].thickness);
        ThicknessSample result = refined ? *s : out[best];
        // the result on the exact curves where the region has a spline; a sample at a reflex corner stays, its point is
        // the vertex, which the approximation sets
        const bool spline =
            std::any_of(region.begin(), region.end(), [](const Edge &e) { return std::holds_alternative<Spline>(e); });
        if (spline && !(refined && atCorner))
            if (const std::optional<ThicknessSample> x =
                    refined ? probe.ExactAt(edge, 0.5 * (a + b)) : probe.ExactAt(bestEdge, bestT))
                result = *x;
        out.push_back(result);
        return out;
    }

}
