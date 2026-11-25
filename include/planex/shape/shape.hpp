// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/shape/face.hpp"

namespace planex2d {

    // gaps: the thin gaps between the faces, not their thin parts; warn: about the slivers kept between faces of different properties
    struct SliverStep {
        double width;
        bool keepOutline = false, gaps = false, warn = false;
    };

    struct SliverRound;

    // every edge is stored once; its owner is the set of parts it belongs to, several for a stretch glued wires share. a
    // part is a group of faces with the same properties, parts 0 .. groups - 1, or a wire
    class Shape {
        private:
            Wire edges;
            // the parts of every owner of the edges, those with a face group first; in each the face group that names
            // the edge (whose edge properties came first) first, then the others sorted
            std::vector<std::vector<uint32_t>> ownerParts;
            std::vector<Properties> groupProps;
            size_t wireCount = 0;
            // counts the changes of the parts with the edges unchanged, e.g. a group split into its faces
            uint64_t groupChanges = 0;
            Identifications ids;

            uint32_t GroupCount32() const { return (uint32_t)groupProps.size(); }
            bool HasGroup(const std::vector<uint32_t> &parts) const { return !parts.empty() && parts.front() < GroupCount32(); }
            uint32_t FaceOwners() const {
                uint32_t n = 0;
                while (n < ownerParts.size() && HasGroup(ownerParts[n]))
                    ++n;
                return n;
            }
            const std::vector<uint32_t> &PartsOf(size_t i) const { return ownerParts[edges.Owner(i)]; }
            void OwnPart(std::vector<uint32_t> owners) {
                ++groupChanges;
                ownerParts.clear();
                for (uint32_t k = 0; k < groupProps.size() + wireCount; ++k)
                    ownerParts.push_back({k});
                edges.SetOwners(std::move(owners), GroupCount32());
            }
            void Compact();
            void SetParts(const std::vector<std::vector<uint32_t>> &parts);
            WireList Parts(size_t first, size_t count) const {
                WireList parts(count, Wire(edges.Axes()));
                for (size_t i = 0; i < edges.Size(); ++i)
                    for (uint32_t p : PartsOf(i))
                        if (p >= first && p < first + count)
                            parts[p - first].Add(edges[i]);
                return parts;
            }

        public:
            // an edge between two faces of one group twice
            WireList Groups() const { return Parts(0, groupProps.size()); }
            std::pair<std::vector<size_t>, std::vector<bool>> GroupEdges(const Wire &w, uint32_t k) const;
            std::vector<std::vector<size_t>> GroupFaces(const Wire &w, uint32_t k) const;
            explicit Shape(const AxisSystem &axes = AxisSystem()) : edges(axes) { edges.SetIdKind("Shape"); }
            Shape(const AxisSystem &axes, const FaceList &faces, const WireList &wires = {});
            explicit Shape(const Face &f) : Shape(f.Axes(), {f}) {}
            // region: a clean edge list
            Shape(Wire region, Properties props) : edges(std::move(region)) {
                edges.SetIdKind("Shape");
                if (!edges.Empty())
                    groupProps.push_back(std::move(props));
                OwnPart(std::vector<uint32_t>(edges.Size(), 0));
            }
            // the edges with the parts each belongs to, ordered as in ownerParts: the first groupProps.size() parts are
            // face groups
            Shape(Wire all, const std::vector<std::vector<uint32_t>> &parts, std::vector<Properties> groups, size_t wires)
                : edges(std::move(all)), groupProps(std::move(groups)), wireCount(wires) {
                edges.SetIdKind("Shape");
                SetParts(parts);
            }

