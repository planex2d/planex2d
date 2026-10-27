// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "sketch/geometry.hpp"

// The relations auto constraints find between the elements of a sketch, where they hold to rounding or
// within an absolute tolerance, strongest first: a relation that follows from those before it is then a weaker one.

namespace planex2d::sketch {

    struct FoundRelation {
        Kind kind;
        std::vector<Ref> refs;
    };

    class RelationFinder {
    public:
        // isNew: per element, a relation needs a new one; skip: elements never related (the origin, the axes); tol: an
        // absolute length, none for rounding
        RelationFinder(const Geometry &g, const AutoConstraint &o, std::vector<char> isNew, std::vector<char> skip,
                       std::optional<double> tol = std::nullopt)
            : g(g), o(o), isNew(std::move(isNew)), skip(std::move(skip)), tol(tol) {}

        std::vector<FoundRelation> Find() {
            Places();
            Clusters();
            if (o.point_on_curve) {
                PenPoints();
                PointsOnCurves();
            }
            if (o.tangent)
                Tangents();
            Lines();
            if (o.equal_radius)
                EqualRadii();
            std::vector<FoundRelation> out;
            for (std::vector<FoundRelation> &kind : found)
                for (FoundRelation &r : kind)
                    out.push_back(std::move(r));
            return out;
        }

    private:
        enum Order { CoincidentOrder, ConcentricOrder, CollinearOrder, OnCurveOrder, TangentOrder, HorizontalOrder, VerticalOrder,
                     PerpendicularOrder, ParallelOrder, EqualRadiusOrder, EqualLengthOrder, Orders };

        struct End {
            size_t element;
            bool atEnd;
        };
        // a place points share (one unknown pair): the first reference to it, an end or a Point before a centre; a centre
        // when only rounds' centres (and their Points) are there
        struct Place {
            size_t unknown;
            Vertex at;
            Ref ref;
            size_t round = none;
            bool refIsCentre = true;
            bool isNew = false;
            std::vector<size_t> elements;
            std::vector<End> ends;

            bool IsCentre() const { return round != none && ends.empty(); }
        };
        struct LineData {
            size_t e;
            Vertex a, d;
            double length, size;
        };

        double Near(double size) const { return tol ? *tol : Tol(size); }
        bool Fresh(size_t a, size_t b) const { return isNew[a] || isNew[b]; }
        bool Used(size_t k) const {
            if (skip[k])
                return false;
            const ElementKind kind = g.elements[k].kind;
            return kind == ElementKind::Point || kind == ElementKind::Line || kind == ElementKind::Arc || kind == ElementKind::Circle ||
                   kind == ElementKind::Spline;
        }
        void Add(Order order, Kind kind, std::vector<Ref> refs) { found[order].push_back({kind, std::move(refs)}); }

        void AddAt(size_t point, Ref ref, std::optional<End> end) {
            const size_t u = g.coordsOf[point];
            if (skipped.contains(u))
                return;
            auto [it, made] = placeOf.try_emplace(u, places.size());
            if (made)
                places.push_back({u, g.Coords(point), ref});
            Place &p = places[it->second];
            const bool centre = ref.part == Part::Center;
            if (centre && p.round == none)
                p.round = ref.element;
            if (!centre && p.refIsCentre) {
                p.ref = ref;
                p.refIsCentre = false;
            }
            p.isNew = p.isNew || isNew[ref.element];
            p.elements.push_back(ref.element);
            if (end)
                p.ends.push_back(*end);
        }
        void Places() {
            for (size_t k = 0; k < g.elements.size(); ++k)
                if (skip[k] && g.elements[k].kind == ElementKind::Point)
                    skipped.insert(g.coordsOf[g.elements[k].Point()]);
            for (size_t k = 0; k < g.elements.size(); ++k) {
                if (!Used(k))
                    continue;
                const Element &e = g.elements[k];
                if (e.kind == ElementKind::Point)
                    AddAt(e.Point(), {k}, std::nullopt);
                if (e.kind == ElementKind::Line || e.kind == ElementKind::Arc || e.kind == ElementKind::Spline) {
                    const std::array<size_t, 2> ends = e.Ends(g);
                    AddAt(ends[0], {k, Part::Start}, End{k, false});
                    AddAt(ends[1], {k, Part::End}, End{k, true});
                }
                if (e.kind == ElementKind::Arc || e.kind == ElementKind::Circle)
                    AddAt(e.Center(), {k, Part::Center}, std::nullopt);
            }
            parent.resize(places.size());
            std::iota(parent.begin(), parent.end(), size_t(0));
            first.assign(places.size(), none);
        }
        size_t Root(size_t p) {
            while (parent[p] != p)
                p = parent[p] = parent[parent[p]];
            return p;
        }
        std::optional<size_t> ClassOf(size_t point) {
            const auto it = placeOf.find(g.coordsOf[point]);
            return it == placeOf.end() ? std::nullopt : std::optional(Root(it->second));
        }

