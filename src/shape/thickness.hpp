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
#include <type_traits>
#include <utility>
#include <vector>

#include "shape/measure.hpp"

namespace planex2d {

    // at: on the boundary, other: where the disc or ray ends, centre: of the disc (the ray's midpoint), in the middle
    // of the wall; walked: the disc of a corner or a bend, grown to the wall it leads to
    struct ThicknessSample {
        Vertex at, other, centre;
        double thickness;
        bool walked = false;
    };

    // the wall thickness at a point of a region's boundary: the diameter of the largest disc inside it that touches
    // there (ball), or the distance along the inward normal to the boundary it meets (ray). A disc is a wall's where
    // the points it touches lie more than 90 degrees apart around its center (two sides face each other); a disc of a
    // corner or a bend, whose touching points lie closer together, grows along the medial axis to the first disc that
    // is a wall's, and that is its thickness. A convex corner sharper than corner degrees holds a disc (a ray: as the
    // disc of its half length around its middle) in its wedge when every point where it touches the boundary lies
    // within 2.2 r / tan(angle / 2) of the corner; such a sample counts as the corner's, not as a wall's
    class ThicknessProbe {
        private:
            const Wire &region;
            bool ball, exact = false;
            size_t n;
            double size, tol, nudge, slack, ridgeSlack;
            // the farthest a spline's approximation lies from its exact curve (0 without splines)
            double deviation = 0.0;
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
            // a point where a disc touches the boundary, and the edge there
            struct Contact {
                Vertex at;
                uint32_t edge;
            };
            // the wall a disc between two lines leads to, by the lines: every disc between them grows along the same
            // ridge
            std::map<std::pair<uint32_t, uint32_t>, std::pair<Vertex, double>> walls;

