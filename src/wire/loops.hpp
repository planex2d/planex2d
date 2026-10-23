// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/vertex.hpp"
#include "curves/edge.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/spline.hpp"
#include "curves/primitiveedge.hpp"
#include "core/timer.hpp"

namespace planex2d {

    // a direction leaving a vertex, its curvature (counterclockwise positive) and how far rounding can turn it
    struct Departure {
        Vertex dir;
        double curvature;
        double spread;
    };

    // in (-pi, pi]: atan2 gives -pi for a leftward direction with y = -0.0
    inline double AngleOf(const Departure &d) {
        const double a = std::atan2(d.dir.y, d.dir.x);
        return a == -std::numbers::pi ? std::numbers::pi : a;
    }

    struct LoopEdge {
        size_t v0, v1;
        Departure d0, d1;
    };

    struct Loop {
        std::vector<std::pair<size_t, bool>> edges;  // true: edge runs from v0 to v1
        bool closed;
    };

    struct NestedLoop {
        Loop loop;
        size_t depth;
        size_t parent;
    };

    template <class E>
    Departure Leaving(const E &x, bool atstart) {
        if constexpr (std::is_same_v<E, Spline>)
            return std::visit([&](const auto &p) { return Leaving(p, atstart); },
                              atstart ? x.Approximation().front() : x.Approximation().back());
        const Vertex t = atstart ? x.Tangent(x.Start()) : x.Tangent(x.End()) * -1.0;
        // as the cut decides: two edges from one vertex are one direction where the shorter's far end lies within tol
        // of the other
        if constexpr (std::is_same_v<E, Arc>)
            return {t, (x.CCW() == atstart ? 1.0 : -1.0) / x.Radius(), Tol(x.LengthScale()) / x.Radius()};
        else
            return {t, 0.0, Tol(x.LengthScale()) / x.Length()};
    }
    template <class... T>
    Departure Leaving(const std::variant<T...> &e, bool atstart) {
        return std::visit([&](const auto &x) { return Leaving(x, atstart); }, e);
    }
    inline Departure Leaving(const PrimitiveEdge &e, bool atstart) {
        return std::visit([&](const auto &x) { return Leaving(x, atstart); }, e);
    }

    // the ends at a vertex counterclockwise by their exact direction (angle in (-pi, pi]); neighbours within rounding of
    // each other (both spreads: each direction may be turned by its own) are one direction, ordered by curvature, then
    // by the exact angle, also across the seam at pi
    template <class T>
    void SortAround(std::vector<T> &a) {
        std::sort(a.begin(), a.end(), [](const T &p, const T &q) { return p.angle < q.angle; });
        auto same = [](const T &p, const T &q, double turn) {
            return std::abs(q.angle + turn - p.angle) <= p.d.spread + q.d.spread;
        };
        for (size_t n = 0; n + 1 < a.size() && same(a.back(), a.front(), 2.0 * std::numbers::pi); ++n) {
            T first = a.front();
            first.angle += 2.0 * std::numbers::pi;
            a.erase(a.begin());
            a.push_back(first);
        }
        for (size_t i = 0; i < a.size();) {
            size_t j = i + 1;
            while (j < a.size() && same(a[j - 1], a[j], 0.0))
                ++j;
            std::stable_sort(a.begin() + (long)i, a.begin() + (long)j, [](const T &p, const T &q) {
                return p.d.curvature != q.d.curvature ? p.d.curvature < q.d.curvature : p.angle < q.angle;
            });
            i = j;
        }
    }

    // a loop is split off at every revisited vertex, so loops are simple
    template <class E>
    std::vector<Loop> BuildLoops(const std::vector<E> &edges, size_t nverts) {
        static Timer t("planex BuildLoops");
        Region reg(t);
        struct End {
            Departure d;
            double angle;
            size_t edge;
            bool atstart;
        };
        std::vector<std::vector<End>> around(nverts);
        for (size_t i = 0; i < edges.size(); ++i) {
            const LoopEdge &e = edges[i];
            around[e.v0].push_back({e.d0, AngleOf(e.d0), i, true});
            around[e.v1].push_back({e.d1, AngleOf(e.d1), i, false});
        }
        for (auto &a : around)
            SortAround(a);
        std::vector<bool> used(edges.size(), false);
        auto unused = [&](size_t v) {
            return std::count_if(around[v].begin(), around[v].end(), [&](const End &h) { return !used[h.edge]; });
        };
        std::vector<Loop> loops;
        // a closed loop runs from its earliest edge, that edge forward: where and which way the walk enters it depends on
        // how the directions at a vertex sort, which a rounding at pi decides
        auto emit = [&](const std::vector<const End *> &trail) {
            Loop loop;
            for (const End *h : trail)
                loop.edges.emplace_back(h->edge, h->atstart);
            const LoopEdge &first = edges[trail.front()->edge];
            const LoopEdge &last = edges[trail.back()->edge];
            loop.closed = (trail.front()->atstart ? first.v0 : first.v1) == (trail.back()->atstart ? last.v1 : last.v0);
            if (loop.closed) {
                if (!std::min_element(loop.edges.begin(), loop.edges.end())->second) {
                    std::reverse(loop.edges.begin(), loop.edges.end());
                    for (auto &step : loop.edges)
                        step.second = !step.second;
                }
                std::rotate(loop.edges.begin(), std::min_element(loop.edges.begin(), loop.edges.end()), loop.edges.end());
            }
            loops.push_back(std::move(loop));
        };
        auto walk = [&](size_t v) {
            std::vector<size_t> path{v};
            std::vector<const End *> trail;
            std::map<size_t, size_t> position{{v, 0}};
            size_t from = around[v].size() - 1;
            for (;;) {
                const std::vector<End> &a = around[v];
                const End *next = nullptr;
                for (size_t s = 1; s <= a.size() && !next; ++s)
                    if (!used[a[(from + s) % a.size()].edge])
                        next = &a[(from + s) % a.size()];
                if (!next)
                    break;
                used[next->edge] = true;
                trail.push_back(next);
                const LoopEdge &e = edges[next->edge];
                v = next->atstart ? e.v1 : e.v0;
                for (size_t k = 0; k < around[v].size(); ++k)
                    if (around[v][k].edge == next->edge && around[v][k].atstart != next->atstart)
                        from = k;
                auto it = position.find(v);
                if (it == position.end()) {
                    position[v] = path.size();
                    path.push_back(v);
                    continue;
                }
                size_t p = it->second;
                emit(std::vector<const End *>(trail.begin() + (long)p, trail.end()));
                trail.resize(p);
                for (size_t k = p + 1; k < path.size(); ++k)
                    position.erase(path[k]);
                path.resize(p + 1);
            }
            if (!trail.empty())
                emit(trail);
        };
        for (size_t v = 0; v < nverts; ++v)
            if (unused(v) % 2 == 1)
                walk(v);
        for (size_t v = 0; v < nverts; ++v)
            while (unused(v) > 0)
                walk(v);
        return loops;
    }

}
