// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/core/config.hpp"
#include "planex/core/bbox.hpp"
#include "planex/curves/edge.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/core/objectid.hpp"
#include "planex/curves/line.hpp"
#include "planex/curves/arc.hpp"
#include "planex/curves/circle.hpp"
#include "planex/curves/spline.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/core/grid.hpp"
#include "planex/core/placement.hpp"
#include "planex/offset/offsetform.hpp"
#include "planex/wire/loops.hpp"

namespace planex2d {

    // an assignment counts as a change too, so a view taken before sees it
    struct ChangeCount {
        uint64_t n = 0;
        ChangeCount() = default;
        ChangeCount(const ChangeCount &) = default;
        ChangeCount &operator=(const ChangeCount &o) {
            n += o.n + 1;
            return *this;
        }
    };

    struct WireList;
    class Face;
    struct FaceList;

    class Wire {
        private:
            EdgeList edges;
            AxisSystem axes;
            // set only for the combined edges of a compound: chains and merges stay within an owner, free edges are
            // removed from owner 0 (the face) only
            std::vector<uint32_t> owners;
            uint32_t faceOwners = 1;
            ChangeCount changed;
            ObjectId objectId{"Wire"};

        public:
            static Vertex EndOf(const Edge &e, bool end) {
                return std::visit([&](const auto &x) { return end ? x.End() : x.Start(); }, e);
            }
            template <class E>
            static void MoveEnds(E &x, const Vertex &a, const Vertex &b, double rounding, double tol = 0.0) {
                if constexpr (std::is_same_v<E, Arc>)
                    x.template Trim<true>(a, b, rounding, tol);
                else
                    x.template Trim<true>(a, b);
            }
            static void MoveEnd(Edge &e, bool end, const Vertex &p, double rounding = 0.0, double tol = 0.0) {
                if (IsClosedEdge(e))
                    return;
                std::visit([&](auto &x) {
                    if constexpr (!IsCircle<decltype(x)>)
                        MoveEnds(x, end ? x.Start() : p, end ? p : x.End(), rounding, tol);
                }, e);
            }

            std::vector<Vertex> Endpoints() const { return edges.Endpoints(); }
            // the 3d line its edges lie on, if they are lines on one line
            std::optional<Axis> StraightLine() const;
            static size_t NumberAfter(const std::string &id, const std::string &prefix) {
                if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0 ||
                    id.find_first_not_of("0123456789", prefix.size()) != std::string::npos)
                    return 0;
                return (size_t)std::stoull(id.substr(prefix.size()));
            }

        private:
            void Move(std::vector<Vertex> &pts, Grid &grid, size_t j, const Vertex &p, double rounding, double tol) {
                grid.Erase(j, pts[j]);
                pts[j] = p;
                grid.Insert(j, p);
                MoveEnd(edges[j / 2], j % 2 == 1, p, rounding, tol);
            }
            bool Matched(const std::vector<Vertex> &pts, const Grid &grid, size_t k) const {
                const bool closed = IsClosedEdge(edges[k / 2]);
                bool matched = false;
                grid.ForCell(pts[k], [&](size_t j) { matched = matched || (j != k && (closed || j / 2 != k / 2) && pts[j] == pts[k]); });
                return matched;
            }

