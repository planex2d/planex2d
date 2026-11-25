// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/target.hpp"

namespace planex2d::sketch {

    // on the edge first, then nearest to the point given (or to near), else the smaller
    double Target::RadiusAround(const Vertex &c, const std::optional<Vertex> &nearPoint, double tol, Choices &choices, const std::string &call) const {
        struct Touching {
            Vertex p;
            double rho;
            bool onEdge;
        };
        std::vector<Touching> found;
        // a touch point found on two pieces (the halves of a circle) counts once
        const auto accept = [&](const Piece &piece, const Vertex &p) {
            const double rho = Norm(p - c);
            const bool on = piece.e.Locate(p, tol) != Location::Off;
            if (!(rho > tol && (on || piece.carrier)))
                return;
            for (Touching &f : found)
                if (f.p.IsClose(p, tol)) {
                    f.onEdge = f.onEdge || on;
                    return;
                }
            found.push_back({p, rho, on});
        };
        for (const Piece &piece : PiecesOf(edges)) {
            if (const Line *l = std::get_if<Line>(&piece.e)) {
                accept(piece, l->Foot(c));
                continue;
            }
            const Arc &arc = std::get<Arc>(piece.e);
            if (Norm(c - arc.Center()) <= tol)
                accept(piece, arc.MidPoint());
            else
                for (const Vertex &p : arc.RadialPoints(c))
                    accept(piece, p);
        }
        if (found.empty())
            throw std::invalid_argument(call + ": no circle around the center touches it");
        const std::optional<Vertex> ref = point ? point : nearPoint;
        const std::vector<size_t> order = BestFirst(found.size(), [&](size_t x, size_t y) {
            if (found[x].onEdge != found[y].onEdge)
                return found[x].onEdge;
            return ref ? Norm(found[x].p - *ref) < Norm(found[y].p - *ref) - tol : found[x].rho < found[y].rho - tol;
        });
        return found[choices.Take(order, !point)].rho;
    }

}
