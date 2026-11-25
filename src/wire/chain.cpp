// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/chain.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace planex2d {

    ChainWalk WalkChain(const EdgeList &edges, double tol, const std::string &call, const std::string &hint) {
        ChainWalk walk;
        if (edges.empty())
            return walk;
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
        for (size_t entry = start;;) {
            used[entry / 2] = true;
            walk.edges.push_back({entry / 2, entry % 2 == 0});
            const size_t leave = entry ^ 1;
            size_t next = none;
            for (size_t k : at[id[leave]])
                if (k != leave)
                    next = k;
            if (next == none)
                break;
            if (used[next / 2]) {
                walk.closed = true;
                break;
            }
            entry = next;
        }
        return walk;
    }

    // ends within tol are joined bit-exactly, a later edge's end moved onto the earlier edge's
    Wire OrderChain(const EdgeList &edges, const AxisSystem &axes, double tol, const std::string &call,
                    const std::string &hint) {
        Wire w(axes);
        const ChainWalk walk = WalkChain(edges, tol, call, hint);
        if (walk.edges.empty())
            return w;
        const double rounding = edges.Tol();
        const Vertex first = Wire::EndOf(edges[walk.edges.front().first], !walk.edges.front().second);
        Vertex joint = first;
        for (const auto &[k, along] : walk.edges) {
            Edge e = edges[k];
            if (!(Wire::EndOf(e, !along) == joint))
                Wire::MoveEnd(e, !along, joint, rounding, tol);
            joint = Wire::EndOf(e, along);
            w.Add(std::move(e));
        }
        if (walk.closed && !(joint == first))
            Wire::MoveEnd(w[w.Size() - 1], walk.edges.back().second, first, rounding, tol);
        return w;
    }

    WireList Wire::OrderedChains() const {
        WireList out;
        const double tol = Tol();
        for (const Wire &w : Wires())
            out.push_back(OrderChain(w.Edges(), axes, tol, "Wires"));
        return out;
    }

    // one edge twice: a spline crossing itself; splines by their approximation
    std::optional<std::pair<size_t, size_t>> Wire::SelfContact() const {
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
        std::vector<Vertex> ends;
        for (size_t i = 0; i < flat.Size(); ++i)
            for (const bool end : {false, true})
                ends.push_back(Wire::EndOf(flat[i], end));
        std::vector<size_t> at;
        std::vector<size_t> count(VertexIdsOf(ends, tol, at), 0);
        for (size_t k = 0; k < ends.size(); ++k)
            if (++count[at[k]] > 2)
                return std::pair{source[(size_t)(std::find(at.begin(), at.end(), at[k]) - at.begin()) / 2], source[k / 2]};
        Grid grid = flat.EdgeGrid();
        std::vector<uint32_t> nearby;
        for (size_t i = 0; i < flat.Size(); ++i) {
            nearby.clear();
            grid.InBox(BoxOf(flat[i]).Grown(tol), nearby);
            for (uint32_t j : nearby) {
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

    void Wire::CheckChain(const std::string &call) const {
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

    FreeEnds FreeEndsOf(const EdgeList &edges) {
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

    void Wire::Extend(const EdgeList &piece, std::optional<double> tol, const std::string &call) {
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
                Wire::MoveEnd(p[f.edge], f.atEnd, to, all.Tol(), t);
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

    // each point with the approximation edge it lies on, a point passed twice once for each pass; the joints of
    // consecutive approximation edges do not count
    std::vector<std::pair<size_t, Vertex>> SelfCrossings(const Spline &s) {
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

    // a spline crossing itself is split there, a loop it closes in two: the core takes no edge that ends where it starts
    void AddUncrossed(Wire &c, const Edge &e) {
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
                return;
            }
        }
        c.Add(e);
    }

    WireList WiresOf(const EdgeList &edges, const AxisSystem &axes, std::optional<double> tol) {
        Wire c(axes);
        for (const Edge &e : edges)
            AddUncrossed(c, e);
        if (c.Empty())
            return {};
        if (tol)
            c.RemoveSmallGaps(*tol);
        std::vector<CutState> state(c.Size());
        Arrange(c, state);
        return c.OrderedChains();
    }

}