        // points apart at one place: each related to the first of them, centres of two rounds concentric
        void Clusters() {
            std::vector<size_t> order(places.size());
            std::iota(order.begin(), order.end(), size_t(0));
            std::sort(order.begin(), order.end(), [&](size_t i, size_t j) { return places[i].at.x < places[j].at.x; });
            for (size_t a = 0; a < order.size(); ++a) {
                const Vertex p = places[order[a]].at;
                const double near = Near(NormInf(p));
                for (size_t b = a + 1; b < order.size() && places[order[b]].at.x - p.x <= near; ++b) {
                    const size_t i = std::min(order[a], order[b]), j = std::max(order[a], order[b]);
                    if (first[j] == none && places[j].at.IsClose(places[i].at, near))
                        first[j] = first[i] == none ? i : first[i];
                }
            }
            for (size_t j = 0; j < places.size(); ++j) {
                const size_t f = first[j];
                if (f == none || !(places[f].isNew || places[j].isNew))
                    continue;
                if (places[f].IsCentre() && places[j].IsCentre()) {
                    if (o.concentric)
                        Add(ConcentricOrder, Kind::Concentric, {{places[f].round}, {places[j].round}});
                } else if (o.coincident) {
                    Add(CoincidentOrder, Kind::Coincident, {places[f].ref, places[j].ref});
                    parent[Root(j)] = Root(f);
                }
            }
        }

        // the direction an element runs at its start or end, as long as the arm that turns it (a line's length, an
        // arc's radius, a spline's chord)
        Vertex Forward(size_t k, bool atEnd) const {
            const Element &e = g.elements[k];
            if (e.kind == ElementKind::Line)
                return g.Coords(e.End()) - g.Coords(e.Start());
            if (e.kind == ElementKind::Arc || e.kind == ElementKind::Circle) {
                const size_t at = e.kind == ElementKind::Circle ? e.OriginPoint() : atEnd ? e.End() : e.Start();
                const Vertex r = g.Coords(at) - g.Coords(e.Center());
                return e.ccw ? Vertex{-r.y, r.x} : Vertex{r.y, -r.x};
            }
            const Curve<double, Vertex> f = e.Curve(g);
            const auto [from, to] = e.Range(g, f);
            const double end = f.params.back();
            double t = atEnd ? to : from;
            if (t > end)
                t -= end;
            const Vertex d = f.At(t, 1);
            const std::array<Vertex, 2> ends = e.EndCoords(g);
            const double arm = Norm(ends[1] - ends[0]), n = Norm(d);
            return n > 0.0 && arm > 0.0 ? d * (arm / n) : d;
        }
        // x and y run along one line where they meet, in the same direction or back
        bool Tangential(End x, End y, const Vertex &at) const {
            const Vertex u = Forward(x.element, x.atEnd), v = Forward(y.element, y.atEnd);
            return std::abs(Cross(u, v)) <= Near(NormInf(at)) * (Norm(u) + Norm(v));
        }
        bool IsLine(size_t k) const { return g.elements[k].kind == ElementKind::Line; }

