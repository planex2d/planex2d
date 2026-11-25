// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/arcconstruction.hpp"

#include <utility>

namespace planex2d::sketch {

    void ArcConstruction::Check() const {
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

    ConstructedArc ArcConstruction::TouchingBoth() {
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
    std::vector<double> ArcConstruction::RadiiTouching(const PrimitiveEdge &piece) const {
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

    ConstructedArc ArcConstruction::EndingTouching() {
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

    ConstructedArc ArcConstruction::BetweenPoints() {
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

    // counterclockwise the short way, -r clockwise, reverse the long way
    ConstructedArc ArcConstruction::OfRadius(const Vertex &s, const Vertex &e, double tol) {
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

    CircleCarrier ArcConstruction::CircleGiven() const {
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

    // the first ahead of s (the last with until=px.last), or with near the one nearest it
    std::optional<Vertex> ArcConstruction::Ahead(const CircleCarrier &k, bool ccw, const Vertex &s, const std::vector<Solution> &hits,
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

    ConstructedArc ArcConstruction::OnCircle() {
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

}
