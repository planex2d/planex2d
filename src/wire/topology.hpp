// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "wire/wire.hpp"
#include "wire/crossing.hpp"
#include "wire/loops.hpp"
#include "core/timer.hpp"

namespace planex2d {

    inline size_t Wire::VertexIds(std::vector<size_t> &id) const {
        return VertexIdsOf(Endpoints(), Tol(), id);
    }

    inline std::vector<Loop> Wire::Loops() const {
        std::vector<size_t> id;
        size_t nv = VertexIds(id);
        std::vector<LoopEdge> loopEdges;
        for (size_t i = 0; i < edges.size(); ++i) {
            loopEdges.push_back({id[2 * i], id[2 * i + 1], Leaving(edges[i], true), Leaving(edges[i], false)});
        }
        return BuildLoops(loopEdges, nv);
    }

    inline std::vector<char> Wire::Traversal() const {
        std::vector<char> forward(edges.size(), 1);
        for (const Loop &loop : Loops()) {
            const bool flip = loop.closed && LoopArea(loop.edges) < 0.0;
            for (auto [i, along] : loop.edges)
                forward[i] = along != flip;
        }
        return forward;
    }

    inline WireList Wire::Wires() const {
        WireList wires;
        for (const Loop &loop : Loops()) {
            Wire &w = wires.emplace_back(axes);
            for (auto [i, forward] : loop.edges)
                w.Add(edges[i]);
        }
        return wires;
    }

    // the cycles of an arrangement in which every edge bounds a face on both sides: each half edge (edge, true:
    // start to end) once, the face on its left; at a vertex the walk turns into the edge next clockwise
    inline std::vector<Loop> Wire::FaceCycles() const {
        std::vector<size_t> id;
        const size_t nv = VertexIds(id), nh = 2 * edges.size();
        struct Out {
            Departure d;
            double angle;
            size_t half;
        };
        std::vector<std::vector<Out>> around(nv);
        for (size_t i = 0; i < edges.size(); ++i)
            for (size_t s = 0; s < 2; ++s) {
                const Departure d = Leaving(edges[i], s == 0);
                around[id[2 * i + s]].push_back({d, AngleOf(d), 2 * i + s});
            }
        std::vector<std::pair<size_t, size_t>> at(nh);
        for (size_t v = 0; v < nv; ++v) {
            SortAround(around[v]);
            for (size_t k = 0; k < around[v].size(); ++k)
                at[around[v][k].half] = {v, k};
        }
        std::vector<bool> used(nh, false);
        std::vector<Loop> cycles;
        for (size_t first = 0; first < nh; ++first) {
            if (used[first])
                continue;
            Loop loop{{}, true};
            for (size_t h = first; !used[h];) {
                used[h] = true;
                loop.edges.emplace_back(h / 2, h % 2 == 0);
                auto [v, k] = at[h ^ 1];
                const std::vector<Out> &a = around[v];
                h = a[(k + a.size() - 1) % a.size()].half;
            }
            cycles.push_back(std::move(loop));
        }
        return cycles;
    }

    inline std::vector<NestedLoop> Wire::Nesting() const {
        static Timer t("planex Nesting");
        Region reg(t);
        std::vector<NestedLoop> nested;
        for (Loop &l : Loops())
            if (l.closed)
                nested.push_back({std::move(l), 0, none});
        // an edge listed twice (the cut between two faces of one part) lies on another loop too: its midpoint
        // would be on that loop's boundary, so the point comes from an edge listed once if the loop has one
        std::vector<std::pair<Vertex, Vertex>> ends;
        for (const Edge &e : edges)
            std::visit([&](const auto &x) {
                Vertex a = x.Start(), b = x.End();
                if (b.x < a.x || (b.x == a.x && b.y < a.y))
                    std::swap(a, b);
                ends.emplace_back(a, b);
            }, e);
        std::vector<std::pair<Vertex, Vertex>> sorted = ends;
        auto before = [](const std::pair<Vertex, Vertex> &p, const std::pair<Vertex, Vertex> &q) {
            return std::tie(p.first.x, p.first.y, p.second.x, p.second.y) < std::tie(q.first.x, q.first.y, q.second.x, q.second.y);
        };
        std::sort(sorted.begin(), sorted.end(), before);
        auto twice = [&](size_t i) {
            auto [lo, hi] = std::equal_range(sorted.begin(), sorted.end(), ends[i], before);
            return hi - lo > 1;
        };
        std::vector<Vertex> point;
        std::vector<BBox> box;
        for (const NestedLoop &n : nested) {
            size_t sample = n.loop.edges.front().first;
            for (auto [i, forward] : n.loop.edges)
                if (!twice(i)) {
                    sample = i;
                    break;
                }
            point.push_back(MidPointOf(edges[sample]));
            BBox b;
            for (auto [i, forward] : n.loop.edges)
                b.Grow(BoxOf(edges[i]));
            box.push_back(b);
        }
        std::vector<std::vector<size_t>> around(nested.size());
        for (size_t k = 0; k < nested.size(); ++k)
            for (size_t j = 0; j < nested.size(); ++j) {
                if (j == k || !box[j].IsInside(point[k]))
                    continue;
                bool odd = false;
                for (auto [i, forward] : nested[j].loop.edges)
                    odd ^= RayParity<false, false>(edges[i], point[k]);
                if (odd)
                    around[k].push_back(j);
            }
        for (size_t k = 0; k < nested.size(); ++k) {
            nested[k].depth = around[k].size();
            for (size_t j : around[k])
                if (around[j].size() + 1 == around[k].size())
                    nested[k].parent = j;
        }
        return nested;
    }