        // the ends at one place (shared or found coincident) that touch there, not two lines
        void Tangents() {
            std::unordered_map<size_t, std::vector<std::pair<End, size_t>>> at;
            for (size_t p = 0; p < places.size(); ++p)
                for (const End &e : places[p].ends)
                    at[Root(p)].push_back({e, p});
            std::set<std::pair<size_t, size_t>> done;
            for (const auto &[root, ends] : at)
                for (size_t i = 0; i < ends.size(); ++i)
                    for (size_t j = i + 1; j < ends.size(); ++j) {
                        const End x = ends[i].first, y = ends[j].first;
                        const size_t a = std::min(x.element, y.element), b = std::max(x.element, y.element);
                        if (a == b || (IsLine(a) && IsLine(b)) || !Fresh(a, b) || done.contains({a, b}))
                            continue;
                        if (Tangential(x, y, places[ends[i].second].at)) {
                            done.insert({a, b});
                            // in the chain's order where one ends and the other starts there
                            const bool yFirst = x.atEnd == y.atEnd ? y.element < x.element : y.atEnd;
                            Add(TangentOrder, Kind::Tangent, {{yFirst ? y.element : x.element}, {yFirst ? x.element : y.element}});
                        }
                    }
        }
        // a full turn's pen point (a Point) on its circle, tangent to what ends there
        void PenPoints() {
            for (size_t k = 0; k < g.elements.size(); ++k) {
                const Element &c = g.elements[k];
                if (!Used(k) || c.kind != ElementKind::Circle || c.OriginPoint() == none)
                    continue;
                const auto it = placeOf.find(g.coordsOf[c.OriginPoint()]);
                if (it == placeOf.end())
                    continue;
                const Place &p = places[it->second];
                const auto point = std::find_if(p.elements.begin(), p.elements.end(), [&](size_t e) { return g.elements[e].kind == ElementKind::Point; });
                if (point == p.elements.end() || !Fresh(*point, k))
                    continue;
                Add(OnCurveOrder, Kind::Coincident, {{*point}, {k}});
                if (o.tangent)
                    for (const End &e : p.ends)
                        if (Fresh(e.element, k) && Tangential(e, {k, false}, p.at))
                            Add(TangentOrder, Kind::Tangent, {{e.element}, {k}});
            }
        }

