// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "curves/circletouch.hpp"
#include "sketch/choices.hpp"
#include "sketch/pen.hpp"
#include "sketch/target.hpp"

namespace planex2d::sketch {

    struct TouchingCircle {
        Vertex center, a, b;
    };

    // the circles of a radius touching a piece of a and one of b (or their carriers). Of a line a target wants the side
    // its point is on, else the side the other target is on (the corner the two face); a circle is touched from the side
    // the other target is on, never enclosed by default; a point inside a circle puts the circle inside it (nested if
    // smaller, around it if larger), one outside touches it from outside; one on it decides nothing and raises. With near
    // every position counts and the nearest wins
    class TouchingCircles {
    public:
        TouchingCircles(const Target &a, const Target &b, double radius, const std::optional<Vertex> &nearPoint, const Pen &pen,
                        double tol, const std::string &call)
            : a(a), b(b), radius(radius), nearPoint(nearPoint), pen(pen), tol(tol) {
            CheckPointOffCircle(a, call);
            CheckPointOffCircle(b, call);
        }

        // reverse takes the second circle, or with only one flips the sides of the targets without a point (the opposite
        // corner)
        std::vector<TouchingCircle> Ranked(Choices &choices) const {
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

    private:
        struct Candidate {
            TouchingCircle circle;
            bool onEdge;
            double hint;
        };

        const Target &a, &b;
        double radius;
        std::optional<Vertex> nearPoint;
        Pen pen;
        double tol;

        void CheckPointOffCircle(const Target &t, const std::string &call) const {
            if (!t.point)
                return;
            for (const Piece &piece : PiecesOf(t.edges))
                if (const Arc *arc = std::get_if<Arc>(&piece.e); arc && std::abs(Norm(*t.point - arc->Center()) - arc->Radius()) <= tol)
                    throw std::invalid_argument(call + ": the point " + PointText(*t.point) + " lies on " +
                                                (t.name ? "the circle '" + *t.name + "'" : "the target's circle") +
                                                ", no unambiguous solution: put it inside or outside the circle");
        }
        std::optional<Touch> Wanted(const PrimitiveEdge &e, const Target &t, const PrimitiveEdge &other, bool flip) const {
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
        double Hint(const PrimitiveEdge &e, const Target &t, const Vertex &touch) const {
            return std::holds_alternative<Line>(e) || !t.point ? 0.0 : Norm(touch - *t.point);
        }
        // the same circle through another piece of a target: the touch points on the edges count
        void Add(std::vector<Candidate> &found, const Candidate &c) const {
            for (Candidate &f : found)
                if (f.circle.center.IsClose(c.circle.center, tol)) {
                    if (c.onEdge && !f.onEdge)
                        f = c;
                    return;
                }
            found.push_back(c);
        }
        std::vector<TouchingCircle> Found(bool flip) const {
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
        std::vector<TouchingCircle> Ordered(const std::vector<Candidate> &found) const {
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
    };

}
