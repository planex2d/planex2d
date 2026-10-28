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

#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "core/grid.hpp"
#include "wire/crossing.hpp"
#include "wire/wire.hpp"
#include "boolean/cut.hpp"
#include "core/timer.hpp"

namespace planex2d {

    struct CutState {
        static constexpr uint32_t noPart = std::numeric_limits<uint32_t>::max();

        unsigned char mask = 0;  // 1 = A, 2 = B
        bool needsCutting = true;
        bool excluded = false;
        bool drop = false;
        bool open = false;
        uint32_t part = noPart;

        // edges of one clean part (a Glue input) meet only at their shared ends, the cut leaves such pairs alone
        bool IsSamePart(const CutState &o) const { return part != noPart && part == o.part; }
        // o is cut against this one
        bool CanCut(const CutState &o) const { return !o.excluded && o.needsCutting && (mask & o.mask) == 0 && !IsSamePart(o); }
    };

    // the circles the edges near them meet are cut into arcs at the meeting points, one point making two halves; a
    // circle nothing meets stays one edge. Two congruent circles meet at no point: Cut shares them. The state of a
    // circle goes to its arcs
    inline void CutCircles(Wire &w, std::vector<CutState> &state) {
        // a closed spline that may be cut goes in as its two halves, as the core cuts them; the merge of the pieces
        // afterwards (MergeOnCarriers) makes it one edge again where nothing cut it
        std::vector<bool> halve(w.Size());
        for (size_t k = 0; k < w.Size(); ++k)
            halve[k] = !state[k].excluded && state[k].needsCutting;
        if (const std::vector<size_t> from = w.HalveClosedSplines(halve); !from.empty()) {
            std::vector<CutState> split;
            for (size_t k : from)
                split.push_back(state[k]);
            state = std::move(split);
        }
        if (!w.Edges().HasCircle())
            return;
        double scale = 0.0;
        for (size_t k = 0; k < w.Size(); ++k)
            if (!state[k].excluded && state[k].needsCutting)
                scale = std::max(scale, LengthScaleOf(w[k]));
        const double tol = Tol(scale);
        Grid grid = w.EdgeGrid();
        std::vector<std::vector<Vertex>> at(w.Size());
        std::vector<uint32_t> near;
        for (size_t i = 0; i < w.Size(); ++i) {
            const Circle *c = std::get_if<Circle>(&w[i]);
            if (!c || state[i].excluded || !state[i].needsCutting)
                continue;
            near.clear();
            grid.InBox(BoxOf(w[i]).Grown(tol), near);
            for (uint32_t j : near) {
                const Circle *other = std::get_if<Circle>(&w[j]);
                if (j == i || (other && j < i) || !state[i].CanCut(state[j]) || (other && c->IsCongruent(*other, tol)))
                    continue;
                const Meetings m = MeetingsOf(w[i], w[j], tol);
                at[i].insert(at[i].end(), m.a.begin(), m.a.end());
                if (other)
                    at[j].insert(at[j].end(), m.b.begin(), m.b.end());
            }
        }
        const std::vector<size_t> from = w.SplitCircles(at, tol);
        if (from.empty())
            return;
        std::vector<CutState> split;
        for (size_t k : from)
            split.push_back(state[k]);
        state = std::move(split);
    }

    // parity of the ray from m per operand: bit 1 odd for A, bit 2 for B; only edges with a parent in wanted count
    template <bool vertical, bool onboundary>
    unsigned char CrossingParity(Grid &grid, const Wire &c, const std::vector<CutState> &state, const Vertex &m,
                                 unsigned char wanted) {
        static Timer t("planex Crossing", false);
        Region reg(t);
        unsigned char odd = 0;
        grid.ForRay<vertical>(m, [&](uint32_t k) {
            const CutState &s = state[k];
            if (s.excluded || !(s.mask & wanted))
                return;
            if (RayParity<vertical, onboundary>(c[k], m))
                odd ^= s.mask;
        });
        return odd;
    }

    // a piece keeps the partition fractions of its parent that lie inside it, measured along the piece
    inline void RemapPartition(Edge &piece, const Edge &parent) {
        const Properties &p = PropsOf(piece);
        if (!HasPartition(p))
            return;
        auto [s, e] = std::visit([](const auto &x) { return std::pair<Vertex, Vertex>{x.Start(), x.End()}; }, piece);
        double t0 = LengthFraction(parent, s), t1 = LengthFraction(parent, e);
        std::vector<double> f;
        for (double x : *p->partition)
            if (double y = (x - t0) / (t1 - t0); y > 0.0 && y < 1.0)
                f.push_back(y);
        std::sort(f.begin(), f.end());
        SetProps(piece, ChangedProperties(p, [&](ShapeProperties &q) {
            if (f.empty())
                q.partition.reset();
            else
                q.partition = std::move(f);
        }));
    }

