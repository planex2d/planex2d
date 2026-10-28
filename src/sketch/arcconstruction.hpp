// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/axissystem.hpp"
#include "core/config.hpp"
#include "curves/circlecarrier.hpp"
#include "sketch/choices.hpp"
#include "sketch/pen.hpp"
#include "sketch/target.hpp"
#include "sketch/touchingcircles.hpp"
#include "wire/wire.hpp"

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

        void Check() const {
            if (r.center && r.congruent)
                r.Fail("give center (or concentric) or coincident, not both");
            if (r.tangent.size() > 2)
                r.Fail("tangent is one target, two targets or a direction (dh, dv)");
            if (r.congruent && r.radius)
                r.Fail("coincident takes the radius of its reference");
            if (r.to && r.to->at && r.until != Until::No && !r.untilTarget)
                r.Fail("until= picks among the intersections with an element as the end, a point has none; through= gives a "
                       "point the arc passes");
            if (r.to.has_value() + r.sweep.has_value() + (r.until != Until::No && !toElement) > 1 || (r.untilTarget && r.to))
                r.Fail("give the end once: a positional to, until= or sweep=");
        }
        ConstructedArc Draw(const Vertex &s, const Vertex &e, const Vertex &c, bool ccw) const {
            const bool move = !s.IsClose(pen.pos, pen.tol);
            const Vertex from = move ? s : pen.pos;
            if (e.IsClose(from, move ? Tol(NormInf(s)) : pen.tol))
                r.Fail("the arc would be a full circle or empty; use Circle");
            choices.Warn(r.call);
            return {move, from, e, c, ccw};
        }

        ConstructedArc TouchingBoth() {
            const std::string a = r.tangent[0].Text(), b = r.tangent[1].Text();
            if (!r.radius || !r.from || !r.to || !r.from->IsSame(r.tangent[0]) || !r.to->IsSame(r.tangent[1]))
                r.Fail("an arc touching two elements goes from the one to the other: Arc(" + a + ", " + b + ", r, tangent=(" + a +
                       ", " + b + "))");
            if (circle || r.through || r.start || r.sweep || r.until != Until::No)
                r.Fail("an arc touching two elements has its own circle and ends");
            const double radius = std::abs(*r.radius);
            const double tol = Tol(std::max(scale, radius));
            const std::vector<TouchingCircle> found = TouchingCircles(r.tangent[0], r.tangent[1], radius, r.nearPoint, pen, tol, r.call).Ranked(choices);
            if (found.empty())
                r.Fail("no arc of that radius touches both on those sides");
            const TouchingCircle &f = found.front();
            return Draw(f.a, f.b, f.center, Cross(f.a - f.center, f.b - f.center) > 0.0);
        }

        // the arc leaves along the pen's direction, its center at pos + left * rho; rho > 0 turns left
        std::vector<double> RadiiTouching(const PrimitiveEdge &piece) const {
            std::vector<double> out;
            if (const Line *l = std::get_if<Line>(&piece)) {
                const Vertex u = Normalized(l->End() - l->Start()), m{-u.y, u.x};
                const double d0 = Dot(pen.pos - l->Start(), m), k = Dot(left, m);
                for (double side : {1.0, -1.0})
                    out.push_back(d0 / (side - k));
                return out;
            }
            const Arc &a = std::get<Arc>(piece);
            const Vertex w = pen.pos - a.Center();
            const double R = a.Radius();
            for (double side : {1.0, -1.0})
                for (double inner : {1.0, -1.0}) {
                    const double rho = (R * R - Dot(w, w)) / (2.0 * (Dot(w, left) - inner * R * side));
                    if (rho * side > 0.0)
                        out.push_back(rho);
                }
            return out;
        }
        ConstructedArc EndingTouching() {
            const Target &t = r.tangent[0];
            const std::string name = t.Text();
            if (!r.to || !r.to->IsSame(t))
                r.Fail("tangent=" + name + " gives no end; the arc ending touching it is Arc(" + name + ", tangent=" + name + ")");
            if (r.from || r.radius || circle || r.through || r.start || r.until != Until::No)
                r.Fail("an arc ending touching " + name + " leaves the pen along its direction; Arc(" + name + ", tangent=" + name +
                       ") takes nothing else");
            const double tol = Tol(scale);
            std::vector<Touching> found;
            for (const Piece &piece : PiecesOf(t.edges))
                for (double rho : RadiiTouching(piece.e)) {
                    if (!std::isfinite(rho) || std::abs(rho) <= tol)
                        continue;
                    const std::optional<Vertex> p = TouchPoint(piece.e, pen.pos + left * rho, std::abs(rho));
                    if (!p)
                        continue;
                    const bool on = piece.e.Locate(*p, tol) != Location::Off;
                    if (on || piece.carrier)
                        found.push_back({rho, {*p, on}});
                }
            if (found.empty())
                r.Fail("no arc leaving along the pen's direction touches it");
            std::vector<Solution> touches;
            for (const Touching &f : found)
                touches.push_back(f.at);
            const Touching &chosen = found[choices.Take(Preference{t.point.value_or(pen.pos), pen.pos, pen.dir, tol}.Ranked(touches), !t.point)];
            return Draw(pen.pos, chosen.at.p, pen.pos + left * chosen.rho, chosen.rho > 0.0);
        }

        ConstructedArc BetweenPoints() {
            if (toElement || fromElement)
                r.Fail("an element as an end needs the circle: center=, concentric= or coincident=; Arc(r, until=<target>) goes "
                       "along the pen's direction until it");
            const Vertex s = r.from ? *r.from->at : pen.pos, e = *r.to->at;
            const double tol = Tol(scale);
            if (!(Norm(e - s) > tol))
                r.Fail("the arc would end where it starts");
            if (r.through.has_value() + r.start.has_value() + r.radius.has_value() > 1)
                r.Fail("give one of a radius, through= and tangent=(dh, dv)");
            if (r.through)
                return Through(s, e, tol);
            if (r.radius)
                return OfRadius(s, e, tol);
            if (!r.start && r.from)
                r.Fail("two ends alone do not fix the arc; give a radius, through=, tangent=(dh, dv), center=, concentric= or "
                       "coincident=");
            return LeavingAlong(s, e, tol);
        }
        ConstructedArc Through(const Vertex &s, const Vertex &e, double tol) const {
            const Vertex p = *r.through - s, q = e - s;
            const double d = 2.0 * Cross(p, q);
            if (std::abs(d) <= tol * Norm(q))
                r.Fail("the three points lie on a line");
            const Vertex c = s + Vertex{q.y * Dot(p, p) - p.y * Dot(q, q), p.x * Dot(q, q) - q.x * Dot(p, p)} * (1.0 / d);
            return Draw(s, e, c, d > 0.0);
        }
        // counterclockwise the short way, -r clockwise, reverse the long way
        ConstructedArc OfRadius(const Vertex &s, const Vertex &e, double tol) {
            const Vertex chord = e - s;
            const double l = Norm(chord), radius = std::abs(*r.radius);
            if (radius < l / 2.0 - tol)
                r.Fail("the radius is smaller than half the distance between the ends");
            const bool ccw = *r.radius > 0.0;
            const double h = std::sqrt(std::max(0.0, radius * radius - l * l / 4.0));
            if (r.reverse)
                choices.MarkSwitched();
            const Vertex n = Vertex{-chord.y, chord.x} * ((ccw != r.reverse ? h : -h) / l);
            return Draw(s, e, s + chord * 0.5 + n, ccw);
        }
        ConstructedArc LeavingAlong(const Vertex &s, const Vertex &e, double tol) const {
            const Vertex chord = e - s;
            const Vertex t = r.start ? Normalized(*r.start) : pen.dir, n{-t.y, t.x};
            const double k = Dot(chord, n);
            if (std::abs(k) <= tol * Norm(chord))
                r.Fail("the direction at the start runs along the chord");
            const double rho = Dot(chord, chord) / (2.0 * k);
            return Draw(s, e, s + n * rho, rho > 0.0);
        }

        CircleCarrier CircleGiven() const {
            if (!circle) {
                if (!r.radius)
                    r.Fail("give a radius, center=, concentric= or coincident=, or the end with through= or tangent=(dh, dv)");
                const double radius = std::abs(*r.radius);
                // as netgen's Arc(r, ang): with ang its sign picks the side, else the sign of r
                return {pen.pos + left * ((r.sweep ? *r.sweep > 0.0 : *r.radius > 0.0) ? radius : -radius), radius};
            }
            if (r.congruent) {
                const std::optional<CircleCarrier> k = CarrierOf(*r.congruent);
                if (!k)
                    r.Fail("coincident needs an arc");
                return *k;
            }
            return {*r.center, r.radius ? std::abs(*r.radius) : Norm((r.from && r.from->at ? *r.from->at : pen.pos) - *r.center)};
        }
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
        // the first ahead of s (the last with until=px.last), or with near the one nearest it
        std::optional<Vertex> Ahead(const CircleCarrier &k, bool ccw, const Vertex &s, const std::vector<Solution> &hits,
                                    const std::optional<Vertex> &nearPoint, double tol) const {
            const double angleTol = tol / k.radius;
            const bool largest = r.until == Until::Last || nearPoint;
            std::optional<std::pair<double, Vertex>> best;
            for (const Solution &x : hits) {
                const double t = SweepAngle(k.center, ccw, s, x.p);
                const double key = nearPoint ? -Norm(x.p - *nearPoint) : t;
                if (t > angleTol && t < 2.0 * std::numbers::pi - angleTol && (!best || (largest ? key > best->first : key < best->first)))
                    best = {key, x.p};
            }
            return best ? std::optional(best->second) : std::nullopt;
        }
        ConstructedArc OnCircle() {
            if (r.through || r.start)
                r.Fail(circle ? "through= and tangent=(dh, dv) fix the circle themselves; give them without center=, concentric= "
                                "or coincident="
                              : "through= and tangent=(dh, dv) need the end: Arc(to, through=...), Arc(to, tangent=(dh, dv))");
            const CircleCarrier k = CircleGiven();
            if (!(k.radius > 0.0))
                r.Fail("the radius is zero");
            const double tol = Tol(std::max(scale, k.LengthScale()));
            const std::vector<Solution> fromSols = r.from ? Meets(*r.from, "from", k, tol) : std::vector<Solution>{};
            if (r.from && fromSols.empty())
                r.Fail("the circle does not meet from");
            const std::vector<Solution> toSols = r.to ? Meets(*r.to, "to", k, tol) : std::vector<Solution>{};
            if (r.to && toSols.empty())
                r.Fail("the circle does not meet to");
            const Vertex s = StartOnCircle(k, fromSols, tol);
            const bool ccw = IsCcw(k, s);
            if (r.to) {
                const std::optional<Vertex> e = Ahead(k, ccw, s, toSols, r.to->point, tol);
                if (!e)
                    r.Fail("the circle does not meet to");
                return Draw(s, *e, k.center, ccw);
            }
            if (r.sweep) {
                if (std::abs(*r.sweep) >= 360.0) {
                    choices.Warn(r.call);
                    return {!s.IsClose(pen.pos, pen.tol), s, s, k.center, ccw, true};
                }
                return Draw(s, Rotated(s, k.center, ccw ? std::abs(*r.sweep) : -std::abs(*r.sweep)), k.center, ccw);
            }
            if (r.until == Until::No)
                r.Fail("give the end: a positional to, sweep= or until=");
            const std::optional<Vertex> e = Ahead(k, ccw, s, CircleHits(k, sketchEdges), std::nullopt, tol);
            if (!e)
                r.Fail("nothing ahead");
            return Draw(s, *e, k.center, ccw);
        }
    };

}
