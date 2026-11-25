// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "planex/constraint_sketch/constraint.hpp"
#include "planex/constraint_sketch/system.hpp"
#include "planex/core/config.hpp"
#include "planex/core/disjointsets.hpp"
#include "planex/core/vertex.hpp"

namespace planex2d::cs {

    class DimensionFinder {
    public:
        explicit DimensionFinder(const System &s, std::vector<char> drawn = {}) : s(s), rounding(Tol(s.Size())), drawn(std::move(drawn)) {
            Features();
            roundStep = RoundStep();
            Lines();
            Chains();
            FindDatum();
        }

        std::vector<Constraint> Candidates() const;
        std::optional<size_t> DatumUnknown() const { return datum ? std::optional(datum->unknown) : std::nullopt; }
        std::optional<size_t> OriginLine() const { return originLine; }
        bool IsAtOrigin() const { return origin; }

    private:
        struct Feature {
            sketch::Ref ref;
            Vertex at;
            size_t unknown;
            double r = 0.0;
        };
        struct LineData {
            size_t e;
            Vertex a, b;

            Vertex Dir() const { return Normalized(b - a); }
            double Length() const { return Norm(b - a); }
            bool IsAlong(bool vertical, double tol) const { return std::abs(vertical ? b.x - a.x : b.y - a.y) <= tol; }
            bool IsHV(double tol) const { return IsAlong(true, tol) || IsAlong(false, tol); }
            bool Touches(const Vertex &p, double tol) const { return a.IsClose(p, tol) || b.IsClose(p, tol); }
        };

        static sketch::Ref Across(bool horizontal) { return sketch::Ref{(size_t)(horizontal ? 1 : 0)}; }
        static double Coordinate(const Vertex &p, bool horizontal) { return horizontal ? p.x : p.y; }

        void Features();
        bool IsHalfTurnOrMore(const sketch::Element &arc) const {
            const Vertex c = s.Coords(arc.Center()), u = s.Coords(arc.Start()) - c, v = s.Coords(arc.End()) - c;
            const double turn = Cross(u, v);
            return arc.ccw ? turn <= rounding * Norm(u) : turn >= -rounding * Norm(u);
        }
        void Lines() {
            for (size_t k = sketch::ownElements; k < s.elements.size(); ++k) {
                const sketch::Element &e = s.elements[k];
                if (e.kind != sketch::ElementKind::Line || e.IsHeld(s))
                    continue;
                const LineData l{k, s.Coords(e.Start()), s.Coords(e.End())};
                if (l.Length() > rounding)
                    lines.push_back(l);
            }
        }
        void Chains();
        void FindDatum();
        std::optional<sketch::Ref> DatumEdge(bool vertical) const {
            if (origin)
                return Across(vertical);
            if (datum)
                for (const LineData &l : lines)
                    if (l.IsAlong(vertical, rounding) && l.Touches(datum->at, rounding))
                        return sketch::Ref{l.e};
            return std::nullopt;
        }
        bool IsTouchingDatum(const Constraint &c) const {
            const std::optional<sketch::Ref> h = DatumEdge(false), v = DatumEdge(true);
            for (const sketch::Ref &r : c.refs) {
                if (datum && r.IsPoint(s) && s.coordsOf[r.PointAt(s)] == datum->unknown)
                    return true;
                if (r.part == sketch::Part::Whole && ((h && r.element == h->element) || (v && r.element == v->element)))
                    return true;
            }
            return false;
        }