            struct Chain {
                std::vector<size_t> edges;
                std::vector<size_t> vids;
            };
            struct Chains {
                std::vector<size_t> id;
                std::vector<std::vector<size_t>> inc;
                std::vector<bool> isSmall, isTiny;
                std::vector<Chain> chains;
                size_t Start(size_t i) const { return id[2 * i]; }
                size_t End(size_t i) const { return id[2 * i + 1]; }
            };
            Properties PropsAtVertex(const Chains &c, size_t i, size_t v) const;
            Ends ChainEnds(const Chains &c, const Chain &ch, size_t b, size_t e) const;
            static constexpr size_t none = (size_t)-1;
            size_t VertexIds(std::vector<size_t> &id) const;
            struct ChainRule {
                std::optional<double> maxLength;
                double maxAngle = 0.0, ignore = 0.0;
                bool fromSplineIsSmall = false, stopAtVertexProps = false, arcsJoin = false;
            };
            Chains ChainCandidates(const ChainRule &rule) const;
            Chains FindChains(const ChainRule &rule) const;
            std::vector<PrimitiveEdge> Oriented(const Chain &ch, size_t b, size_t e, const Chains &c) const;
            struct ChainPiece {
                Edge edge;
                size_t b, e;
            };
            void FitArcsBetween(const Chains &c, const Chain &ch, size_t b, size_t e, double tol, double ignore, std::vector<ChainPiece> &out) const;
            std::vector<ChainPiece> FitRingArcs(const Chains &c, const Chain &ch, double tol, double ignore) const;
            std::vector<Spline> SplitAtMarkedVertices(const Chains &c, const Chain &ch, size_t b, size_t e, Spline s,
                                                      const std::vector<PrimitiveEdge> &oriented) const;
            std::vector<bool> SmallEdgeFlags(double tol, bool splinePieces) const;