            // the point of edge j nearest p: on its lines and arcs (a spline's approximation, where the samples lie),
            // or on the exact curves for the final sample
            Nearest Near(size_t j, const Vertex &p) const {
                return exact ? NearestOnEdge(region[j], p) : Approximated(j, p);
            }
            Nearest Approximated(size_t j, const Vertex &p) const {
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
            static std::vector<Vertex> PointsOf(const std::vector<Contact> &touch) {
                std::vector<Vertex> out;
                for (const Contact &q : touch)
                    out.push_back(q.at);
                return out;
            }
            // where the disc of radius r around c touches a line or an arc of edge j, to within: an arc around c along
            // all of it, as its ends and its middle (more than 90 degrees apart exactly when it sweeps more)
            template <class P>
            void Touch(const P &part, const Vertex &c, double r, double within, uint32_t j,
                       std::vector<Contact> &out) const {
                if constexpr (std::is_same_v<P, Arc>)
                    if (part.Center().IsClose(c, within) && std::abs(part.Radius() - r) <= within) {
                        for (const Vertex &q : {part.Start(), part.MidPoint(), part.End()})
                            out.push_back({q, j});
                        return;
                    }
                if (const Vertex q = part.NearestPoint(c); Norm(q - c) <= r + within)
                    out.push_back({q, j});
            }
            // the points where the disc of radius r around c touches the boundary to within, on the pieces Near
            // measures
            std::vector<Contact> Contacts(const Vertex &c, double r, double within) {
                nearby.clear();
                grid.InBox(BBox::Of(c, c).Grown(r + 2.0 * within), nearby);
                std::vector<Contact> out;
                for (uint32_t j : nearby) {
                    if (exact && std::holds_alternative<Spline>(region[j])) {
                        if (const Nearest m = Near(j, c); m.d <= r + within)
                            out.push_back({m.a, j});
                        continue;
                    }
                    std::visit(
                        [&](const auto &x) { ForEachPart(x, [&](const auto &q) { Touch(q, c, r, within, j, out); }); },
                        region[j]);
                }
                return out;
            }
            // two of the touching points lie more than 90 degrees apart around c: sides that face each other, a wall
            static bool IsFacing(const Vertex &c, const std::vector<Contact> &touch) {
                for (size_t i = 0; i < touch.size(); ++i)
                    for (size_t k = i + 1; k < touch.size(); ++k) {
                        const Vertex a = touch[i].at - c, b = touch[k].at - c;
                        if (Dot(a, b) < -1e-9 * Norm(a) * Norm(b))
                            return true;
                    }
                return false;
            }
            // the point of the ridge of the distance to the boundary (the medial axis) on the segment c - h w to
            // c + h w, where that distance is largest (golden section), and the distance
            std::pair<Vertex, double> Ridge(const Vertex &c, const Vertex &w, double h) {
                const double g = (std::sqrt(5.0) - 1.0) / 2.0;
                double a = -h, b = h;
                double x = b - g * (b - a), y = a + g * (b - a);
                double fx = Clearance(c + w * x), fy = Clearance(c + w * y);
                for (int it = 0; it < 60 && b - a > 1e-14 * size; ++it)
                    if (fx > fy) {
                        b = y;
                        y = x;
                        fy = fx;
                        x = b - g * (b - a);
                        fx = Clearance(c + w * x);
                    } else {
                        a = x;
                        x = y;
                        fx = fy;
                        y = a + g * (b - a);
                        fy = Clearance(c + w * y);
                    }
                const Vertex m = c + w * (0.5 * (a + b));
                return {m, Clearance(m)};
            }
            // the two lines a disc touches, when it touches two lines only: every disc between them grows along the
            // same ridge, to the same wall (on the approximations: a wall found there may touch a spline's
            // approximation only)
            std::optional<std::pair<uint32_t, uint32_t>> LinesOf(const std::vector<Contact> &touch) const {
                if (exact || touch.size() != 2 || touch[0].edge == touch[1].edge ||
                    !std::holds_alternative<Line>(region[touch[0].edge]) ||
                    !std::holds_alternative<Line>(region[touch[1].edge]))
                    return std::nullopt;
                return std::minmax(touch[0].edge, touch[1].edge);
            }
            // from the disc (c, r) whose touching points (touch) do not face each other: along the ridge it lies on,
            // growing, away from its two touching points farthest apart, the first disc whose touching points do, or
            // the disc where the ridge grows no more (the largest around, touching all round); the steps stay within a
            // quarter of the radius, so the ridge's search stays inside. It ends at a pair of lines whose wall is
            // known, and collects the pairs it passes in passed
            std::optional<std::pair<Vertex, double>> FacingAlong(Vertex c, double r, std::vector<Contact> touch,
                                                                 std::vector<std::pair<uint32_t, uint32_t>> &passed) {
                double h = 0.05 * r + 1e-6 * size;
                for (int step = 0; step < 400 && !touch.empty(); ++step) {
                    if (const std::optional<std::pair<uint32_t, uint32_t>> lines = LinesOf(touch)) {
                        if (const auto known = walls.find(*lines); known != walls.end())
                            return known->second;
                        if (passed.empty() || passed.back() != *lines)
                            passed.push_back(*lines);
                    }
                    Vertex grow = c - touch[0].at;
                    double widest = 2.0;
                    for (size_t i = 0; i < touch.size(); ++i)
                        for (size_t k = i + 1; k < touch.size(); ++k) {
                            const Vertex a = Normalized(c - touch[i].at), b = Normalized(c - touch[k].at);
                            if (Dot(a, b) < widest) {
                                widest = Dot(a, b);
                                grow = a + b;
                            }
                        }
                    grow = Normalized(grow);
                    const Vertex across{-grow.y, grow.x};
                    h = std::min(h, 0.25 * r);
                    const auto [next, grown] = Ridge(c + grow * h, across, 2.0 * h);
                    if (grown <= r) {
                        h *= 0.5;
                        if (h < 1e-12 * size)
                            return std::pair{c, r};
                        continue;
                    }
                    std::vector<Contact> reached = Contacts(next, grown, ridgeSlack);
                    if (IsFacing(next, reached)) {
                        double lo = 0.0, hi = h;
                        std::pair<Vertex, double> first{next, grown};
                        for (int it = 0; it < 30; ++it) {
                            const double m = 0.5 * (lo + hi);
                            const auto [cm, rm] = Ridge(c + grow * m, across, 2.0 * m);
                            if (IsFacing(cm, Contacts(cm, rm, ridgeSlack))) {
                                hi = m;
                                first = {cm, rm};
                            } else
                                lo = m;
                        }
                        return first;
                    }
                    c = next;
                    r = grown;
                    touch = std::move(reached);
                    h *= 1.5;
                }
                return std::nullopt;
            }
            // the wall the disc (c, r) of a corner or a bend leads to, and the wall of every pair of lines its walk
            // passes
            std::optional<std::pair<Vertex, double>> WallOf(const Vertex &c, double r,
                                                            const std::vector<Contact> &touch) {
                std::vector<std::pair<uint32_t, uint32_t>> passed;
                const std::optional<std::pair<Vertex, double>> wall = FacingAlong(c, r, touch, passed);
                if (wall)
                    for (const std::pair<uint32_t, uint32_t> &lines : passed)
                        walls[lines] = *wall;
                return wall;
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
                const Vertex c = p + inward * lo;
                const std::vector<Contact> touch = Contacts(c, lo, slack);
                if (IsFacing(c, touch)) {
                    const auto [other, j] = Touching(c, lo, p);
                    if (j == n || (!sharp.empty() && IsHeld(PointsOf(touch), lo)))
                        return std::nullopt;
                    return ThicknessSample{p, other.a, c, 2.0 * lo};
                }
                // a corner's or a bend's disc: the wall it leads to, seen from p
                const std::optional<std::pair<Vertex, double>> wall = WallOf(c, lo, touch);
                if (!wall)
                    return std::nullopt;
                const auto &[centre, r] = *wall;
                const std::vector<Contact> reached = Contacts(centre, r, ridgeSlack);
                if (!sharp.empty() && IsHeld(PointsOf(reached), r))
                    return std::nullopt;
                Vertex farthest = reached.front().at;
                for (const Contact &q : reached)
                    if (Norm(q.at - p) > Norm(farthest - p))
                        farthest = q.at;
                return ThicknessSample{p, farthest, centre, 2.0 * r, true};
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
                // 1e-11: a point of a ridge from its golden section is good to about 1e-13 of the size; the first disc
                // along it that faces comes before the wall by this
                ridgeSlack = 1e-11 * size;
                for (const Edge &e : region)
                    if (const Spline *s = std::get_if<Spline>(&e))
                        deviation = std::max(deviation, s->ApproximationTol());
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
            // the distance from c to the boundary: the nearest edge within a box grown until one lies within it (the
            // grid's cells reach past the box, so an edge they return farther than the box is not yet the nearest), on
            // the approximations; on the exact curves only the edges whose approximation comes that near, to twice its
            // deviation
            double Clearance(const Vertex &c) {
                double d = std::numeric_limits<double>::infinity();
                for (double r = 0.01 * size; r < 8.0 * size; r *= 4.0) {
                    nearby.clear();
                    grid.InBox(BBox::Of(c, c).Grown(r), nearby);
                    for (uint32_t j : nearby)
                        d = std::min(d, Approximated(j, c).d);
                    if (d <= r)
                        break;
                }
                if (!exact)
                    return d;
                const double within = d + 2.0 * deviation;
                nearby.clear();
                grid.InBox(BBox::Of(c, c).Grown(within), nearby);
                double e = std::numeric_limits<double>::infinity();
                for (uint32_t j : nearby)
                    if (Approximated(j, c).d <= within)
                        e = std::min(e, Near(j, c).d);
                return e;
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

    // samples spread along the boundary by length; refine: -1 the thinnest, its candidates refined along their edges,
    // 1 the thickest, refined along its edge, by golden section (appended last)
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
        // per sample: its edge and the fraction of it
        std::vector<std::pair<size_t, double>> at;
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
                at.push_back({i, t});
            }
        if (refine == 0 || out.empty())
            return out;
        const bool spline =
            std::any_of(region.begin(), region.end(), [](const Edge &e) { return std::holds_alternative<Spline>(e); });
        const double g = (std::sqrt(5.0) - 1.0) / 2.0;
        // the thinnest: every local minimum within 1.5 times the thinnest sample refined along its edge, so that a thin
        // wall between samples is found (a sample walked to its wall is its wall already)
        if (refine < 0) {
            std::vector<size_t> candidates;
            for (size_t j = 0; j < out.size(); ++j) {
                const double v = out[j].thickness, margin = 1e-9 * v;
                const bool dip = (j == 0 || v < out[j - 1].thickness - margin) && (j + 1 == out.size() || v < out[j + 1].thickness - margin);
                if (!out[j].walked && v <= 1.5 * out[best].thickness && (j == best || dip))
                    candidates.push_back(j);
            }
            // golden section on edge e from the bracket [a, b] for steps, the sample j's value where none is found
            const auto narrowed = [&](size_t j, size_t e, double &a, double &b, int steps) {
                const auto value = [&](double u) {
                    const std::optional<ThicknessSample> x = probe.At(e, u);
                    return x ? x->thickness : out[j].thickness;
                };
                for (int it = 0; it < steps && b - a > 1e-15; ++it) {
                    const double c = b - g * (b - a), d = a + g * (b - a);
                    if (value(c) < value(d))
                        b = d;
                    else
                        a = c;
                }
            };
            ThicknessSample result = out[best];
            size_t edge = bestEdge, winner = best;
            double where = bestT, lo = 0.0, hi = 0.0;
            // 20 steps take the bracket of two sample spacings to 1e-4 of one; the thinnest found then goes on to rounding
            // (it may lie at a knife edge's tip, at the end of its edge)
            for (size_t j : candidates) {
                const auto [e, t] = at[j];
                double a = std::max(0.0, t - 1.0 / (double)count(e)), b = std::min(1.0, t + 1.0 / (double)count(e));
                narrowed(j, e, a, b, 20);
                if (const std::optional<ThicknessSample> x = probe.At(e, 0.5 * (a + b)); x && x->thickness < result.thickness) {
                    result = *x;
                    edge = e;
                    where = 0.5 * (a + b);
                    winner = j;
                    lo = a;
                    hi = b;
                }
            }
            if (hi > lo) {
                narrowed(winner, edge, lo, hi, 60);
                if (const std::optional<ThicknessSample> x = probe.At(edge, 0.5 * (lo + hi)); x && x->thickness <= result.thickness) {
                    result = *x;
                    where = 0.5 * (lo + hi);
                }
            }
            // the result on the exact curves where the region has a spline
            if (spline)
                if (const std::optional<ThicknessSample> x = probe.ExactAt(edge, where))
                    result = *x;
            out.push_back(result);
            return out;
        }
        // the thickest: golden section within a sample spacing either side of the best sample, on its edge
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
        auto value = [&](double t) {
            const std::optional<ThicknessSample> s = sample(t);
            return -(s ? s->thickness : out[best].thickness);
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
        if (spline && !(refined && atCorner))
            if (const std::optional<ThicknessSample> x =
                    refined ? probe.ExactAt(edge, 0.5 * (a + b)) : probe.ExactAt(bestEdge, bestT))
                result = *x;
        out.push_back(result);
        return out;
    }

}
