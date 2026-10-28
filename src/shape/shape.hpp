// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/axissystem.hpp"
#include "shape/face.hpp"

namespace planex2d {

    // faces that do not overlap and wires on one plane, cut against each other. every edge is stored once; its owner is
    // the set of parts it belongs to, several for a stretch glued wires share. a part is a group of faces with the same
    // properties (split into single faces only when they are asked for), parts 0 .. groups - 1, or a wire
    class Shape {
        friend class PlanexWriter;
        friend class PlanexReader;

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
            // one owner per part, in part order
            void OwnPart(std::vector<uint32_t> owners) {
                ++groupChanges;
                ownerParts.clear();
                for (uint32_t k = 0; k < groupProps.size() + wireCount; ++k)
                    ownerParts.push_back({k});
                edges.SetOwners(std::move(owners), GroupCount32());
            }
            // parts without edges go, the others are renumbered
            void Compact() {
                ++groupChanges;
                std::vector<bool> usedOwner(ownerParts.size(), false), usedPart(groupProps.size() + wireCount, false);
                for (size_t i = 0; i < edges.Size(); ++i)
                    usedOwner[edges.Owner(i)] = true;
                for (size_t o = 0; o < ownerParts.size(); ++o)
                    if (usedOwner[o])
                        for (uint32_t p : ownerParts[o])
                            usedPart[p] = true;
                std::vector<uint32_t> part(usedPart.size()), owner(ownerParts.size());
                std::vector<Properties> props;
                size_t wires = 0;
                uint32_t next = 0;
                for (size_t k = 0; k < usedPart.size(); ++k) {
                    if (!usedPart[k])
                        continue;
                    part[k] = next++;
                    if (k < groupProps.size())
                        props.push_back(groupProps[k]);
                    else
                        ++wires;
                }
                std::vector<std::vector<uint32_t>> sets;
                for (size_t o = 0; o < ownerParts.size(); ++o) {
                    if (!usedOwner[o])
                        continue;
                    owner[o] = (uint32_t)sets.size();
                    std::vector<uint32_t> &s = sets.emplace_back();
                    for (uint32_t p : ownerParts[o])
                        s.push_back(part[p]);
                }
                std::vector<uint32_t> owners;
                for (size_t i = 0; i < edges.Size(); ++i)
                    owners.push_back(owner[edges.Owner(i)]);
                ownerParts = std::move(sets);
                groupProps = std::move(props);
                wireCount = wires;
                edges.SetOwners(std::move(owners), FaceOwners());
            }
            // the parts every edge belongs to, ordered as in ownerParts: the first groupProps.size() parts are face groups
            void SetParts(const std::vector<std::vector<uint32_t>> &parts) {
                ++groupChanges;
                ownerParts.clear();
                std::map<std::vector<uint32_t>, uint32_t> index;
                for (const std::vector<uint32_t> &p : parts)
                    index.emplace(p, 0);
                for (bool faces : {true, false})
                    for (auto &[p, o] : index)
                        if (HasGroup(p) == faces) {
                            o = (uint32_t)ownerParts.size();
                            ownerParts.push_back(p);
                        }
                std::vector<uint32_t> owners;
                for (const std::vector<uint32_t> &p : parts)
                    owners.push_back(index[p]);
                edges.SetOwners(std::move(owners), FaceOwners());
            }
            WireList Parts(size_t first, size_t count) const {
                WireList parts(count, Wire(edges.Axes()));
                for (size_t i = 0; i < edges.Size(); ++i)
                    for (uint32_t p : PartsOf(i))
                        if (p >= first && p < first + count)
                            parts[p - first].Add(edges[i]);
                return parts;
            }

