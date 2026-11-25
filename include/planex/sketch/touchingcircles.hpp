// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "planex/curves/circletouch.hpp"
#include "planex/sketch/choices.hpp"
#include "planex/sketch/pen.hpp"
#include "planex/sketch/target.hpp"

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

        std::vector<TouchingCircle> Ranked(Choices &choices) const;

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
        std::optional<Touch> Wanted(const PrimitiveEdge &e, const Target &t, const PrimitiveEdge &other, bool flip) const;
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
        std::vector<TouchingCircle> Found(bool flip) const;
        std::vector<TouchingCircle> Ordered(const std::vector<Candidate> &found) const;
    };

}
