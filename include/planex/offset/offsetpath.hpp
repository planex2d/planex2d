// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "planex/boolean/arrangement.hpp"
#include "planex/boolean/cutloop.hpp"
#include "planex/wire/corner.hpp"
#include "planex/curves/parametricfit.hpp"
#include "planex/offset/offsetform.hpp"
#include "planex/wire/chain.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d {

    // a spur: the straight piece along a curved edge's end tangent (a sharp corner, a square cap)
    enum class OffsetSource { Edge, Corner, Cap, SpurBefore, SpurAfter };
    // source and index as one layer number: 5 k + kind
    inline int OffsetTag(OffsetSource kind, size_t k) { return 5 * (int)k + (int)kind; }
    inline OffsetSource SourceOfTag(int tag) { return (OffsetSource)(tag % 5); }
    inline size_t IndexOfTag(int tag) { return (size_t)(tag / 5); }

    Vertex LeftNormal(const Edge &e, const Vertex &p);
    // a spline's from its exact curve: its approximation's pieces turn off it by up to the approximation's tolerance
    inline Vertex ExactLeftNormal(const Edge &e, const Vertex &p) {
        if (const Spline *spline = std::get_if<Spline>(&e)) {
            const Vertex t = spline->ExactTangent(p);
            return Vertex{-t.y, t.x};
        }
        return LeftNormal(e, p);
    }

    // positive counterclockwise
    inline double ChainArea(const EdgeList &chain) {
        const Vertex o = StartOf(chain.front());
        double a = 0.0;
        for (const Edge &e : chain)
            a += SignedArea(e, o);
        return a;
    }

    // s > 0: to the left of the edge
    class OffsetEdge {
        private:
            const Edge &edge;
            ExactCurve curve;
            double s;

        public:
            OffsetEdge(const Edge &e, double side) : edge(e), curve(e), s(side) {}

            Vertex Heading(bool atEnd) const { return curve.Direction(atEnd ? curve.Last() : curve.First()); }
            Vertex Source(bool atEnd) const { return atEnd ? EndOf(edge) : StartOf(edge); }
            Vertex Point(bool atEnd) const {
                const Vertex t = Heading(atEnd);
                return Source(atEnd) + Vertex{-t.y, t.x} * s;
            }
            std::optional<Edge> Offset(double tol) const;
    };

    class OffsetPath {
        public:
            EdgeList edges;

            void Add(Edge e, const Properties &p, double tol);
            void AddLine(const Vertex &a, const Vertex &b, const Properties &p, double tol) {
                if (!a.IsClose(b, tol))
                    Add(Line(a, b), p, tol);
            }
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

    struct JoinProps {
        Properties corner, in, out;
    };

    inline bool IsFold(const Vertex &t1, const Vertex &t2) { return std::abs(Cross(t1, t2)) <= 1e-12 && Dot(t1, t2) < 0.0; }
    inline bool IsInsideOfCorner(const Vertex &t1, const Vertex &t2, double s) { return !IsFold(t1, t2) && s * Cross(t1, t2) > 0.0; }

    void JoinCorner(OffsetPath &path, const Vertex &v, const Vertex &a, const Vertex &t1, const Vertex &b, const Vertex &t2, double s,
                    OffsetCorner corner, const JoinProps &cp, double tol);

    // right and left as seen along out, the heading out of the chain
    struct OpenEnd {
        Vertex v, out, right, left;
    };

    OpenEnd OpenEndOf(const Edge &e, bool atEnd, double d);

    void Cap(OffsetPath &path, const OpenEnd &at, double d, OffsetEnd end, const Properties &p, const Properties &side, double tol);

    OffsetPath OffsetSide(const EdgeList &chain, bool closed, double s, OffsetCorner corner, bool tags, double tol);

    std::vector<EdgeList> RawBand(EdgeList chain, bool closed, double d, OffsetEnd end, OffsetCorner corner, bool tags, double tol);

    int WindingStep(const Wire &cut, size_t i, const Wire &raw, Grid &grid, double tol);

    Wire PositiveRegion(const Wire &raw);

    inline Wire TaggedBand(const EdgeList &chain, double d, OffsetEnd end, OffsetCorner corner, const AxisSystem &axes, double tol) {
        Wire raw(axes);
        for (EdgeList &loop : RawBand(chain, false, d, end, corner, true, tol))
            for (Edge &e : loop)
                raw.Add(std::move(e));
        return PositiveRegion(raw);
    }
    // d > 0 outside, < 0 inside
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
