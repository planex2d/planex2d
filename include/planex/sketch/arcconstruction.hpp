// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/core/config.hpp"
#include "planex/curves/circlecarrier.hpp"
#include "planex/sketch/choices.hpp"
#include "planex/sketch/pen.hpp"
#include "planex/sketch/target.hpp"
#include "planex/sketch/touchingcircles.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d::sketch {

    struct ArcRefs : EndRefs {
        std::optional<double> radius, sweep;  // sweep in degrees
        std::optional<Vertex> center, through, start;
        std::optional<Edge> congruent;
    };

    struct ConstructedArc {
        bool move;
        Vertex start, end, center;
        bool ccw;
        bool full = false;
    };

    class ArcConstruction {
    public:
        ArcConstruction(const ArcRefs &refs, const Pen &pen, const AxisSystem &axes, const EdgeList &sketchEdges)
            : r(refs), pen(pen), axes(axes), sketchEdges(sketchEdges), choices{refs.reverse} {
            circle = r.center || r.congruent;
            fromElement = r.from && !r.from->at;
            toElement = r.to && !r.to->at;
            Check();
            left = {-pen.dir.y, pen.dir.x};
            scale = r.Scale(pen.pos);
            if (r.through)
                scale = std::max(scale, NormInf(*r.through));
        }

        ConstructedArc Drawn() {
            if (r.tangent.size() == 2)
                return TouchingBoth();
            if (r.tangent.size() == 1)
                return EndingTouching();
            if (!circle && r.to)
                return BetweenPoints();
            return OnCircle();
        }

    private:
        struct Touching {
            double rho;
            Solution at;
        };

        const ArcRefs &r;
        const Pen &pen;
        const AxisSystem &axes;
        const EdgeList &sketchEdges;
        Choices choices;
        bool circle, fromElement, toElement;
        Vertex left;
        double scale;

        void Check() const;
        ConstructedArc Draw(const Vertex &s, const Vertex &e, const Vertex &c, bool ccw) const {
            const bool move = !s.IsClose(pen.pos, pen.tol);
            const Vertex from = move ? s : pen.pos;
            if (e.IsClose(from, move ? Tol(NormInf(s)) : pen.tol))
                r.Fail("the arc would be a full circle or empty; use Circle");
            choices.Warn(r.call);
            return {move, from, e, c, ccw};
        }

        ConstructedArc TouchingBoth();

        std::vector<double> RadiiTouching(const PrimitiveEdge &piece) const;
        ConstructedArc EndingTouching();

        ConstructedArc BetweenPoints();
        ConstructedArc Through(const Vertex &s, const Vertex &e, double tol) const {
            const Vertex p = *r.through - s, q = e - s;
            const double d = 2.0 * Cross(p, q);
            if (std::abs(d) <= tol * Norm(q))
                r.Fail("the three points lie on a line");
            const Vertex c = s + Vertex{q.y * Dot(p, p) - p.y * Dot(q, q), p.x * Dot(q, q) - q.x * Dot(p, p)} * (1.0 / d);
            return Draw(s, e, c, d > 0.0);
        }
        ConstructedArc OfRadius(const Vertex &s, const Vertex &e, double tol);
        ConstructedArc LeavingAlong(const Vertex &s, const Vertex &e, double tol) const {
            const Vertex chord = e - s;
            const Vertex t = r.start ? Normalized(*r.start) : pen.dir, n{-t.y, t.x};
            const double k = Dot(chord, n);
            if (std::abs(k) <= tol * Norm(chord))
                r.Fail("the direction at the start runs along the chord");
            const double rho = Dot(chord, chord) / (2.0 * k);
            return Draw(s, e, s + n * rho, rho > 0.0);
        }

        CircleCarrier CircleGiven() const;
        std::vector<Solution> CircleHits(const CircleCarrier &k, const EdgeList &edges) const {
            Wire carrier(axes);
            const Vertex east = k.center + Vertex{k.radius, 0.0}, west = k.center - Vertex{k.radius, 0.0};
            carrier.Add(Arc(east, west, k.center, true));
            carrier.Add(Arc(west, east, k.center, true));
            std::vector<Solution> out;
            for (const Vertex &x : CarrierHits(carrier, edges))
                out.push_back({x});
            return out;
        }
        std::vector<Solution> Meets(const Target &t, const std::string &what, const CircleCarrier &k, double tol) const {
            if (!t.at)
                return CircleHits(k, t.edges);
            if (std::abs(Norm(*t.at - k.center) - k.radius) > tol)
                r.Fail(what + " " + PointText(*t.at) + " is not on the arc's circle");
            return {{*t.at}};
        }
        Vertex StartOnCircle(const CircleCarrier &k, const std::vector<Solution> &fromSols, double tol) const {
            if (r.from)
                return fromSols[Preference{r.from->point.value_or(pen.pos), pen.pos, pen.dir, tol}.Ranked(fromSols)[0]].p;
            if (circle && std::abs(Norm(pen.pos - k.center) - k.radius) > tol) {
                if (pen.pos == k.center)
                    r.Fail("the pen is at the center; give from");
                return k.center + Normalized(pen.pos - k.center) * k.radius;
            }
            return pen.pos;
        }
        bool IsCcw(const CircleCarrier &k, const Vertex &s) {
            const Vertex radial = s - k.center;
            bool ccw = r.sweep ? *r.sweep > 0.0 : r.from && r.to ? true : Dot(Vertex{-radial.y, radial.x}, pen.dir) >= 0.0;
            if (r.reverse) {
                ccw = !ccw;
                choices.MarkSwitched();
            }
            return ccw;
        }
        // in [0, 2pi)
        static double SweepAngle(const Vertex &c, bool ccw, const Vertex &a, const Vertex &b) {
            const Vertex u = a - c, v = b - c;
            const double t = std::atan2(ccw ? Cross(u, v) : -Cross(u, v), Dot(u, v));
            return t < 0.0 ? t + 2.0 * std::numbers::pi : t;
        }
        std::optional<Vertex> Ahead(const CircleCarrier &k, bool ccw, const Vertex &s, const std::vector<Solution> &hits,
                                    const std::optional<Vertex> &nearPoint, double tol) const;
        ConstructedArc OnCircle();
    };

}
