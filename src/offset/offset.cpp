// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/offset/offset.hpp"

#include <cmath>
#include <functional>
#include <queue>
#include <stdexcept>
#include <vector>

namespace planex2d {

    static void AddRegion(OffsetPath path, bool clean, const AxisSystem &axes, WireList &pieces) {
        path.Close();
        if (ChainArea(path.edges) < 0.0)
            path.Reverse();
        Wire w(axes);
        for (Edge &e : path.edges)
            w.Add(std::move(e));
        pieces.push_back(clean ? PositiveRegion(w) : std::move(w));
    }

    static bool CanCrossItself(const Edge &e, double d, double tol) {
        const Arc *arc = std::get_if<Arc>(&e);
        return std::holds_alternative<Spline>(e) || (arc && arc->Radius() <= d + tol);
    }

    static void EdgeBands(const EdgeList &chain, double d, OffsetCorner corner, const AxisSystem &axes, double tol, WireList &pieces) {
        for (const Edge &e : chain) {
            OffsetPath band;
            band.edges = RawBand({e}, false, d, OffsetEnd::Flat, corner, false, tol).front();
            AddRegion(std::move(band), CanCrossItself(e, d, tol), axes, pieces);
        }
    }

    static void CornerPieces(const EdgeList &chain, bool closed, double d, OffsetCorner corner, const AxisSystem &axes, double tol,
                             WireList &pieces) {
        const size_t n = chain.size();
        for (size_t k = closed ? 0 : 1; k < n; ++k) {
            const size_t before = (k + n - 1) % n;
            const Vertex v = StartOf(chain[k]);
            const Properties p = WithoutPartition(PropsOf(chain[before]));
            for (double s : {d, -d}) {
                const OffsetEdge in(chain[before], s), out(chain[k], s);
                if (IsInsideOfCorner(in.Heading(true), out.Heading(false), s) || in.Point(true).IsClose(out.Point(false), tol))
                    continue;
                OffsetPath piece;
                piece.AddLine(v, in.Point(true), p, tol);
                JoinCorner(piece, v, in.Point(true), in.Heading(true), out.Point(false), out.Heading(false), s, corner, {p, p, p}, tol);
                piece.AddLine(out.Point(false), v, p, tol);
                AddRegion(std::move(piece), false, axes, pieces);
            }
        }
    }

    static void CapPieces(const EdgeList &chain, double d, OffsetEnd end, const AxisSystem &axes, double tol, WireList &pieces) {
        for (bool atEnd : {true, false}) {
            const Edge &e = atEnd ? chain.back() : chain.front();
            const OpenEnd at = OpenEndOf(e, atEnd, d);
            const Properties p = WithoutPartition(PropsOf(e));
            OffsetPath cap;
            Cap(cap, at, d, end, p, p, tol);
            cap.AddLine(at.left, at.right, nullptr, tol);
            AddRegion(std::move(cap), false, axes, pieces);
        }
    }

    void BandPieces(const EdgeList &chain, bool closed, double d, OffsetEnd end, OffsetCorner corner, const AxisSystem &axes, double tol,
                    WireList &pieces) {
        if (std::holds_alternative<Circle>(chain.front())) {
            Wire ring(axes);
            for (EdgeList &loop : RawBand(chain, true, d, end, corner, false, tol))
                for (Edge &e : loop)
                    ring.Add(std::move(e));
            pieces.push_back(std::move(ring));
            return;
        }
        EdgeBands(chain, d, corner, axes, tol, pieces);
        CornerPieces(chain, closed, d, corner, axes, tol, pieces);
        if (!closed && end != OffsetEnd::Flat)
            CapPieces(chain, d, end, axes, tol, pieces);
    }

