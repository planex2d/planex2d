// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/boolean/cutloop.hpp"

#include <algorithm>
#include <utility>

#include "planex/boolean/pairrecord.hpp"

namespace planex2d {

    // the snap after the cut merges within the tolerance of the edges cut here: the cut decides with it too
    static double CutTol(const Wire &w, const std::vector<CutState> &state) {
        double scale = 0.0;
        for (size_t k = 0; k < w.Size(); ++k)
            if (state[k].IsToCut())
                scale = std::max(scale, LengthScaleOf(w[k]));
        return Tol(scale);
    }

    // one meeting point makes two halves; a circle nothing meets stays one edge. Two congruent circles meet at no point:
    // Cut shares them
    void CutCircles(Wire &w, std::vector<CutState> &state) {
        // a closed spline that may be cut goes in as its two halves, as the core cuts them; the merge of the pieces
        // afterwards (MergeOnCarriers) makes it one edge again where nothing cut it
        std::vector<bool> halve(w.Size());
        for (size_t k = 0; k < w.Size(); ++k)
            halve[k] = state[k].IsToCut();
        if (const std::vector<size_t> from = w.HalveClosedSplines(halve); !from.empty()) {
            std::vector<CutState> split;
            for (size_t k : from)
                split.push_back(state[k]);
            state = std::move(split);
        }
        if (!w.Edges().HasCircle())
            return;
        const double tol = CutTol(w, state);
        Grid grid = w.EdgeGrid();
        std::vector<std::vector<Vertex>> at(w.Size());
        std::vector<uint32_t> nearby;
        for (size_t i = 0; i < w.Size(); ++i) {
            const Circle *c = std::get_if<Circle>(&w[i]);
            if (!c || !state[i].IsToCut())
                continue;
            nearby.clear();
            grid.InBox(BoxOf(w[i]).Grown(tol), nearby);
            for (uint32_t j : nearby) {
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

    // a piece keeps the partition fractions of its parent that lie inside it, measured along the piece
    void RemapPartition(Edge &piece, const Edge &parent) {
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

    // the one copy Cut leaves of a shared stretch: an open wire cancels nothing, the copy is the closed
    // edge's stretch as it is (both open: either)
    struct SharedStretch {
        bool drop, openA, bothOpen, cancel, bFirst, gone;
        Properties merged, kept;

        SharedStretch(const Edge &a, const CutState &sa, const Edge &b, const CutState &sb, CutResult r, bool drop)
            : drop(drop), openA(sa.open), bothOpen(sa.open && sb.open), cancel(!sa.open && !sb.open),
              bFirst((sb.mask & 1) && !(sa.mask & 1)), gone(bothOpen ? sa.drop || sb.drop : openA ? sb.drop : sa.drop) {
            if (r != CutResult::Crossing)
                merged = bFirst ? MergedProperties(PropsOf(b), PropsOf(a)) : MergedProperties(PropsOf(a), PropsOf(b));
            kept = cancel || bothOpen ? merged : openA ? PropsOf(b) : PropsOf(a);
        }
        void Mark(Edge &e, CutState &s, unsigned char other) const {
            if (!drop) {
                s.mask ^= other;
            } else if (cancel) {
                s.drop = true;
            } else {
                s.open = bothOpen;
                s.drop = gone;
            }
            SetProps(e, drop ? kept : merged);
        }
        const Edge &PartitionSource(const Edge &pa, const Edge &pb) const {
            const Edge &first = bFirst ? pb : pa, &second = bFirst ? pa : pb;
            if (!drop || cancel || bothOpen)
                return HasPartition(PropsOf(first)) ? first : second;
            return openA ? pb : pa;
        }
    };

    CutState &CutLoop::Append(Edge &e, unsigned char mask, size_t parent, bool open) {
        w.Append(std::move(e), w.Owner(parent));
        grid.Insert(w.Size() - 1, BoxOf(w[w.Size() - 1]));
        return state.emplace_back(CutState{.mask = mask, .open = open, .part = state[parent].part});
    }

    // what the cut of w[i] and w[j] leaves: the pieces appended, a shared stretch kept once (drop: as InitialClean
    // keeps it; an operand along it twice cancels there, as in ShareCongruent); pa, pb: the edges before the cut where
    // one has a partition
    void CutLoop::Settle(size_t i, size_t j, CutResult r, CutPieces &pieces, const std::optional<Edge> &pa,
                         const std::optional<Edge> &pb) {
        const size_t firstNew = w.Size();
        const unsigned char ma = state[i].mask, mb = state[j].mask;
        const bool openA = state[i].open, openB = state[j].open;
        const SharedStretch shared(w[i], state[i], w[j], state[j], r, drop);
        if (pieces.AShared(0))
            shared.Mark(w[i], state[i], mb);
        if (pieces.bShared)
            shared.Mark(w[j], state[j], ma);
        if (r == CutResult::Congruent)
            state[i].excluded = true;
        for (size_t k = 0; k < pieces.a.size(); ++k) {
            CutState &s = Append(pieces.a[k], ma, i, openA);
            if (pieces.AShared(k + 1))
                shared.Mark(w[w.Size() - 1], s, mb);
        }
        for (Edge &e : pieces.b)
            Append(e, mb, j, openB);
        if (pa) {
            const Edge &sharedSource = shared.PartitionSource(*pa, *pb);
            RemapPartition(w[i], pieces.AShared(0) ? sharedSource : *pa);
            RemapPartition(w[j], pieces.bShared ? sharedSource : *pb);
            for (size_t k = 0; k < pieces.a.size(); ++k)
                RemapPartition(w[firstNew + k], pieces.AShared(k + 1) ? sharedSource : *pa);
            for (size_t k = 0; k < pieces.b.size(); ++k)
                RemapPartition(w[firstNew + pieces.a.size() + k], *pb);
        }
    }

    // every pair once in index order; then the pieces of edges split at a point off them once more against every edge
    // near them: they turned by up to tol, and what was decided on the edge before may not hold for them
    void CutLoop::CutAll(CutRecord &rec) {
        if (w.Size() < 2)
            return;
        const double tol = CutTol(w, state);
        std::vector<bool> off;
        const auto cut = [&](size_t i, size_t j) {
            std::optional<Edge> pa, pb;
            if (HasPartition(PropsOf(w[i])) || HasPartition(PropsOf(w[j]))) {
                pa = w[i];
                pb = w[j];
            }
            CutPieces pieces;
            CutResult r = std::visit([&](auto &a, auto &b) { return Cut(a, b, pieces, rec, tol); }, w[i], w[j]);
            if (r == CutResult::Untouched)
                return;
            const size_t firstNew = w.Size(), firstB = firstNew + pieces.a.size();
            Settle(i, j, r, pieces, pa, pb);
            off.resize(w.Size(), false);
            if (pieces.aOff) {
                off[i] = true;
                std::fill(off.begin() + (std::ptrdiff_t)firstNew, off.begin() + (std::ptrdiff_t)firstB, true);
            }
            if (pieces.bOff) {
                off[j] = true;
                std::fill(off.begin() + (std::ptrdiff_t)firstB, off.end(), true);
            }
        };
        std::vector<uint32_t> nearby;
        for (size_t i = 0; i < w.Size(); ++i) {
            if (!state[i].IsToCut())
                continue;
            nearby.clear();
            grid.InBox(BoxOf(w[i]).Grown(tol), nearby);
            std::sort(nearby.begin(), nearby.end());
            for (uint32_t j : nearby) {
                if (state[i].excluded)
                    break;
                if (j <= i || !state[i].CanCut(state[j]))
                    continue;
                cut(i, j);
            }
        }
        // at most three more passes: a stack of edges within tol of each other settles within them
        for (int pass = 0; pass < 3 && std::find(off.begin(), off.end(), true) != off.end(); ++pass) {
            const std::vector<bool> again = std::exchange(off, std::vector<bool>(w.Size(), false));
            for (size_t i = 0; i < again.size(); ++i) {
                if (!again[i] || !state[i].IsToCut())
                    continue;
                nearby.clear();
                grid.InBox(BoxOf(w[i]).Grown(tol), nearby);
                std::sort(nearby.begin(), nearby.end());
                for (uint32_t j : nearby) {
                    if (state[i].excluded)
                        break;
                    if (j == i || (j < again.size() && again[j] && j < i) || !state[i].CanCut(state[j]))
                        continue;
                    cut(i, j);
                }
            }
        }
    }

    void CutAll(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, CutRecord *record) {
        static Timer t("planex CutAll");
        Region reg(t);
        CutRecord own;
        CutLoop(w, grid, state, drop).CutAll(record ? *record : own);
    }

    // edges of two inputs between the same two vertices and within tol of each other halfway are one edge, kept as a
    // congruent cut keeps it: the cut of another pair made them after their own pair was decided. Two of one input
    // there cancel
    void CutLoop::ShareCongruent(double tol) {
        const std::vector<EdgeSpan> spans = SortedSpans(w, [&](size_t k) { return state[k].IsToCut(); });
        auto bulge = [&](const EdgeSpan &s) {
            const Vertex m = MidPointOf(w[s.k]);
            const Vertex d = s.hi - s.lo;
            return Cross(d, m - (s.lo + s.hi) * 0.5) / Norm(d);
        };
        for (size_t a = 0; a < spans.size();) {
            const size_t b = SameSpansEnd(spans, a);
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
                    Settle(i, j, CutResult::Congruent, pieces, pa, pb);
                }
            a = b;
        }
    }

    void ShareCongruent(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, double tol) {
        CutLoop(w, grid, state, drop).ShareCongruent(tol);
    }

}
