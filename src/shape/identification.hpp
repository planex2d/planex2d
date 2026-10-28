// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "curves/edge.hpp"

namespace planex2d {

    // x -> m x + t in the plane, m row by row
    struct Affine2 {
        std::array<double, 4> m{1.0, 0.0, 0.0, 1.0};
        Vertex t{};

        Vertex operator()(const Vertex &p) const { return {m[0] * p.x + m[1] * p.y + t.x, m[2] * p.x + m[3] * p.y + t.y}; }
        double Det() const { return m[0] * m[3] - m[1] * m[2]; }
        // this after b
        Affine2 operator*(const Affine2 &b) const {
            return {{m[0] * b.m[0] + m[1] * b.m[2], m[0] * b.m[1] + m[1] * b.m[3], m[2] * b.m[0] + m[3] * b.m[2], m[2] * b.m[1] + m[3] * b.m[3]},
                    (*this)(b.t)};
        }
        Affine2 Inverse() const {
            const double d = Det();
            if (!(std::abs(d) > 0.0))
                throw std::invalid_argument("the transformation is singular");
            const Affine2 r{{m[3] / d, -m[1] / d, -m[2] / d, m[0] / d}, {}};
            const Vertex u = r(t);
            return {r.m, {-u.x, -u.y}};
        }
        // the angle (radians) its rotational part turns by
        double Turn() const { return std::atan2(m[2] - m[1], m[0] + m[3]); }

        static Affine2 Translation(const Vertex &d) { return {{1.0, 0.0, 0.0, 1.0}, d}; }
        static Affine2 Rotation(const Vertex &c, double degrees) {
            const double a = Radians(degrees), cs = std::cos(a), sn = std::sin(a);
            return {{cs, -sn, sn, cs}, {c.x - cs * c.x + sn * c.y, c.y - sn * c.x - cs * c.y}};
        }
        // at the line through a along d
        static Affine2 Reflection(const Vertex &a, const Vertex &d) {
            const Vertex u = Normalized(d);
            const Affine2 r{{2.0 * u.x * u.x - 1.0, 2.0 * u.x * u.y, 2.0 * u.x * u.y, 2.0 * u.y * u.y - 1.0}, {}};
            const Vertex ra = r(a);
            return {r.m, {a.x - ra.x, a.y - ra.y}};
        }
        static Affine2 Scaling(const Vertex &c, double s) { return {{s, 0.0, 0.0, s}, {c.x - s * c.x, c.y - s * c.y}}; }
    };

    // a mesh identification of the edges named from with those named to (netgen's Identify): trafo maps from onto to,
    // none finds it from the geometry at every export (GetTransformation); type: netgen's Identifications::ID_TYPE
    struct Identification {
        std::string name, from, to;
        std::optional<Affine2> trafo;
        int type = 2;
    };
    using Identifications = std::vector<Identification>;

    // a's, then those of b whose names a does not have
    inline void MergeIdentifications(Identifications &a, const Identifications &b) {
        for (const Identification &i : b)
            if (std::none_of(a.begin(), a.end(), [&](const Identification &j) { return j.name == i.name; }))
                a.push_back(i);
    }
    // after the plane's points move by motion
    inline void MoveIdentifications(Identifications &ids, const Affine2 &motion) {
        for (Identification &i : ids)
            if (i.trafo)
                i.trafo = motion * *i.trafo * motion.Inverse();
    }

