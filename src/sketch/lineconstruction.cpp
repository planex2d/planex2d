// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/lineconstruction.hpp"

#include <cmath>
#include <stdexcept>

#include "planex/curves/circlecarrier.hpp"

namespace planex2d::sketch {

    static std::optional<size_t> NearestSolution(const std::vector<Solution> &points, const Vertex &other, size_t except, double tol) {
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
    static std::optional<std::pair<size_t, size_t>> ClosestPair(const std::vector<Solution> &a, const std::vector<Solution> &b, double tol, Choices &choices) {
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
        const std::optional<size_t> i = NearestSolution(a, b[best->second].p, best->first, tol), j = NearestSolution(b, a[best->first].p, best->second, tol);
        if (i || j)
            choices.MarkSwitched();
        else
            choices.MarkOnlyOne();
        return std::pair{i.value_or(best->first), j.value_or(best->second)};
    }

    static void TangentsFrom(const Arc &a, const Vertex &p, double tol, bool carrier, std::vector<Solution> &points, std::vector<Vertex> &along) {
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

    static std::optional<CircleCarrier> CommonCircle(const EdgeList &edges, double tol) {
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

    static std::vector<std::pair<Vertex, Vertex>> Bitangents(const CircleCarrier &a, const CircleCarrier &b) {
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

    ConstructedLine LineConstruction::Drawn() {
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

    void LineConstruction::Check() const {
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

    void LineConstruction::FindDirection() {
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

    void LineConstruction::TouchBoth() {
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

    void LineConstruction::TouchOne() {
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

    std::vector<std::pair<double, Vertex>> LineConstruction::Hits(const EdgeList &targets, const Vertex &p) const {
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

    std::vector<Solution> LineConstruction::Meets(const Target &t, const std::string &what) const {
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

    std::pair<std::optional<Vertex>, std::optional<Vertex>> LineConstruction::ChosenEnds(const std::vector<Solution> &fromSols,
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

    Vertex LineConstruction::DefaultEnd(const Vertex &s, const std::vector<Solution> &toSols, bool toFree) {
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

    // d on the side of the point, -d on the other
    void LineRefs::SetDistanceTowards(double d, const Vertex &side) {
        const Line *ref = parallel ? std::get_if<Line>(&*parallel) : nullptr;
        if (!ref)
            throw std::invalid_argument(call + ": distance=(d, point) needs a line for parallel");
        const double left = Cross(ref->End() - ref->Start(), side - ref->Start());
        if (left == 0.0)
            throw std::invalid_argument(call + ": the point of distance=(d, point) lies on the line it is parallel to");
        distance = left > 0.0 ? d : -d;
    }

    // d on the side of the reference that at is on; at on the reference: d to its left
    void LineRefs::SetDistanceFrom(double d, const Vertex &at) {
        const Line *ref = parallel ? std::get_if<Line>(&*parallel) : nullptr;
        distance = d;
        if (!ref)
            return;
        const Vertex u = ref->End() - ref->Start();
        if (Cross(u, at - ref->Start()) / Norm(u) < -Tol(NormInf(at)))
            distance = -d;
    }

}