    template <bool exact>
    double Wire::LoopArea(const std::vector<std::pair<size_t, bool>> &loop) const {
        if (loop.empty())
            return 0.0;
        const Vertex o = StartOf(edges[loop.front().first]);
        double a = 0.0;
        for (auto [i, forward] : loop) {
            if constexpr (exact)
                a += (forward ? 1.0 : -1.0) * ExactSignedArea(edges[i], o);
            else
                a += (forward ? 1.0 : -1.0) * SignedArea(edges[i], o);
        }
        return a;
    }
    inline double Wire::Area() const {
        double area = 0.0;
        for (const NestedLoop &n : Nesting())
            area += (n.depth % 2 == 0 ? 1.0 : -1.0) * std::abs(LoopArea<true>(n.loop.edges));
        return area;
    }

    // as (edge, true for its end)
    inline std::vector<std::pair<size_t, bool>> Wire::OpenEnds() const {
        std::vector<Vertex> pts = Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        std::vector<std::pair<size_t, bool>> open;
        for (size_t k = 0; k < pts.size(); ++k)
            if (!Matched(pts, grid, k))
                open.emplace_back(k / 2, k % 2 == 1);
        return open;
    }

    inline bool Wire::HasEvenDegrees(double tol) const {
        const std::vector<Vertex> pts = Endpoints();
        const Grid grid = Grid::Of(pts, tol);
        for (size_t k = 0; k < pts.size(); ++k) {
            size_t ends = 0;
            grid.ForNear(pts[k], [&](size_t j) { ends += pts[j].IsClose(pts[k], tol); });
            if (ends % 2 == 1)
                return false;
        }
        return true;
    }

    inline std::vector<char> Wire::FaceOrientation() const {
        std::vector<char> reverse(edges.size(), 0);
        const std::vector<Loop> loops = Loops();
        struct Info {
            const Loop *loop;
            double area = 0.0;
            Vertex point;
            BBox box;
        };
        std::vector<Info> info;
        for (const Loop &loop : loops) {
            if (!loop.closed)
                continue;
            Info in{&loop};
            in.area = LoopArea(loop.edges);
            for (auto [i, f] : loop.edges)
                in.box.Grow(BoxOf(edges[i]));
            in.point = MidPointOf(edges[loop.edges.front().first]);
            info.push_back(in);
        }
        for (const Info &k : info) {
            size_t depth = 0;
            for (const Info &j : info) {
                if (&j == &k || !j.box.IsInside(k.point))
                    continue;
                bool odd = false;
                for (auto [i, f] : j.loop->edges)
                    odd ^= RayParity<false, false>(edges[i], k.point);
                depth += odd;
            }
            const bool flip = (k.area > 0.0) != (depth % 2 == 0);
            for (auto [i, forward] : k.loop->edges)
                reverse[i] = forward == flip;
        }
        return reverse;
    }

    inline bool Wire::Contains(const Vertex &p) const {
        bool odd = false;
        for (const Edge &e : edges)
            odd ^= RayParity<false, false>(e, p);
        return odd;
    }

    // a point strictly inside the region: on the horizontal line through the widest gap between the heights of its
    // vertices and arc extremes no crossing is a touch, there the middle of the widest stretch inside
    inline std::optional<Vertex> Wire::InteriorPoint() const {
        std::vector<double> heights;
        auto on = [](const Arc &a, const Vertex &p) { return a.Param(p) <= a.Sweep(); };
        for (const Edge &e : edges)
            std::visit([&](const auto &x) {
                ForEachPart(x, [&](const auto &p) {
                    heights.push_back(p.Start().y);
                    heights.push_back(p.End().y);
                    if constexpr (std::is_same_v<std::decay_t<decltype(p)>, Arc>)
                        for (double s : {-1.0, 1.0})
                            if (Vertex q{p.Center().x, p.Center().y + s * p.Radius()}; on(p, q))
                                heights.push_back(q.y);
                });
            }, e);
        if (heights.size() < 2)
            return std::nullopt;
        std::sort(heights.begin(), heights.end());
        size_t widest = 0;
        for (size_t k = 1; k + 1 < heights.size(); ++k)
            if (heights[k + 1] - heights[k] > heights[widest + 1] - heights[widest])
                widest = k;
        const double y = 0.5 * (heights[widest] + heights[widest + 1]);
        std::vector<double> xs;
        for (const Edge &e : edges)
            std::visit([&](const auto &x) {
                ForEachPart(x, [&](const auto &p) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(p)>, Arc>) {
                        double dy = y - p.Center().y, r = p.Radius();
                        if (std::abs(dy) >= r)
                            return;
                        double dx = std::sqrt((r - dy) * (r + dy));
                        for (double s : {-1.0, 1.0})
                            if (Vertex q{p.Center().x + s * dx, y}; on(p, q))
                                xs.push_back(q.x);
                    } else {
                        Vertex a = p.Start(), b = p.End();
                        if ((a.y < y) != (b.y < y))
                            xs.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
                    }
                });
            }, e);
        std::sort(xs.begin(), xs.end());
        if (xs.size() < 2)
            return std::nullopt;
        size_t best = 0;
        for (size_t k = 2; k + 1 < xs.size(); k += 2)
            if (xs[k + 1] - xs[k] > xs[best + 1] - xs[best])
                best = k;
        return Vertex{0.5 * (xs[best] + xs[best + 1]), y};
    }

    // cell = max(median edge extent, side of a square of area / n): few edges per cell, few cells per edge
    inline Grid Wire::EdgeGrid() const {
        static Timer t("planex EdgeGrid");
        Region reg(t);
        if (edges.empty())
            return Grid(BBox::Of({}, {}), 1.0);
        std::vector<BBox> boxes;
        for (const Edge &e : edges)
            boxes.push_back(BoxOf(e));
        return Grid::OfBoxes(boxes);
    }

}
