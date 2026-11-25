// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/touchingcircles.hpp"

#include <utility>

namespace planex2d::sketch {

    // reverse takes the second circle, or with only one flips the sides of the targets without a point (the opposite
    // corner)
    std::vector<TouchingCircle> TouchingCircles::Ranked(Choices &choices) const {
        std::vector<TouchingCircle> found = Found(false);
        if (!choices.IsReverse())
            return found;
        if (found.size() > 1) {
            choices.MarkSwitched();
            std::swap(found[0], found[1]);
            return found;
        }
        if (a.PointPicks(tol) && b.PointPicks(tol))
            return found;
        std::vector<TouchingCircle> flipped = Found(true);
        if (flipped.empty()) {
            choices.MarkOnlyOne();
            return found;
        }
        choices.MarkSwitched();
        return flipped;
    }

    std::optional<Touch> TouchingCircles::Wanted(const PrimitiveEdge &e, const Target &t, const PrimitiveEdge &other, bool flip) const {
        const bool line = std::holds_alternative<Line>(e);
        if (!line && t.point) {
            const Arc &arc = std::get<Arc>(e);
            const double d = Norm(*t.point - arc.Center());
            return d > arc.Radius() ? Touch::Outside : radius < arc.Radius() ? Touch::Inside : Touch::Around;
        }
        const bool picks = t.PointPicks(tol);
        if (nearPoint && !picks)
            return std::nullopt;
        const Touch side = Side(e, line && picks ? *t.point : other.MidPoint()) > 0.0 ? Touch::Outside : Touch::Inside;
        return flip && !picks ? Opposite(side) : side;
    }

    std::vector<TouchingCircle> TouchingCircles::Found(bool flip) const {
        std::vector<Candidate> found;
        ForEachCenter(PiecesOf(a.edges), PiecesOf(b.edges), radius, [&](const Piece &pa, const Piece &pb, const Locus &la, const Locus &lb, const Vertex &x) {
            const std::optional<Touch> wa = Wanted(pa.e, a, pb.e, flip), wb = Wanted(pb.e, b, pa.e, flip);
            if ((wa && la.kind != *wa) || (wb && lb.kind != *wb))
                return;
            const std::optional<Vertex> ta = TouchPoint(pa.e, x, radius), tb = TouchPoint(pb.e, x, radius);
            if (!ta || !tb || ta->IsClose(*tb, tol))
                return;
            const bool onA = pa.e.Locate(*ta, tol) != Location::Off, onB = pb.e.Locate(*tb, tol) != Location::Off;
            if ((!onA && !pa.carrier) || (!onB && !pb.carrier))
                return;
            Add(found, {{x, *ta, *tb}, onA && onB, Hint(pa.e, a, *ta) + Hint(pb.e, b, *tb)});
        });
        return Ordered(found);
    }

    std::vector<TouchingCircle> TouchingCircles::Ordered(const std::vector<Candidate> &found) const {
        std::vector<Solution> centers;
        for (const Candidate &c : found)
            centers.push_back({c.circle.center, c.onEdge});
        const std::vector<size_t> order =
            Preference{nearPoint.value_or(pen.pos), pen.pos, pen.dir, tol}.Ranked(centers);
        const std::vector<size_t> byHint = BestFirst(order.size(), [&](size_t i, size_t j) { return found[order[i]].hint < found[order[j]].hint - tol; });
        std::vector<TouchingCircle> out;
        for (size_t k : byHint)
            out.push_back(found[order[k]].circle);
        return out;
    }

}
