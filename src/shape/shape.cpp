// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/shape.hpp"

#include <map>

namespace planex2d {

    void Shape::Compact() {
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

    void Shape::SetParts(const std::vector<std::vector<uint32_t>> &parts) {
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

    // second: which of the edges cut through the group (listed twice)
    std::pair<std::vector<size_t>, std::vector<bool>> Shape::GroupEdges(const Wire &w, uint32_t k) const {
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

    std::vector<std::vector<size_t>> Shape::GroupFaces(const Wire &w, uint32_t k) const {
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

    Shape::Shape(const AxisSystem &axes, const FaceList &faces, const WireList &wires): edges(axes), wireCount(wires.size()) {
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

    // the faces of group k become groups of their own, in their order and in place of k, so every face keeps
    // its index in Faces() and the edges stay as they are
    void Shape::SeparateFaces(uint32_t k) {
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

    Shape &Shape::MoveVertices(const std::vector<std::pair<Vertex, Vertex>> &moves, double rounding, double tol) {
        if (moves.empty())
            return *this;
        auto target = [&](const Vertex &p) -> const Vertex * {
            for (const auto &[from, to] : moves)
                if (from == p)
                    return &to;
            return nullptr;
        };
        std::vector<bool> gone(edges.Size(), false);
        for (size_t i = 0; i < edges.Size(); ++i) {
            if (IsClosedEdge(edges[i]))
                continue;
            const Vertex *a = target(StartOf(edges[i])), *b = target(EndOf(edges[i]));
            const Vertex start = a ? *a : StartOf(edges[i]), end = b ? *b : EndOf(edges[i]);
            const Arc *arc = std::get_if<Arc>(&edges[i]);
            if (start == end && arc && arc->Length() > tol) {
                Circle c(arc->Center(), start, arc->CCW());
                c.props = arc->props;
                c.ends = arc->ends;
                edges[i] = c;
            } else if (start == end)
                gone[i] = true;
            else if (a || b)
                std::visit([&](auto &x) {
                    if constexpr (!IsCircle<decltype(x)>)
                        Wire::MoveEnds(x, start, end, rounding, tol);
                }, edges[i]);
        }
        edges.Erase(gone);
        Compact();
        return *this;
    }

    Shape &Shape::Defeature(const Cleanup &cleanup) {
        Identifications kept = std::move(ids);
        if (cleanup.merge_slivers_thinner > 0.0)
            *this = WithoutSlivers(cleanup.merge_slivers_thinner);
        if (cleanup.remove_holes_smaller > 0.0)
            *this = WithoutSmallHoles(cleanup.remove_holes_smaller);
        if (cleanup.remove_faces_smaller > 0.0)
            *this = WithoutSmallFaces(cleanup.remove_faces_smaller, cleanup.merge_small_faces_instead);
        ids = std::move(kept);
        return *this;
    }

    // on all edges at once, so a vertex shared between parts moves with both
    Shape &Shape::Simplify(const ArcFit &arcs, const SplineFit &fit,
                           const Cleanup &cleanup) {
        edges = edges.Cleaned(arcs, fit, cleanup, SidesOf(ids));
        Compact();
        if (cleanup.IsDefeaturing()) {
            Defeature(cleanup);
            edges = edges.Cleaned(arcs, fit, cleanup, SidesOf(ids));
            Compact();
        }
        return *this;
    }

    Shape::ExportPlan Shape::Export(const ArcFit &arcs, const SplineFit &fit,
                                    const Cleanup &cleanup) const {
        if (cleanup.IsDefeaturing()) {
            Shape s = *this;
            s.edges = s.edges.Cleaned(arcs, fit, cleanup, SidesOf(s.ids));
            s.Compact();
            s.Defeature(cleanup);
            Cleanup rest = cleanup;
            rest.merge_slivers_thinner = rest.remove_holes_smaller = rest.remove_faces_smaller = 0.0;
            return s.Export(arcs, fit, rest);
        }
        ExportPlan plan{edges.Cleaned(arcs, fit, cleanup, SidesOf(ids)), {}, {}, {}, groupProps, groupProps.size() + wireCount, ids};
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

    std::optional<Properties> ShapeList::CommonFaceProps() const {
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

    void ShapeList::CheckParts(bool unite) const {
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

}
