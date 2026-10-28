// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

#include "boolean/arrangement.hpp"
#include "boolean/cutloop.hpp"
#include "curves/corner.hpp"
#include "curves/parametricfit.hpp"
#include "offset/offsetform.hpp"
#include "wire/chain.hpp"
#include "wire/wire.hpp"

namespace planex2d {

    // what a piece of a raw offset comes from, for the sketch's offset: the offset of edge k, the corner at vertex k, the
    // cap at the chain's end vertex k, or the straight piece along a curved edge k's end tangent before its offset
    // (a sharp corner at its start, a square cap) or after it; written as its layer 5 k + kind
    enum class OffsetSource { Edge, Corner, Cap, SpurBefore, SpurAfter };
    inline int OffsetTag(OffsetSource kind, size_t k) { return 5 * (int)k + (int)kind; }
    inline OffsetSource SourceOfTag(int tag) { return (OffsetSource)(tag % 5); }
    inline size_t IndexOfTag(int tag) { return (size_t)(tag / 5); }

    // the unit normal to the left of a line, arc or spline at its point p
    inline Vertex LeftNormal(const Edge &e, const Vertex &p) {
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

    // the area a closed chain encloses, positive counterclockwise
    inline double ChainArea(const EdgeList &chain) {
        const Vertex o = StartOf(chain.front());
        double a = 0.0;
        for (const Edge &e : chain)
            a += SignedArea(e, o);
        return a;
    }

    // an edge of a chain offset by s (> 0 to its left): where its offset starts and ends, and the offset between
    class OffsetEdge {
        private:
            const Edge &edge;
            ExactCurve curve;
            double s;

        public:
            OffsetEdge(const Edge &e, double side) : edge(e), curve(e), s(side) {}

            // the unit direction along the edge at its start or end
            Vertex Heading(bool atEnd) const { return curve.Direction(atEnd ? curve.Last() : curve.First()); }
            Vertex Source(bool atEnd) const { return atEnd ? EndOf(edge) : StartOf(edge); }
            Vertex Point(bool atEnd) const {
                const Vertex t = Heading(atEnd);
                return Source(atEnd) + Vertex{-t.y, t.x} * s;
            }
            // none where it shrinks to a point (an arc of radius s offset towards its centre); a spline's: one spline
            // through the exact offset within spline_tol, its ends exactly at Point
            std::optional<Edge> Offset(double tol) const {
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
    };

    // a raw offset path: edges appended end to start, each new one moved onto the last end where it starts within tol
    class OffsetPath {
        public:
            EdgeList edges;

            void Add(Edge e, const Properties &p, double tol) {
                if (!edges.empty() && StartOf(e) != EndOf(edges.back())) {
                    if (!StartOf(e).IsClose(EndOf(edges.back()), tol))
                        throw std::logic_error("Offset: internal error, the raw offset path has a gap");
                    Wire::MoveEnd(e, false, EndOf(edges.back()));
                }
                SetProps(e, p);
                edges.push_back(std::move(e));
            }
            void AddLine(const Vertex &a, const Vertex &b, const Properties &p, double tol) {
                if (!a.IsClose(b, tol))
                    Add(Line(a, b), p, tol);
            }
            // the last end onto the first start, which it reaches within tol
            void Close() {
                if (edges.size() > 1 && EndOf(edges.back()) != StartOf(edges.front()))
                    Wire::MoveEnd(edges.back(), true, StartOf(edges.front()));
            }
            void Reverse() {
                std::reverse(edges.begin(), edges.end());
                for (Edge &e : edges)
                    e = ReversedOf(e);
            }
    };

    // the properties of a corner's pieces: of the corner, and of the straight pieces that run on along the edge before
    // it and the one after it
    struct JoinProps {
        Properties corner, in, out;
    };

    // the raw path around the corner at v from a (where an offset arriving with heading t1 ends) to b (where the next,
    // leaving with heading t2, starts), the offsets at s: outside the corner (the side it turns away from) its form,
    // inside through v, which the winding cleanup takes away again
    inline void JoinCorner(OffsetPath &path, const Vertex &v, const Vertex &a, const Vertex &t1, const Vertex &b, const Vertex &t2, double s,
                           OffsetCorner corner, const JoinProps &cp, double tol) {
        const Properties &p = cp.corner;
        if (a.IsClose(b, tol))
            return;
        const double turn = Cross(t1, t2);
        // a fold back (turn 0, the headings opposite) has its outside ahead, on either side
        const bool fold = std::abs(turn) <= 1e-12 && Dot(t1, t2) < 0.0;
        if (!fold && s * turn > 0.0) {
            path.AddLine(a, v, p, tol);
            path.AddLine(v, b, p, tol);
            return;
        }
        if (corner == OffsetCorner::Round) {
            path.Add(Arc(a, b, v, fold ? s < 0.0 : turn > 0.0), p, tol);
            return;
        }
        if (corner == OffsetCorner::Sharp && !fold) {
            // the two end tangents meet at m
            const Vertex m = a + t1 * (Cross(b - a, t2) / turn);
            path.AddLine(a, m, cp.in, tol);
            path.AddLine(m, b, cp.out, tol);
            return;
        }
        path.AddLine(a, b, p, tol);
    }

    // the cap around the open end v (heading t out of the chain) from a, on the right, to b, on the left, at d; side:
    // the properties of a square cap's pieces along the end (as the edge's offsets run on)
    inline void Cap(OffsetPath &path, const Vertex &v, const Vertex &t, const Vertex &a, const Vertex &b, double d, OffsetEnd end,
                    const Properties &p, const Properties &side, double tol) {
        if (end == OffsetEnd::Round)
            path.Add(Arc(a, b, v, true), p, tol);
        else if (end == OffsetEnd::Flat)
            path.AddLine(a, b, p, tol);
        else {
            const Vertex a2 = a + t * d, b2 = b + t * d;
            path.AddLine(a, a2, side, tol);
            path.AddLine(a2, b2, p, tol);
            path.AddLine(b2, b, side, tol);
        }
    }

    // the offsets of a chain's edges at s (> 0 to the left) joined around its vertices; closed: also around its first
    // vertex, from the end of the last edge's offset. tags: each piece's layer its OffsetTag, else its edge's properties
    inline OffsetPath OffsetSide(const EdgeList &chain, bool closed, double s, OffsetCorner corner, bool tags, double tol) {
        const size_t n = chain.size();
        std::vector<OffsetEdge> offsets;
        for (const Edge &e : chain)
            offsets.emplace_back(e, s);
        const auto props = [&](OffsetSource kind, size_t k, size_t edge) {
            const Properties p = WithoutPartition(PropsOf(chain[edge]));
            return tags ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.layer = OffsetTag(kind, k); }) : p;
        };
        // a sharp corner's piece along a line's offset runs on as that offset; along a curve's it is a spur of its own
        const auto along = [&](OffsetSource spur, size_t edge) {
            return props(std::holds_alternative<Line>(chain[edge]) ? OffsetSource::Edge : spur, edge, edge);
        };
        OffsetPath path;
        for (size_t k = 0; k < n; ++k) {
            if (k > 0 || closed) {
                const size_t before = (k + n - 1) % n;
                JoinCorner(path, StartOf(chain[k]), offsets[before].Point(true), offsets[before].Heading(true), offsets[k].Point(false),
                           offsets[k].Heading(false), s, corner,
                           {props(OffsetSource::Corner, k, before), along(OffsetSource::SpurAfter, before), along(OffsetSource::SpurBefore, k)}, tol);
            }
            if (std::optional<Edge> e = offsets[k].Offset(tol))
                path.Add(std::move(*e), props(OffsetSource::Edge, k, k), tol);
        }
        if (closed)
            path.Close();
        return path;
    }