        public:
            explicit Wire(const AxisSystem &axes = AxisSystem()) : axes(axes) {}
            const std::string &Id() { return objectId.Get(); }
            const std::string &AssignedId() const { return objectId.id; }
            void SetId(std::string id) { objectId.id = std::move(id); }
            const std::string &IdKind() const { return objectId.kind; }
            void SetIdKind(std::string kind) { objectId.kind = std::move(kind); }
            void AssignIds(bool renumber = false);
            void AssignVertexIds(bool renumber = false);
            void ClearVertexIds() {
                for (Edge &e : edges)
                    std::visit([](auto &x) {
                        if (!x.ends)
                            return;
                        x.ends = MakeEnds(WithoutId(x.ends->start), WithoutId(x.ends->end));
                    }, e);
            }
            size_t LastNumber(const std::string &prefix) const {
                size_t last = 0;
                for (const Edge &e : edges)
                    if (const Properties &p = PropsOf(e); p && p->id)
                        last = std::max(last, NumberAfter(*p->id, prefix));
                return last;
            }
            bool IsIdTaken(const std::string &id) const {
                for (const Edge &e : edges)
                    if (const Properties &p = PropsOf(e); p && p->id == id)
                        return true;
                for (size_t k = 0; k < 2 * edges.size(); ++k)
                    if (const Properties p = EndpointProps(k); p && p->id == id)
                        return true;
                return false;
            }
            std::string IdOf(size_t i) {
                AssignIds();
                return Id() + "." + *PropsOf(edges[i])->id;
            }
            bool Empty() const { return edges.empty(); }
            size_t Size() const { return edges.size(); }
            const EdgeList &Edges() const { return edges; }
            Vertex Endpoint(size_t k) const { return EndOf(edges[k / 2], k % 2 == 1); }
            Properties EndpointProps(size_t k) const { return PropsAtEnd(EndsOf(edges[k / 2]), k % 2 == 1); }
            void SetEndpointProps(size_t k, Properties p) {
                Edge &e = edges[k / 2];
                Properties s = PropsAtEnd(EndsOf(e), false), t = PropsAtEnd(EndsOf(e), true);
                (k % 2 == 1 ? t : s) = std::move(p);
                SetEnds(e, MakeEnds(std::move(s), std::move(t)));
            }
            std::vector<std::vector<size_t>> VertexEndpoints() const {
                std::vector<size_t> id;
                std::vector<std::vector<size_t>> at(VertexIds(id));
                for (size_t k = 0; k < id.size(); ++k)
                    if (!IsClosedEdge(edges[k / 2]))
                        at[id[k]].push_back(k);
                std::erase_if(at, [](const std::vector<size_t> &v) { return v.empty(); });
                return at;
            }
            Properties VertexProps(const std::vector<size_t> &endpoints) const {
                Properties p;
                for (size_t k : endpoints)
                    p = MergedProperties(p, EndpointProps(k));
                return p;
            }
            std::vector<size_t> EndpointsAt(const Vertex &p) const {
                for (std::vector<size_t> &at : VertexEndpoints())
                    if (Endpoint(at.front()).IsClose(p, Tol()))
                        return std::move(at);
                return {};
            }
            std::vector<Vertex> VerticesNamed(const std::string &name) const {
                std::vector<Vertex> points;
                for (const std::vector<size_t> &at : VertexEndpoints())
                    if (NameOf(VertexProps(at)) == name)
                        points.push_back(Endpoint(at.front()));
                return points;
            }
            const AxisSystem &Axes() const { return axes; }
            void SetAxes(const AxisSystem &a) { axes = a; }
            BBox Box() const { return edges.Box(); }
            double Distance(const Vertex &p) const {
                double nearest = std::numeric_limits<double>::infinity();
                for (const Edge &e : edges)
                    nearest = std::min(nearest, planex2d::Distance(p, e));
                return nearest;
            }
            double LengthScale() const { return edges.LengthScale(); }
            double Tol() const { return edges.Tol(); }
            Grid EdgeGrid() const;
            Wire &Rotate(const Vertex &c, double angle) {
                IsFinite("Rotate", {c.x, c.y, angle});
                double rad = Radians(angle);
                double cs = std::cos(rad);
                double sn = std::sin(rad);
                for (Edge &e : edges)
                    std::visit([&](auto &x) { x.Rotate(c, cs, sn); }, e);
                return *this;
            }
            Wire &Translate(const Vertex &d) {
                IsFinite("Translate", {d.x, d.y});
                for (Edge &e : edges)
                    std::visit([&](auto &x) { x.Translate(d); }, e);
                return *this;
            }
            // in 3d only the plane moves
            Wire &Rotate(const Axis &axis, double angle) {
                IsFinite("Rotate", {angle});
                axes.Rotate(axis, angle);
                return *this;
            }
            Wire &Translate(const Vector3 &v) {
                IsFinite("Translate", {v.x, v.y, v.z});
                axes.Translate(v);
                return *this;
            }
            Wire &Reverse() {
                std::reverse(edges.begin(), edges.end());
                std::reverse(owners.begin(), owners.end());
                for (Edge &e : edges)
                    std::visit([](auto &x) { x = x.Reversed(); }, e);
                return *this;
            }
            Wire &Mirror(const Vertex &a, const Vertex &d) {
                IsFinite("Mirror", {a.x, a.y, d.x, d.y});
                Vertex u = MirrorDirection(d);
                for (Edge &e : edges)
                    std::visit([&](auto &x) { x.Mirror(a, u); }, e);
                return *this;
            }
            Wire &Scale(const Vertex &c, double s, bool scaleMaxh = false, bool scaleMinh = false);
            // in 3d at the plane (its origin and normal); the mirrored axes keep right-handed with their y direction
            // reversed, so the coordinates are mirrored at the u axis, which is exact
            Wire &Mirror(const AxisSystem &plane) {
                axes.Mirror(plane);
                return Mirror(Vertex{}, Vertex{1.0, 0.0});
            }
            Wire &Scale(const Vector3 &p, double s, bool scaleMaxh = false, bool scaleMinh = false) {
                IsFinite("Scale", {p.x, p.y, p.z});
                Scale(Vertex{}, s, scaleMaxh, scaleMinh);
                axes.Scale(p, s);
                return *this;
            }
            std::vector<Loop> Loops() const;
            WireList Wires() const;
            // the edges as (index, true: start to end); about the loop's first vertex, which keeps the terms of large
            // coordinates from cancelling
            template <bool exact = false>
            double LoopArea(const std::vector<std::pair<size_t, bool>> &loop) const;
            double Length() const {
                double l = 0.0;
                for (const Edge &e : edges)
                    l += ExactLengthOf(e);
                return l;
            }
            // per edge: true if it runs from start to end along its loop, counterclockwise for a closed loop, else
            // along the chain
            std::vector<char> Traversal() const;
            std::vector<Loop> FaceCycles() const;
            std::vector<NestedLoop> Nesting() const;
            double Area() const;
            // per edge: true if it must run the other way so that its face lies on its left
            std::vector<char> FaceOrientation() const;
            // p must not lie on an edge
            bool Contains(const Vertex &p) const;
            std::optional<Vertex> InteriorPoint() const;
            // of a clean edge list
            FaceList Faces(const Properties &props = nullptr) const;
            // of a clean edge list
            Face SingleFace(const std::string &call) const;
            Edge &operator[](size_t i) { return edges[i]; }
            const Edge &operator[](size_t i) const { return edges[i]; }
            auto begin() { return edges.begin(); }
            auto end() { return edges.end(); }
            auto begin() const { return edges.begin(); }
            auto end() const { return edges.end(); }
            uint64_t Changes() const { return changed.n; }
            void Add(Edge e) {
                edges.push_back(std::move(e));
                ++changed.n;
            }
            void Append(Edge e, uint32_t owner) {
                edges.push_back(std::move(e));
                ++changed.n;
                if (!owners.empty())
                    owners.push_back(owner);
            }
            void Insert(size_t at, Edge e, uint32_t owner) {
                edges.insert(edges.begin() + (long)at, std::move(e));
                ++changed.n;
                if (!owners.empty())
                    owners.insert(owners.begin() + (long)at, owner);
            }
            void Erase(const std::vector<bool> &removed) {
                planex2d::Erase(edges, removed);
                ++changed.n;
                if (!owners.empty())
                    planex2d::Erase(owners, removed);
            }
            std::vector<size_t> SplitCircles(const std::vector<std::vector<Vertex>> &at, double tol);
            void SplitAt(const std::vector<std::vector<Vertex>> &at, double tol);
            std::vector<size_t> HalveClosedSplines(const std::vector<bool> &which);
            // owners below faceOwners are faces, the others wires
            void SetOwners(std::vector<uint32_t> o, uint32_t faces = 1) {
                owners = std::move(o);
                faceOwners = faces;
            }
            uint32_t Owner(size_t i) const { return owners.empty() ? 0 : owners[i]; }
            template <class F>
            void RemoveIf(F drop) {
                std::erase_if(edges, [&](const Edge &e) { return std::visit(drop, e); });
                ++changed.n;
            }
            template <class E>
            std::vector<E> All() const {
                std::vector<E> all;
                for (const Edge &e : edges)
                    if (const E *x = std::get_if<E>(&e))
                        all.push_back(*x);
                return all;
            }

