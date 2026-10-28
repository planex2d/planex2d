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
#include "wire/wire.hpp"

namespace planex2d::sketch {

    inline std::optional<size_t> NearestTo(const std::vector<Solution> &points, const Vertex &other, size_t except, double tol) {
        std::optional<size_t> found;
        double nearest = 0.0;
        for (size_t k = 0; k < points.size(); ++k) {
            if (k == except || points[k].p.IsClose(other, tol))
                continue;
            const double d = Norm(points[k].p - other);
            if (!found || d < nearest) {
                found = k;
                nearest = d;
            }
        }
        return found;
    }

    // reversed, each side takes its second best against the other side's first choice
    inline std::optional<std::pair<size_t, size_t>> ClosestPair(const std::vector<Solution> &a, const std::vector<Solution> &b, double tol,
                                                                Choices &choices) {
        std::optional<std::pair<size_t, size_t>> best;
        double nearest = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = 0; j < b.size(); ++j)
                if (!a[i].p.IsClose(b[j].p, tol) && (!best || Norm(a[i].p - b[j].p) < nearest)) {
                    best = {i, j};
                    nearest = Norm(a[i].p - b[j].p);
                }
        if (!best || !choices.IsReverse())
            return best;
        const std::optional<size_t> i = NearestTo(a, b[best->second].p, best->first, tol), j = NearestTo(b, a[best->first].p, best->second, tol);
        if (i || j)
            choices.MarkSwitched();
        else
            choices.MarkOnlyOne();
        return std::pair{i.value_or(best->first), j.value_or(best->second)};
    }

    // where the lines from p touch the circle of the arc, or the direction along it if p lies on it
    inline void TangentsFrom(const Arc &a, const Vertex &p, double tol, bool carrier, std::vector<Solution> &points,
                             std::vector<Vertex> &along) {
        const Vertex v = p - a.Center();
        const double d = Norm(v), r = a.Radius();
        if (std::abs(d - r) <= tol) {
            if (carrier || a.Locate(p, tol) != Location::Off)
                along.push_back(Vertex{-v.y, v.x} / d);
            return;
        }
        if (d < r)
            return;
        const Vertex base = a.Center() + v * (r * r / (d * d));
        const Vertex n = Vertex{-v.y, v.x} * (r * std::sqrt((d - r) * (d + r)) / (d * d));
        for (const Vertex &t : {base + n, base - n}) {
            const bool on = a.Locate(t, tol) != Location::Off;
            if (on || carrier)
                points.push_back({t, on});
        }
    }

    inline std::optional<CircleCarrier> CommonCircle(const EdgeList &edges, double tol) {
        std::optional<CircleCarrier> c;
        for (const Edge &e : edges) {
            const std::optional<CircleCarrier> k = CarrierOf(e);
            if (!k)
                return std::nullopt;
            if (!c)
                c = k;
            else if (!c->center.IsClose(k->center, tol) || std::abs(c->radius - k->radius) > tol)
                return std::nullopt;
        }
        return c;
    }

    // the touch points on a and b of the lines touching both
    inline std::vector<std::pair<Vertex, Vertex>> Bitangents(const CircleCarrier &a, const CircleCarrier &b) {
        const Vertex between = b.center - a.center;
        const double d = Norm(between);
        std::vector<std::pair<Vertex, Vertex>> out;
        if (d == 0.0)
            return out;
        const Vertex u = between / d, v{-u.y, u.x};
        // the unit normal n of a touching line: n.u = k; b is touched on a's side (s = 1) or the other (s = -1)
        for (double s : {1.0, -1.0}) {
            const double k = (a.radius - s * b.radius) / d;
            if (std::abs(k) > 1.0)
                continue;
            const double h = std::sqrt((1.0 - k) * (1.0 + k));
            for (double sign : {1.0, -1.0}) {
                const Vertex n = u * k + v * (sign * h);
                out.emplace_back(a.center + n * a.radius, b.center + n * (s * b.radius));
                if (h == 0.0)
                    break;
            }
        }
        return out;
    }

    struct LineRefs : EndRefs {
        std::optional<double> length;
        std::optional<Edge> parallel, perpendicular, congruent, angleRef;
        std::optional<double> angle, distance;
        std::optional<Vertex> through;
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

        ConstructedLine Drawn() {
            const bool anyFree = AnyFree();
            FindDirection();
            if (tangentPair)
                TouchBoth();
            if (r.tangent.size() == 1)
                TouchOne();
            // with no free choice reverse turns a line that runs forward around
            if (r.reverse && !anyFree && (r.length || r.until != Until::No)) {
                direction = direction * -1.0;
                choices.MarkSwitched();
            }
            const std::vector<Solution> fromSols = r.from && !fromTouch ? Meets(*r.from, "from") : std::vector<Solution>{};
            const std::vector<Solution> toSols = r.to && !toTouch ? Meets(*r.to, "to") : std::vector<Solution>{};
            const bool fromFree = r.from && !fromTouch && !r.from->point && !fromAt;
            const bool toFree = r.to && !toTouch && !r.to->point && !toAt;
            auto [s, e] = ChosenEnds(fromSols, toSols, fromFree, toFree);
            if (!s)
                s = DefaultStart();
            if (!e)
                e = DefaultEnd(*s, toSols, toFree);
            if (fromElement && toElement && e->IsClose(*s, tol))
                r.Fail("the line would end where it starts");
            choices.Warn(r.call);
            return {s->IsClose(pen.pos, pen.tol) ? pen.pos : *s, *e};
        }

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

        void Check() const {
            if (r.tangent.size() > 2)
                r.Fail("tangent is one target or two");
            if (r.parallel.has_value() + r.perpendicular.has_value() + r.angle.has_value() + r.congruent.has_value() +
                    !r.tangent.empty() > 1)
                r.Fail("give at most one of parallel, perpendicular, angle, coincident and tangent");
            if (r.to && r.to->at && r.until != Until::No && !r.untilTarget)
                r.Fail("until= picks among the intersections with an element as the end, a point has none; through= gives a "
                       "point the line passes");
            if (r.length.has_value() + r.to.has_value() + (r.until != Until::No && !toElement) > 1 || (r.untilTarget && r.to))
                r.Fail("give the end once: a length, a positional to or until=");
            if (r.distance && !r.parallel)
                r.Fail("distance needs parallel");
            if (r.distance && r.through)
                r.Fail("give distance or through, not both");
            if (r.distance && !r.length && !r.to && r.until == Until::No)
                r.Fail("parallel with distance needs the end: a length sk.Line(20, parallel=..., distance=...), an end target "
                       "sk.Line(\"B\", parallel=..., distance=...), until=\"first\" or until=\"last\"; sk.Offset(\"L1\", d) "
                       "draws an offset copy");
            if (r.tangent.size() == 1 && (r.through || fromElement))
                r.Fail("a tangent starts at the pen or at a point given as from");
            if (tangentPair && r.through)
                r.Fail("a line touching two targets lies where they are");
            if ((fromElement || toElement) && !directed && !(r.through && !fromElement))
                r.Fail("an element as an end needs what fixes the line: through=, parallel=, perpendicular=, angle=, coincident= "
                       "or tangent=; until=<target> goes along the pen's direction until it");
        }
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
        void FindDirection() {
            if (r.parallel) {
                const Vertex u = DirectionOf(*r.parallel, "parallel");
                direction = Sense(u);
                if (r.distance)
                    anchor = AsLine(*r.parallel, "parallel").Start() + Vertex{-u.y, u.x} * *r.distance;
            } else if (r.perpendicular) {
                const Vertex u = DirectionOf(*r.perpendicular, "perpendicular");
                direction = Sense(Vertex{-u.y, u.x});
            } else if (r.angle) {
                direction = Rotated(r.angleRef ? DirectionOf(*r.angleRef, "angle") : Vertex{1.0, 0.0}, *r.angle);
            } else if (r.congruent) {
                direction = Sense(DirectionOf(*r.congruent, "coincident"));
                anchor = AsLine(*r.congruent, "coincident").Start();
            } else if (throughGivesDirection) {
                if (!(Norm(*r.through - origin) > tol))
                    r.Fail("through= is the start; give a point the line passes after it");
                direction = Normalized(*r.through - origin);
                anchor = origin;
            } else if (toAt && r.tangent.empty()) {
                if (!(Norm(*toAt - anchor) > tol))
                    r.Fail("the line would end where it starts");
                direction = Normalized(*toAt - anchor);
            }
        }
        void TouchBoth() {
            const std::optional<CircleCarrier> a = CommonCircle(r.tangent[0].edges, tol), b = CommonCircle(r.tangent[1].edges, tol);
            if (!a || !b)
                r.Fail("a line touching two targets needs one circle or arc for each");
            const Vertex between = b->center - a->center;
            const auto side = [&](const Vertex &p, const CircleCarrier &c) { return Cross(between, p - c.center) >= 0.0 ? 1.0 : -1.0; };
            std::array<double, 2> want;
            for (size_t k = 0; k < 2; ++k) {
                const Target &t = r.tangent[k];
                want[k] = side(t.point.value_or(nearPoint), k == 0 ? *a : *b);
                if (r.reverse && !t.point) {
                    want[k] = -want[k];
                    choices.MarkSwitched();
                }
            }
            bool found = false;
            for (const auto &[ta, tb] : Bitangents(*a, *b))
                if (!found && side(ta, *a) == want[0] && side(tb, *b) == want[1]) {
                    touch = {ta, tb};
                    found = true;
                }
            if (!found)
                r.Fail("no line touches both on those sides");
            direction = Normalized(touch[1] - touch[0]);
            anchor = touch[0];
        }
        void TouchOne() {
            const Target &t = r.tangent[0];
            std::vector<Solution> sols;
            std::vector<Vertex> along;
            for (const Piece &piece : PiecesOf(t.edges))
                if (const Arc *a = std::get_if<Arc>(&piece.e))
                    TangentsFrom(*a, origin, tol, piece.carrier, sols, along);
            if (!along.empty()) {
                direction = Sense(along.front());
                if (r.reverse && !t.point) {
                    direction = direction * -1.0;
                    choices.MarkSwitched();
                }
            } else if (!sols.empty()) {
                touchPoint = sols[choices.Take(Preference{t.point.value_or(nearPoint), origin, pen.dir, tol}.Ranked(sols), !t.point)].p;
                direction = Normalized(*touchPoint - origin);
            } else {
                r.Fail("no tangent from the current position");
            }
        }
        // with their position along the direction
        std::vector<std::pair<double, Vertex>> Hits(const EdgeList &targets, const Vertex &p) const {
            if (targets.empty())
                return {};
            const BBox box = targets.Box();
            const double reach = 2.0 * (Norm(p - box.Center()) + Norm(box.Size()));
            Wire carrier(axes);
            carrier.Add(Line(p - direction * reach, p + direction * reach));
            std::vector<std::pair<double, Vertex>> out;
            for (const Vertex &x : CarrierHits(carrier, targets))
                out.emplace_back(Dot(x - p, direction), x);
            return out;
        }
        std::vector<Solution> Meets(const Target &t, const std::string &what) const {
            if (t.at) {
                if (std::abs(Cross(direction, *t.at - anchor)) > tol)
                    r.Fail(what + " " + PointText(*t.at) + " is not on the line");
                return {{*t.at}};
            }
            std::vector<Solution> v;
            for (const auto &[at, x] : Hits(t.edges, anchor))
                v.push_back({x});
            if (v.empty())
                r.Fail("the line does not meet " + what);
            return v;
        }
        std::pair<std::optional<Vertex>, std::optional<Vertex>> ChosenEnds(const std::vector<Solution> &fromSols,
                                                                           const std::vector<Solution> &toSols, bool fromFree, bool toFree) {
            if (fromFree && toFree && !r.nearPoint) {
                const std::optional<std::pair<size_t, size_t>> pair = ClosestPair(fromSols, toSols, tol, choices);
                if (!pair)
                    r.Fail("from and to meet the line in the same point only");
                return {fromSols[pair->first].p, toSols[pair->second].p};
            }
            std::optional<Vertex> s, e;
            if (fromTouch)
                s = touch[*fromTouch];
            if (toTouch)
                e = touch[*toTouch];
            if (r.to && !toTouch && r.to->point)
                e = toSols[Preference{*r.to->point, anchor, direction, tol}.Ranked(toSols)[0]].p;
            if (r.from && !fromTouch) {
                const Vertex ref = r.from->point ? *r.from->point : r.nearPoint ? *r.nearPoint : e ? *e : pen.pos;
                s = fromSols[choices.Take(Preference{ref, anchor, direction, tol}.Ranked(fromSols), fromFree)].p;
            }
            return {s, e};
        }
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
        Vertex DefaultEnd(const Vertex &s, const std::vector<Solution> &toSols, bool toFree) {
            if (r.to && r.until != Until::No) {
                std::vector<std::pair<double, Vertex>> along;
                for (const Solution &x : toSols)
                    along.emplace_back(Dot(x.p - s, direction), x.p);
                const std::optional<Vertex> e = Ahead(along);
                if (!e)
                    r.Fail("the line does not meet to ahead");
                return *e;
            }
            if (r.to) {
                std::vector<Solution> ends;
                for (const Solution &x : toSols)
                    if (!x.p.IsClose(s, tol))
                        ends.push_back(x);
                if (ends.empty())
                    r.Fail("the line does not meet to");
                return ends[choices.Take(Preference{r.nearPoint.value_or(s), s, direction, tol}.Ranked(ends),
                                         toFree)].p;
            }
            if (r.length)
                return s + direction * *r.length;
            if (r.until != Until::No) {
                const std::optional<Vertex> e = Ahead(Hits(sketchEdges, s));
                if (!e)
                    r.Fail("nothing ahead; until=px.first and px.last see the sketch's own elements, until=<target> goes until a "
                           "given target");
                return *e;
            }
            if (touchPoint)
                return *touchPoint;
            if (tangentPair)
                return s.IsClose(touch[0], tol) ? touch[1] : touch[0];
            r.Fail("give the end: a length, a positional to or until=");
        }
    };

}
