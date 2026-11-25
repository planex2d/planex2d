// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/core/config.hpp"
#include "planex/sketch/choices.hpp"
#include "planex/sketch/pen.hpp"
#include "planex/sketch/target.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d::sketch {

    struct LineRefs : EndRefs {
        std::optional<double> length;
        std::optional<Edge> parallel, perpendicular, congruent, angleRef;
        std::optional<double> angle, distance;
        std::optional<Vertex> through;

        void SetDistanceTowards(double d, const Vertex &side);
        void SetDistanceFrom(double d, const Vertex &at);
    };

    struct ConstructedLine {
        Vertex start, end;
    };

    class LineConstruction {
    public:
        LineConstruction(const LineRefs &refs, const Pen &pen, const AxisSystem &axes, const EdgeList &sketchEdges)
            : r(refs), pen(pen), axes(axes), sketchEdges(sketchEdges), choices{refs.reverse} {
            fromElement = r.from && !r.from->at;
            toElement = r.to && !r.to->at;
            tangentPair = r.tangent.size() == 2;
            directed = r.parallel || r.perpendicular || r.angle || r.congruent || !r.tangent.empty();
            throughGivesDirection = r.through && !directed;
            Check();
            nearPoint = r.nearPoint.value_or(pen.pos);
            if (tangentPair)
                for (size_t k = 0; k < 2; ++k) {
                    if (r.from && r.from->IsSame(r.tangent[k]))
                        fromTouch = k;
                    if (r.to && r.to->IsSame(r.tangent[k]))
                        toTouch = k;
                }
            tol = Tol(r.Scale(pen.pos));
            fromAt = r.from ? r.from->at : std::nullopt;
            toAt = r.to ? r.to->at : std::nullopt;
            origin = fromAt.value_or(pen.pos);
            anchor = r.through.value_or(origin);
            if (!r.through && !r.distance && (r.parallel || r.perpendicular || r.angle))
                anchor = PointOfEnds().value_or(anchor);
            direction = pen.dir;
        }

        ConstructedLine Drawn();

    private:
        const LineRefs &r;
        const Pen &pen;
        const AxisSystem &axes;
        const EdgeList &sketchEdges;
        Choices choices;
        bool fromElement, toElement, tangentPair, directed, throughGivesDirection;
        Vertex nearPoint;
        double tol;
        std::optional<Vertex> fromAt, toAt;
        Vertex origin, anchor, direction;
        std::optional<size_t> fromTouch, toTouch;
        std::array<Vertex, 2> touch;
        std::optional<Vertex> touchPoint;

        void Check() const;
        const Line &AsLine(const Edge &e, const std::string &what) const {
            const Line *l = std::get_if<Line>(&e);
            if (!l)
                r.Fail(what + " needs a line");
            return *l;
        }
        Vertex DirectionOf(const Edge &e, const std::string &what) const {
            const Line &l = AsLine(e, what);
            return Normalized(l.End() - l.Start());
        }
        Vertex Sense(const Vertex &v) const { return Dot(v, pen.dir) < 0.0 ? v * -1.0 : v; }
        bool AnyFree() const {
            bool free = (r.from && !r.from->point && !r.from->at && !fromTouch) || (r.to && !r.to->point && !r.to->at && !toTouch);
            for (const Target &t : r.tangent)
                free = free || !t.point;
            return free;
        }
        std::optional<Vertex> PointOfEnds() const {
            if (r.from && r.from->point && !r.from->at)
                return r.from->point;
            if (r.to && r.to->point && !r.to->at)
                return r.to->point;
            return std::nullopt;
        }
        void FindDirection();
        void TouchBoth();
        void TouchOne();
        std::vector<std::pair<double, Vertex>> Hits(const EdgeList &targets, const Vertex &p) const;
        std::vector<Solution> Meets(const Target &t, const std::string &what) const;
        std::pair<std::optional<Vertex>, std::optional<Vertex>> ChosenEnds(const std::vector<Solution> &fromSols,
                                                                           const std::vector<Solution> &toSols, bool fromFree, bool toFree);
        Vertex DefaultStart() const {
            if (tangentPair)
                return toTouch == size_t{0} ? touch[1] : touch[0];
            if (r.through && !throughGivesDirection)
                return *r.through;
            if (r.congruent || r.distance)
                return anchor + direction * Dot(pen.pos - anchor, direction);
            return origin;
        }
        // the first ahead, the last with until=px.last
        std::optional<Vertex> Ahead(const std::vector<std::pair<double, Vertex>> &along) const {
            std::optional<std::pair<double, Vertex>> best;
            for (const auto &h : along)
                if (h.first > tol && (!best || (r.until == Until::Last ? h.first > best->first : h.first < best->first)))
                    best = h;
            return best ? std::optional(best->second) : std::nullopt;
        }
        Vertex DefaultEnd(const Vertex &s, const std::vector<Solution> &toSols, bool toFree);
    };

}
