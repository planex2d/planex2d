// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/disjointsets.hpp"
#include "planex/core/grid.hpp"
#include "planex/core/vertex.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/sketch/pitchcirclefinder.hpp"
#include "planex/sketch/symmetryfinder.hpp"


namespace planex2d::sketch {

    struct FoundRelation {
        Kind kind;
        std::vector<Ref> refs;
    };

    class RelationFinder {
    public:
        // tol: an absolute length, none for rounding
        RelationFinder(const Geometry &g, const AutoConstraint &o, std::vector<char> isNew, std::vector<char> skip,
                       std::optional<double> tol = std::nullopt, std::vector<FoundAxis> axes = {}, std::vector<FoundPitchCircle> pitches = {})
            : g(g), o(o), isNew(std::move(isNew)), skip(std::move(skip)), tol(tol), axes(std::move(axes)), pitches(std::move(pitches)) {}

        std::vector<FoundRelation> Find();

    private:
        enum Order { JointOrder, ConcentricOrder, TangentOrder, HorizontalOrder, VerticalOrder, PerpendicularOrder, ParallelOrder,
                     CoincidentOrder, OnCurveOrder, MidpointOrder, CollinearOrder, SymmetricOrder, PitchOrder, EqualRadiusOrder,
                     EqualLengthOrder, Orders };

        struct End {
            size_t element;
            bool atEnd;
        };
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

        void AddAt(size_t point, Ref ref, std::optional<End> end);
        void Places();
        std::optional<size_t> ClassOf(size_t point) {
            const auto it = placeOf.find(g.coordsOf[point]);
            return it == placeOf.end() ? std::nullopt : std::optional(classes.Find(it->second));
        }

        void Clusters();

        Vertex Forward(size_t k, bool atEnd) const;
        bool Tangential(End x, End y, const Vertex &at) const {
            const Vertex u = Forward(x.element, x.atEnd), v = Forward(y.element, y.atEnd);
            return std::abs(Cross(u, v)) <= Near(NormInf(at)) * (Norm(u) + Norm(v));
        }
        bool IsLine(size_t k) const { return g.elements[k].kind == ElementKind::Line; }
        bool IsOfOneSpline(size_t a, size_t b) const {
            const auto spline = [&](size_t k) {
                const Element &e = g.elements[k];
                return e.kind == ElementKind::Spline ? e.SplineIndex() : g.LegOf(e);
            };
            return spline(a) != none && spline(a) == spline(b);
        }

        void Tangents();
        void PenPoints();

        void PointsOnCurves();
        bool IsAtMiddle(size_t k, const Place &place) const {
            if (!IsLine(k))
                return false;
            const Vertex a = g.Coords(g.elements[k].Start()), b = g.Coords(g.elements[k].End());
            const double nearTol = Near(std::max({NormInf(place.at), NormInf(a), NormInf(b)}));
            return Norm(b - a) > 2.0 * nearTol && place.at.IsClose((a + b) * 0.5, nearTol);
        }
        bool OnCurve(size_t k, const Place &place) const;

        bool IsOnePlace(size_t p, size_t q) {
            const std::optional<size_t> cp = ClassOf(p), cq = ClassOf(q);
            return (cp && cq && *cp == *cq) || g.coordsOf[p] == g.coordsOf[q];
        }
        bool Shares(const LineData &a, const LineData &b) {
            const std::array<size_t, 2> x = g.elements[a.e].Ends(g), y = g.elements[b.e].Ends(g);
            for (size_t p : x)
                for (size_t q : y)
                    if (IsOnePlace(p, q))
                        return true;
            return false;
        }
        Part FarEnd(const LineData &a, const LineData &b) {
            const size_t start = g.elements[b.e].Start();
            for (size_t p : g.elements[a.e].Ends(g))
                if (IsOnePlace(p, start))
                    return Part::End;
            return Part::Start;
        }
        bool Parallel(const LineData &a, const LineData &b) const {
            return std::abs(Cross(a.d, b.d)) <= Near(std::max(a.size, b.size)) * (a.length + b.length);
        }
        bool OnCarrier(const LineData &a, const LineData &b) const {
            const double nearTol = Near(std::max(a.size, b.size));
            return std::abs(Cross(b.a - a.a, a.d)) / a.length <= nearTol && std::abs(Cross(b.a + b.d - a.a, a.d)) / a.length <= nearTol;
        }

