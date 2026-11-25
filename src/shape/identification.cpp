// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/identification.hpp"

#include <limits>

namespace planex2d::identify {

    Chain ChainOf(const EdgeList &given, const std::string &what, double tol) {
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
    std::optional<Affine2> AffineFit(const std::vector<Vertex> &p, const std::vector<Vertex> &q) {
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

    std::optional<Affine2> Similarity(const Vertex &p0, const Vertex &p1, const Vertex &q0, const Vertex &q1, bool mirrored) {
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

    struct Candidate {
        Affine2 map;
        bool backwards;
        double miss;
    };

    static std::vector<Candidate> Candidates(const Chain &ca, const Chain &cb, double tol) {
        const size_t n = ca.points.size(), m = ca.edges.size();
        const auto farthest =
            std::max_element(ca.points.begin(), ca.points.end(), [&](const Vertex &x, const Vertex &y) {
                return Norm(x - ca.points.front()) < Norm(y - ca.points.front());
            });
        const size_t last = (size_t)(farthest - ca.points.begin());
        const Vertex base = *farthest - ca.points.front();
        bool straight = true;
        for (const Vertex &p : ca.points)
            straight = straight && std::abs(Cross(base, p - ca.points.front())) <= tol * Norm(base);
        std::vector<Candidate> all;
        for (bool backwards : {false, true})
            for (size_t shift = 0; shift < (ca.closed ? n : 1); ++shift) {
                const auto point = [&](size_t k) { return backwards ? (ca.closed ? (shift + n - k) % n : n - 1 - k) : (shift + k) % n; };
                const auto edge = [&](size_t k) { return backwards ? (ca.closed ? (shift + 2 * m - k - 1) % m : m - 1 - k) : (shift + k) % m; };
                std::vector<Vertex> q(n);
                for (size_t k = 0; k < n; ++k)
                    q[k] = cb.points[point(k)];
                std::vector<std::optional<Affine2>> maps;
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
        return all;
    }

    // of the candidates that land within tol, the map closest to a translation: orientation kept, then the smallest turn
    static std::optional<Candidate> Best(const std::vector<Candidate> &all, double tol) {
        const auto better = [](const Candidate &x, const Candidate &y) {
            const bool kx = x.map.Det() > 0.0, ky = y.map.Det() > 0.0;
            if (kx != ky)
                return kx;
            return std::abs(x.map.Turn()) < std::abs(y.map.Turn());
        };
        std::optional<Candidate> best;
        for (const Candidate &c : all)
            if (c.miss <= tol && (!best || better(c, *best)))
                best = c;
        return best;
    }

}

namespace planex2d {

    Affine2 GetTransformation(const EdgeList &a, const EdgeList &b, bool reverse,
                              const std::string &aName, const std::string &bName) {
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
        if (cb.points.size() != ca.points.size() || cb.edges.size() != ca.edges.size() || ca.closed != cb.closed)
            throw std::invalid_argument("GetTransformation: " + aName + " has " + std::to_string(ca.points.size()) + " vertices" +
                                        (ca.closed ? " (closed)" : "") + ", " + bName + " " + std::to_string(cb.points.size()) +
                                        (cb.closed ? " (closed)" : "") + "; both need the same");
        std::vector<Candidate> all = Candidates(ca, cb, tol);
        const std::optional<Candidate> best = Best(all, tol);
        if (!best) {
            double least = std::numeric_limits<double>::infinity();
            for (const Candidate &c : all)
                least = std::min(least, c.miss);
            throw std::invalid_argument("GetTransformation: no map takes " + aName + " onto " + bName + " vertex onto vertex; the "
                                        "best misses a vertex or edge midpoint by " + std::to_string(least));
        }
        if (!reverse)
            return best->map;
        std::erase_if(all, [&](const Candidate &c) { return c.backwards == best->backwards; });
        const std::optional<Candidate> other = Best(all, tol);
        if (!other)
            throw std::invalid_argument("GetTransformation(reverse=True): " + aName + " does not map onto " + bName + " the other way round");
        return other->map;
    }

}
