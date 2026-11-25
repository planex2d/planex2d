// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "planex/boolean/cut.hpp"
#include "planex/core/bbox.hpp"
#include "planex/core/config.hpp"
#include "planex/core/grid.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/circle.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/curves/spline.hpp"
#include "planex/sketch/geometry.hpp"


namespace planex2d::sketch {

    // the lower index first
    struct Fault {
        std::vector<size_t> elements;
        Vertex at;
    };
    struct Faults {
        std::vector<Fault> openEnds, missingCoincident, crossings, overlaps, degenerate;
    };

    class Diagnosis {
    public:
        // gap: ends of two elements closer than it are a miss
        Diagnosis(const Geometry &g, std::vector<char> looked, std::optional<double> gap)
            : g(g), looked(std::move(looked)), gap(gap), tol(Tol(g.Size())), nearTol(4.0 * tol), degenerate(g.elements.size(), 0) {}

        template <class EdgesOf>
        Faults Find(const EdgesOf &edgesOf) {
            for (size_t k = 0; k < g.elements.size(); ++k)
                if (looked[k] && !IsDegenerate(k))
                    for (const Edge &e : edgesOf(k))
                        Flatten(e, k);
            Ends();
            Contacts();
            for (std::vector<Fault> *list : {&out.openEnds, &out.missingCoincident, &out.crossings, &out.overlaps, &out.degenerate})
                std::sort(list->begin(), list->end(), [](const Fault &a, const Fault &b) {
                    return a.elements != b.elements ? a.elements < b.elements : a.at.x != b.at.x ? a.at.x < b.at.x : a.at.y < b.at.y;
                });
            return std::move(out);
        }

    private:
        struct End {
            size_t element;
            size_t point;
            Vertex at;
        };
        struct Contact {
            std::vector<Vertex> points;
            std::vector<PrimitiveEdge> shared;
        };

        const Geometry &g;
        std::vector<char> looked;
        std::optional<double> gap;
        double tol;
        // Cut's own tolerance grows with the scale of its edges, up to about twice the sketch's size
        double nearTol;
        std::vector<char> degenerate;
        std::vector<PrimitiveEdge> flat;
        std::vector<size_t> source;
        BBox box;
        Faults out;

        bool IsDegenerate(size_t k);
        std::optional<Vertex> DegenerateLine(const Element &e) const {
            const Vertex a = g.Coords(e.Start()), b = g.Coords(e.End());
            if (a.IsClose(b, Tol(std::max(NormInf(a), NormInf(b)))))
                return (a + b) * 0.5;
            return std::nullopt;
        }
        std::optional<Vertex> DegenerateRound(const Element &e) const {
            const Vertex c = g.Coords(e.Center());
            const double r = g.values[e.Radius()], size = Tol(NormInf(c) + r);
            if (r <= size)
                return c;
            if (e.kind == ElementKind::Arc && r * Arc(g.Coords(e.Start()), g.Coords(e.End()), c, e.ccw).Sweep() <= size)
                return e.At(g, 0.5);
            return std::nullopt;
        }
        std::optional<Vertex> DegeneratePoles(const SplineData &d) const;
        std::optional<Vertex> DegenerateSpline(const Element &e) const;
        static Vertex HalfwayAlong(const std::vector<Vertex> &q);

        void Flatten(const Edge &e, size_t k) {
            std::visit([&](const auto &x) { ForEachPart(x, [&](const auto &p) { AddFlat(p, k); }); }, e);
        }
        void AddFlat(const PrimitiveEdge &p, size_t k) {
            flat.push_back(p);
            source.push_back(k);
            box.Grow(BoxOf(p));
        }

        bool IsLookedEnd(size_t k) const {
            const Element &e = g.elements[k];
            if (!looked[k] || degenerate[k])
                return false;
            return e.HasEnds() && !e.IsClosed(g);
        }
        double Gap() const {
            if (gap)
                return *gap;
            const double size = box.IsEmpty() ? 0.0 : std::max(box.Size().x, box.Size().y);
            return 1e-3 * (size > 0.0 ? size : 1.0);
        }
        bool IsOnePoint(const End &a, const End &b) const {
            return g.coordsOf[a.point] == g.coordsOf[b.point] || a.at.IsClose(b.at, tol);
        }
        void Ends();

        void Contacts();
        void CutPair(size_t i, size_t j, Contact &c) const;
        static PrimitiveEdge PrimitiveOf(const Edge &e) {
            if (const Line *l = std::get_if<Line>(&e))
                return *l;
            return std::get<Arc>(e);
        }

        bool IsEndOf(size_t k, const Vertex &p) const {
            if (!IsLookedEnd(k))
                return false;
            const std::array<Vertex, 2> ends = g.elements[k].EndCoords(g);
            return p.IsClose(ends[0], nearTol) || p.IsClose(ends[1], nearTol);
        }
        bool IsOn(const std::vector<PrimitiveEdge> &stretches, const Vertex &p) const {
            for (const PrimitiveEdge &s : stretches)
                if (s.Distance(p) <= nearTol)
                    return true;
            return false;
        }
        bool IsOn(const std::vector<Vertex> &points, const Vertex &p) const {
            for (const Vertex &q : points)
                if (q.IsClose(p, nearTol))
                    return true;
            return false;
        }

        std::vector<Vertex> StretchMiddles(const std::vector<PrimitiveEdge> &shared) const;
        static Vertex HalfwayAlong(const std::vector<PrimitiveEdge> &shared, const std::vector<std::pair<size_t, bool>> &run);
    };

}