        void Lines();
        void Perpendiculars(const std::vector<LineData> &lines, const std::vector<char> &hv);
        std::vector<std::vector<size_t>> DirectionRuns(const std::vector<LineData> &lines) const;
        std::vector<size_t> Collinears(const std::vector<LineData> &lines, const std::vector<size_t> &run);
        void ParallelRuns(const std::vector<LineData> &lines, const std::vector<char> &hv);
        void CentreRows();
        void EqualLengths(const std::vector<LineData> &lines);
        void EqualRadii();

        void Symmetries(const FoundAxis &axis);
        void OnPitchCircle(const FoundPitchCircle &pitch) {
            const Ref centre = CentreRef(pitch.middle);
            Add(PitchOrder, Kind::Concentric, {{pitch.middle}, {pitch.circle}});
            for (size_t k = 0; k < pitch.holes.size(); ++k) {
                Add(PitchOrder, Kind::Coincident, {CentreRef(pitch.holes[k]), {pitch.circle}});
                Add(PitchOrder, Kind::Coincident, {{pitch.spokes[k], Part::Start}, centre});
                Add(PitchOrder, Kind::Coincident, {{pitch.spokes[k], Part::End}, CentreRef(pitch.holes[k])});
            }
        }
        Ref CentreRef(size_t round) {
            const std::optional<size_t> p = ClassOf(g.elements[round].Center());
            return p ? places[*p].ref : Ref{round, Part::Center};
        }
        std::vector<size_t> RootsByX() {
            std::vector<size_t> out;
            for (size_t p = 0; p < places.size(); ++p)
                if (classes.IsRoot(p))
                    out.push_back(p);
            std::sort(out.begin(), out.end(), [&](size_t i, size_t j) { return places[i].at.x < places[j].at.x; });
            return out;
        }
        size_t PlaceAt(const std::vector<size_t> &byX, const Vertex &q, bool centre) const {
            const double nearTol = Near(NormInf(q));
            auto it = std::lower_bound(byX.begin(), byX.end(), q.x - nearTol, [&](size_t p, double x) { return places[p].at.x < x; });
            for (; it != byX.end() && places[*it].at.x <= q.x + nearTol; ++it)
                if (places[*it].IsCentre() == centre && places[*it].at.IsClose(q, nearTol))
                    return *it;
            return none;
        }
        void Anchor(const FoundAxis &axis, const Ref &end, const std::vector<size_t> &mirror, std::vector<char> &paired,
                    std::vector<char> &anchored);
        bool IsArcCentre(size_t p) const { return places[p].IsCentre() && g.elements[places[p].round].kind == ElementKind::Arc; }
        bool IsOwnEnd(size_t p, size_t line) const {
            return std::find(places[p].elements.begin(), places[p].elements.end(), line) != places[p].elements.end();
        }
        bool IsMirroredPair(size_t p, size_t q) const {
            if (!places[p].IsCentre())
                return true;
            const Element &x = g.elements[places[p].round], &y = g.elements[places[q].round];
            return x.kind == ElementKind::Circle && y.kind == ElementKind::Circle &&
                   std::abs(std::abs(g.values[x.Radius()]) - std::abs(g.values[y.Radius()])) <= Near(NormInf(places[p].at));
        }
        void MirroredRounds(const FoundAxis &axis, const std::vector<size_t> &mirror) {
            for (size_t i = 0; i < g.elements.size(); ++i)
                for (size_t j = i + 1; j < g.elements.size(); ++j)
                    if (Used(i) && Used(j) && IsMirroredRound(i, j, axis, mirror))
                        Add(SymmetricOrder, Kind::Equal, {{i}, {j}});
        }
        bool IsMirroredRound(size_t i, size_t j, const FoundAxis &axis, const std::vector<size_t> &mirror);

        const Geometry &g;
        const AutoConstraint o;
        const std::vector<char> isNew, skip;
        const std::optional<double> tol;
        const std::vector<FoundAxis> axes;
        const std::vector<FoundPitchCircle> pitches;
        std::vector<Place> places;
        std::unordered_map<size_t, size_t> placeOf;
        std::set<size_t> skipped;
        DisjointSets classes{0};
        std::vector<size_t> first;
        std::array<std::vector<FoundRelation>, Orders> found;
    };

}
