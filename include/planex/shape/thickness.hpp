// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "planex/shape/measure.hpp"

namespace planex2d {

    // at: on the boundary, other: where the disc or ray ends, centre: of the disc (the ray's midpoint), in the middle
    // of the wall; walked: the disc of a corner or a bend, grown to the wall it leads to
    struct ThicknessSample {
        Vertex at, other, centre;
        double thickness;
        bool walked = false;
    };

    // a disc is a wall's where the points it touches lie more than 90 degrees apart around its center (two sides face
    // each other); a disc of a corner or a bend grows along the medial axis to the first disc that is a wall's. A convex
    // corner sharper than corner degrees holds a disc (a ray: as the disc of its half length around its middle) when
    // every point where it touches the boundary lies within 2.2 r / tan(angle / 2) of the corner; such a sample counts
    // as the corner's, not as a wall's
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
            // the inward normals of its edges at it, each away from the other edge
            struct ReflexCorner {
                Vertex at;
                size_t edges[2];
                Vertex normals[2];
            };
            std::vector<ReflexCorner> reflex;
            struct Contact {
                Vertex at;
                uint32_t edge;
            };
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
            struct Corner {
                Vertex at;
                size_t edges[2];
                Vertex leaving[2];
            };
            std::vector<Corner> Corners() const;
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
            std::vector<Contact> Contacts(const Vertex &c, double r, double within);
            static bool IsFacing(const Vertex &c, const std::vector<Contact> &touch) {
                for (size_t i = 0; i < touch.size(); ++i)
                    for (size_t k = i + 1; k < touch.size(); ++k) {
                        const Vertex a = touch[i].at - c, b = touch[k].at - c;
                        if (Dot(a, b) < -1e-9 * Norm(a) * Norm(b))
                            return true;
                    }
                return false;
            }
            std::pair<Vertex, double> Ridge(const Vertex &c, const Vertex &w, double h);
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
            std::optional<std::pair<Vertex, double>> FacingAlong(Vertex c, double r, std::vector<Contact> touch,
                                                                 std::vector<std::pair<uint32_t, uint32_t>> &passed);
            std::optional<std::pair<Vertex, double>> WallOf(const Vertex &c, double r,
                                                            const std::vector<Contact> &touch) {
                std::vector<std::pair<uint32_t, uint32_t>> passed;
                const std::optional<std::pair<Vertex, double>> wall = FacingAlong(c, r, touch, passed);
                if (wall)
                    for (const std::pair<uint32_t, uint32_t> &lines : passed)
                        walls[lines] = *wall;
                return wall;
            }
            void FindReflexCorners();
            std::optional<ThicknessSample> From(const Vertex &p, const Vertex &inward);
            std::pair<Nearest, size_t> Touching(const Vertex &c, double r, const Vertex &p);
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
            ThicknessProbe(const Wire &region, bool ball, double corner);
            std::optional<ThicknessSample> At(size_t i, double t) {
                return Along(i, PointAtFraction(region[i], t), TangentAtFraction(region[i], t));
            }
            std::optional<ThicknessSample> ExactAt(size_t i, double t);
            std::optional<ThicknessSample> Along(size_t i, const Vertex &p, const Vertex &tangent) {
                Vertex inward{-tangent.y, tangent.x};
                if (!region.Contains(p + inward * nudge))
                    inward = inward * -1.0;
                return From(p, inward);
            }
            // s: where the disc's center lies on the way from one edge's inward normal to the other's (0 and 1: the limits of the
            // edges' own discs)
            std::optional<ThicknessSample> AtCorner(size_t k, double s) {
                const ReflexCorner &c = reflex[k];
                return From(c.at, Normalized(c.normals[0] * (1.0 - s) + c.normals[1] * s));
            }
            double CornerDirectionTo(size_t k, const Vertex &c) const {
                const ReflexCorner &r = reflex[k];
                const double a = Cross(r.normals[0], c - r.at), b = Cross(r.normals[1], c - r.at);
                return a == b ? 0.5 : std::clamp(a / (a - b), 0.0, 1.0);
            }
            std::optional<size_t> ReflexCornerAt(const Vertex &p) const {
                for (size_t k = 0; k < reflex.size(); ++k)
                    if (reflex[k].at.IsClose(p, tol))
                        return k;
                return std::nullopt;
            }
            std::pair<size_t, double> EdgeAt(const Vertex &p);
            double Clearance(const Vertex &c);
            std::optional<std::pair<Vertex, Vertex>> Pole();
    };

    std::vector<ThicknessSample> Thickness(const Wire &region, size_t samples, bool ball, double corner, int refine = 0);

}