        public:
            // the edges of every face group, an edge between two faces of one group twice
            WireList Groups() const { return Parts(0, groupProps.size()); }
            // the edges of group k in w (edges or their cleaned copy, with the same owners) once each, and which of them
            // cut through it (listed twice)
            std::pair<std::vector<size_t>, std::vector<bool>> GroupEdges(const Wire &w, uint32_t k) const {
                std::vector<size_t> index;
                std::vector<bool> cut;
                for (size_t i = 0; i < w.Size(); ++i) {
                    const std::vector<uint32_t> &p = ownerParts[w.Owner(i)];
                    if (long n = std::count(p.begin(), p.end(), k); n > 0) {
                        index.push_back(i);
                        cut.push_back(n > 1);
                    }
                }
                return {index, cut};
            }
            // the faces of group k as edge indices into w, from the arrangement of its edges: pieces touching only at
            // points are faces of their own
            std::vector<std::vector<size_t>> GroupFaces(const Wire &w, uint32_t k) const {
                auto [index, cut] = GroupEdges(w, k);
                Wire local(w.Axes());
                for (size_t i : index)
                    local.Add(w[i]);
                std::vector<std::vector<size_t>> faces = RegionFaces(local, cut);
                for (std::vector<size_t> &f : faces)
                    for (size_t &i : f)
                        i = index[i];
                return faces;
            }
            explicit Shape(const AxisSystem &axes = AxisSystem()) : edges(axes) { edges.SetIdKind("Shape"); }
            Shape(const AxisSystem &axes, const FaceList &faces, const WireList &wires = {})
                : edges(axes), wireCount(wires.size()) {
                edges.SetIdKind("Shape");
                std::vector<uint32_t> owners;
                for (size_t k = 0; k < faces.size(); ++k) {
                    if (!faces[k].Axes().IsClose(axes))
                        throw std::invalid_argument("the parts lie on different planes; move them onto one plane first");
                    groupProps.push_back(faces[k].Props());
                    MergeIdentifications(ids, faces[k].Ids());
                    for (const Edge &e : faces[k].Edges()) {
                        edges.Add(e);
                        owners.push_back((uint32_t)k);
                    }
                }
                for (size_t k = 0; k < wires.size(); ++k) {
                    if (!wires[k].Axes().IsClose(axes))
                        throw std::invalid_argument("the parts lie on different planes; move them onto one plane first");
                    for (const Edge &e : wires[k]) {
                        edges.Add(e);
                        owners.push_back((uint32_t)(faces.size() + k));
                    }
                }
                OwnPart(std::move(owners));
            }
            explicit Shape(const Face &f) : Shape(f.Axes(), {f}) {}
            // the regions of a clean edge list as one group
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
            // the faces of group k become groups of their own, in their order and in place of k, so every face keeps
            // its index in Faces() and the edges stay as they are
            void SeparateFaces(uint32_t k) {
                const std::vector<std::vector<size_t>> faces = GroupFaces(edges, k);
                if (faces.size() < 2)
                    return;
                const auto extra = (uint32_t)(faces.size() - 1);
                std::vector<std::vector<uint32_t>> facesOf(edges.Size());
                for (uint32_t f = 0; f < faces.size(); ++f)
                    for (size_t i : faces[f])
                        facesOf[i].push_back(k + f);
                std::vector<std::vector<uint32_t>> parts(edges.Size());
                for (size_t i = 0; i < edges.Size(); ++i) {
                    const std::vector<uint32_t> &of = PartsOf(i);
                    const auto inGroup = (size_t)(std::count(of.begin(), of.end(), k));
                    // an edge listed twice for the group stays so (a cut through one face), one on none of its faces
                    // stays with the first
                    while (facesOf[i].size() < inGroup)
                        facesOf[i].push_back(facesOf[i].empty() ? k : facesOf[i].front());
                    bool placed = false;
                    for (uint32_t q : of) {
                        if (q != k) {
                            parts[i].push_back(q > k ? q + extra : q);
                        } else if (!placed) {
                            parts[i].insert(parts[i].end(), facesOf[i].begin(), facesOf[i].end());
                            placed = true;
                        }
                    }
                }
                groupProps.insert(groupProps.begin() + k + 1, extra, groupProps[k]);
                SetParts(parts);
            }
            WireList WireParts() const { return Parts(groupProps.size(), wireCount); }
            // the loops of every face group and the chains of every wire
            WireList AllWires() const {
                WireList all;
                for (const Wire &part : Parts(0, groupProps.size() + wireCount))
                    for (Wire &w : part.Wires())
                        all.push_back(std::move(w));
                return all;
            }
            // the edges of all faces: a clean region
            Wire FaceEdges() const {
                Wire w(edges.Axes());
                for (size_t i = 0; i < edges.Size(); ++i)
                    if (HasGroup(PartsOf(i)))
                        w.Add(edges[i]);
                return w;
            }
            // the edges on no face: those of the wires alone
            Wire WireEdges() const {
                Wire w(edges.Axes());
                for (size_t i = 0; i < edges.Size(); ++i)
                    if (!HasGroup(PartsOf(i)))
                        w.Add(edges[i]);
                return w;
            }
            // an edge counted for two faces: between two of them, or a cut through one face part (listed twice for it)
            bool HasSharedFaces() const {
                for (const std::vector<uint32_t> &p : ownerParts)
                    if (std::count_if(p.begin(), p.end(), [&](uint32_t k) { return k < GroupCount32(); }) > 1)
                        return true;
                return false;
            }
            // the edges on exactly one face: the outline of all faces together
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
            // pieces on one carrier with the same owner join, see Wire::MergeOnCarriers
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
            // its faces of less area than area thrown away, or (merge) each united with the face it shares the longest
            // boundary with (thrown away when it shares none); the wires stay
            Shape WithoutSmallFaces(double area, bool merge) const;
            // on all edges at once, so a vertex shared between parts moves with both
            Shape &Simplify(const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                            const Cleanup &cleanup = config::state->cleanup) {
                if (cleanup.remove_faces_smaller > 0.0) {
                    Identifications kept = std::move(ids);
                    *this = WithoutSmallFaces(cleanup.remove_faces_smaller, cleanup.merge_small_faces_instead);
                    ids = std::move(kept);
                }
                edges = edges.Cleaned(arcs, fit, cleanup);
                Compact();
                return *this;
            }
            // what an export needs, without OCC: the cleaned edges, per group its faces (edge indices), per edge its
            // direction (reversed: it runs with a face it bounds on its right) and the parts it belongs to (the groups
            // first, then the wires)
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
                              const Cleanup &cleanup = config::state->cleanup) const {
                if (cleanup.remove_faces_smaller > 0.0) {
                    Cleanup rest = cleanup;
                    rest.remove_faces_smaller = 0.0;
                    Shape merged = WithoutSmallFaces(cleanup.remove_faces_smaller, cleanup.merge_small_faces_instead);
                    merged.ids = ids;
                    return merged.Export(arcs, fit, rest);
                }
                ExportPlan plan{edges.Cleaned(arcs, fit, cleanup), {}, {}, {}, groupProps, groupProps.size() + wireCount, ids};
                const Wire &c = plan.edges;
                // every edge runs with a face it bounds on its left, so the mesh normal points out of it: for an edge
                // between two faces a face of its first owner group, the one that names it
                plan.facesOfGroup.resize(groupProps.size());
                plan.reverse.assign(c.Size(), 0);
                std::vector<char> oriented(c.Size(), 0);
                std::vector<std::vector<char>> orientation;
                for (uint32_t k = 0; k < groupProps.size(); ++k) {
                    plan.facesOfGroup[k] = GroupFaces(c, k);
                    for (const std::vector<size_t> &face : plan.facesOfGroup[k]) {
                        Wire local(c.Axes());
                        for (size_t i : face)
                            local.Add(c[i]);
                        orientation.push_back(local.FaceOrientation());
                    }
                }
                for (bool byOwner : {true, false}) {
                    size_t f = 0;
                    for (uint32_t k = 0; k < groupProps.size(); ++k)
                        for (const std::vector<size_t> &face : plan.facesOfGroup[k]) {
                            const std::vector<char> &r = orientation[f++];
                            for (size_t j = 0; j < face.size(); ++j)
                                if (!oriented[face[j]] && (!byOwner || ownerParts[c.Owner(face[j])].front() == k)) {
                                    oriented[face[j]] = 1;
                                    plan.reverse[face[j]] = r[j];
                                }
                        }
                }
                for (size_t i = 0; i < c.Size(); ++i)
                    plan.partsOf.push_back(ownerParts[c.Owner(i)]);
                return plan;
            }
    };

    struct ShapeList : std::vector<Shape> {
        using vector::vector;

        AxisSystem CommonAxes() const {
            for (const Shape &s : *this)
                if (!s.Axes().IsClose(front().Axes()))
                    throw std::invalid_argument("the shapes lie on different planes; move them onto one plane first");
            return empty() ? AxisSystem() : front().Axes();
        }
        // the properties all faces of the shapes have, none if they differ
        std::optional<Properties> CommonFaceProps() const {
            Properties props;
            bool first = true;
            for (const Shape &s : *this)
                for (size_t k = 0; k < s.GroupCount(); ++k) {
                    if (!first && !SameProperties(props, s.GroupProps(k)))
                        return std::nullopt;
                    props = s.GroupProps(k);
                    first = false;
                }
            return props;
        }
        // only faces, or only wires for a union
        void CheckParts(bool unite) const {
            bool faces = false, wires = false;
            for (const Shape &s : *this) {
                faces = faces || s.HasFaces();
                wires = wires || s.WireCount() > 0;
            }
            if (wires && faces)
                throw std::invalid_argument(
                    "a boolean of faces and wires is not defined (a Shape from a Glue keeps its wires): unite the faces "
                    "first, px.Unify(shape.faces), which removes the inner and free wires, or do the boolean first and "
                    "Glue the wires to its result");
            if (wires && !unite)
                throw std::invalid_argument("only faces have an intersection, a difference or a symmetric difference");
        }
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
        // the identifications of all shapes, the first wins where names clash
        Identifications MergedIds() const {
            Identifications ids;
            for (const Shape &s : *this)
                MergeIdentifications(ids, s.Ids());
            return ids;
        }
        ShapeList Rest() const { return empty() ? ShapeList() : ShapeList(begin() + 1, end()); }
    };

}

#include "shape/shapeops.hpp"