        // the ends and Points (one per place found coincident) on a line, arc or circle, away from its ends
        void PointsOnCurves() {
            std::vector<size_t> targets;
            Vertex lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
            std::vector<std::pair<Vertex, Vertex>> boxes(g.elements.size());
            for (size_t k = 0; k < g.elements.size(); ++k) {
                const Element &e = g.elements[k];
                if (!Used(k) || !(e.kind == ElementKind::Line || e.kind == ElementKind::Arc || e.kind == ElementKind::Circle))
                    continue;
                Vertex a, b;
                if (e.kind == ElementKind::Line) {
                    const Vertex p = g.Coords(e.Start()), q = g.Coords(e.End());
                    a = {std::min(p.x, q.x), std::min(p.y, q.y)};
                    b = {std::max(p.x, q.x), std::max(p.y, q.y)};
                } else {
                    const Vertex c = g.Coords(e.Center());
                    const double r = std::abs(g.values[e.Radius()]);
                    a = {c.x - r, c.y - r};
                    b = {c.x + r, c.y + r};
                }
                const double near = Near(std::max(NormInf(a), NormInf(b)));
                boxes[k] = {{a.x - near, a.y - near}, {b.x + near, b.y + near}};
                lo = {std::min(lo.x, boxes[k].first.x), std::min(lo.y, boxes[k].first.y)};
                hi = {std::max(hi.x, boxes[k].second.x), std::max(hi.y, boxes[k].second.y)};
                targets.push_back(k);
            }
            if (targets.empty())
                return;
            const double cell = std::max({(hi.x - lo.x + hi.y - lo.y) / (2.0 * std::sqrt(double(targets.size()))), 1e-300});
            const auto index = [&](double v, double from) { return (int64_t)std::floor((v - from) / cell); };
            const auto key = [](int64_t i, int64_t j) { return (i << 32) ^ (j & 0xffffffff); };
            std::unordered_map<int64_t, std::vector<size_t>> grid;
            for (size_t k : targets)
                for (int64_t i = index(boxes[k].first.x, lo.x); i <= index(boxes[k].second.x, lo.x); ++i)
                    for (int64_t j = index(boxes[k].first.y, lo.y); j <= index(boxes[k].second.y, lo.y); ++j)
                        grid[key(i, j)].push_back(k);
            for (size_t p = 0; p < places.size(); ++p) {
                const Place &place = places[p];
                if (place.IsCentre() || first[p] != none)
                    continue;
                const auto it = grid.find(key(index(place.at.x, lo.x), index(place.at.y, lo.y)));
                if (it == grid.end())
                    continue;
                for (size_t k : it->second)
                    if ((place.isNew || isNew[k]) &&
                        std::find(place.elements.begin(), place.elements.end(), k) == place.elements.end() && OnCurve(k, place))
                        Add(OnCurveOrder, Kind::Coincident, {place.ref, {k}});
            }
        }
        bool OnCurve(size_t k, const Place &place) const {
            const Element &e = g.elements[k];
            const Vertex p = place.at;
            if (e.kind == ElementKind::Line) {
                const Vertex a = g.Coords(e.Start()), d = g.Coords(e.End()) - a;
                const double length = Norm(d), near = Near(std::max(NormInf(p), NormInf(a)));
                if (!(length > 2.0 * near))
                    return false;
                const double along = Dot(p - a, d) / length;
                return std::abs(Cross(p - a, d)) / length <= near && along > near && along < length - near;
            }
            const Vertex c = g.Coords(e.Center());
            const double r = std::abs(g.values[e.Radius()]), near = Near(std::max(NormInf(p), NormInf(c) + r));
            if (std::abs(Norm(p - c) - r) > near)
                return false;
            if (e.kind == ElementKind::Circle)
                return e.OriginPoint() == none || g.coordsOf[e.OriginPoint()] != place.unknown;
            return Norm(p - g.Coords(e.Start())) > near && Norm(p - g.Coords(e.End())) > near && e.Param(g, p) < 1.0;
        }

        bool Shares(const LineData &a, const LineData &b) {
            const std::array<size_t, 2> x = g.elements[a.e].Ends(g), y = g.elements[b.e].Ends(g);
            for (size_t p : x)
                for (size_t q : y) {
                    const std::optional<size_t> cp = ClassOf(p), cq = ClassOf(q);
                    if ((cp && cq && *cp == *cq) || g.coordsOf[p] == g.coordsOf[q])
                        return true;
                }
            return false;
        }
        // the end of line b not at the place it shares with a
        Part FarEnd(const LineData &a, const LineData &b) {
            const std::array<size_t, 2> x = g.elements[a.e].Ends(g);
            const size_t start = g.elements[b.e].Start();
            for (size_t p : x) {
                const std::optional<size_t> cp = ClassOf(p), cq = ClassOf(start);
                if ((cp && cq && *cp == *cq) || g.coordsOf[p] == g.coordsOf[start])
                    return Part::End;
            }
            return Part::Start;
        }
        bool Parallel(const LineData &a, const LineData &b) const {
            return std::abs(Cross(a.d, b.d)) <= Near(std::max(a.size, b.size)) * (a.length + b.length);
        }
        bool OnCarrier(const LineData &a, const LineData &b) const {
            const double near = Near(std::max(a.size, b.size));
            return std::abs(Cross(b.a - a.a, a.d)) / a.length <= near && std::abs(Cross(b.a + b.d - a.a, a.d)) / a.length <= near;
        }

