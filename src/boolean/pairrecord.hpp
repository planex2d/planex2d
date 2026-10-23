// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "boolean/cutloop.hpp"

namespace planex2d {

    inline bool VertexLess(const Vertex &p, const Vertex &q) { return p.x < q.x || (p.x == q.x && p.y < q.y); }

    // the vertices the cut put onto others, each with the one standing for all of them
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
            explicit VertexMerges(const std::vector<Identified> &ids) {
                for (const Identified &d : ids) {
                    pts.push_back(d.from);
                    pts.push_back(d.to);
                }
                std::sort(pts.begin(), pts.end(), VertexLess);
                pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
                parent.resize(pts.size());
                for (size_t i = 0; i < pts.size(); ++i)
                    parent[i] = i;
                for (const Identified &d : ids) {
                    const size_t a = Root(Index(d.from));
                    const size_t b = Root(Index(d.to));
                    if (a != b)
                        parent[a] = b;
                }
                for (size_t i = 0; i < pts.size(); ++i)
                    parent[i] = Root(i);
            }
            bool Empty() const { return pts.empty(); }
            Vertex Of(const Vertex &p) const {
                const size_t i = Index(p);
                return i == pts.size() ? p : pts[parent[i]];
            }
    };

    // the ends the cut identified move onto their vertex; an edge left without length goes
    template <class F>
    void ApplyIdentified(Wire &w, std::vector<CutState> &state, const VertexMerges &ids, F use) {
        for (size_t k = 0; k < w.Size(); ++k) {
            if (state[k].excluded || !use(k))
                continue;
            std::visit([&](auto &e) {
                if constexpr (!IsCircle<decltype(e)>) {
                    if (!ids.Empty()) {
                        const Vertex s = ids.Of(e.Start());
                        const Vertex t = ids.Of(e.End());
                        if (s != e.Start() || t != e.End())
                            e.template Trim<true>(s, t);
                    }
                    if (e.Start() == e.End())
                        state[k].excluded = true;
                }
            }, w[k]);
        }
    }

    // edges of two inputs between the same two vertices and within tol of each other halfway are one edge, kept as a
    // congruent cut keeps it: the cut of another pair made them after their own pair was decided. Two of one input
    // there cancel
    inline void ShareCongruent(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, double tol) {
        struct Span {
            Vertex lo;
            Vertex hi;
            uint32_t k;
        };
        std::vector<Span> spans;
        for (size_t k = 0; k < w.Size(); ++k) {
            if (state[k].excluded || !state[k].needsCutting)
                continue;
            auto [p, q] = std::visit([](const auto &x) { return std::pair{x.Start(), x.End()}; }, w[k]);
            if (p == q)
                continue;
            if (VertexLess(q, p))
                std::swap(p, q);
            spans.push_back({p, q, (uint32_t)k});
        }
        std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) {
            return a.lo != b.lo ? VertexLess(a.lo, b.lo) : a.hi != b.hi ? VertexLess(a.hi, b.hi) : a.k < b.k;
        });
        auto bulge = [&](const Span &s) {
            const Vertex m = MidPointOf(w[s.k]);
            const Vertex d = s.hi - s.lo;
            return Cross(d, m - (s.lo + s.hi) * 0.5) / Norm(d);
        };
        for (size_t a = 0; a < spans.size();) {
            size_t b = a + 1;
            while (b < spans.size() && spans[b].lo == spans[a].lo && spans[b].hi == spans[a].hi)
                ++b;
            for (size_t x = a; x < b; ++x)
                for (size_t y = x + 1; y < b; ++y) {
                    const uint32_t i = spans[x].k, j = spans[y].k;
                    if (state[i].excluded || state[j].excluded || std::abs(bulge(spans[x]) - bulge(spans[y])) > tol)
                        continue;
                    // two of one operand there: a spike out and back the cut left at a corner, the two cancel
                    if (const unsigned char common = state[i].mask & state[j].mask; common != 0) {
                        for (uint32_t k : {i, j}) {
                            state[k].mask &= (unsigned char)~common;
                            state[k].excluded = state[k].mask == 0;
                        }
                        continue;
                    }
                    std::optional<Edge> pa, pb;
                    if (HasPartition(PropsOf(w[i])) || HasPartition(PropsOf(w[j]))) {
                        pa = w[i];
                        pb = w[j];
                    }
                    CutPieces pieces;
                    pieces.bShared = true;
                    Settle(w, grid, state, drop, i, j, CutResult::Congruent, pieces, pa, pb);
                }
            a = b;
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
            void Link(uint32_t a, uint32_t b, unsigned char p) {
                auto [ra, pa] = Find(a);
                auto [rb, pb] = Find(b);
                if (ra == rb) {
                    if ((pa ^ pb) != p)
                        conflict[ra] = 1;
                    return;
                }
                if (size[ra] > size[rb])
                    std::swap(ra, rb);
                parent[ra] = rb;
                parity[ra] = (unsigned char)(pa ^ pb ^ p);
                size[rb] += size[ra];
                conflict[rb] |= conflict[ra];
            }
            bool Conflict(uint32_t root) const { return conflict[root] != 0; }
    };

    // a piece leaving a vertex: its direction there and how far rounding may turn it
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

    // the pieces of one operand at a vertex lie in the sectors between the other operand's pieces, neighbouring sectors
    // on opposite sides of it; a piece whose direction rounding cannot tell from one of the other's stays unlinked
    inline void LinkBySectors(const std::vector<Spoke> &own, const std::vector<Spoke> &other, SideLinks &links) {
        if (other.size() % 2 != 0)
            return;
        bool first = true;
        uint32_t ref = 0;
        size_t refSector = 0;
        for (const Spoke &s : own) {
            size_t below = 0;
            bool sure = true;
            for (const Spoke &o : other) {
                double d = std::abs(s.angle - o.angle);
                d = std::min(d, 2.0 * std::numbers::pi - d);
                sure = sure && d > s.spread + o.spread;
                below += o.angle < s.angle;
            }
            if (!sure)
                continue;
            if (first) {
                ref = s.piece;
                refSector = below;
                first = false;
            } else {
                links.Link(s.piece, ref, (unsigned char)((below ^ refSector) & 1));
            }
        }
    }

    // the side of each piece of one operand near the other (-1: not decided here): the side changes only at the
    // crossings the cut recorded, one inside test per run of linked pieces, on its longest piece
    template <class F>
    std::vector<signed char> SidesByRuns(const Wire &c, const std::vector<CutState> &state, const CutRecord &rec,
                                         const VertexMerges &ids, double tol, F inside) {
        static Timer t("planex Boolean runs");
        Region reg(t);
        const size_t n = c.Size();
        std::vector<signed char> side(n, -1);
        auto single = [&](size_t k) {
            return !state[k].excluded && state[k].needsCutting && (state[k].mask == 1 || state[k].mask == 2);
        };
        std::vector<std::pair<Vertex, uint32_t>> ends;
        for (size_t k = 0; k < n; ++k)
            if (!state[k].excluded && state[k].needsCutting)
                std::visit([&](const auto &x) {
                    ends.emplace_back(x.Start(), (uint32_t)k);
                    ends.emplace_back(x.End(), (uint32_t)k);
                }, c[k]);
        auto byVertex = [](const auto &p, const auto &q) { return VertexLess(p.first, q.first); };
        std::sort(ends.begin(), ends.end(), byVertex);
        std::vector<std::pair<Vertex, PairKind>> through;
        for (const PairEvent &e : rec.events)
            if (e.la == Location::Inside && e.lb == Location::Inside && e.kind != PairKind::Overlap)
                through.emplace_back(ids.Of(e.at), e.kind);
        std::sort(through.begin(), through.end(), byVertex);

        SideLinks links(n);
        std::vector<uint32_t> own[2];
        std::vector<Spoke> spokes[2];
        for (size_t i = 0; i < ends.size();) {
            const Vertex v = ends[i].first;
            size_t j = i;
            bool shared = false;
            own[0].clear();
            own[1].clear();
            for (; j < ends.size() && ends[j].first == v; ++j) {
                const unsigned char m = state[ends[j].second].mask;
                shared = shared || m == 3;
                if (m == 1 || m == 2)
                    own[m - 1].push_back(ends[j].second);
            }
            i = j;
            if (shared)
                continue;
            if (own[0].empty() || own[1].empty()) {
                for (const std::vector<uint32_t> &o : own)
                    for (size_t k = 1; k < o.size(); ++k)
                        links.Link(o[k], o[0], 0);
                continue;
            }
            const auto at = std::equal_range(through.begin(), through.end(), std::pair{v, PairKind::Crossing}, byVertex);
            const bool known = at.first != at.second &&
                               std::all_of(at.first, at.second, [&](const auto &e) { return e.second == at.first->second; });
            if (known && own[0].size() == 2 && own[1].size() == 2) {
                const unsigned char p = at.first->second == PairKind::Crossing ? 1 : 0;
                links.Link(own[0][1], own[0][0], p);
                links.Link(own[1][1], own[1][0], p);
                continue;
            }
            for (size_t o = 0; o < 2; ++o) {
                spokes[o].clear();
                for (uint32_t k : own[o])
                    spokes[o].push_back(std::visit([&](const auto &x) { return SpokeOf(x, v, tol, k); }, c[k]));
            }
            LinkBySectors(spokes[0], spokes[1], links);
            LinkBySectors(spokes[1], spokes[0], links);
        }

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
