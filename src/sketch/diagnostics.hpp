// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "boolean/cut.hpp"
#include "core/bbox.hpp"
#include "core/config.hpp"
#include "core/grid.hpp"
#include "core/vertex.hpp"
#include "curves/circle.hpp"
#include "curves/edgelist.hpp"
#include "curves/primitiveedge.hpp"
#include "curves/spline.hpp"
#include "sketch/geometry.hpp"

// The geometry faults of a sketch's drawing: ends no other element meets, ends of two elements that
// nearly meet, elements that cross or touch, elements that share a stretch, and elements too small to be one.

namespace planex2d::sketch {

    // a fault: the elements it concerns, the lower index first, and the place it is marked at
    struct Fault {
        std::vector<size_t> elements;
        Vertex at;
    };
    struct Faults {
        std::vector<Fault> openEnds, missingCoincident, crossings, overlaps, degenerate;
    };

    class Diagnosis {
    public:
        // looked: per element, whether it is looked at (the drawn lines, arcs, circles and splines); gap: ends of two
        // elements closer than it are a missing coincident, none for 1e-3 of the size of what is looked at
        Diagnosis(const Geometry &g, std::vector<char> looked, std::optional<double> gap)
            : g(g), looked(std::move(looked)), gap(gap), tol(Tol(g.Size())), near(4.0 * tol), degenerate(g.elements.size(), 0) {}

        // edgesOf(k): element k's edges as the sketch exports them
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
        // what two elements have in common: the points they meet at and the stretches of the lower one they share
        struct Contact {
            std::vector<Vertex> points;
            std::vector<PrimitiveEdge> shared;
        };

        const Geometry &g;
        std::vector<char> looked;
        std::optional<double> gap;
        double tol;
        // Cut's own tolerance grows with the scale of its edges, up to about twice the sketch's size
        double near;
        std::vector<char> degenerate;
        std::vector<PrimitiveEdge> flat;
        std::vector<size_t> source;
        BBox box;
        Faults out;

        bool IsDegenerate(size_t k) {
            const Element &e = g.elements[k];
            std::optional<Vertex> at;
            if (e.kind == ElementKind::Line)
                at = DegenerateLine(e);
            else if (e.kind == ElementKind::Arc || e.kind == ElementKind::Circle)
                at = DegenerateRound(e);
            else if (e.kind == ElementKind::Spline)
                at = DegenerateSpline(e);
            if (!at)
                return false;
            degenerate[k] = 1;
            box.Grow(*at);
            out.degenerate.push_back({{k}, *at});
            return true;
        }
        std::optional<Vertex> DegenerateLine(const Element &e) const {
            const Vertex a = g.Coords(e.Start()), b = g.Coords(e.End());
            if (a.IsClose(b, Tol(std::max(NormInf(a), NormInf(b)))))
                return (a + b) * 0.5;
            return std::nullopt;
        }
        // a circle of no radius, an arc of no radius or no length
        std::optional<Vertex> DegenerateRound(const Element &e) const {
            const Vertex c = g.Coords(e.Center());
            const double r = g.values[e.Radius()], size = Tol(NormInf(c) + r);
            if (r <= size)
                return c;
            if (e.kind == ElementKind::Arc && r * Arc(g.Coords(e.Start()), g.Coords(e.End()), c, e.ccw).Sweep() <= size)
                return e.At(g, 0.5);
            return std::nullopt;
        }
        // two of its points one after the other (the last and the first of a closed one) at one place: the middle of
        // the polyline through its points
        std::optional<Vertex> DegenerateSpline(const Element &e) const {
            std::vector<Vertex> q = e.SplinePoints(g);
            if (g.splines[e.SplineIndex()].closed)
                q.push_back(q.front());
            double size = 0.0;
            for (const Vertex &p : q)
                size = std::max(size, NormInf(p));
            bool repeated = false;
            for (size_t k = 1; k < q.size(); ++k)
                repeated = repeated || q[k].IsClose(q[k - 1], Tol(size));
            if (!repeated)
                return std::nullopt;
            double length = 0.0;
            for (size_t k = 1; k < q.size(); ++k)
                length += Norm(q[k] - q[k - 1]);
            double left = 0.5 * length;
            for (size_t k = 1; k < q.size(); ++k) {
                const double l = Norm(q[k] - q[k - 1]);
                if (l > 0.0 && left <= l)
                    return q[k - 1] + (q[k] - q[k - 1]) * (left / l);
                left -= l;
            }
            return q.front();
        }