        // 0.5 in the decade two below the largest extent: 0.5 for a part 100 to 999 across, 0.0005 for one 0.1 to 0.999
        double RoundStep() const {
            Vertex lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
            for (const std::vector<Feature> *list : {&corners, &centres})
                for (const Feature &f : *list) {
                    lo = {std::min(lo.x, f.at.x - f.r), std::min(lo.y, f.at.y - f.r)};
                    hi = {std::max(hi.x, f.at.x + f.r), std::max(hi.y, f.at.y + f.r)};
                }
            const double extent = std::max(hi.x - lo.x, hi.y - lo.y);
            return extent > 0.0 ? 0.5 * std::pow(10.0, std::floor(std::log10(extent)) - 2.0) : 0.5;
        }
        bool HasRoundValue(const Constraint &c) const {
            const bool angle = c.kind == sketch::Kind::Angle;
            const double step = angle ? 1.0 : roundStep;
            // half the last of the three decimals a drawing prints: 5e-4 of a 0.5 step, of a degree
            return std::abs(c.value - step * std::round(c.value / step)) < (angle ? 5e-4 : 1e-3 * step);
        }
        Constraint Valued(Constraint c) const {
            c.value = std::abs(s.Measured(c));
            return c;
        }
        void Append(std::vector<Constraint> step, bool nearest, std::vector<Constraint> &out) const {
            std::erase_if(step, [&](const Constraint &c) { return c.value <= rounding; });
            std::stable_sort(step.begin(), step.end(), [&](const Constraint &a, const Constraint &b) { return IsTouchingDatum(a) && !IsTouchingDatum(b); });
            std::stable_sort(step.begin(), step.end(), [&](const Constraint &a, const Constraint &b) {
                const long long x = std::llround(a.value / rounding), y = std::llround(b.value / rounding);
                return nearest ? x < y : x > y;
            });
            out.insert(out.end(), step.begin(), step.end());
        }

        void Angles(std::vector<Constraint> &out) const;
        struct LineEnd {
            Vertex at, away;
            size_t line;
        };
        static size_t WidestGap(const std::vector<LineEnd> &at);
        Constraint FromH(size_t line) const {
            Constraint c = Valued({sketch::Kind::Angle, {Across(false), {line}}});
            if (c.value > 90.0)
                c.value = 180.0 - c.value;
            return c;
        }
        std::vector<std::pair<size_t, size_t>> Neighbours() const;
        void Sizes(std::vector<Constraint> &out) const;

        void Extents(std::vector<Constraint> &out) const;
        std::optional<size_t> LineAt(double level, bool vertical) const {
            for (const LineData &l : lines)
                if (l.IsAlong(vertical, rounding) && std::abs(Coordinate(l.a, vertical) - level) <= rounding)
                    return l.e;
            return std::nullopt;
        }

        void CentreDistances(bool hv, std::vector<Constraint> &out) const;
        bool IsAligned(const Vertex &p, const Vertex &q) const { return std::abs(p.x - q.x) <= rounding || std::abs(p.y - q.y) <= rounding; }
        void CentreToEdges(std::vector<Constraint> &out) const;

        void ParallelLines(bool hv, std::vector<Constraint> &out) const;
        void Between(const LineData &a, const LineData &b, const Vertex &n, std::vector<Constraint> &own,
                     std::vector<Constraint> &apart) const {
            if (std::abs(Dot(n, b.a - a.a)) > rounding)
                (chain[a.e] == chain[b.e] ? own : apart).push_back(Valued({sketch::Kind::Distance, {{a.e}, {b.e}}}));
        }
        void Lengths(bool hv, std::vector<Constraint> &out) const {
            std::vector<Constraint> step;
            for (const LineData &l : lines)
                if (l.IsHV(rounding) == hv)
                    step.push_back(Valued({sketch::Kind::Distance, {{l.e}}}));
            Append(step, false, out);
        }
        void EndsAlong(bool hv, std::vector<Constraint> &out) const;

        const System &s;
        const double rounding;
        double roundStep = 0.5;
        std::vector<Feature> corners, centres;
        std::vector<LineData> lines;
        std::vector<size_t> chain;
        std::optional<Feature> datum;
        std::optional<size_t> originLine;
        bool origin = false;
        const std::vector<char> drawn;
    };

}