        // horizontal, vertical, perpendicular at a corner, collinear, parallel, equal length
        void Lines() {
            std::vector<LineData> lines;
            for (size_t k = 0; k < g.elements.size(); ++k)
                if (Used(k) && IsLine(k)) {
                    const Vertex a = g.Coords(g.elements[k].Start()), b = g.Coords(g.elements[k].End());
                    const double size = std::max(NormInf(a), NormInf(b));
                    if (Norm(b - a) > 2.0 * Near(size))
                        lines.push_back({k, a, b - a, Norm(b - a), size});
                }
            std::vector<char> hv(g.elements.size(), 0);
            for (const LineData &l : lines) {
                const double near = Near(l.size);
                if (o.horizontal && isNew[l.e] && std::abs(l.d.y) <= near) {
                    hv[l.e] = 'h';
                    Add(HorizontalOrder, Kind::Horizontal, {{l.e}});
                } else if (o.vertical && isNew[l.e] && std::abs(l.d.x) <= near) {
                    hv[l.e] = 'v';
                    Add(VerticalOrder, Kind::Vertical, {{l.e}});
                }
            }
            if (o.perpendicular)
                Perpendiculars(lines, hv);
            if (o.parallel || o.collinear)
                ParallelRuns(lines, hv);
            if (o.equal_length)
                EqualLengths(lines);
        }
        void Perpendiculars(const std::vector<LineData> &lines, const std::vector<char> &hv) {
            std::unordered_map<size_t, std::vector<size_t>> at;
            for (size_t i = 0; i < lines.size(); ++i)
                for (size_t p : g.elements[lines[i].e].Ends(g))
                    if (const std::optional<size_t> c = ClassOf(p))
                        at[*c].push_back(i);
            std::set<std::pair<size_t, size_t>> done;
            for (const auto &[c, here] : at)
                for (size_t i = 0; i < here.size(); ++i)
                    for (size_t j = i + 1; j < here.size(); ++j) {
                        const LineData &a = lines[std::min(here[i], here[j])], &b = lines[std::max(here[i], here[j])];
                        if (a.e == b.e || !Fresh(a.e, b.e) || (hv[a.e] && hv[b.e] && hv[a.e] != hv[b.e]) || done.contains({a.e, b.e}))
                            continue;
                        if (std::abs(Dot(a.d, b.d)) <= Near(std::max(a.size, b.size)) * (a.length + b.length)) {
                            done.insert({a.e, b.e});
                            Add(PerpendicularOrder, Kind::Perpendicular, {{a.e}, {b.e}});
                        }
                    }
        }
        // lines by direction: on one carrier collinear with the first there, else parallel to the run's first
        void ParallelRuns(const std::vector<LineData> &lines, const std::vector<char> &hv) {
            const auto angle = [&](size_t i) {
                const double t = std::atan2(lines[i].d.y, lines[i].d.x);
                return t < 0.0 ? t + std::numbers::pi : t >= std::numbers::pi ? t - std::numbers::pi : t;
            };
            std::vector<size_t> order(lines.size());
            std::iota(order.begin(), order.end(), size_t(0));
            std::sort(order.begin(), order.end(), [&](size_t i, size_t j) { return angle(i) < angle(j); });
            std::vector<std::vector<size_t>> runs;
            for (size_t i : order)
                if (!runs.empty() && Parallel(lines[runs.back().front()], lines[i]))
                    runs.back().push_back(i);
                else
                    runs.push_back({i});
            if (runs.size() > 1 && Parallel(lines[runs.back().front()], lines[runs.front().front()])) {
                runs.front().insert(runs.front().end(), runs.back().begin(), runs.back().end());
                runs.pop_back();
            }
            for (std::vector<size_t> &run : runs) {
                std::sort(run.begin(), run.end());
                std::vector<size_t> reps;
                if (o.collinear) {
                    const LineData &base = lines[run.front()];
                    const Vertex u = base.d * (1.0 / base.length);
                    std::vector<size_t> byOffset = run;
                    const auto offset = [&](size_t i) { return Cross(u, lines[i].a); };
                    std::stable_sort(byOffset.begin(), byOffset.end(), [&](size_t i, size_t j) { return offset(i) < offset(j); });
                    std::vector<std::vector<size_t>> carriers;
                    for (size_t i : byOffset)
                        if (!carriers.empty() && OnCarrier(lines[carriers.back().front()], lines[i]))
                            carriers.back().push_back(i);
                        else
                            carriers.push_back({i});
                    for (std::vector<size_t> &on : carriers) {
                        std::sort(on.begin(), on.end());
                        reps.push_back(on.front());
                        const LineData &f = lines[on.front()];
                        for (size_t m = 1; m < on.size(); ++m) {
                            const LineData &l = lines[on[m]];
                            if (!Fresh(f.e, l.e))
                                continue;
                            if (Shares(f, l))
                                Add(CollinearOrder, Kind::Coincident, {{l.e, FarEnd(f, l)}, {f.e}});
                            else
                                Add(CollinearOrder, Kind::Coincident, {{f.e}, {l.e}});
                        }
                    }
                    std::sort(reps.begin(), reps.end());
                } else
                    reps = run;
                if (!o.parallel)
                    continue;
                const LineData &f = lines[reps.front()];
                for (size_t m = 1; m < reps.size(); ++m) {
                    const LineData &l = lines[reps[m]];
                    if (!Fresh(f.e, l.e) || Shares(f, l) || (hv[f.e] && hv[f.e] == hv[l.e]))
                        continue;
                    Add(ParallelOrder, Kind::Parallel, {{f.e}, {l.e}});
                }
            }
        }
        void EqualLengths(const std::vector<LineData> &lines) {
            std::vector<size_t> order(lines.size());
            std::iota(order.begin(), order.end(), size_t(0));
            std::stable_sort(order.begin(), order.end(), [&](size_t i, size_t j) { return lines[i].length < lines[j].length; });
            for (size_t a = 0; a < order.size();) {
                const LineData &f = lines[order[a]];
                size_t b = a + 1;
                for (; b < order.size() && lines[order[b]].length - f.length <= Near(std::max(f.size, lines[order[b]].size)); ++b)
                    if (Fresh(f.e, lines[order[b]].e))
                        Add(EqualLengthOrder, Kind::Equal, {{f.e}, {lines[order[b]].e}});
                a = b;
            }
        }
        void EqualRadii() {
            struct RoundData {
                size_t e;
                double r, size;
            };
            std::vector<RoundData> rounds;
            for (size_t k = 0; k < g.elements.size(); ++k) {
                const Element &e = g.elements[k];
                if (Used(k) && (e.kind == ElementKind::Arc || e.kind == ElementKind::Circle)) {
                    const double r = std::abs(g.values[e.Radius()]);
                    rounds.push_back({k, r, NormInf(g.Coords(e.Center())) + r});
                }
            }
            std::stable_sort(rounds.begin(), rounds.end(), [](const RoundData &a, const RoundData &b) { return a.r < b.r; });
            for (size_t a = 0; a < rounds.size();) {
                size_t b = a + 1;
                for (; b < rounds.size() && rounds[b].r - rounds[a].r <= Near(std::max(rounds[a].size, rounds[b].size)); ++b)
                    if (Fresh(rounds[a].e, rounds[b].e))
                        Add(EqualRadiusOrder, Kind::Equal, {{rounds[a].e}, {rounds[b].e}});
                a = b;
            }
        }

        const Geometry &g;
        const AutoConstraint o;
        const std::vector<char> isNew, skip;
        const std::optional<double> tol;
        std::vector<Place> places;
        std::unordered_map<size_t, size_t> placeOf;
        std::set<size_t> skipped;
        std::vector<size_t> parent, first;
        std::array<std::vector<FoundRelation>, Orders> found;
    };

}