        // lines and arcs as they are, a circle as its halves, a spline as its approximation
        void Flatten(const Edge &e, size_t k) {
            if (const Spline *s = std::get_if<Spline>(&e)) {
                for (const PrimitiveEdge &p : s->Approximation())
                    AddFlat(p, k);
            } else if (const Circle *c = std::get_if<Circle>(&e)) {
                for (const Arc &half : c->Halves())
                    AddFlat(half, k);
            } else if (const Line *l = std::get_if<Line>(&e)) {
                AddFlat(*l, k);
            } else {
                AddFlat(std::get<Arc>(e), k);
            }
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
            return e.kind == ElementKind::Line || e.kind == ElementKind::Arc ||
                   ((e.kind == ElementKind::Spline || e.kind == ElementKind::OffsetCurve) && !e.IsClosed(g));
        }
        double Gap() const {
            if (gap)
                return *gap;
            const double size = box.IsEmpty() ? 0.0 : std::max(box.Size().x, box.Size().y);
            return 1e-3 * (size > 0.0 ? size : 1.0);
        }
        // the ends sorted along h: an end is met by another element's end at its place (one unknown, or within the
        // rounding); ends of two elements closer than the gap and not one point are a missing coincident
        void Ends() {
            std::vector<End> ends;
            for (size_t k = 0; k < g.elements.size(); ++k)
                if (IsLookedEnd(k))
                    for (size_t p : g.elements[k].Ends(g))
                        ends.push_back({k, p, g.Coords(p)});
            std::sort(ends.begin(), ends.end(), [](const End &a, const End &b) { return a.at.x < b.at.x; });
            const double reach = std::max(Gap(), tol);
            std::vector<char> met(ends.size(), 0);
            for (size_t i = 0; i < ends.size(); ++i)
                for (size_t j = i + 1; j < ends.size() && ends[j].at.x - ends[i].at.x <= reach; ++j) {
                    const End &a = ends[i], &b = ends[j];
                    if (a.element == b.element)
                        continue;
                    if (g.coordsOf[a.point] == g.coordsOf[b.point] || a.at.IsClose(b.at, tol)) {
                        met[i] = met[j] = 1;
                        continue;
                    }
                    if (Norm(b.at - a.at) < Gap()) {
                        const auto [first, second] = std::minmax(a.element, b.element);
                        out.missingCoincident.push_back({{first, second}, (a.at + b.at) * 0.5});
                    }
                }
            for (size_t i = 0; i < ends.size(); ++i)
                if (!met[i])
                    out.openEnds.push_back({{ends[i].element}, ends[i].at});
        }

        // the pieces of two elements near each other in a grid, cut pairwise; per pair of elements the stretches they
        // share are overlaps and the points they meet at elsewhere crossings, unless both end there
        void Contacts() {
            if (flat.size() < 2)
                return;
            std::vector<BBox> boxes;
            for (const PrimitiveEdge &p : flat)
                boxes.push_back(BoxOf(p));
            Grid grid = Grid::OfBoxes(boxes);
            std::map<std::pair<size_t, size_t>, Contact> contacts;
            std::vector<uint32_t> nearby;
            for (size_t i = 0; i < flat.size(); ++i) {
                nearby.clear();
                grid.InBox(boxes[i].Grown(near), nearby);
                for (uint32_t j : nearby)
                    if (j > i && source[j] != source[i])
                        CutPair(i, j, contacts[{source[i], source[j]}]);
            }
            for (auto &[pair, contact] : contacts) {
                for (const Vertex &m : StretchMiddles(contact.shared))
                    out.overlaps.push_back({{pair.first, pair.second}, m});
                std::vector<Vertex> seen;
                for (const Vertex &p : contact.points) {
                    if ((IsEndOf(pair.first, p) && IsEndOf(pair.second, p)) || IsOn(contact.shared, p) || IsOn(seen, p))
                        continue;
                    seen.push_back(p);
                    out.crossings.push_back({{pair.first, pair.second}, p});
                }
            }
        }
        // the pieces flat[i] and flat[j] of two elements, the lower one first
        void CutPair(size_t i, size_t j, Contact &c) const {
            PrimitiveEdge a = flat[i], b = flat[j];
            CutPieces pieces;
            CutRecord rec;
            const CutResult r = std::visit([&](auto &x, auto &y) { return Cut(x, y, pieces, rec, tol); }, a, b);
            for (const PairEvent &e : rec.events)
                if (e.kind != PairKind::Overlap)
                    c.points.push_back(e.at);
            if (r != CutResult::Overlap && r != CutResult::Congruent)
                return;
            // b is left whole where it lies on a as a whole
            if (pieces.bShared) {
                c.shared.push_back(b);
                return;
            }
            for (size_t k = 0; k < pieces.a.size() + 1; ++k)
                if (pieces.AShared(k))
                    c.shared.push_back(k == 0 ? a : PrimitiveOf(pieces.a[k - 1]));
        }
        static PrimitiveEdge PrimitiveOf(const Edge &e) {
            if (const Line *l = std::get_if<Line>(&e))
                return *l;
            return std::get<Arc>(e);
        }

