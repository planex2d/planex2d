// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/relationfinder.hpp"

#include <cstdint>
#include <numbers>
#include <numeric>

namespace planex2d::sketch {

    std::vector<FoundRelation> RelationFinder::Find() {
        Places();
        Clusters();
        if (o.point_on_curve)
            PenPoints();
        if (o.point_on_curve || o.midpoint)
            PointsOnCurves();
        if (o.tangent)
            Tangents();
        Lines();
        if (o.horizontal || o.vertical)
            CentreRows();
        if (o.equal_radius)
            EqualRadii();
        for (const FoundAxis &axis : axes)
            Symmetries(axis);
        for (const FoundPitchCircle &pitch : pitches)
            OnPitchCircle(pitch);
        std::vector<FoundRelation> out;
        for (std::vector<FoundRelation> &kind : found)
            for (FoundRelation &r : kind)
                out.push_back(std::move(r));
        return out;
    }

    void RelationFinder::AddAt(size_t point, Ref ref, std::optional<End> end) {
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

    void RelationFinder::Places() {
        for (size_t k = 0; k < g.elements.size(); ++k)
            if (skip[k] && g.elements[k].kind == ElementKind::Point)
                skipped.insert(g.coordsOf[g.elements[k].Point()]);
        for (size_t k = 0; k < g.elements.size(); ++k) {
            if (!Used(k))
                continue;
            const Element &e = g.elements[k];
            if (e.kind == ElementKind::Point)
                AddAt(e.Point(), {k}, std::nullopt);
            if (e.HasEnds() && !e.IsPeriodicBSpline(g)) {
                const std::array<size_t, 2> ends = e.Ends(g);
                AddAt(ends[0], {k, Part::Start}, End{k, false});
                AddAt(ends[1], {k, Part::End}, End{k, true});
            }
            if (e.IsRound())
                AddAt(e.Center(), {k, Part::Center}, std::nullopt);
        }
        classes = DisjointSets(places.size());
        first.assign(places.size(), none);
    }

    void RelationFinder::Clusters() {
        std::vector<size_t> order(places.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::sort(order.begin(), order.end(), [&](size_t i, size_t j) { return places[i].at.x < places[j].at.x; });
        for (size_t a = 0; a < order.size(); ++a) {
            const Vertex p = places[order[a]].at;
            const double nearTol = Near(NormInf(p));
            for (size_t b = a + 1; b < order.size() && places[order[b]].at.x - p.x <= nearTol; ++b) {
                const size_t i = std::min(order[a], order[b]), j = std::max(order[a], order[b]);
                if (first[j] == none && places[j].at.IsClose(places[i].at, nearTol))
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
                Add(places[f].ends.empty() || places[j].ends.empty() ? CoincidentOrder : JointOrder, Kind::Coincident,
                    {places[f].ref, places[j].ref});
                classes.Join(j, f);
            }
        }
    }

    // the direction an element runs at its start or end, as long as the arm that turns it (a line's length, an
    // arc's radius, a spline's chord)
    Vertex RelationFinder::Forward(size_t k, bool atEnd) const {
        const Element &e = g.elements[k];
        if (e.kind == ElementKind::Line)
            return g.Coords(e.End()) - g.Coords(e.Start());
        if (e.IsRound()) {
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

    void RelationFinder::Tangents() {
        std::unordered_map<size_t, std::vector<std::pair<End, size_t>>> at;
        for (size_t p = 0; p < places.size(); ++p)
            for (const End &e : places[p].ends)
                at[classes.Find(p)].push_back({e, p});
        std::set<std::pair<size_t, size_t>> done;
        for (const auto &[root, ends] : at)
            for (size_t i = 0; i < ends.size(); ++i)
                for (size_t j = i + 1; j < ends.size(); ++j) {
                    const End x = ends[i].first, y = ends[j].first;
                    const size_t a = std::min(x.element, y.element), b = std::max(x.element, y.element);
                    if (a == b || (IsLine(a) && IsLine(b)) || !Fresh(a, b) || done.contains({a, b}) || IsOfOneSpline(a, b))
                        continue;
                    if (Tangential(x, y, places[ends[i].second].at)) {
                        done.insert({a, b});
                        const bool yFirst = x.atEnd == y.atEnd ? y.element < x.element : y.atEnd;
                        Add(TangentOrder, Kind::Tangent, {{yFirst ? y.element : x.element}, {yFirst ? x.element : y.element}});
                    }
                }
    }

    void RelationFinder::PenPoints() {
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

    void RelationFinder::PointsOnCurves() {
        std::vector<size_t> targets;
        std::vector<BBox> boxes;
        for (size_t k = 0; k < g.elements.size(); ++k) {
            const Element &e = g.elements[k];
            if (!Used(k) || !(e.kind == ElementKind::Line || e.IsRound()))
                continue;
            BBox box;
            if (e.kind == ElementKind::Line)
                box = BBox::Of(g.Coords(e.Start()), g.Coords(e.End()));
            else {
                const Vertex c = g.Coords(e.Center());
                const double r = std::abs(g.values[e.Radius()]);
                box = BBox::Of(c - Vertex{r, r}, c + Vertex{r, r});
            }
            boxes.push_back(box.Grown(Near(box.NormInf())));
            targets.push_back(k);
        }
        if (targets.empty())
            return;
        Grid grid = Grid::OfBoxes(boxes);
        std::vector<uint32_t> nearby;
        for (size_t p = 0; p < places.size(); ++p) {
            const Place &place = places[p];
            const bool centre = place.IsCentre();
            if ((centre && !o.midpoint) || first[p] != none)
                continue;
            nearby.clear();
            grid.InBox(BBox::Of(place.at, place.at), nearby);
            std::sort(nearby.begin(), nearby.end());
            for (uint32_t t : nearby) {
                const size_t k = targets[t];
                if (!(place.isNew || isNew[k]) || std::find(place.elements.begin(), place.elements.end(), k) != place.elements.end())
                    continue;
                if (o.midpoint && IsAtMiddle(k, place))
                    Add(MidpointOrder, Kind::Coincident, {place.ref, {k, Part::Mid}});
                else if (!centre && o.point_on_curve && OnCurve(k, place))
                    Add(OnCurveOrder, Kind::Coincident, {place.ref, {k}});
            }
        }
    }

    bool RelationFinder::OnCurve(size_t k, const Place &place) const {
        const Element &e = g.elements[k];
        const Vertex p = place.at;
        if (e.kind == ElementKind::Line) {
            const Vertex a = g.Coords(e.Start()), d = g.Coords(e.End()) - a;
            const double length = Norm(d), nearTol = Near(std::max(NormInf(p), NormInf(a)));
            if (!(length > 2.0 * nearTol))
                return false;
            const double along = Dot(p - a, d) / length;
            return std::abs(Cross(p - a, d)) / length <= nearTol && along > nearTol && along < length - nearTol;
        }
        const Vertex c = g.Coords(e.Center());
        const double r = std::abs(g.values[e.Radius()]), nearTol = Near(std::max(NormInf(p), NormInf(c) + r));
        if (std::abs(Norm(p - c) - r) > nearTol)
            return false;
        if (e.kind == ElementKind::Circle)
            return e.OriginPoint() == none || g.coordsOf[e.OriginPoint()] != place.unknown;
        return Norm(p - g.Coords(e.Start())) > nearTol && Norm(p - g.Coords(e.End())) > nearTol &&
               e.Param(g, p) < 1.0;
    }

    void RelationFinder::Lines() {
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
            const double nearTol = Near(l.size);
            if (o.horizontal && isNew[l.e] && std::abs(l.d.y) <= nearTol) {
                hv[l.e] = 'h';
                Add(HorizontalOrder, Kind::Horizontal, {{l.e}});
            } else if (o.vertical && isNew[l.e] && std::abs(l.d.x) <= nearTol) {
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

    void RelationFinder::Perpendiculars(const std::vector<LineData> &lines, const std::vector<char> &hv) {
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

    std::vector<std::vector<size_t>> RelationFinder::DirectionRuns(const std::vector<LineData> &lines) const {
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
        return runs;
    }

    std::vector<size_t> RelationFinder::Collinears(const std::vector<LineData> &lines, const std::vector<size_t> &run) {
        std::vector<size_t> reps;
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
        return reps;
    }

    void RelationFinder::ParallelRuns(const std::vector<LineData> &lines, const std::vector<char> &hv) {
        for (std::vector<size_t> &run : DirectionRuns(lines)) {
            std::sort(run.begin(), run.end());
            const std::vector<size_t> reps = o.collinear ? Collinears(lines, run) : run;
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

    void RelationFinder::CentreRows() {
        std::vector<size_t> centres;
        for (size_t p = 0; p < places.size(); ++p)
            if (classes.IsRoot(p) && places[p].IsCentre() && g.elements[places[p].round].kind == ElementKind::Circle)
                centres.push_back(p);
        for (const bool horizontal : {true, false}) {
            if (!(horizontal ? o.horizontal : o.vertical))
                continue;
            const auto level = [&](size_t p) { return horizontal ? places[p].at.y : places[p].at.x; };
            std::stable_sort(centres.begin(), centres.end(), [&](size_t i, size_t j) { return level(i) < level(j); });
            for (size_t a = 0; a < centres.size();) {
                const Place &f = places[centres[a]];
                size_t b = a + 1;
                for (; b < centres.size() && level(centres[b]) - level(centres[a]) <= Near(std::max(NormInf(f.at), NormInf(places[centres[b]].at))); ++b)
                    if (Fresh(f.round, places[centres[b]].round))
                        Add(horizontal ? HorizontalOrder : VerticalOrder, horizontal ? Kind::Horizontal : Kind::Vertical, {f.ref, places[centres[b]].ref});
                a = b;
            }
        }
    }

    void RelationFinder::EqualLengths(const std::vector<LineData> &lines) {
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

    void RelationFinder::EqualRadii() {
        struct RoundData {
            size_t e;
            double r, size;
        };
        std::vector<RoundData> rounds;
        for (size_t k = 0; k < g.elements.size(); ++k) {
            const Element &e = g.elements[k];
            if (Used(k) && e.IsRound()) {
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

    void RelationFinder::Symmetries(const FoundAxis &axis) {
        const Ref on{axis.line};
        std::vector<size_t> mirror(places.size(), none);
        std::vector<char> paired(places.size(), 0), anchored(g.elements.size(), 0);
        const std::vector<size_t> byX = RootsByX();
        for (size_t p : byX)
            mirror[p] = PlaceAt(byX, axis.Mirrored(places[p].at), places[p].IsCentre());
        if (axis.isNew)
            for (const Part end : {Part::Start, Part::End})
                Anchor(axis, {axis.line, end}, mirror, paired, anchored);
        for (size_t k = 0; k < g.elements.size(); ++k) {
            if (!Used(k) || !IsLine(k))
                continue;
            const std::optional<size_t> a = ClassOf(g.elements[k].Start()), b = ClassOf(g.elements[k].End());
            if (!a || !b || *a == *b || mirror[*a] != *b)
                continue;
            paired[*a] = paired[*b] = 1;
            if (!anchored[k])
                Add(SymmetricOrder, Kind::Coincident, {{k, Part::Mid}, on});
            Add(SymmetricOrder, Kind::Perpendicular, {{k}, on});
        }
        for (size_t p : byX) {
            const size_t q = mirror[p];
            if (q == none || paired[p] || IsOwnEnd(p, axis.line))
                continue;
            if (q == p && !IsArcCentre(p))
                Add(SymmetricOrder, Kind::Coincident, {places[p].ref, on});
            else if (p < q && !paired[q] && IsMirroredPair(p, q))
                Add(SymmetricOrder, Kind::Symmetric, {places[p].ref, places[q].ref, on});
        }
        MirroredRounds(axis, mirror);
    }

    void RelationFinder::Anchor(const FoundAxis &axis, const Ref &end, const std::vector<size_t> &mirror, std::vector<char> &paired,
                                std::vector<char> &anchored) {
        const Vertex at = g.Coords(end.part == Part::Start ? g.elements[axis.line].Start() : g.elements[axis.line].End());
        const double nearTol = Near(NormInf(at));
        for (size_t p = 0; p < places.size(); ++p)
            if (classes.IsRoot(p) && mirror[p] == p && places[p].at.IsClose(at, nearTol)) {
                paired[p] = 1;
                Add(SymmetricOrder, Kind::Coincident, {end, places[p].ref});
                return;
            }
        for (size_t k = 0; k < g.elements.size(); ++k) {
            if (!Used(k) || !(IsLine(k) || g.elements[k].kind == ElementKind::Arc))
                continue;
            const Element &e = g.elements[k];
            const Vertex a = g.Coords(e.Start()), b = g.Coords(e.End());
            if (!axis.Mirrored(a).IsClose(b, nearTol))
                continue;
            if (IsLine(k) && at.IsClose((a + b) * 0.5, nearTol)) {
                anchored[k] = 1;
                Add(SymmetricOrder, Kind::Coincident, {end, {k, Part::Mid}});
                return;
            }
            if (!IsLine(k) && std::abs(Norm(at - g.Coords(e.Center())) - std::abs(g.values[e.Radius()])) <= nearTol) {
                Add(SymmetricOrder, Kind::Coincident, {end, {k}});
                return;
            }
        }
    }

    bool RelationFinder::IsMirroredRound(size_t i, size_t j, const FoundAxis &axis, const std::vector<size_t> &mirror) {
        const Element &x = g.elements[i], &y = g.elements[j];
        if (x.kind != y.kind || !x.IsRound() ||
            std::abs(std::abs(g.values[x.Radius()]) - std::abs(g.values[y.Radius()])) > Near(NormInf(g.Coords(x.Center()))))
            return false;
        if (x.kind == ElementKind::Circle)
            return axis.Mirrored(g.Coords(x.Center())).IsClose(g.Coords(y.Center()), Near(NormInf(g.Coords(x.Center()))));
        const std::optional<size_t> xs = ClassOf(x.Start()), xe = ClassOf(x.End()), ys = ClassOf(y.Start()), ye = ClassOf(y.End());
        if (!xs || !xe || !ys || !ye)
            return false;
        return x.ccw == y.ccw ? mirror[*xs] == *ye && mirror[*xe] == *ys : mirror[*xs] == *ys && mirror[*xe] == *ye;
    }

}
