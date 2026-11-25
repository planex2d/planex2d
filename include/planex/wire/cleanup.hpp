// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "planex/wire/wire.hpp"
#include "planex/core/timer.hpp"

namespace planex2d {

    template <class A, class B>
    std::optional<Edge> Joined(const A &a, const B &b, const Vertex &v) {
        if constexpr (std::is_same_v<A, B> && !IsCircle<A>) {
            if (auto r = a.Joined(b, v))
                return Edge(std::move(*r));
        }
        return std::nullopt;
    }

    // in passes until none joins: a join decides at the scale of the two edges it joins, so a short piece one neighbour
    // refused may join the edge the other neighbour became
    template <class F>
    void Wire::MergeOnCarriers(F use) {
        static Timer t("planex MergeOnCarriers");
        Region reg(t);
        std::vector<bool> selected(edges.size());
        for (size_t i = 0; i < edges.size(); ++i)
            selected[i] = use(i);
        for (bool joined = true; joined;) {
            joined = false;
            const size_t n = edges.size();
            std::vector<Vertex> pts = Endpoints();
            Grid grid = Grid::Of(pts, 0.0);
            auto partner = [&](size_t k) -> std::optional<size_t> {
                std::optional<size_t> found;
                size_t count = 0;
                grid.ForCell(pts[k], [&](size_t j) {
                    if (j / 2 != k / 2 && pts[j] == pts[k]) {
                        found = j;
                        ++count;
                    }
                });
                return count == 1 ? found : std::nullopt;
            };
            std::vector<bool> removed(n, false);
            // an edge already runs between the far ends of the two, as near as their join would (a tip cut off within
            // tol): joined they would be two edges that bound nothing, apart the three stay a thin triangle
            auto doubles = [&](const Edge &e, size_t i, size_t j) {
                const Vertex p = planex2d::StartOf(e), q = planex2d::EndOf(e), m = MidPointOf(e);
                const double tol = planex2d::Tol(LengthScaleOf(e));
                bool found = false;
                grid.ForCell(p, [&](size_t k) {
                    const size_t o = k / 2;
                    found = found || (o != i && o != j && !removed[o] && pts[k] == p && pts[k ^ 1] == q &&
                                      MidPointOf(edges[o]).IsClose(m, tol));
                });
                return found;
            };
            for (size_t i = 0; i < n; ++i) {
                if (removed[i] || !selected[i] || HasPartition(PropsOf(edges[i])))
                    continue;
                Edge current = edges[i];
                bool any = false;
                for (size_t k : {2 * i + 1, 2 * i}) {
                    for (;;) {
                        std::optional<size_t> j = partner(k);
                        if (!j || *j / 2 == i || removed[*j / 2] || Owner(*j / 2) != Owner(i) ||
                            !SameProperties(PropsOf(current), PropsOf(edges[*j / 2])) || HasPropsAt(current, pts[k]) ||
                            Marked(EndpointProps(*j)))
                            break;
                        std::optional<Edge> e = std::visit([&](const auto &a, const auto &b) { return Joined(a, b, pts[k]); },
                                                           current, edges[*j / 2]);
                        if (!e || doubles(*e, i, *j / 2))
                            break;
                        current = std::move(*e);
                        removed[*j / 2] = true;
                        any = true;
                        k = *j ^ 1;
                    }
                }
                if (any) {
                    edges[i] = std::move(current);
                    joined = true;
                }
            }
            Erase(removed);
            planex2d::Erase(selected, removed);
        }
    }

    inline bool IsFromSpline(const Edge &e) {
        return std::visit([](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline> || IsCircle<decltype(x)>)
                return false;
            else
                return x.fromSpline;
        }, e);
    }

    std::vector<size_t> SmallRuns(const std::vector<Vertex> &pts, Grid &grid, const std::vector<bool> &isSmall);
    std::vector<double> SmallRunLengths(const EdgeList &edges, const std::vector<Vertex> &pts, Grid &grid,
                                        const std::vector<bool> &isSmall);

}