        bool IsEndOf(size_t k, const Vertex &p) const {
            if (!IsLookedEnd(k))
                return false;
            const std::array<Vertex, 2> ends = g.elements[k].EndCoords(g);
            return p.IsClose(ends[0], near) || p.IsClose(ends[1], near);
        }
        bool IsOn(const std::vector<PrimitiveEdge> &stretches, const Vertex &p) const {
            for (const PrimitiveEdge &s : stretches)
                if (s.Distance(p) <= near)
                    return true;
            return false;
        }
        bool IsOn(const std::vector<Vertex> &points, const Vertex &p) const {
            for (const Vertex &q : points)
                if (q.IsClose(p, near))
                    return true;
            return false;
        }

        // the shared pieces chained at their ends into stretches, each one's point halfway along it; a stretch starts at
        // a piece end no other piece meets, a closed one anywhere
        std::vector<Vertex> StretchMiddles(const std::vector<PrimitiveEdge> &shared) const {
            std::vector<Vertex> middles;
            std::vector<char> used(shared.size(), 0);
            const auto meets = [&](const Vertex &p, size_t except) {
                for (size_t k = 0; k < shared.size(); ++k)
                    if (k != except && !used[k] && (shared[k].Start().IsClose(p, near) || shared[k].End().IsClose(p, near)))
                        return k;
                return none;
            };
            for (size_t first = 0; first < shared.size(); ++first) {
                if (used[first])
                    continue;
                size_t start = first;
                bool reversed = false;
                for (size_t k = first; k < shared.size(); ++k)
                    if (!used[k] && (meets(shared[k].Start(), k) == none || meets(shared[k].End(), k) == none)) {
                        start = k;
                        reversed = meets(shared[k].Start(), k) != none;
                        break;
                    }
                std::vector<std::pair<size_t, bool>> run{{start, reversed}};
                used[start] = 1;
                Vertex tip = reversed ? shared[start].Start() : shared[start].End();
                for (size_t next = meets(tip, start); next != none; next = meets(tip, next)) {
                    const bool flip = !shared[next].Start().IsClose(tip, near);
                    run.push_back({next, flip});
                    used[next] = 1;
                    tip = flip ? shared[next].Start() : shared[next].End();
                }
                middles.push_back(HalfwayAlong(shared, run));
            }
            return middles;
        }
        static Vertex HalfwayAlong(const std::vector<PrimitiveEdge> &shared, const std::vector<std::pair<size_t, bool>> &run) {
            double length = 0.0;
            for (const auto &[k, flip] : run)
                length += shared[k].Length();
            double left = 0.5 * length;
            for (const auto &[k, flip] : run) {
                const double l = shared[k].Length();
                if (left <= l) {
                    const double t = l > 0.0 ? left / l : 0.5;
                    return shared[k].PointAtFraction(flip ? 1.0 - t : t);
                }
                left -= l;
            }
            return shared[run.back().first].MidPoint();
        }
    };

}
