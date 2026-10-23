// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

#include "shape/shape.hpp"
#include "core/disjointsets.hpp"

namespace planex2d {

    inline std::string LengthText(double d) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3g", d);
        return buf;
    }

    inline size_t ChainCount(const std::vector<size_t> &id, size_t nv, size_t edges) {
        DisjointSets chain(nv);
        for (size_t i = 0; i < edges; ++i)
            chain.Join(id[2 * i], id[2 * i + 1]);
        size_t count = 0;
        for (size_t v = 0; v < nv; ++v)
            count += chain.IsRoot(v);
        return count;
    }

    inline std::string BranchError(const std::string &call, size_t ends, const Vertex &p) {
        return call + ": " + std::to_string(ends) + " edge ends meet at " + PointText(p) +
               "; a wire is one chain without branches (an 8 is two wires): px.Wires(edges) returns the chains as a list";
    }

    // the edges in order along their single chain, each keeping its own direction; ends within tol are joined
    // bit-exactly, a later edge's end moved onto the earlier edge's. Raises for a branch or several chains (hint: the
    // fix named in the message)
    inline Wire OrderChain(const EdgeList &edges, const AxisSystem &axes, double tol, const std::string &call,
                           const std::string &hint = "px.Wires(edges) returns them as a list, tol= joins ends further apart") {
        Wire w(axes);
        if (edges.empty())
            return w;
        const size_t none = (size_t)-1;
        const std::vector<Vertex> pts = edges.Endpoints();
        std::vector<size_t> id;
        const size_t nv = VertexIdsOf(pts, tol, id);
        std::vector<std::vector<size_t>> at(nv);
        for (size_t k = 0; k < pts.size(); ++k)
            at[id[k]].push_back(k);
        for (const std::vector<size_t> &a : at)
            if (a.size() > 2)
                throw std::invalid_argument(BranchError(call, a.size(), pts[a.front()]));
        if (const size_t chains = ChainCount(id, nv, edges.size()); chains > 1) {
            std::vector<Vertex> open;
            for (const std::vector<size_t> &a : at)
                if (a.size() == 1)
                    open.push_back(pts[a.front()]);
            std::string gap;
            if (open.size() >= 2 && open.size() <= 2000) {
                double d = Norm(open[1] - open[0]);
                for (size_t i = 0; i < open.size(); ++i)
                    for (size_t j = i + 1; j < open.size(); ++j)
                        d = std::min(d, Norm(open[j] - open[i]));
                gap = " (the closest open ends are " + LengthText(d) + " apart)";
            }
            throw std::invalid_argument(call + ": the edges form " + std::to_string(chains) + " chains" + gap +
                                        "; a wire is one chain: " + hint);
        }
        size_t start = none;
        for (size_t k = 0; k < pts.size(); ++k)
            if (at[id[k]].size() == 1 && (start == none || (k % 2 == 0 && start % 2 == 1)))
                start = k;
        if (start == none)
            start = 0;
        std::vector<bool> used(edges.size(), false);
        size_t entry = start;
        Vertex joint = pts[start];
        for (;;) {
            Edge e = edges[entry / 2];
            used[entry / 2] = true;
            if (!(Wire::EndOf(e, entry % 2 == 1) == joint))
                Wire::MoveEnd(e, entry % 2 == 1, joint);
            const size_t leave = entry ^ 1;
            joint = Wire::EndOf(e, leave % 2 == 1);
            w.Add(std::move(e));
            size_t next = none;
            for (size_t k : at[id[leave]])
                if (k != leave)
                    next = k;
            if (next == none)
                break;
            if (used[next / 2]) {
                Edge &last = w[w.Size() - 1];
                if (!(joint == pts[start]))
                    Wire::MoveEnd(last, leave % 2 == 1, pts[start]);
                break;
            }
            entry = next;
        }
        return w;
    }

    // the first two edges of the wire that cross, overlap or touch away from their shared ends (one edge twice: a spline
    // crossing itself), splines by their approximation
    inline std::optional<std::pair<size_t, size_t>> Wire::SelfContact() const {
        Wire flat(axes);
        std::vector<size_t> source;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (const Spline *s = std::get_if<Spline>(&edges[i])) {
                for (const PrimitiveEdge &a : s->Approximation()) {
                    flat.Add(std::visit([](const auto &x) -> Edge { return x; }, a));
                    source.push_back(i);
                }
            } else if (const Circle *c = std::get_if<Circle>(&edges[i])) {
                for (const Arc &half : c->Halves()) {
                    flat.Add(half);
                    source.push_back(i);
                }
            } else {
                flat.Add(edges[i]);
                source.push_back(i);
            }
        }
        if (flat.Size() < 2)
            return std::nullopt;
        const double tol = flat.Tol();
        Grid grid = flat.EdgeGrid();
        std::vector<uint32_t> near;
        for (size_t i = 0; i < flat.Size(); ++i) {
            near.clear();
            grid.InBox(BoxOf(flat[i]).Grown(tol), near);
            for (uint32_t j : near) {
                if (j <= i)
                    continue;
                Edge a = flat[i], b = flat[j];
                CutPieces pieces;
                CutRecord rec;
                if (std::visit([&](auto &x, auto &y) { return Cut(x, y, pieces, rec, tol); }, a, b) != CutResult::Untouched)
                    return std::pair{source[i], source[j]};
            }
        }
        return std::nullopt;
    }

    inline std::string EdgeText(const Edge &e) {
        return "the edge from " + PointText(Wire::EndOf(e, false)) + " to " + PointText(Wire::EndOf(e, true));
    }

    // raises unless the wire's ends meet bit-exactly at most two at a point and it neither crosses nor overlaps itself
    inline void Wire::CheckChain(const std::string &call) const {
        const std::vector<Vertex> pts = Endpoints();
        std::vector<size_t> id;
        std::vector<size_t> count(VertexIdsOf(pts, 0.0, id), 0);
        for (size_t k = 0; k < pts.size(); ++k)
            if (++count[id[k]] > 2) {
                size_t n = 0;
                for (size_t j = 0; j < pts.size(); ++j)
                    n += id[j] == id[k];
                throw std::invalid_argument(BranchError(call, n, pts[k]));
            }
        const std::optional<std::pair<size_t, size_t>> hit = SelfContact();
        if (!hit)
            return;
        const std::string what = hit->first == hit->second ? EdgeText(edges[hit->first]) + " crosses or touches itself"
                                                           : EdgeText(edges[hit->first]) + " and " + EdgeText(edges[hit->second]) +
                                                                 " cross, overlap or touch";
        throw std::invalid_argument(call + ": " + what +
                                    "; a wire neither crosses nor overlaps itself: px.Wires(edges) cuts the edges into clean wires");
    }

    // the one chain of the edges, in any order, joined within tol (default rounding); the edges are never cut
    inline Wire ChainOf(const EdgeList &edges, const AxisSystem &axes, std::optional<double> tol, const std::string &call) {
        Wire w = OrderChain(edges, axes, edges.JoinTol(tol), call);
        w.CheckChain(call);
        return w;
    }

    struct FreeEnd {
        Vertex point;
        size_t edge;
        bool atEnd;
    };
    struct FreeEnds {
        FreeEnd start, end;
        bool closed;
    };

    inline FreeEnds FreeEndsOf(const EdgeList &edges) {
        const size_t n = edges.size();
        auto side = [&](size_t i, size_t neighbour) {
            const Vertex s = Wire::EndOf(edges[i], false), e = Wire::EndOf(edges[i], true);
            const Vertex a = Wire::EndOf(edges[neighbour], false), b = Wire::EndOf(edges[neighbour], true);
            const bool endJoins = e == a || e == b;
            return endJoins ? FreeEnd{s, i, false} : FreeEnd{e, i, true};
        };
        FreeEnds f;
        if (n == 1) {
            f.start = {Wire::EndOf(edges[0], false), 0, false};
            f.end = {Wire::EndOf(edges[0], true), 0, true};
        } else {
            f.start = side(0, 1);
            f.end = side(n - 1, n - 2);
        }
        const bool twoEdgeLoop = n == 2 && (f.start.point == Wire::EndOf(edges[1], false) || f.start.point == Wire::EndOf(edges[1], true));
        f.closed = f.start.point == f.end.point || twoEdgeLoop;
        return f;
    }

    // the chain piece added at a free end of the wire: appended at its end, prepended at its start, in either
    // direction; touching both ends closes the wire. The piece's ends move onto the wire's, the wire's edges stay as
    // they are. Raises, changing nothing, if the wire is closed, the piece touches neither end within tol (default
    // rounding) or the result crosses, overlaps or branches
    inline void Wire::Extend(const EdgeList &piece, std::optional<double> tol, const std::string &call) {
        if (piece.empty())
            return;
        if (edges.empty()) {
            Wire r = *this;
            for (const Edge &e : piece)
                r.Add(e);
            r.CheckChain(call);
            *this = std::move(r);
            return;
        }
        const FreeEnds a = FreeEndsOf(edges);
        if (a.closed)
            throw std::invalid_argument(call + ": the wire is closed, nothing can be added to it; px.Wires(edges) builds "
                                        "several wires from loose edges");
        const FreeEnds b = FreeEndsOf(piece);
        if (b.closed)
            throw std::invalid_argument(call + ": the added wire is closed, it cannot extend another; px.Wires(edges) "
                                        "builds several wires from loose edges");
        EdgeList all = edges;
        all.insert(all.end(), piece.begin(), piece.end());
        const double t = all.JoinTol(tol);
        const double toEndFromStart = Norm(b.start.point - a.end.point), toEndFromEnd = Norm(b.end.point - a.end.point);
        const double toStartFromEnd = Norm(b.end.point - a.start.point), toStartFromStart = Norm(b.start.point - a.start.point);
        const bool atEnd = std::min(toEndFromStart, toEndFromEnd) <= t;
        const bool atStart = std::min(toStartFromEnd, toStartFromStart) <= t;
        if (!atEnd && !atStart) {
            const double d = std::min(std::min(toEndFromStart, toEndFromEnd), std::min(toStartFromEnd, toStartFromStart));
            throw std::invalid_argument(call + ": the added edges' nearest end is " + LengthText(d) +
                                        " from the wire's free ends (joined within " + LengthText(t) +
                                        "); tol= joins a larger gap, px.Wires(edges) builds several wires from loose edges");
        }
        EdgeList p = piece;
        auto snap = [&](const FreeEnd &f, const Vertex &to) {
            if (!(f.point == to))
                Wire::MoveEnd(p[f.edge], f.atEnd, to);
        };
        EdgeList out;
        if (atEnd) {
            const bool forward = toEndFromStart <= toEndFromEnd;
            const FreeEnd &touch = forward ? b.start : b.end, &other = forward ? b.end : b.start;
            snap(touch, a.end.point);
            if (Norm(other.point - a.start.point) <= t)
                snap(other, a.start.point);
            out = edges;
            if (forward)
                out.insert(out.end(), p.begin(), p.end());
            else
                out.insert(out.end(), p.rbegin(), p.rend());
        } else {
            const bool forward = toStartFromEnd <= toStartFromStart;
            snap(forward ? b.end : b.start, a.start.point);
            if (forward)
                out.assign(p.begin(), p.end());
            else
                out.assign(p.rbegin(), p.rend());
            out.insert(out.end(), edges.begin(), edges.end());
        }
        Wire r = *this;
        r.Erase(std::vector<bool>(r.Size(), true));
        for (Edge &e : out)
            r.Add(std::move(e));
        r.CheckChain(call);
        *this = std::move(r);
    }

    // the chains of edges that meet only at shared ends (as BuildLoops splits them), each in order as OrderChain
    inline WireList Wire::OrderedChains() const {
        WireList out;
        const double tol = Tol();
        for (const Wire &w : Wires())
            out.push_back(OrderChain(w.Edges(), axes, tol, "Wires"));
        return out;
    }

    // where the spline crosses or touches itself, each point with the approximation edge it lies on, a point passed
    // twice once for each pass; the joints of consecutive approximation edges do not count
    inline std::vector<std::pair<size_t, Vertex>> SelfCrossings(const Spline &s) {
        const std::span<const PrimitiveEdge> ap = s.Approximation();
        const size_t n = ap.size();
        const double tol = Tol(s.LengthScale());
        std::vector<std::pair<size_t, Vertex>> at;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j) {
                if (!BoxOf(ap[i]).Overlaps(BoxOf(ap[j]), tol))
                    continue;
                const Points on = std::visit([&](const auto &a, const auto &b) { return CommonPoints(a, b, tol); }, ap[i], ap[j]);
                for (const Vertex &x : on) {
                    const Location li = ap[i].Locate(x, tol), lj = ap[j].Locate(x, tol);
                    const bool joint = (j == i + 1 && li == Location::AtEnd && lj == Location::AtStart) ||
                                       (i == 0 && j == n - 1 && li == Location::AtStart && lj == Location::AtEnd);
                    if (joint)
                        continue;
                    at.emplace_back(i, x);
                    at.emplace_back(j, x);
                }
            }
        return at;
    }

    // loose edges as clean wires: a spline crossing itself split there (a loop it closes in two, the core takes no edge
    // that ends where it starts), ends within tol joined, then cut against each
    // other (crossings, T-junctions, an overlapping stretch kept once, ends within rounding snapped), then the chains;
    // nothing is merged
    inline WireList WiresOf(const EdgeList &edges, const AxisSystem &axes, std::optional<double> tol) {
        Wire c(axes);
        for (const Edge &e : edges) {
            if (const Spline *s = std::get_if<Spline>(&e)) {
                std::vector<std::pair<size_t, Vertex>> at = SelfCrossings(*s);
                if (!at.empty()) {
                    for (Spline &piece : s->SplitAt(std::move(at), 0.0)) {
                        if (!(piece.Start() == piece.End())) {
                            c.Add(std::move(piece));
                            continue;
                        }
                        const std::span<const PrimitiveEdge> ap = piece.Approximation();
                        SplitPoints mid;
                        mid.Add(0.0, ap[ap.size() / 2 - 1].End());
                        for (Spline &half : piece.Split(mid, 0.0))
                            c.Add(std::move(half));
                    }
                    continue;
                }
            }
            c.Add(e);
        }
        if (c.Empty())
            return {};
        if (tol)
            c.RemoveSmallGaps(*tol);
        std::vector<CutState> state(c.Size());
        Arrange(c, state);
        return c.OrderedChains();
    }

}