            const Identifications &Ids() const { return ids; }
            Identifications &Ids() { return ids; }
            bool HasFaces() const { return !groupProps.empty(); }
            bool HasOnlyWires() const { return groupProps.empty() && wireCount > 0; }
            size_t GroupCount() const { return groupProps.size(); }
            size_t WireCount() const { return wireCount; }
            bool Empty() const { return edges.Empty(); }
            const Wire &Edges() const { return edges; }
            Wire &Edges() { return edges; }
            uint64_t GroupChanges() const { return groupChanges; }
            // every face as its group and its edge indices, in the order of Faces()
            std::vector<std::pair<uint32_t, std::vector<size_t>>> FaceIndices() const {
                std::vector<std::pair<uint32_t, std::vector<size_t>>> faces;
                for (uint32_t k = 0; k < groupProps.size(); ++k)
                    for (std::vector<size_t> &f : GroupFaces(edges, k))
                        faces.emplace_back(k, std::move(f));
                return faces;
            }
            Face FaceOf(uint32_t k, const std::vector<size_t> &index) const {
                Wire w(edges.Axes());
                for (size_t i : index)
                    w.Add(edges[i]);
                return Face(std::move(w), groupProps.at(k));
            }
            FaceList Faces() const {
                FaceList faces;
                for (const auto &[k, index] : FaceIndices())
                    faces.push_back(FaceOf(k, index));
                return faces;
            }
            void SeparateFaces(uint32_t k);
            WireList WireParts() const { return Parts(groupProps.size(), wireCount); }
            WireList AllWires() const { return Parts(0, groupProps.size() + wireCount).Chains(); }
            Wire FaceEdges() const {
                Wire w(edges.Axes());
                for (size_t i = 0; i < edges.Size(); ++i)
                    if (HasGroup(PartsOf(i)))
                        w.Add(edges[i]);
                return w;
            }
            // the edges of every face, one between two faces or through one twice: the ray parity is that of the union of the faces
            Wire FaceRegion() const {
                Wire w(edges.Axes());
                for (const Wire &g : Groups())
                    for (const Edge &e : g)
                        w.Add(e);
                return w;
            }
            Wire WireEdges() const {
                Wire w(edges.Axes());
                for (size_t i = 0; i < edges.Size(); ++i)
                    if (!HasGroup(PartsOf(i)))
                        w.Add(edges[i]);
                return w;
            }
            bool HasSharedFaces() const {
                for (const std::vector<uint32_t> &p : ownerParts)
                    if (std::count_if(p.begin(), p.end(), [&](uint32_t k) { return k < GroupCount32(); }) > 1)
                        return true;
                return false;
            }
            Wire Outline() const {
                Wire w(edges.Axes());
                for (size_t i = 0; i < edges.Size(); ++i) {
                    const std::vector<uint32_t> &p = PartsOf(i);
                    if (std::count_if(p.begin(), p.end(), [&](uint32_t k) { return k < GroupCount32(); }) == 1)
                        w.Add(edges[i]);
                }
                return w;
            }
            Shape &Compacted() {
                Compact();
                return *this;
            }
            Shape &JoinOnCarriers() {
                edges.MergeOnCarriers();
                return *this;
            }
            const Properties &GroupProps(size_t k) const { return groupProps.at(k); }
            void SetGroupProps(size_t k, Properties p) { groupProps.at(k) = std::move(p); }
            double Area() const {
                double a = 0.0;
                for (const Wire &g : Groups())
                    a += g.Area();
                return a;
            }
            const AxisSystem &Axes() const { return edges.Axes(); }
            BBox Box() const { return edges.Box(); }
            Shape &Rotate(const Vertex &c, double angle) {
                edges.Rotate(c, angle);
                MoveIdentifications(ids, Affine2::Rotation(c, angle));
                return *this;
            }
            Shape &Translate(const Vertex &d) {
                edges.Translate(d);
                MoveIdentifications(ids, Affine2::Translation(d));
                return *this;
            }
            Shape &Rotate(const Axis &axis, double angle) {
                edges.Rotate(axis, angle);
                return *this;
            }
            Shape &Translate(const Vector3 &v) {
                edges.Translate(v);
                return *this;
            }
            Shape &Mirror(const Vertex &a, const Vertex &d) {
                edges.Mirror(a, d);
                MoveIdentifications(ids, Affine2::Reflection(a, d));
                return *this;
            }
            // in 3d the plane moves; mirroring also mirrors the coordinates in it (v -> -v), scaling scales them
            Shape &Mirror(const AxisSystem &plane) {
                edges.Mirror(plane);
                MoveIdentifications(ids, Affine2::Reflection({}, {1.0, 0.0}));
                return *this;
            }
            Shape &Scale(const Vertex &c, double s, bool scaleMaxh = false, bool scaleMinh = false) {
                edges.Scale(c, s, scaleMaxh, scaleMinh);
                for (Properties &p : groupProps)
                    p = ScaledSizes(p, s, scaleMaxh, scaleMinh);
                MoveIdentifications(ids, Affine2::Scaling(c, s));
                return *this;
            }
            Shape &Scale(const Vector3 &p, double s, bool scaleMaxh = false, bool scaleMinh = false) {
                edges.Scale(p, s, scaleMaxh, scaleMinh);
                for (Properties &q : groupProps)
                    q = ScaledSizes(q, s, scaleMaxh, scaleMinh);
                MoveIdentifications(ids, Affine2::Scaling({}, s));
                return *this;
            }
            Shape &FitArcs(const ArcFit &fit) {
                edges.FitArcs(fit);
                return *this;
            }
            Shape &MergeCollinearEdges(double tol) {
                edges.MergeCollinearEdges(tol);
                return *this;
            }
            Shape &RemoveSmallGaps(double tol) {
                edges.RemoveSmallGaps(tol);
                return *this;
            }
            Shape &RemoveSmallEdges(double tol) {
                edges.RemoveSmallEdges(tol);
                Compact();
                return *this;
            }
            Shape &MoveVertices(const std::vector<std::pair<Vertex, Vertex>> &moves, double rounding, double tol);
            Shape WithoutSmallFaces(double area, bool merge) const;
            Shape WithoutSmallHoles(double area) const;
            Shape WithoutSlivers(double width, bool keepOutline = false) const;
            SliverRound SliversMoved(const SliverStep &step) const;
            Shape &Defeature(const Cleanup &cleanup);
            Shape &Simplify(const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                            const Cleanup &cleanup = config::state->cleanup);
            // reversed: the edge runs with a face it bounds on its right; parts: the groups first, then the wires
            struct ExportPlan {
                Wire edges;
                std::vector<std::vector<std::vector<size_t>>> facesOfGroup;
                std::vector<char> reverse;
                std::vector<std::vector<uint32_t>> partsOf;
                std::vector<Properties> groupProps;
                size_t parts = 0;
                Identifications ids;

