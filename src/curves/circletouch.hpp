// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "core/vertex.hpp"
#include "curves/arc.hpp"
#include "curves/circle.hpp"
#include "curves/edgelist.hpp"
#include "curves/line.hpp"
#include "curves/primitiveedge.hpp"
#include "curves/spline.hpp"

namespace planex2d {

    // a line or arc of the target edges, a circle's two halves; a spline's approximation pieces have no carrier to extend onto
    struct Piece {
        PrimitiveEdge e;
        bool carrier;
    };
    inline std::vector<Piece> PiecesOf(const Edge &e) {
        if (const Line *l = std::get_if<Line>(&e))
            return {{*l, true}};
        if (const Arc *a = std::get_if<Arc>(&e))
            return {{*a, true}};
        if (const Circle *c = std::get_if<Circle>(&e)) {
            const std::array<Arc, 2> halves = c->Halves();
            return {{halves[0], true}, {halves[1], true}};
        }
        std::vector<Piece> out;
        for (const PrimitiveEdge &p : std::get<Spline>(e).Approximation())
            out.push_back({p, false});
        return out;
    }
    inline std::vector<Piece> PiecesOf(const EdgeList &edges) {
        std::vector<Piece> out;
        for (const Edge &e : edges)
            for (const Piece &p : PiecesOf(e))
                out.push_back(p);
        return out;
    }

    // where the centers of the circles of radius r touching a piece lie: two lines at distance r from a line, two
    // circles around an arc's center
    // how a circle with its center on the locus touches the piece: on a line's left (1) or right (-1); an arc's circle
    // from outside (1), from inside (-1), or around it (2, the circle the larger)
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
        // the carrier of a piece
        static Locus Of(const PrimitiveEdge &piece) {
            if (const Line *l = std::get_if<Line>(&piece))
                return Through(l->Start(), Normalized(l->End() - l->Start()));
            const Arc &a = std::get<Arc>(piece);
            return Circle(a.Center(), a.Radius());
        }
        // zero on the locus: the distance left of a line, outside of a circle
        double Value(const Vertex &q) const { return line ? Cross(u, q - p) : Norm(q - p) - radius; }
    };
    inline std::vector<Locus> CenterLoci(const PrimitiveEdge &piece, double r) {
        if (const Line *l = std::get_if<Line>(&piece)) {
            Vertex u = Normalized(l->End() - l->Start());
            Vertex m{-u.y, u.x};
            return {{true, l->Start() + m * r, u, 0.0, Touch::Outside}, {true, l->Start() - m * r, u, 0.0, Touch::Inside}};
        }
        const Arc &a = std::get<Arc>(piece);
        std::vector<Locus> loci{{false, a.Center(), {}, a.Radius() + r, Touch::Outside}};
        if (a.Radius() != r)
            loci.push_back({false, a.Center(), {}, std::abs(a.Radius() - r), r < a.Radius() ? Touch::Inside : Touch::Around});
        return loci;
    }
    inline std::vector<Vertex> Meet(const Locus &a, const Locus &b) {
        if (a.line && b.line) {
            double c = Cross(a.u, b.u);
            if (c == 0.0)
                return {};
            return {a.p + a.u * (Cross(b.p - a.p, b.u) / c)};
        }
        if (!a.line && b.line)
            return Meet(b, a);
        if (a.line) {
            Vertex foot = a.p + a.u * Dot(b.p - a.p, a.u);
            double h2 = b.radius * b.radius - Dot(foot - b.p, foot - b.p);
            if (h2 < 0.0)
                return {};
            double h = std::sqrt(h2);
            return {foot + a.u * h, foot - a.u * h};
        }
        Vertex d = b.p - a.p;
        double l = Norm(d);
        if (l == 0.0)
            return {};
        double x = (l * l + a.radius * a.radius - b.radius * b.radius) / (2.0 * l);
        double h2 = a.radius * a.radius - x * x;
        if (h2 < 0.0)
            return {};
        Vertex u = d / l;
        Vertex base = a.p + u * x, n = Vertex{-u.y, u.x} * std::sqrt(h2);
        return {base + n, base - n};
    }
    // where the circle around x with radius r touches the piece
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