    Wire Wire::Offset(double d, OffsetEnd ends, OffsetCorner corners) const {
        if (!(d > 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be positive and finite");
        const double tol = planex2d::Tol(Box().NormInf() + d);
        WireList pieces;
        for (const Loop &loop : Loops()) {
            EdgeList chain;
            for (auto [i, along] : loop.edges)
                chain.push_back(along ? edges[i] : ReversedOf(edges[i]));
            BandPieces(chain, loop.closed, d, ends, corners, axes, tol, pieces);
        }
        Wire result = BalancedBoolean(std::move(pieces), BooleanOp::Union, true);
        result.SetAxes(axes);
        return result;
    }

    // d > 0 outside, < 0 inside
    Wire LoopOffset(const EdgeList &chain, double d, OffsetCorner corners, const AxisSystem &axes, double tol) {
        Wire out(axes);
        if (const Circle *c = std::get_if<Circle>(&chain.front())) {
            if (c->Radius() + d > tol) {
                Circle o(c->Center(), c->Center() + (c->Origin() - c->Center()) * ((c->Radius() + d) / c->Radius()), true);
                o.props = WithoutPartition(PropsOf(chain.front()));
                out.Add(o);
            }
            return out;
        }
        const bool ccw = ChainArea(chain) > 0.0;
        OffsetPath path = OffsetSide(chain, true, ccw ? -d : d, corners, false, tol);
        if (!ccw)
            path.Reverse();
        for (Edge &e : path.edges)
            out.Add(std::move(e));
        return PositiveRegion(out);
    }

    EdgeList InRunOrder(const EdgeList &pieces, double tol) {
        const size_t n = pieces.size();
        std::vector<Vertex> starts(n), ends(n);
        for (size_t k = 0; k < n; ++k) {
            starts[k] = StartOf(pieces[k]);
            ends[k] = EndOf(pieces[k]);
        }
        const Grid startGrid = Grid::Of(starts, tol), endGrid = Grid::Of(ends, tol);
        std::vector<size_t> predecessors(n, 0);
        std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> runStarts;
        for (size_t k = 0; k < n; ++k) {
            endGrid.ForNear(starts[k], [&](size_t j) {
                if (ends[j].IsClose(starts[k], tol))
                    ++predecessors[k];
            });
            if (predecessors[k] == 0)
                runStarts.push(k);
        }
        std::vector<bool> used(n, false);
        EdgeList out;
        size_t first = 0;
        while (out.size() < n) {
            while (!runStarts.empty() && used[runStarts.top()])
                runStarts.pop();
            while (used[first])
                ++first;
            size_t k = runStarts.empty() ? first : runStarts.top();
            while (k < n) {
                used[k] = true;
                out.push_back(pieces[k]);
                size_t next = n;
                startGrid.ForNear(ends[k], [&](size_t j) {
                    if (!starts[j].IsClose(ends[k], tol))
                        return;
                    if (--predecessors[j] == 0 && !used[j])
                        runStarts.push(j);
                    if (!used[j] && j < next)
                        next = j;
                });
                k = next;
            }
        }
        return out;
    }

    // s > 0: the chain's left, < 0: its right
    EdgeList OpenSideOffset(const EdgeList &chain, double s, OffsetCorner corners, const AxisSystem &axes, double tol) {
        OffsetPath path = OffsetSide(chain, false, s, corners, false, tol);
        if (path.edges.empty())
            return {};
        const Properties back = ChangedProperties(nullptr, [](ShapeProperties &q) { q.layer = -1; });
        const Vertex first = StartOf(path.edges.front());
        path.AddLine(EndOf(path.edges.back()), EndOf(chain.back()), back, tol);
        for (size_t k = chain.size(); k-- > 0;)
            path.Add(ReversedOf(chain[k]), back, tol);
        path.AddLine(StartOf(chain.front()), first, back, tol);
        path.Close();
        // on the left the loop runs clockwise, back along the chain on its right
        if (s > 0.0)
            path.Reverse();
        Wire raw(axes);
        for (Edge &e : path.edges)
            raw.Add(std::move(e));
        EdgeList pieces;
        for (const Edge &e : PositiveRegion(raw))
            if (PropsOf(e) != back)
                pieces.push_back(s > 0.0 ? ReversedOf(e) : e);
        return InRunOrder(pieces, tol);
    }

    // one side: d > 0 left of an open chain and outside a closed one, d < 0 right and inside
    Wire OffsetCurves(std::span<const Wire> wires, double d, std::optional<bool> bothSides, OffsetEnd ends, OffsetCorner corners) {
        if (!(d != 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be nonzero and finite");
        BBox box;
        for (const Wire &w : wires)
            box.Grow(w.Box());
        const double tol = planex2d::Tol(box.NormInf() + std::abs(d));
        const AxisSystem &axes = wires.front().Axes();
        WireList pieces;
        Wire out(axes);
        for (const Wire &w : wires)
            for (const Loop &loop : w.Loops()) {
                EdgeList chain;
                for (auto [i, along] : loop.edges)
                    chain.push_back(along ? w[i] : ReversedOf(w[i]));
                if (bothSides.value_or(!loop.closed)) {
                    BandPieces(chain, loop.closed, std::abs(d), ends, corners, axes, tol, pieces);
                    continue;
                }
                const EdgeList side = loop.closed ? LoopOffset(chain, d, corners, axes, tol).Edges() : OpenSideOffset(chain, d, corners, axes, tol);
                for (const Edge &e : side)
                    out.Add(e);
            }
        if (!pieces.empty())
            for (const Edge &e : BalancedBoolean(std::move(pieces), BooleanOp::Union, true))
                out.Add(e);
        Wire ordered(axes);
        for (Edge &e : InRunOrder(out.Edges(), tol))
            ordered.Add(std::move(e));
        return ordered;
    }

    Wire Line::Offset(double d) const { return EdgeOffset(*this, d); }

    Wire Arc::Offset(double d) const { return EdgeOffset(*this, d); }

    Wire Circle::Offset(double d) const { return EdgeOffset(*this, d); }

    Wire Spline::Offset(double d) const { return EdgeOffset(*this, d); }

    Wire Wire::OffsetCurves(double d, std::optional<bool> bothSides, OffsetEnd ends, OffsetCorner corners) const {
        return planex2d::OffsetCurves(std::span<const Wire>(this, 1), d, bothSides, ends, corners);
    }

}
