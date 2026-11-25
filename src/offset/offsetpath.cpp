// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/offset/offsetpath.hpp"

#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace planex2d {

    Vertex LeftNormal(const Edge &e, const Vertex &p) {
        if (const Line *l = std::get_if<Line>(&e)) {
            const Vertex u = Normalized(l->End() - l->Start());
            return Vertex{-u.y, u.x};
        }
        if (const Spline *spline = std::get_if<Spline>(&e)) {
            const Vertex t = std::visit([&](const auto &x) { return x.template Tangent<false>(x.NearestPoint(p)); }, spline->NearestPiece(p));
            return Vertex{-t.y, t.x};
        }
        if (const Circle *c = std::get_if<Circle>(&e)) {
            const Vertex out = Normalized(p - c->Center());
            return c->CCW() ? out * -1.0 : out;
        }
        const Arc &a = std::get<Arc>(e);
        const Vertex out = Normalized(p - a.Center());
        return a.CCW() ? out * -1.0 : out;
    }

    std::optional<Edge> OffsetEdge::Offset(double tol) const {
        const Vertex a = Point(false), b = Point(true);
        if (std::holds_alternative<Line>(edge))
            return Line(a, b);
        if (const Arc *arc = std::get_if<Arc>(&edge)) {
            const double rho = arc->CCW() ? arc->Radius() - s : arc->Radius() + s;
            if (std::abs(rho) <= tol)
                return std::nullopt;
            return Arc(a, b, arc->Center(), arc->CCW());
        }
        const Curve<double, Vertex> c = std::get<Spline>(edge).Segment();
        const auto point = [&](double u) {
            const Vertex t = c.Direction(u);
            return c.At(u) + Vertex{-t.y, t.x} * s;
        };
        Curve<double, Vertex> f = SampledFit(point, c.First(), c.Last(), "Offset", config::state->spline_tol);
        f.poles.front() = a;
        f.poles.back() = b;
        return Spline::FromCurve(f).front();
    }

    void OffsetPath::Add(Edge e, const Properties &p, double tol) {
        if (!edges.empty() && StartOf(e) != EndOf(edges.back())) {
            if (!StartOf(e).IsClose(EndOf(edges.back()), tol))
                throw std::logic_error("Offset: internal error, the raw offset path has a gap of " +
                                       NumberText(Norm(StartOf(e) - EndOf(edges.back())), 17) + " at (" + NumberText(StartOf(e).x, 17) + ", " +
                                       NumberText(StartOf(e).y, 17) + "), tol " + NumberText(tol, 17) + ", after edge " +
                                       std::to_string(edges.size()));
            Wire::MoveEnd(e, false, EndOf(edges.back()), tol);
        }
        SetProps(e, p);
        edges.push_back(std::move(e));
    }

    // inside the corner the path runs through v: the winding cleanup takes that away again
    void JoinCorner(OffsetPath &path, const Vertex &v, const Vertex &a, const Vertex &t1, const Vertex &b, const Vertex &t2, double s,
                    OffsetCorner corner, const JoinProps &cp, double tol) {
        const Properties &p = cp.corner;
        if (a.IsClose(b, tol))
            return;
        const double turn = Cross(t1, t2);
        const bool fold = IsFold(t1, t2);
        if (IsInsideOfCorner(t1, t2, s)) {
            // legs of length s no longer than tol would both be left out, and the gap between a and b with them
            if (std::abs(s) <= tol) {
                path.AddLine(a, b, p, tol);
                return;
            }
            path.AddLine(a, v, p, tol);
            path.AddLine(v, b, p, tol);
            return;
        }
        if (corner == OffsetCorner::Round) {
            path.Add(Arc(a, b, v, fold ? s < 0.0 : turn > 0.0), p, tol);
            return;
        }
        if (corner == OffsetCorner::Sharp && !fold) {
            const Vertex m = a + t1 * (Cross(b - a, t2) / turn);
            path.AddLine(a, m, cp.in, tol);
            path.AddLine(m, b, cp.out, tol);
            return;
        }
        path.AddLine(a, b, p, tol);
    }

    OpenEnd OpenEndOf(const Edge &e, bool atEnd, double d) {
        const OffsetEdge right(e, atEnd ? -d : d), left(e, atEnd ? d : -d);
        return {left.Source(atEnd), atEnd ? left.Heading(true) : -left.Heading(false), right.Point(atEnd), left.Point(atEnd)};
    }

    void Cap(OffsetPath &path, const OpenEnd &at, double d, OffsetEnd end, const Properties &p, const Properties &side, double tol) {
        if (end == OffsetEnd::Round)
            path.Add(Arc(at.right, at.left, at.v, true), p, tol);
        else if (end == OffsetEnd::Flat)
            path.AddLine(at.right, at.left, p, tol);
        else {
            const Vertex a = at.right + at.out * d, b = at.left + at.out * d;
            path.AddLine(at.right, a, side, tol);
            path.AddLine(a, b, p, tol);
            path.AddLine(b, at.left, side, tol);
        }
    }

    static Properties PieceProps(const EdgeList &chain, bool tags, OffsetSource kind, size_t k, size_t edge) {
        return tags ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.layer = OffsetTag(kind, k); })
                    : WithoutPartition(PropsOf(chain[edge]));
    }

    static Properties SpurProps(const EdgeList &chain, bool tags, OffsetSource spur, size_t edge) {
        return PieceProps(chain, tags, std::holds_alternative<Line>(chain[edge]) ? OffsetSource::Edge : spur, edge, edge);
    }

    // s > 0: to the left of the chain
    OffsetPath OffsetSide(const EdgeList &chain, bool closed, double s, OffsetCorner corner, bool tags, double tol) {
        const size_t n = chain.size();
        std::vector<OffsetEdge> offsets;
        for (const Edge &e : chain)
            offsets.emplace_back(e, s);
        OffsetPath path;
        // a join and a shrunk arc start where the path ends: pieces left out within tol must not add up
        const auto end = [&](const Vertex &planned) { return path.edges.empty() ? planned : EndOf(path.edges.back()); };
        for (size_t k = 0; k < n; ++k) {
            if (k > 0 || closed) {
                const size_t before = (k + n - 1) % n;
                JoinCorner(path, StartOf(chain[k]), end(offsets[before].Point(true)), offsets[before].Heading(true), offsets[k].Point(false),
                           offsets[k].Heading(false), s, corner,
                           {PieceProps(chain, tags, OffsetSource::Corner, k, before), SpurProps(chain, tags, OffsetSource::SpurAfter, before),
                            SpurProps(chain, tags, OffsetSource::SpurBefore, k)},
                           tol);
            }
            const Properties p = PieceProps(chain, tags, OffsetSource::Edge, k, k);
            if (std::optional<Edge> e = offsets[k].Offset(tol))
                path.Add(std::move(*e), p, tol);
            else
                path.AddLine(end(offsets[k].Point(false)), offsets[k].Point(true), p, tol);
        }
        if (closed)
            path.Close();
        return path;
    }

    // open: one counterclockwise loop (the right side, the end cap, the left side back, the start cap); closed: the
    // outside's loop counterclockwise, the inside's clockwise
    std::vector<EdgeList> RawBand(EdgeList chain, bool closed, double d, OffsetEnd end, OffsetCorner corner, bool tags, double tol) {
        if (const Circle *c = std::get_if<Circle>(&chain.front())) {
            const Vertex u = (c->Origin() - c->Center()) * (1.0 / c->Radius());
            const Properties p = PieceProps(chain, tags, OffsetSource::Edge, 0, 0);
            Circle outer(c->Center(), c->Center() + u * (c->Radius() + d), true), inner(c->Center(), c->Center() + u * (c->Radius() - d), false);
            outer.props = inner.props = p;
            if (c->Radius() - d <= tol)
                return {{outer}};
            return {{outer}, {inner}};
        }
        if (closed) {
            // the chain is not turned round, so the tags keep its numbers
            const bool ccw = ChainArea(chain) > 0.0;
            OffsetPath outer = OffsetSide(chain, true, ccw ? -d : d, corner, tags, tol), inner = OffsetSide(chain, true, ccw ? d : -d, corner, tags, tol);
            if (!ccw)
                outer.Reverse();
            if (ccw)
                inner.Reverse();
            return {std::move(outer.edges), std::move(inner.edges)};
        }
        const size_t n = chain.size();
        OffsetPath right = OffsetSide(chain, false, -d, corner, tags, tol), left = OffsetSide(chain, false, d, corner, tags, tol);
        Cap(right, OpenEndOf(chain.back(), true, d), d, end, PieceProps(chain, tags, OffsetSource::Cap, n, n - 1),
            SpurProps(chain, tags, OffsetSource::SpurAfter, n - 1), tol);
        left.Reverse();
        for (Edge &e : left.edges) {
            const Properties p = PropsOf(e);
            right.Add(std::move(e), p, tol);
        }
        Cap(right, OpenEndOf(chain.front(), false, d), d, end, PieceProps(chain, tags, OffsetSource::Cap, 0, 0),
            SpurProps(chain, tags, OffsetSource::SpurBefore, 0), tol);
        right.Close();
        return {std::move(right.edges)};
    }

    // from the right of edge i to its left: +1 for each raw edge through its middle running its way, -1 for each
    // running against it; one that only ends there (a piece no longer than about tol) does not count
    int WindingStep(const Wire &cut, size_t i, const Wire &raw, Grid &grid, double tol) {
        const Vertex m = MidPointOf(cut[i]), normal = LeftNormal(cut[i], m);
        const double rounding = tol / config::state->k;
        std::vector<uint32_t> nearby;
        grid.InBox(BBox::Of(m, m).Grown(tol), nearby);
        int step = 0;
        for (uint32_t r : nearby) {
            const Vertex q = NearestPointOf(raw[r], m);
            const bool end = !IsClosedEdge(raw[r]) && (q.IsClose(StartOf(raw[r]), rounding) || q.IsClose(EndOf(raw[r]), rounding));
            if (q.IsClose(m, tol) && !end)
                step += Dot(LeftNormal(raw[r], q), normal) > 0.0 ? 1 : -1;
        }
        return step;
    }

    // the outside is the last entry, 0
    static std::vector<int> FaceWindings(const Wire &c, const Wire &raw, const std::vector<size_t> &faceOf, size_t faces) {
        Grid grid = raw.EdgeGrid();
        const double tol = c.Tol();
        std::vector<std::vector<std::pair<size_t, int>>> next(faces + 1);
        for (size_t i = 0; i < c.Size(); ++i) {
            const int step = WindingStep(c, i, raw, grid, tol);
            const size_t left = faceOf[2 * i], right = faceOf[2 * i + 1];
            next[right].emplace_back(left, step);
            next[left].emplace_back(right, -step);
        }
        constexpr int unknown = std::numeric_limits<int>::min();
        std::vector<int> winding(faces + 1, unknown);
        std::deque<size_t> queue{faces};
        winding[faces] = 0;
        while (!queue.empty()) {
            const size_t f = queue.front();
            queue.pop_front();
            for (auto [g, by] : next[f])
                if (winding[g] == unknown) {
                    winding[g] = winding[f] + by;
                    queue.push_back(g);
                }
        }
        return winding;
    }

    // Clipper's positive fill rule; the kept edges have the region on their left
    Wire PositiveRegion(const Wire &raw) {
        // a spline's offset crosses itself past its tightest bend (a swallowtail)
        Wire c(raw.Axes());
        for (const Edge &e : raw.Edges())
            AddUncrossed(c, e);
        std::vector<CutState> state(c.Size());
        Arrange(c, state);
        const std::vector<HalfEdges> faces = ArrangementFaces(c);
        const std::vector<size_t> faceOf = SideFaces(faces, c.Size());
        const std::vector<int> winding = FaceWindings(c, raw, faceOf, faces.size());
        Wire out(c.Axes());
        for (size_t i = 0; i < c.Size(); ++i) {
            const bool left = winding[faceOf[2 * i]] > 0, right = winding[faceOf[2 * i + 1]] > 0;
            if (left != right)
                out.Add(left ? c[i] : ReversedOf(c[i]));
        }
        out.MergeOnCarriers();
        return out;
    }

}
