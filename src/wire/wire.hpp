// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/axissystem.hpp"
#include "core/config.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "curves/edgelist.hpp"
#include "core/objectid.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/circle.hpp"
#include "curves/spline.hpp"
#include "curves/primitiveedge.hpp"
#include "core/grid.hpp"
#include "core/placement.hpp"
#include "wire/loops.hpp"

namespace planex2d {

    // how often a wire's edge list changed; an assignment counts as a change too, so a view taken before sees it
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
            static void MoveEnd(Edge &e, bool end, const Vertex &p) {
                std::visit([&](auto &x) {
                    if constexpr (!IsCircle<decltype(x)>) {
                        if (end)
                            x.template Trim<true>(x.Start(), p);
                        else
                            x.template Trim<true>(p, x.End());
                    }
                }, e);
            }

            std::vector<Vertex> Endpoints() const { return edges.Endpoints(); }
            // n of an id prefix + n, else 0
            static size_t NumberAfter(const std::string &id, const std::string &prefix) {
                if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0 ||
                    id.find_first_not_of("0123456789", prefix.size()) != std::string::npos)
                    return 0;
                return (size_t)std::stoull(id.substr(prefix.size()));
            }

        private:
            void Move(std::vector<Vertex> &pts, Grid &grid, size_t j, const Vertex &p) {
                grid.Erase(j, pts[j]);
                pts[j] = p;
                grid.Insert(j, p);
                MoveEnd(edges[j / 2], j % 2 == 1, p);
            }
            // a circle's start and end are one point: they match each other
            bool Matched(const std::vector<Vertex> &pts, const Grid &grid, size_t k) const {
                const bool circle = std::holds_alternative<Circle>(edges[k / 2]);
                bool matched = false;
                grid.ForCell(pts[k], [&](size_t j) { matched = matched || (j != k && (circle || j / 2 != k / 2) && pts[j] == pts[k]); });
                return matched;
            }

            struct Chain {
                std::vector<size_t> edges;
                std::vector<size_t> vids;
            };
            struct Chains {
                std::vector<size_t> id;
                std::vector<std::vector<size_t>> inc;
                std::vector<bool> small, tiny;
                std::vector<Chain> chains;
                size_t Start(size_t i) const { return id[2 * i]; }
                size_t End(size_t i) const { return id[2 * i + 1]; }
            };
            Properties PropsAtVertex(const Chains &c, size_t i, size_t v) const;
            Ends ChainEnds(const Chains &c, const Chain &ch, size_t b, size_t e) const;
            static constexpr size_t none = (size_t)-1;
            size_t VertexIds(std::vector<size_t> &id) const;
            Chains FindChains(double maxLength, double maxAngle, double ignore, bool fromSplineIsSmall, bool stopAtVertexProps) const;
            std::vector<PrimitiveEdge> Oriented(const Chain &ch, size_t b, size_t e, const Chains &c) const;

        public:
            explicit Wire(const AxisSystem &axes = AxisSystem()) : axes(axes) {}
            const std::string &Id() { return objectId.Get(); }
            const std::string &AssignedId() const { return objectId.id; }
            void SetId(std::string id) { objectId.id = std::move(id); }
            const std::string &IdKind() const { return objectId.kind; }
            void SetIdKind(std::string kind) { objectId.kind = std::move(kind); }
            // the edges' ids within the object ("Line.3"): those without one numbered on per kind in edge order,
            // renumber: all anew. The edges stay what they are, views taken before stay valid
            void AssignIds(bool renumber = false) {
                auto kindOf = [](const Edge &e) -> std::string {
                    return std::holds_alternative<Line>(e) ? "Line" : std::holds_alternative<Arc>(e) ? "Arc" : std::holds_alternative<Circle>(e) ? "Circle" : "Spline";
                };
                std::map<std::string, size_t> last;
                if (!renumber)
                    for (const Edge &e : edges)
                        if (const Properties &p = PropsOf(e); p && p->id)
                            last[kindOf(e)] = std::max(last[kindOf(e)], NumberAfter(*p->id, kindOf(e) + "."));
                for (Edge &e : edges) {
                    const Properties p = PropsOf(e);
                    if (!renumber && p && p->id)
                        continue;
                    const std::string kind = kindOf(e);
                    SetProps(e, ChangedProperties(p, [&](ShapeProperties &q) { q.id = kind + "." + std::to_string(++last[kind]); }));
                }
            }
            // the vertices' ids ("Vertex.3"), on the edge ends there like their other properties: those without one
            // numbered on in the order the vertices first appear, renumber: all anew
            void AssignVertexIds(bool renumber = false) {
                const std::vector<std::vector<size_t>> vertices = VertexEndpoints();
                const std::string prefix = "Vertex.";
                size_t last = 0;
                if (!renumber)
                    for (const std::vector<size_t> &at : vertices)
                        if (const Properties p = VertexProps(at); p && p->id)
                            last = std::max(last, NumberAfter(*p->id, prefix));
                for (const std::vector<size_t> &at : vertices) {
                    const Properties p = VertexProps(at);
                    if (!renumber && p && p->id)
                        continue;
                    const Properties q = ChangedProperties(p, [&](ShapeProperties &x) { x.id = prefix + std::to_string(++last); });
                    for (size_t k : at)
                        SetEndpointProps(k, q);
                }
            }
            void ClearVertexIds() {
                for (Edge &e : edges)
                    std::visit([](auto &x) {
                        if (!x.ends)
                            return;
                        x.ends = MakeEnds(WithoutId(x.ends->start), WithoutId(x.ends->end));
                    }, e);
            }
            // the largest n among the edges' ids prefix + n
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
            // a circle has no vertex: its ends are left out
            std::vector<std::vector<size_t>> VertexEndpoints() const {
                std::vector<size_t> id;
                std::vector<std::vector<size_t>> at(VertexIds(id));
                for (size_t k = 0; k < id.size(); ++k)
                    if (!std::holds_alternative<Circle>(edges[k / 2]))
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
            // the edge ends at the vertex at p, none if there is none
            std::vector<size_t> EndpointsAt(const Vertex &p) const {
                for (std::vector<size_t> &at : VertexEndpoints())
                    if (Endpoint(at.front()).IsClose(p, Tol()))
                        return std::move(at);
                return {};
            }
            // the points of the vertices named name
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
            Wire &Mirror(const Vertex &a, const Vertex &d) {
                IsFinite("Mirror", {a.x, a.y, d.x, d.y});
                Vertex u = MirrorDirection(d);
                for (Edge &e : edges)
                    std::visit([&](auto &x) { x.Mirror(a, u); }, e);
                return *this;
            }
            // mesh sizes stay unless asked for
            Wire &Scale(const Vertex &c, double s, bool scaleMaxh = false, bool scaleMinh = false) {
                CheckScale(s);
                IsFinite("Scale", {c.x, c.y});
                for (Edge &e : edges)
                    std::visit([&](auto &x) { x.Scale(c, s); }, e);
                if (!scaleMaxh && !scaleMinh)
                    return *this;
                std::map<Properties, Properties> scaled;
                auto sized = [&](const Properties &p) {
                    auto [it, isNew] = scaled.try_emplace(p);
                    if (isNew)
                        it->second = ScaledSizes(p, s, scaleMaxh, scaleMinh);
                    return it->second;
                };
                for (Edge &e : edges) {
                    SetProps(e, sized(PropsOf(e)));
                    if (const Ends &x = EndsOf(e))
                        SetEnds(e, MakeEnds(sized(x->start), sized(x->end)));
                }
                return *this;
            }
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
            // the edges (index, true: start to end) of a loop, about its first vertex: that keeps the terms of large
            // coordinates from cancelling; splines by their approximation
            double LoopArea(const std::vector<std::pair<size_t, bool>> &loop) const;
            double Length() const {
                double l = 0.0;
                for (const Edge &e : edges)
                    l += LengthOf(e);
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
            // the regions of a clean edge list as faces, each with props; pieces touching only at points are faces of
            // their own
            FaceList Faces(const Properties &props = nullptr) const;
            // the single region of a clean edge list, an empty face if there is none
            Face SingleFace(const std::string &call) const;
            Edge &operator[](size_t i) { return edges[i]; }
            const Edge &operator[](size_t i) const { return edges[i]; }
            auto begin() { return edges.begin(); }
            auto end() { return edges.end(); }
            auto begin() const { return edges.begin(); }
            auto end() const { return edges.end(); }
            // edges added, removed or reordered (moved edges keep their places)
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
            // the circles with points in at (per edge) by their arcs between the points; per edge now, the index it had
            // before (empty: nothing changed)
            std::vector<size_t> SplitCircles(const std::vector<std::vector<Vertex>> &at, double tol) {
                if (std::all_of(at.begin(), at.end(), [](const std::vector<Vertex> &p) { return p.empty(); }))
                    return {};
                EdgeList split;
                std::vector<uint32_t> splitOwners;
                std::vector<size_t> from;
                auto put = [&](Edge e, size_t i) {
                    split.push_back(std::move(e));
                    from.push_back(i);
                    if (!owners.empty())
                        splitOwners.push_back(owners[i]);
                };
                for (size_t i = 0; i < edges.size(); ++i) {
                    const Circle *c = std::get_if<Circle>(&edges[i]);
                    if (c && !at[i].empty())
                        for (Arc &arc : c->Arcs(at[i], tol))
                            put(std::move(arc), i);
                    else
                        put(edges[i], i);
                }
                edges = std::move(split);
                owners = std::move(splitOwners);
                ++changed.n;
                return from;
            }
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

            // splinePieces: also the pieces of a spline (the export's last step; a spline fit would refit them)
            void RemoveSmallEdges(double tol, bool splinePieces = false);

            void RemoveFreeEdges();

            void FitArcs(const ArcFit &fit);

            void FitSplines(const SplineFit &fit);

            Wire Cleaned(const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup) const;
            Wire &Simplify(const ArcFit &arcs = config::arc_fit, const SplineFit &fit = config::spline_fit,
                           const Cleanup &cleanup = config::cleanup);
            Wire Offset(double d) const;

            // each corner of at cut by the rule (FilletRule, ChamferRule): the edges there shortened, the new edge between
            // them; the corners that cannot be cut are warned about, none raises. In shape/fillet.hpp
            template <class Rule>
            void CutCorners(const std::vector<Vertex> &at, const Rule &rule, const std::string &call);
    };

    struct WireList : std::vector<Wire> {
        using vector::vector;

        // the region bounded by the wires, holes by nesting; stretches drawn twice by closed wires cancel, an open
        // wire's stretch on another edge is left out
        Wire Region() const;
    };

}

#include "wire/topology.hpp"
#include "wire/cleanup.hpp"
#include "wire/fit.hpp"
