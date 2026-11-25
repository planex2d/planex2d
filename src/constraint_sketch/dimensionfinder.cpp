// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/dimensionfinder.hpp"

#include <array>
#include <numbers>
#include <set>

namespace planex2d::cs {

    std::vector<Constraint> DimensionFinder::Candidates() const {
        std::vector<Constraint> out;
        Angles(out);
        Sizes(out);
        Extents(out);
        for (const bool hv : {true, false}) {
            CentreDistances(hv, out);
            if (hv)
                CentreToEdges(out);
            ParallelLines(hv, out);
            Lengths(hv, out);
            EndsAlong(hv, out);
        }
        std::stable_partition(out.begin(), out.end(), [&](const Constraint &c) { return HasRoundValue(c); });
        return out;
    }

    void DimensionFinder::Features() {
        std::set<size_t> seen;
        const auto add = [&](std::vector<Feature> &to, const sketch::Ref &r, size_t point, double radius) {
            const size_t u = s.coordsOf[point];
            if ((s.held[u] && s.held[u + 1]) || !seen.insert(u).second)
                return;
            to.push_back({r, s.Coords(point), u, radius});
        };
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k) {
            const sketch::Element &e = s.elements[k];
            if (e.kind == sketch::ElementKind::Circle || (e.kind == sketch::ElementKind::Arc && IsHalfTurnOrMore(e)))
                add(centres, {k, sketch::Part::Center}, e.Center(), std::abs(s.values[e.Radius()]));
        }
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k) {
            const sketch::Element &e = s.elements[k];
            if (e.kind == sketch::ElementKind::Line || e.kind == sketch::ElementKind::Arc ||
                (e.kind == sketch::ElementKind::Spline && !e.IsPeriodicBSpline(s))) {
                const std::array<size_t, 2> ends = e.Ends(s);
                add(corners, {k, sketch::Part::Start}, ends[0], 0.0);
                add(corners, {k, sketch::Part::End}, ends[1], 0.0);
            }
            if (e.kind == sketch::ElementKind::Point)
                add(corners, {k}, e.Point(), 0.0);
        }
    }

    void DimensionFinder::Chains() {
        DisjointSets joined(s.elements.size());
        std::vector<size_t> first(s.values.size(), s.elements.size());
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k) {
            const sketch::Element &e = s.elements[k];
            if ((e.kind != sketch::ElementKind::Line && e.kind != sketch::ElementKind::Arc && e.kind != sketch::ElementKind::Spline) ||
                e.IsPeriodicBSpline(s))
                continue;
            for (const size_t point : e.Ends(s)) {
                const size_t u = s.coordsOf[point];
                if (first[u] == s.elements.size())
                    first[u] = k;
                else
                    joined.Join(k, first[u]);
            }
        }
        chain.resize(s.elements.size());
        for (size_t k = 0; k < s.elements.size(); ++k)
            chain[k] = joined.Find(k);
    }

    void DimensionFinder::FindDatum() {
        for (const std::vector<Feature> *list : {&corners, &centres})
            for (const Feature &f : *list)
                if (NormInf(f.at) <= rounding) {
                    datum = f;
                    origin = true;
                    return;
                }
        for (const LineData &l : lines)
            if (std::abs(Cross(l.Dir(), l.a)) <= rounding && Dot(l.a, l.b) <= 0.0) {
                origin = true;
                originLine = l.e;
                return;
            }
        Vertex lo{INFINITY, INFINITY};
        for (const std::vector<Feature> *list : {&corners, &centres})
            for (const Feature &f : *list)
                lo = {std::min(lo.x, f.at.x), std::min(lo.y, f.at.y)};
        for (const Feature &f : corners)
            if (f.ref.part != sketch::Part::Whole && (!datum || Norm(f.at - lo) < Norm(datum->at - lo)))
                datum = f;
        if (!datum)
            for (const Feature &f : centres)
                if (!datum || f.r > datum->r)
                    datum = f;
    }

    void DimensionFinder::Angles(std::vector<Constraint> &out) const {
        std::vector<char> meets(lines.size(), 0);
        std::vector<Constraint> between;
        for (const auto &[i, j] : Neighbours()) {
            between.push_back(Valued({sketch::Kind::Angle, {{lines[i].e}, {lines[j].e}}}));
            meets[i] = meets[j] = 1;
        }
        for (const bool meeting : {false, true}) {
            for (size_t i = 0; i < lines.size(); ++i)
                if (!lines[i].IsHV(rounding) && (meets[i] != 0) == meeting)
                    out.push_back(FromH(lines[i].e));
            if (!meeting)
                out.insert(out.end(), between.begin(), between.end());
        }
    }

    size_t DimensionFinder::WidestGap(const std::vector<LineEnd> &at) {
        size_t widest = 0;
        double most = -1.0;
        for (size_t k = 0; k < at.size(); ++k) {
            const Vertex u = at[k].away, v = at[(k + 1) % at.size()].away;
            double gap = std::atan2(Cross(u, v), Dot(u, v));
            if (gap <= 0.0)
                gap += 2.0 * std::numbers::pi;
            if (gap > most) {
                most = gap;
                widest = k;
            }
        }
        return widest;
    }

    std::vector<std::pair<size_t, size_t>> DimensionFinder::Neighbours() const {
        std::vector<LineEnd> ends;
        for (size_t i = 0; i < lines.size(); ++i) {
            ends.push_back({lines[i].a, lines[i].b - lines[i].a, i});
            ends.push_back({lines[i].b, lines[i].a - lines[i].b, i});
        }
        std::sort(ends.begin(), ends.end(), [](const LineEnd &x, const LineEnd &y) { return x.at.x < y.at.x; });
        std::vector<char> done(ends.size(), 0);
        std::vector<std::pair<size_t, size_t>> out;
        for (size_t m = 0; m < ends.size(); ++m) {
            std::vector<LineEnd> at;
            for (size_t n = m; n < ends.size() && ends[n].at.x - ends[m].at.x <= rounding; ++n)
                if (!done[n] && ends[n].at.IsClose(ends[m].at, rounding)) {
                    at.push_back(ends[n]);
                    done[n] = 1;
                }
            std::sort(at.begin(), at.end(), [](const LineEnd &x, const LineEnd &y) { return std::atan2(x.away.y, x.away.x) < std::atan2(y.away.y, y.away.x); });
            const size_t widest = WidestGap(at);
            for (size_t k = 0; k < at.size(); ++k) {
                if (k == widest)
                    continue;
                const LineData &a = lines[at[k].line], &b = lines[at[(k + 1) % at.size()].line];
                const double turn = rounding / std::min(a.Length(), b.Length());
                if (a.e != b.e && std::abs(Cross(a.Dir(), b.Dir())) > turn && std::abs(Dot(a.Dir(), b.Dir())) > turn)
                    out.push_back({at[k].line, at[(k + 1) % at.size()].line});
            }
        }
        return out;
    }

    void DimensionFinder::Sizes(std::vector<Constraint> &out) const {
        std::vector<std::pair<double, Constraint>> step;
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k) {
            const sketch::Element &e = s.elements[k];
            if (e.IsHeld(s) || !e.IsRound())
                continue;
            const double r = std::abs(s.values[e.Radius()]);
            if (r > rounding)
                step.push_back({r, Valued({e.kind == sketch::ElementKind::Arc ? sketch::Kind::Radius : sketch::Kind::Diameter, {{k}}})});
        }
        std::stable_sort(step.begin(), step.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &[r, c] : step)
            out.push_back(c);
    }

    void DimensionFinder::Extents(std::vector<Constraint> &out) const {
        std::vector<Constraint> step;
        for (const bool horizontal : {true, false}) {
            const Feature *lo = nullptr, *hi = nullptr;
            for (const std::vector<Feature> *list : {&corners, &centres})
                for (const Feature &f : *list) {
                    if (!lo || Coordinate(f.at, horizontal) < Coordinate(lo->at, horizontal))
                        lo = &f;
                    if (!hi || Coordinate(f.at, horizontal) > Coordinate(hi->at, horizontal))
                        hi = &f;
                }
            if (!lo)
                return;
            const std::optional<size_t> a = LineAt(Coordinate(lo->at, horizontal), horizontal), b = LineAt(Coordinate(hi->at, horizontal), horizontal);
            step.push_back(Valued(a && b ? Constraint{sketch::Kind::Distance, {{*a}, {*b}}}
                                         : Constraint{sketch::Kind::Distance, {lo->ref, hi->ref, Across(horizontal)}}));
        }
        Append(step, false, out);
    }

    void DimensionFinder::CentreDistances(bool hv, std::vector<Constraint> &out) const {
        std::vector<Constraint> step;
        std::set<std::pair<size_t, size_t>> done;
        const auto pair = [&](size_t i, size_t j, std::optional<bool> horizontal) {
            if (i == j || !done.insert({std::min(i, j), std::max(i, j)}).second)
                return;
            std::vector<sketch::Ref> refs{centres[i].ref, centres[j].ref};
            if (horizontal)
                refs.push_back(Across(*horizontal));
            step.push_back(Valued({sketch::Kind::Distance, refs}));
        };
        if (hv)
            for (const bool horizontal : {true, false}) {
                done.clear();
                std::vector<size_t> order(centres.size());
                for (size_t i = 0; i < order.size(); ++i)
                    order[i] = i;
                std::stable_sort(order.begin(), order.end(),
                                 [&](size_t i, size_t j) { return Coordinate(centres[i].at, horizontal) < Coordinate(centres[j].at, horizontal); });
                for (size_t m = 1; m < order.size(); ++m)
                    pair(order[m - 1], order[m], horizontal);
                if (order.size() > 2)
                    pair(order.front(), order.back(), horizontal);
            }
        else
            for (size_t i = 0; i < centres.size(); ++i) {
                std::optional<size_t> nearest;
                for (size_t j = 0; j < centres.size(); ++j)
                    if (j != i && !IsAligned(centres[i].at, centres[j].at) &&
                        (!nearest || Norm(centres[j].at - centres[i].at) < Norm(centres[*nearest].at - centres[i].at)))
                        nearest = j;
                if (nearest)
                    pair(i, *nearest, std::nullopt);
            }
        Append(step, false, out);
    }

    void DimensionFinder::CentreToEdges(std::vector<Constraint> &out) const {
        for (const bool vertical : {true, false}) {
            const std::optional<sketch::Ref> edge = DatumEdge(vertical);
            if (!edge)
                continue;
            std::vector<Constraint> step;
            for (const Feature &c : centres)
                step.push_back(Valued({sketch::Kind::Distance, {c.ref, *edge}}));
            Append(step, true, out);
        }
    }

    void DimensionFinder::ParallelLines(bool hv, std::vector<Constraint> &out) const {
        std::vector<std::vector<size_t>> groups;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].IsHV(rounding) != hv)
                continue;
            bool placed = false;
            for (std::vector<size_t> &g : groups)
                if (!placed && std::abs(Cross(lines[g.front()].Dir(), lines[i].Dir())) <= rounding / std::min(lines[g.front()].Length(), lines[i].Length())) {
                    g.push_back(i);
                    placed = true;
                }
            if (!placed)
                groups.push_back({i});
        }
        std::vector<Constraint> own, apart;
        for (std::vector<size_t> &g : groups) {
            const Vertex n{-lines[g.front()].Dir().y, lines[g.front()].Dir().x};
            std::stable_sort(g.begin(), g.end(), [&](size_t i, size_t j) { return Dot(n, lines[i].a) < Dot(n, lines[j].a); });
            for (size_t m = 1; m < g.size(); ++m)
                Between(lines[g[m - 1]], lines[g[m]], n, own, apart);
            if (g.size() > 2)
                Between(lines[g.front()], lines[g.back()], n, own, apart);
        }
        Append(own, false, out);
        Append(apart, false, out);
    }

    void DimensionFinder::EndsAlong(bool hv, std::vector<Constraint> &out) const {
        std::vector<Constraint> step;
        const auto isDrawn = [&](size_t k) { return drawn.empty() || drawn[k]; };
        for (const LineData &l : lines) {
            if (l.IsHV(rounding) != hv || !isDrawn(l.e))
                continue;
            for (const Feature &p : corners) {
                if (!isDrawn(p.ref.element))
                    continue;
                const double t = Dot(p.at - l.a, l.Dir());
                if (t <= rounding || t >= l.Length() - rounding || std::abs(Cross(l.Dir(), p.at - l.a)) > rounding)
                    continue;
                step.push_back(Valued({sketch::Kind::Distance, {p.ref, {l.e, sketch::Part::Start}}}));
                step.push_back(Valued({sketch::Kind::Distance, {p.ref, {l.e, sketch::Part::End}}}));
            }
        }
        Append(step, false, out);
    }

}