    // the raw band of width 2 d around a chain (each edge oriented along it): open, one loop counterclockwise along it:
    // its right side, the end cap, its left side back, the start cap; closed, the loop on its outside and the one on its
    // inside turned back (a clockwise chain is taken the other way round)
    inline std::vector<EdgeList> RawBand(EdgeList chain, bool closed, double d, OffsetEnd end, OffsetCorner corner, bool tags, double tol) {
        if (const Circle *c = std::get_if<Circle>(&chain.front())) {
            // the concentric circles, the inner one unless d reaches the radius
            const Vertex u = (c->Origin() - c->Center()) * (1.0 / c->Radius());
            const Properties p = tags ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.layer = OffsetTag(OffsetSource::Edge, 0); })
                                      : WithoutPartition(PropsOf(chain.front()));
            Circle outer(c->Center(), c->Center() + u * (c->Radius() + d), true), inner(c->Center(), c->Center() + u * (c->Radius() - d), false);
            outer.props = inner.props = p;
            if (c->Radius() - d <= tol)
                return {{outer}};
            return {{outer}, {inner}};
        }
        if (closed) {
            // the outside's loop counterclockwise, the inside's clockwise; the chain stays as it is, so the tags keep its
            // numbers
            const bool ccw = ChainArea(chain) > 0.0;
            OffsetPath outer = OffsetSide(chain, true, ccw ? -d : d, corner, tags, tol), inner = OffsetSide(chain, true, ccw ? d : -d, corner, tags, tol);
            if (!ccw)
                outer.Reverse();
            if (ccw)
                inner.Reverse();
            return {std::move(outer.edges), std::move(inner.edges)};
        }
        const size_t n = chain.size();
        const auto capProps = [&](size_t k, size_t edge) {
            return tags ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.layer = OffsetTag(OffsetSource::Cap, k); })
                        : WithoutPartition(PropsOf(chain[edge]));
        };
        // a square cap's pieces along the end run on as a line's offsets, along a curve's they are spurs
        const auto sideProps = [&](OffsetSource spur, size_t edge) {
            const OffsetSource kind = std::holds_alternative<Line>(chain[edge]) ? OffsetSource::Edge : spur;
            return tags ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.layer = OffsetTag(kind, edge); })
                        : WithoutPartition(PropsOf(chain[edge]));
        };
        OffsetPath right = OffsetSide(chain, false, -d, corner, tags, tol), left = OffsetSide(chain, false, d, corner, tags, tol);
        const OffsetEdge firstRight(chain.front(), -d), firstLeft(chain.front(), d), lastRight(chain.back(), -d), lastLeft(chain.back(), d);
        Cap(right, EndOf(chain.back()), lastLeft.Heading(true), lastRight.Point(true), lastLeft.Point(true), d, end, capProps(n, n - 1),
            sideProps(OffsetSource::SpurAfter, n - 1), tol);
        left.Reverse();
        for (Edge &e : left.edges) {
            const Properties p = PropsOf(e);
            right.Add(std::move(e), p, tol);
        }
        Cap(right, StartOf(chain.front()), firstLeft.Heading(false) * -1.0, firstLeft.Point(false), firstRight.Point(false), d, end, capProps(0, 0),
            sideProps(OffsetSource::SpurBefore, 0), tol);
        right.Close();
        return {std::move(right.edges)};
    }

    // how much the winding number of the raw loops grows across edge i of their arrangement, from its right to its left:
    // the raw edges through its middle, +1 each running its way, -1 each running against it
    inline int WindingStep(const Wire &cut, size_t i, const Wire &raw, Grid &grid, double tol) {
        const Vertex m = MidPointOf(cut[i]), normal = LeftNormal(cut[i], m);
        std::vector<uint32_t> near;
        grid.InBox(BBox::Of(m, m).Grown(tol), near);
        int step = 0;
        for (uint32_t r : near) {
            const Vertex q = NearestPointOf(raw[r], m);
            if (q.IsClose(m, tol))
                step += Dot(LeftNormal(raw[r], q), normal) > 0.0 ? 1 : -1;
        }
        return step;
    }

    // the edges of the region the raw loops wind around positively (Clipper's positive fill rule): the loops cut where
    // they cross or overlap, an edge kept where the winding number on one side is > 0 and on the other <= 0, turned to
    // have that region on its left
    inline Wire PositiveRegion(const Wire &raw) {
        // a spline's offset crosses itself past its tightest bend (a swallowtail)
        Wire c(raw.Axes());
        for (const Edge &e : raw.Edges())
            AddUncrossed(c, e);
        std::vector<CutState> state(c.Size());
        Arrange(c, state);
        const std::vector<HalfEdges> faces = ArrangementFaces(c);
        const size_t outside = faces.size();
        std::vector<size_t> faceOf(2 * c.Size(), outside);
        for (size_t k = 0; k < faces.size(); ++k)
            for (auto [i, forward] : faces[k])
                faceOf[2 * i + (forward ? 0 : 1)] = k;
        Wire rawEdges = raw;
        Grid grid = rawEdges.EdgeGrid();
        const double tol = c.Tol();
        std::vector<int> step(c.Size());
        std::vector<std::vector<std::pair<size_t, int>>> next(faces.size() + 1);
        for (size_t i = 0; i < c.Size(); ++i) {
            step[i] = WindingStep(c, i, rawEdges, grid, tol);
            const size_t left = faceOf[2 * i], right = faceOf[2 * i + 1];
            next[right].emplace_back(left, step[i]);
            next[left].emplace_back(right, -step[i]);
        }
        constexpr int unknown = std::numeric_limits<int>::min();
        std::vector<int> winding(faces.size() + 1, unknown);
        std::deque<size_t> queue{outside};
        winding[outside] = 0;
        while (!queue.empty()) {
            const size_t f = queue.front();
            queue.pop_front();
            for (auto [g, by] : next[f])
                if (winding[g] == unknown) {
                    winding[g] = winding[f] + by;
                    queue.push_back(g);
                }
        }
        Wire out(c.Axes());
        for (size_t i = 0; i < c.Size(); ++i) {
            const bool left = winding[faceOf[2 * i]] > 0, right = winding[faceOf[2 * i + 1]] > 0;
            if (left != right)
                out.Add(left ? c[i] : ReversedOf(c[i]));
        }
        out.MergeOnCarriers();
        return out;
    }

    // for the sketch's offset, each piece's layer its OffsetTag: the band of width 2 d around an open chain, and the offset
    // of a closed chain by d (> 0 outside, < 0 inside), each the region its raw loops wind around positively
    inline Wire TaggedBand(const EdgeList &chain, double d, OffsetEnd end, OffsetCorner corner, const AxisSystem &axes, double tol) {
        Wire raw(axes);
        for (EdgeList &loop : RawBand(chain, false, d, end, corner, true, tol))
            for (Edge &e : loop)
                raw.Add(std::move(e));
        return PositiveRegion(raw);
    }
    inline Wire TaggedLoopOffset(const EdgeList &chain, double d, OffsetCorner corner, const AxisSystem &axes, double tol) {
        const bool ccw = ChainArea(chain) > 0.0;
        OffsetPath path = OffsetSide(chain, true, ccw ? -d : d, corner, true, tol);
        if (!ccw)
            path.Reverse();
        Wire raw(axes);
        for (Edge &e : path.edges)
            raw.Add(std::move(e));
        return PositiveRegion(raw);
    }

}