            std::vector<std::pair<size_t, bool>> OpenEnds() const;
            bool IsClosed() const { return OpenEnds().empty(); }
            std::optional<std::pair<size_t, size_t>> SelfContact() const;
            void CheckChain(const std::string &call) const;
            void Extend(const EdgeList &piece, std::optional<double> tol, const std::string &call);
            WireList OrderedChains() const;
            bool HasEvenDegrees(double tol) const;
            std::vector<size_t> SmallEdges(double size) const;

            void RemoveSmallGaps(double tol);

            void MergeCollinearEdges(double tol);

            template <class F>
            void MergeOnCarriers(F use);
            void MergeOnCarriers();

            void RemoveSmallEdges(double tol, bool splinePieces = false, const std::vector<std::string> &sides = {});

            void RemoveFreeEdges();

            void FitArcs(const ArcFit &fit);

            void FitSplines(const SplineFit &fit);

            Wire Cleaned(const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup, const std::vector<std::string> &sides = {}) const;
            Wire &Simplify(const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                           const Cleanup &cleanup = config::state->cleanup);
            Wire Offset(double d, OffsetEnd ends = OffsetEnd::Round, OffsetCorner corners = OffsetCorner::Round) const;
            Wire OffsetCurves(double d, std::optional<bool> bothSides, OffsetEnd ends, OffsetCorner corners) const;

            // defined in shape/fillet.hpp
            template <class Rule>
            void CutCorners(const std::vector<Vertex> &at, const Rule &rule, const std::string &call);
    };

    struct WireList : std::vector<Wire> {
        using vector::vector;

        Wire Region(const char *call = nullptr) const;
        WireList Chains() const {
            WireList all;
            for (const Wire &part : *this)
                for (Wire &w : part.Wires())
                    all.push_back(std::move(w));
            return all;
        }
    };

}
