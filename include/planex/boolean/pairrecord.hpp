// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "planex/boolean/cutloop.hpp"

namespace planex2d {

    inline bool VertexLess(const Vertex &p, const Vertex &q) { return p.x < q.x || (p.x == q.x && p.y < q.y); }

    // an edge k by its two ends, the lesser first
    struct EdgeSpan {
        Vertex lo;
        Vertex hi;
        uint32_t k;
    };

    template <class F>
    std::vector<EdgeSpan> SortedSpans(const Wire &w, F use) {
        std::vector<EdgeSpan> spans;
        for (size_t k = 0; k < w.Size(); ++k) {
            if (!use(k))
                continue;
            auto [p, q] = std::visit([](const auto &x) { return std::pair{x.Start(), x.End()}; }, w[k]);
            if (p == q)
                continue;
            if (VertexLess(q, p))
                std::swap(p, q);
            spans.push_back({p, q, (uint32_t)k});
        }
        std::sort(spans.begin(), spans.end(), [](const EdgeSpan &a, const EdgeSpan &b) {
            return a.lo != b.lo ? VertexLess(a.lo, b.lo) : a.hi != b.hi ? VertexLess(a.hi, b.hi) : a.k < b.k;
        });
        return spans;
    }
    inline size_t SameSpansEnd(const std::vector<EdgeSpan> &spans, size_t a) {
        size_t b = a + 1;
        while (b < spans.size() && spans[b].lo == spans[a].lo && spans[b].hi == spans[a].hi)
            ++b;
        return b;
    }

    class VertexMerges {
        private:
            std::vector<Vertex> pts;
            std::vector<size_t> parent;

            size_t Index(const Vertex &p) const {
                auto it = std::lower_bound(pts.begin(), pts.end(), p, VertexLess);
                return it != pts.end() && *it == p ? (size_t)(it - pts.begin()) : pts.size();
            }
            size_t Root(size_t i) const {
                while (parent[i] != i)
                    i = parent[i];
                return i;
            }

        public:
            explicit VertexMerges(const std::vector<Identified> &ids);
            bool Empty() const { return pts.empty(); }
            Vertex Of(const Vertex &p) const {
                const size_t i = Index(p);
                return i == pts.size() ? p : pts[parent[i]];
            }
    };

    template <class F>
    void ApplyIdentified(Wire &w, std::vector<CutState> &state, const VertexMerges &ids, F use) {
        const double rounding = w.Tol();
        for (size_t k = 0; k < w.Size(); ++k) {
            if (state[k].excluded || !use(k))
                continue;
            const bool closed = IsClosedEdge(w[k]);
            std::visit([&](auto &e) {
                if constexpr (!IsCircle<decltype(e)>) {
                    if (!ids.Empty()) {
                        const Vertex s = ids.Of(e.Start());
                        const Vertex t = ids.Of(e.End());
                        if (s != e.Start() || t != e.End())
                            Wire::MoveEnds(e, s, t, rounding);
                    }
                    if (e.Start() == e.End() && !closed)
                        state[k].excluded = true;
                }
            }, w[k]);
        }
    }

    // union-find over pieces with the parity of their sides of the other operand: 0 the same side, 1 opposite sides; a
    // cycle of links with odd parity marks its component as conflicting
    class SideLinks {
        private:
            std::vector<uint32_t> parent;
            std::vector<uint32_t> size;
            std::vector<unsigned char> parity;
            std::vector<unsigned char> conflict;

        public:
            explicit SideLinks(size_t n) : parent(n), size(n, 1), parity(n, 0), conflict(n, 0) {
                for (size_t i = 0; i < n; ++i)
                    parent[i] = (uint32_t)i;
            }
            std::pair<uint32_t, unsigned char> Find(uint32_t k) {
                if (parent[k] == k)
                    return {k, 0};
                const auto [r, p] = Find(parent[k]);
                parity[k] ^= p;
                parent[k] = r;
                return {r, parity[k]};
            }
            void Link(uint32_t a, uint32_t b, unsigned char p);
            bool Conflict(uint32_t root) const { return conflict[root] != 0; }
    };

    struct Spoke {
        double angle;
        double spread;
        uint32_t piece;
    };

    // the margin covers the rounding of the vertex and of the direction
    inline Spoke SpokeOf(const Line &e, const Vertex &v, double tol, uint32_t k) {
        const Vertex d = v == e.Start() ? e.End() - e.Start() : e.Start() - e.End();
        const double l = Norm(d);
        return {std::atan2(d.y, d.x), l > 0.0 ? 4.0 * tol / l : std::numeric_limits<double>::infinity(), k};
    }
    inline Spoke SpokeOf(const Arc &e, const Vertex &v, double tol, uint32_t k) {
        const Vertex r = v - e.Center();
        const Vertex t = e.CCW() == (v == e.Start()) ? Vertex{-r.y, r.x} : Vertex{r.y, -r.x};
        return {std::atan2(t.y, t.x), 4.0 * tol / e.Radius(), k};
    }
    inline Spoke SpokeOf(const Circle &e, const Vertex &v, double tol, uint32_t k) {
        const Vertex t = e.Tangent<false>(v);
        return {std::atan2(t.y, t.x), 4.0 * tol / e.Radius(), k};
    }
    inline Spoke SpokeOf(const Spline &e, const Vertex &v, double tol, uint32_t k) {
        const std::span<const PrimitiveEdge> parts = e.Approximation();
        return std::visit([&](const auto &x) { return SpokeOf(x, v, tol, k); }, v == e.Start() ? parts.front() : parts.back());
    }

    void LinkBySectors(const std::vector<Spoke> &own, const std::vector<Spoke> &other, SideLinks &links);

    SideLinks SideLinksOf(const Wire &c, const std::vector<CutState> &state, const CutRecord &rec, const VertexMerges &ids, double tol);

    // the side of each piece of one operand near the other (-1: not decided here): one inside test per run of linked
    // pieces, on its longest piece
    template <class F>
    std::vector<signed char> SidesByRuns(const Wire &c, const std::vector<CutState> &state, const CutRecord &rec,
                                         const VertexMerges &ids, double tol, F inside) {
        static Timer t("planex Boolean runs");
        Region reg(t);
        const size_t n = c.Size();
        std::vector<signed char> side(n, -1);
        auto single = [&](size_t k) {
            return state[k].IsToCut() && (state[k].mask == 1 || state[k].mask == 2);
        };
        SideLinks links = SideLinksOf(c, state, rec, ids, tol);
        constexpr uint32_t none = std::numeric_limits<uint32_t>::max();
        std::vector<uint32_t> longest(n, none);
        std::vector<double> length(n, 0.0);
        for (size_t k = 0; k < n; ++k) {
            if (!single(k))
                continue;
            length[k] = LengthOf(c[k]);
            const uint32_t r = links.Find((uint32_t)k).first;
            if (longest[r] == none || length[k] > length[longest[r]])
                longest[r] = (uint32_t)k;
        }
        std::vector<signed char> rootSide(n, -1);
        for (size_t k = 0; k < n; ++k) {
            if (!single(k))
                continue;
            const auto [r, p] = links.Find((uint32_t)k);
            if (links.Conflict(r))
                continue;
            if (rootSide[r] < 0)
                rootSide[r] = static_cast<signed char>((inside(longest[r]) ? 1 : 0) ^ links.Find(longest[r]).second);
            side[k] = static_cast<signed char>(rootSide[r] ^ p);
        }
        return side;
    }

}