    // what the cut of w[i] and w[j] leaves: the pieces appended, a shared stretch kept once (drop: as InitialClean
    // keeps it); pa, pb: the edges before the cut where one has a partition
    inline void Settle(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, size_t i, size_t j, CutResult r,
                       CutPieces &pieces, const std::optional<Edge> &pa, const std::optional<Edge> &pb) {
        auto append = [&](Edge &e, unsigned char mask, size_t parent, bool open) -> CutState & {
            w.Append(std::move(e), w.Owner(parent));
            grid.Insert(w.Size() - 1, BoxOf(w[w.Size() - 1]));
            return state.emplace_back(CutState{.mask = mask, .open = open, .part = state[parent].part});
        };
        const size_t firstNew = w.Size();
        const unsigned char ma = state[i].mask;
        const unsigned char mb = state[j].mask;
        const bool openA = state[i].open, openB = state[j].open;
        const bool bFirst = (mb & 1) && !(ma & 1);
        Properties merged;
        if (r != CutResult::Crossing)
            merged = bFirst ? MergedProperties(PropsOf(w[j]), PropsOf(w[i])) : MergedProperties(PropsOf(w[i]), PropsOf(w[j]));
        // the one copy Cut leaves of a shared stretch: an open wire cancels nothing, the copy is the closed
        // edge's stretch as it is (both open: either)
        const bool cancel = !openA && !openB;
        const bool gone = openA && openB ? state[i].drop || state[j].drop : openA ? state[j].drop : state[i].drop;
        const Properties kept = cancel || (openA && openB) ? merged : openA ? PropsOf(w[j]) : PropsOf(w[i]);
        auto share = [&](Edge &e, CutState &s, unsigned char other) {
            if (!drop) {
                s.mask |= other;
            } else if (cancel) {
                s.drop = true;
            } else {
                s.open = openA && openB;
                s.drop = gone;
            }
            SetProps(e, drop ? kept : merged);
        };
        if (pieces.AShared(0))
            share(w[i], state[i], mb);
        if (pieces.bShared)
            share(w[j], state[j], ma);
        if (r == CutResult::Congruent)
            state[i].excluded = true;
        for (size_t k = 0; k < pieces.a.size(); ++k) {
            CutState &s = append(pieces.a[k], ma, i, openA);
            if (pieces.AShared(k + 1))
                share(w[w.Size() - 1], s, mb);
        }
        for (Edge &e : pieces.b)
            append(e, mb, j, openB);
        if (pa) {
            const Edge &first = bFirst ? *pb : *pa, &second = bFirst ? *pa : *pb;
            const Edge &sharedSource = !drop || cancel || (openA && openB) ? (HasPartition(PropsOf(first)) ? first : second)
                                                                             : openA ? *pb : *pa;
            RemapPartition(w[i], pieces.AShared(0) ? sharedSource : *pa);
            RemapPartition(w[j], pieces.bShared ? sharedSource : *pb);
            for (size_t k = 0; k < pieces.a.size(); ++k)
                RemapPartition(w[firstNew + k], pieces.AShared(k + 1) ? sharedSource : *pa);
            for (size_t k = 0; k < pieces.b.size(); ++k)
                RemapPartition(w[firstNew + pieces.a.size() + k], *pb);
        }
    }

    inline void CutAll(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, CutRecord *record = nullptr) {
        static Timer t("planex CutAll");
        Region reg(t);
        CutRecord own;
        CutRecord &rec = record ? *record : own;
        if (w.Size() < 2)
            return;
        // the snap after the cut merges within the tolerance of the edges cut here: the cut decides with it too
        double scale = 0.0;
        for (size_t k = 0; k < w.Size(); ++k)
            if (!state[k].excluded && state[k].needsCutting)
                scale = std::max(scale, LengthScaleOf(w[k]));
        const double tol = Tol(scale);
        std::vector<uint32_t> near;
        for (size_t i = 0; i < w.Size(); ++i) {
            if (state[i].excluded || !state[i].needsCutting)
                continue;
            near.clear();
            grid.InBox(BoxOf(w[i]).Grown(tol), near);
            std::sort(near.begin(), near.end());
            for (uint32_t j : near) {
                if (state[i].excluded)
                    break;
                if (j <= i || !state[i].CanCut(state[j]))
                    continue;
                std::optional<Edge> pa, pb;
                if (HasPartition(PropsOf(w[i])) || HasPartition(PropsOf(w[j]))) {
                    pa = w[i];
                    pb = w[j];
                }
                CutPieces pieces;
                CutResult r = std::visit([&](auto &a, auto &b) { return Cut(a, b, pieces, rec, tol); }, w[i], w[j]);
                if (r != CutResult::Untouched)
                    Settle(w, grid, state, drop, i, j, r, pieces, pa, pb);
            }
        }
    }

}
