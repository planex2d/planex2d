// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/circletouch.hpp"

#include <array>

namespace planex2d {

    std::vector<Piece> PiecesOf(const Edge &e) {
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

    std::vector<Locus> CenterLoci(const PrimitiveEdge &piece, double r) {
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

    std::vector<Vertex> Meet(const Locus &a, const Locus &b) {
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

}