    namespace identify {
        // the edges in order along one connected chain; a closed chain lists its start once
        struct Chain {
            std::vector<Vertex> points;
            EdgeList edges;
            bool closed = false;
        };
        inline Chain ChainOf(const EdgeList &given, const std::string &what, double tol) {
            const EdgeList edges = given.Expanded();
            if (edges.empty())
                throw std::invalid_argument("GetTransformation: " + what + " has no edges");
            for (const Edge &e : edges)
                if (std::holds_alternative<Spline>(e))
                    throw std::invalid_argument("GetTransformation: " + what + " has a spline; it maps lines and arcs");
            const auto degree = [&](const Vertex &p) {
                size_t n = 0;
                for (const Edge &e : edges)
                    n += (size_t)StartOf(e).IsClose(p, tol) + (size_t)EndOf(e).IsClose(p, tol);
                return n;
            };
            size_t first = 0;
            bool flip = false;
            for (size_t k = 0; k < edges.size(); ++k) {
                if (degree(StartOf(edges[k])) > 2 || degree(EndOf(edges[k])) > 2)
                    throw std::invalid_argument("GetTransformation: " + what + " branches; it must be one chain of edges");
                if (degree(StartOf(edges[k])) == 1 || degree(EndOf(edges[k])) == 1) {
                    first = k;
                    flip = degree(StartOf(edges[k])) != 1;
                    break;
                }
            }
            Chain c;
            std::vector<bool> used(edges.size(), false);
            used[first] = true;
            c.edges.push_back(flip ? ReversedOf(edges[first]) : edges[first]);
            c.points = {StartOf(c.edges.back()), EndOf(c.edges.back())};
            while (c.edges.size() < edges.size()) {
                const Vertex at = c.points.back();
                size_t k = 0;
                while (k < edges.size() && (used[k] || !(StartOf(edges[k]).IsClose(at, tol) || EndOf(edges[k]).IsClose(at, tol))))
                    ++k;
                if (k == edges.size())
                    throw std::invalid_argument("GetTransformation: " + what + " is not one connected chain of edges");
                used[k] = true;
                c.edges.push_back(StartOf(edges[k]).IsClose(at, tol) ? edges[k] : ReversedOf(edges[k]));
                c.points.push_back(EndOf(c.edges.back()));
            }
            if (c.points.size() > 2 && c.points.back().IsClose(c.points.front(), tol)) {
                c.closed = true;
                c.points.pop_back();
            }
            return c;
        }

