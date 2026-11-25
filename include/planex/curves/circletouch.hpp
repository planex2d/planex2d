// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/curves/arc.hpp"
#include "planex/curves/circle.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/curves/line.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/curves/spline.hpp"

namespace planex2d {

    // a spline's approximation pieces have no carrier to extend onto
    struct Piece {
        PrimitiveEdge e;
        bool carrier;
    };
    std::vector<Piece> PiecesOf(const Edge &e);
    inline std::vector<Piece> PiecesOf(const EdgeList &edges) {
        std::vector<Piece> out;
        for (const Edge &e : edges)
            for (const Piece &p : PiecesOf(e))
                out.push_back(p);
        return out;
    }

    // the side of a piece a touching circle lies on: Outside also left of a line, Inside also right of it; Around: it
    // encloses the arc's circle
    enum class Touch { Inside = -1, Any = 0, Outside = 1, Around = 2 };
    inline Touch Opposite(Touch t) { return t == Touch::Outside ? Touch::Inside : t == Touch::Inside ? Touch::Outside : t; }

    struct Locus {
        bool line;
        Vertex p;           // a point of the line, or the circle's center
        Vertex u;           // the line's unit direction
        double radius = 0;  // of the circle
        Touch kind = Touch::Any;

        static Locus Circle(const Vertex &c, double rad) { return {false, c, {}, rad, Touch::Any}; }
        static Locus Through(const Vertex &q, const Vertex &dir) { return {true, q, dir, 0.0, Touch::Any}; }
        static Locus Of(const PrimitiveEdge &piece) {
            if (const Line *l = std::get_if<Line>(&piece))
                return Through(l->Start(), Normalized(l->End() - l->Start()));
            const Arc &a = std::get<Arc>(piece);
            return Circle(a.Center(), a.Radius());
        }
        // zero on the locus: the distance left of a line, outside of a circle
        double Value(const Vertex &q) const { return line ? Cross(u, q - p) : Norm(q - p) - radius; }
    };
    std::vector<Locus> CenterLoci(const PrimitiveEdge &piece, double r);
    std::vector<Vertex> Meet(const Locus &a, const Locus &b);
    inline std::optional<Vertex> TouchPoint(const PrimitiveEdge &piece, const Vertex &x, double r) {
        if (const Line *l = std::get_if<Line>(&piece))
            return l->Foot(x);
        const Arc &a = std::get<Arc>(piece);
        if (x == a.Center())
            return std::nullopt;
        Vertex u = Normalized(x - a.Center());
        Vertex closest = a.Center() + u * a.Radius(), farthest = a.Center() - u * a.Radius();
        return std::abs(Norm(closest - x) - r) <= std::abs(Norm(farthest - x) - r) ? closest : farthest;
    }
    // +1 left of a line piece or outside an arc's circle, -1 right of it or inside
    inline double Side(const PrimitiveEdge &piece, const Vertex &p) {
        if (const Line *l = std::get_if<Line>(&piece))
            return Cross(l->End() - l->Start(), p - l->Start()) >= 0.0 ? 1.0 : -1.0;
        const Arc &a = std::get<Arc>(piece);
        return Norm(p - a.Center()) > a.Radius() ? 1.0 : -1.0;
    }

    template <class F>
    void ForEachCenter(const std::vector<Piece> &as, const std::vector<Piece> &bs, double r, F f) {
        for (const Piece &pa : as)
            for (const Piece &pb : bs)
                for (const Locus &la : CenterLoci(pa.e, r))
                    for (const Locus &lb : CenterLoci(pb.e, r))
                        for (const Vertex &x : Meet(la, lb))
                            f(pa, pb, la, lb, x);
    }

}