                bool IsOnGroup(size_t i) const { return !partsOf[i].empty() && partsOf[i].front() < groupProps.size(); }
            };
            ExportPlan Export(const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                              const Cleanup &cleanup = config::state->cleanup) const;
    };

    // deferred: pieces wait for another round
    struct SliverRound {
        Shape shape;
        bool deferred;
    };

    struct ShapeList : std::vector<Shape> {
        using vector::vector;

        AxisSystem CommonAxes() const {
            for (const Shape &s : *this)
                if (!s.Axes().IsClose(front().Axes()))
                    throw std::invalid_argument("the shapes lie on different planes; move them onto one plane first");
            return empty() ? AxisSystem() : front().Axes();
        }
        std::optional<Properties> CommonFaceProps() const;
        void CheckParts(bool unite) const;
        WireList Outlines() const {
            WireList outlines;
            for (const Shape &s : *this)
                outlines.push_back(s.Outline());
            return outlines;
        }
        std::vector<FaceList> FaceLists() const {
            std::vector<FaceList> faces;
            for (const Shape &s : *this)
                faces.push_back(s.Faces());
            return faces;
        }
        Identifications MergedIds() const {
            Identifications ids;
            for (const Shape &s : *this)
                MergeIdentifications(ids, s.Ids());
            return ids;
        }
        ShapeList Rest() const { return empty() ? ShapeList() : ShapeList(begin() + 1, end()); }
    };

}