        // the map with f(p[k]) = q[k] in the least squares sense
        inline std::optional<Affine2> AffineFit(const std::vector<Vertex> &p, const std::vector<Vertex> &q) {
            std::array<double, 9> a{};
            std::array<double, 3> bu{}, bv{};
            for (size_t k = 0; k < p.size(); ++k) {
                const std::array<double, 3> r{p[k].x, p[k].y, 1.0};
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 3; ++j)
                        a[3 * i + j] += r[i] * r[j];
                    bu[i] += r[i] * q[k].x;
                    bv[i] += r[i] * q[k].y;
                }
            }
            const auto det = [](const std::array<double, 9> &x) {
                return x[0] * (x[4] * x[8] - x[5] * x[7]) - x[1] * (x[3] * x[8] - x[5] * x[6]) + x[2] * (x[3] * x[7] - x[4] * x[6]);
            };
            const double d = det(a);
            if (!(std::abs(d) > 0.0))
                return std::nullopt;
            const auto solve = [&](const std::array<double, 3> &b) {
                std::array<double, 3> x{};
                for (int c = 0; c < 3; ++c) {
                    std::array<double, 9> ac = a;
                    for (int i = 0; i < 3; ++i)
                        ac[3 * i + c] = b[i];
                    x[c] = det(ac) / d;
                }
                return x;
            };
            const std::array<double, 3> u = solve(bu), v = solve(bv);
            return Affine2{{u[0], u[1], v[0], v[1]}, {u[2], v[2]}};
        }

        // the similarity taking p0 to q0 and p1 to q1, mirrored or not
        inline std::optional<Affine2> Similarity(const Vertex &p0, const Vertex &p1, const Vertex &q0, const Vertex &q1, bool mirrored) {
            const Vertex dp = p1 - p0, dq = q1 - q0;
            const double n = dp.x * dp.x + dp.y * dp.y;
            if (!(n > 0.0))
                return std::nullopt;
            Affine2 s;
            if (!mirrored) {
                const double ar = (dq.x * dp.x + dq.y * dp.y) / n, ai = (dq.y * dp.x - dq.x * dp.y) / n;
                s.m = {ar, -ai, ai, ar};
            } else {
                const double ar = (dq.x * dp.x - dq.y * dp.y) / n, ai = (dq.y * dp.x + dq.x * dp.y) / n;
                s.m = {ar, ai, ai, -ar};
            }
            const Vertex m0 = s(p0);
            s.t = {q0.x - m0.x, q0.y - m0.y};
            return s;
        }
    }

    // the map of the plane taking the chain of lines and arcs a onto b, vertex onto vertex in order: a straight chain
    // (or a single arc, or a circle) by a similarity, else by the affine map of its vertices; every vertex and edge
    // midpoint must land. Of the pairings (start onto start or end; around a closed chain every start) the map closest
    // to a translation (orientation kept, the smallest turn), with reverse the best one running the other way
    inline Affine2 GetTransformation(const EdgeList &a, const EdgeList &b, bool reverse = false,
                                     const std::string &aName = "a", const std::string &bName = "b") {
        using namespace identify;
        double scale = 0.0;
        BBox box;
        for (const EdgeList *side : {&a, &b})
            for (const Edge &e : *side)
                for (const Vertex &p : {StartOf(e), EndOf(e), MidPointOf(e)}) {
                    scale = std::max(scale, NormInf(p));
                    box.Grow(p);
                }
        // well inside netgen's identification tolerance, 1e-7 of the size
        const double tol = 1e-9 * std::max(scale, Norm(box.Size()));
        const Chain ca = ChainOf(a, aName, tol), cb = ChainOf(b, bName, tol);
        const size_t n = ca.points.size(), m = ca.edges.size();
        if (cb.points.size() != n || cb.edges.size() != m || ca.closed != cb.closed)
            throw std::invalid_argument("GetTransformation: " + aName + " has " + std::to_string(n) + " vertices" +
                                        (ca.closed ? " (closed)" : "") + ", " + bName + " " + std::to_string(cb.points.size()) +
                                        (cb.closed ? " (closed)" : "") + "; both need the same");
        bool straight = true;
        const auto farthest =
            std::max_element(ca.points.begin(), ca.points.end(), [&](const Vertex &x, const Vertex &y) {
                return Norm(x - ca.points.front()) < Norm(y - ca.points.front());
            });
        const Vertex base = *farthest - ca.points.front();
        for (const Vertex &p : ca.points)
            straight = straight && std::abs(Cross(base, p - ca.points.front())) <= tol * Norm(base);
        struct Candidate {
            Affine2 map;
            bool backwards;
            double miss;
        };
        std::vector<Candidate> all;
        for (bool backwards : {false, true})
            for (size_t shift = 0; shift < (ca.closed ? n : 1); ++shift) {
                const auto point = [&](size_t k) { return backwards ? (ca.closed ? (shift + n - k) % n : n - 1 - k) : (shift + k) % n; };
                const auto edge = [&](size_t k) { return backwards ? (ca.closed ? (shift + 2 * m - k - 1) % m : m - 1 - k) : (shift + k) % m; };
                std::vector<Vertex> q(n);
                for (size_t k = 0; k < n; ++k)
                    q[k] = cb.points[point(k)];
                std::vector<std::optional<Affine2>> maps;
                const size_t last = (size_t)(farthest - ca.points.begin());
                if (straight)
                    for (bool mirrored : {false, true})
                        maps.push_back(Similarity(ca.points.front(), ca.points[last], q.front(), q[last], mirrored));
                else
                    maps.push_back(AffineFit(ca.points, q));
                for (const std::optional<Affine2> &f : maps) {
                    if (!f)
                        continue;
                    double miss = 0.0;
                    for (size_t k = 0; k < n; ++k)
                        miss = std::max(miss, Norm((*f)(ca.points[k]) - q[k]));
                    for (size_t k = 0; k < m; ++k)
                        miss = std::max(miss, Norm((*f)(MidPointOf(ca.edges[k])) - MidPointOf(cb.edges[edge(k)])));
                    all.push_back({*f, backwards, miss});
                }
            }
        const auto better = [](const Candidate &x, const Candidate &y) {
            const bool kx = x.map.Det() > 0.0, ky = y.map.Det() > 0.0;
            if (kx != ky)
                return kx;
            return std::abs(x.map.Turn()) < std::abs(y.map.Turn());
        };
        const Candidate *best = nullptr;
        for (const Candidate &c : all)
            if (c.miss <= tol && (!best || better(c, *best)))
                best = &c;
        if (!best) {
            double least = std::numeric_limits<double>::infinity();
            for (const Candidate &c : all)
                least = std::min(least, c.miss);
            throw std::invalid_argument("GetTransformation: no map takes " + aName + " onto " + bName + " vertex onto vertex; the "
                                        "best misses a vertex or edge midpoint by " + std::to_string(least));
        }
        if (!reverse)
            return best->map;
        const Candidate *other = nullptr;
        for (const Candidate &c : all)
            if (c.miss <= tol && c.backwards != best->backwards && (!other || better(c, *other)))
                other = &c;
        if (!other)
            throw std::invalid_argument("GetTransformation(reverse=True): " + aName + " does not map onto " + bName + " the other way round");
        return other->map;
    }

}
