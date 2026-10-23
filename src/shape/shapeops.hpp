// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/axissystem.hpp"
#include "shape/shape.hpp"

namespace planex2d {

    // all edges cut against each other: crossings split, an overlapping stretch stays once; one wire per chain
    inline Shape WireUnion(const ShapeList &shapes) {
        const AxisSystem axes = shapes.CommonAxes();
        Wire c(axes);
        for (const Shape &s : shapes)
            for (const Edge &e : s.Edges())
                c.Add(e);
        std::vector<CutState> state(c.Size());
        Arrange(c, state);
        c.MergeOnCarriers();
        return Shape(axes, {}, c.Wires());
    }

    // the parts of a glue in list order: every face group (its region with its properties) and every wire
    struct GluePart {
        Wire edges;
        bool face;
        Properties props;
    };

    // the parts as one shape: everything cut against everything; every area covered by a different set of face parts
    // is a group of its own (A - B, A * B, B - A), in the order of the sets, with the properties of its parts merged in
    // list order, those of a whole part (one no other group holds a piece of) first. an edge belongs to the groups on
    // its two sides and the wires lying on it; a stretch of a wire through a group separates two faces of it (listed
    // twice), one that does not separate stays an edge of the wire alone (internal to the face in the export). where
    // parts share an edge, the properties of the whole parts' edges come first, then the others in list order; the
    // group on the side of the part whose edge comes first is the edge's first owner, its normal points out of it
    inline Shape Glue(const ShapeList &shapes) {
        const AxisSystem axes = shapes.CommonAxes();
        std::vector<GluePart> parts;
        for (const Shape &s : shapes) {
            WireList groups = s.Groups();
            for (size_t k = 0; k < groups.size(); ++k)
                parts.push_back({groups[k], true, s.GroupProps(k)});
            for (Wire &w : s.WireParts())
                parts.push_back({std::move(w), false, nullptr});
        }
        std::vector<uint32_t> wireIndex(parts.size());
        size_t wires = 0;
        for (size_t k = 0; k < parts.size(); ++k)
            if (!parts[k].face)
                wireIndex[k] = (uint32_t)(wires++);
        Wire input(axes);
        std::vector<uint32_t> source;
        for (size_t k = 0; k < parts.size(); ++k)
            for (const Edge &e : parts[k].edges) {
                input.Add(e);
                source.push_back((uint32_t)k);
            }
        Wire c = input;
        c.SetOwners(source, 0);
        std::vector<CutState> state(c.Size());
        for (size_t k = 0; k < c.Size(); ++k)
            state[k].part = source[k];
        Arrange(c, state);
        const double tol = input.Tol();
        // the face parts covering every face of the arrangement, in list order
        const std::vector<HalfEdges> faces = ArrangementFaces(c);
        std::vector<BBox> boxes;
        for (const GluePart &p : parts)
            boxes.push_back(p.edges.Box());
        std::vector<std::vector<uint32_t>> cover(faces.size() + 1);
        for (size_t f = 0; f < faces.size(); ++f)
            if (std::optional<Vertex> p = EdgesOf(c, faces[f]).InteriorPoint())
                for (size_t k = 0; k < parts.size(); ++k)
                    if (parts[k].face && boxes[k].IsInside(*p) && parts[k].edges.Contains(*p))
                        cover[f].push_back((uint32_t)k);
        // a part is whole if every face it covers has the same cover
        std::vector<size_t> firstFace(parts.size(), faces.size());
        std::vector<bool> whole(parts.size(), false);
        for (size_t f = 0; f < faces.size(); ++f)
            for (uint32_t k : cover[f]) {
                if (firstFace[k] == faces.size()) {
                    firstFace[k] = f;
                    whole[k] = true;
                } else if (cover[firstFace[k]] != cover[f]) {
                    whole[k] = false;
                }
            }
        std::map<std::vector<uint32_t>, uint32_t> groupOf;
        for (const std::vector<uint32_t> &set : cover)
            if (!set.empty())
                groupOf.emplace(set, 0);
        std::vector<Properties> groupProps;
        for (auto &[set, g] : groupOf) {
            g = (uint32_t)groupProps.size();
            Properties props;
            const bool hasWhole = std::any_of(set.begin(), set.end(), [&](uint32_t k) { return whole[k]; });
            for (uint32_t k : set)
                if (whole[k] == hasWhole)
                    props = MergedProperties(props, parts[k].props);
            groupProps.push_back(props);
        }
        const auto none = (uint32_t)groupProps.size();
        std::vector<uint32_t> group(faces.size() + 1, none);
        for (size_t f = 0; f < faces.size(); ++f)
            if (!cover[f].empty())
                group[f] = groupOf[cover[f]];
        std::vector<size_t> faceOf(2 * c.Size(), faces.size());
        for (size_t f = 0; f < faces.size(); ++f)
            for (auto [i, forward] : faces[f])
                faceOf[2 * i + (forward ? 0 : 1)] = f;
        // the input edges through every piece's midpoint
        Grid grid = input.EdgeGrid();
        std::vector<uint32_t> near;
        std::vector<std::vector<uint32_t>> owners;
        std::vector<bool> drop(c.Size(), false);
        for (size_t i = 0; i < c.Size(); ++i) {
            const Vertex m = MidPointOf(c[i]);
            std::vector<size_t> on;
            near.clear();
            grid.InBox(BBox::Of(m, m).Grown(tol), near);
            for (uint32_t j : near)
                if (Distance(m, input[j]) <= tol)
                    on.push_back(j);
            std::vector<uint32_t> lying{c.Owner(i)};
            for (size_t j : on)
                lying.push_back(source[j]);
            std::vector<uint32_t> set;
            for (uint32_t k : lying)
                if (!parts[k].face)
                    set.push_back(none + wireIndex[k]);
            const size_t fl = faceOf[2 * i], fr = faceOf[2 * i + 1];
            const uint32_t left = group[fl], right = group[fr];
            if (left != right) {
                for (uint32_t g : {left, right})
                    if (g != none)
                        set.push_back(g);
            } else if (left != none && fl != fr && !set.empty()) {
                set.push_back(left);
                set.push_back(left);
            }
            std::sort(set.begin(), set.end());
            set.erase(std::unique(set.begin(), set.end(), [&](uint32_t x, uint32_t y) { return x == y && x >= none; }), set.end());
            if (set.empty()) {
                drop[i] = true;
                continue;
            }
            // from the input edges on it of the parts it belongs to: whole parts first, then list order
            auto covers = [&](size_t f, size_t k) { return std::find(cover[f].begin(), cover[f].end(), k) != cover[f].end(); };
            auto belongs = [&](size_t k) {
                return parts[k].face ? covers(fl, k) || covers(fr, k) : std::find(lying.begin(), lying.end(), k) != lying.end();
            };
            std::vector<size_t> from;
            for (size_t j : on)
                if (belongs(source[j]))
                    from.push_back(j);
            if (!from.empty()) {
                std::stable_sort(from.begin(), from.end(), [&](size_t x, size_t y) {
                    return whole[source[x]] != whole[source[y]] ? whole[source[x]] : source[x] < source[y];
                });
                Properties props;
                for (size_t j : from)
                    props = MergedProperties(props, PropsOf(input[j]));
                // the cut remapped the partition onto this piece
                const Properties &piece = PropsOf(c[i]);
                props = ChangedProperties(props, [&](ShapeProperties &q) { q.partition = piece ? piece->partition : std::nullopt; });
                SetProps(c[i], props);
                // the group on the side of the part that names the edge comes first
                const uint32_t namer = source[from.front()];
                if (parts[namer].face && covers(fl, namer) != covers(fr, namer)) {
                    auto at = std::find(set.begin(), set.end(), covers(fl, namer) ? left : right);
                    std::rotate(set.begin(), at, at + 1);
                }
            }
            owners.push_back(std::move(set));
        }
        c.Erase(drop);
        Shape glued(std::move(c), owners, std::move(groupProps), wires);
        return glued.JoinOnCarriers().Compacted();
    }

    // the wire parts of w cut where they meet region (a clean region), the pieces inside it (on its boundary counts as
    // inside) or outside, one wire part per connected chain
    inline Shape WiresIn(const Shape &w, const Wire &region, bool inside) {
        WireList chains;
        for (const Wire &part : w.WireParts()) {
            Wire c = part;
            std::vector<CutState> state(c.Size(), CutState{1});
            for (const Edge &e : region) {
                c.Append(e, 0);
                state.push_back({2});
            }
            Arrange(c, state);
            const double tol = c.Tol();
            Wire kept(w.Axes());
            for (size_t k = 0; k < c.Size(); ++k) {
                if (!(state[k].mask & 1))
                    continue;
                const Vertex m = MidPointOf(c[k]);
                bool on = (state[k].mask & 2) != 0;
                for (const Edge &e : region)
                    on = on || Distance(m, e) <= tol;
                if ((on || region.Contains(m)) == inside)
                    kept.Add(c[k]);
            }
            for (Wire &chain : kept.Wires())
                chains.push_back(std::move(chain));
        }
        return Shape(w.Axes(), {}, chains);
    }
    // faces whose identical edges (within the rounding tolerance) are stored once, owned by every face that has them
    inline Shape SharingFaces(const AxisSystem &axes, const FaceList &faces, const WireList &wires = {}) {
        Wire all(axes);
        std::vector<uint32_t> of;
        std::vector<Properties> props;
        for (size_t k = 0; k < faces.size(); ++k) {
            props.push_back(faces[k].Props());
            for (const Edge &e : faces[k].Edges()) {
                all.Add(e);
                of.push_back((uint32_t)k);
            }
        }
        for (size_t k = 0; k < wires.size(); ++k)
            for (const Edge &e : wires[k]) {
                all.Add(e);
                of.push_back((uint32_t)(faces.size() + k));
            }
        const double tol = all.Tol();
        auto key = [](const Edge &e) {
            return std::visit([](const auto &x) { return std::array<Vertex, 3>{x.Start(), x.End(), x.MidPoint()}; }, e);
        };
        std::vector<std::array<Vertex, 3>> keys;
        std::vector<Vertex> mids;
        for (const Edge &e : all) {
            keys.push_back(key(e));
            mids.push_back(keys.back()[2]);
        }
        Grid grid = Grid::Of(mids, tol);
        Wire kept(axes);
        std::vector<std::vector<uint32_t>> owners;
        std::vector<size_t> into(all.Size(), all.Size());
        for (size_t i = 0; i < all.Size(); ++i) {
            size_t same = all.Size();
            grid.ForNear(mids[i], [&](size_t j) {
                if (same == all.Size() && j < i && into[j] < all.Size() && keys[j][2].IsClose(keys[i][2], tol) &&
                    ((keys[j][0].IsClose(keys[i][0], tol) && keys[j][1].IsClose(keys[i][1], tol)) ||
                     (keys[j][0].IsClose(keys[i][1], tol) && keys[j][1].IsClose(keys[i][0], tol))))
                    same = j;
            });
            if (same < all.Size()) {
                into[i] = into[same];
                owners[into[i]].push_back(of[i]);
                // the later face's edge properties come first
                SetProps(kept[into[i]], MergedProperties(PropsOf(all[i]), PropsOf(kept[into[i]])));
                continue;
            }
            into[i] = kept.Size();
            kept.Add(all[i]);
            owners.push_back({of[i]});
        }
        // the later face names a shared edge: its group first
        for (std::vector<uint32_t> &o : owners) {
            std::sort(o.begin(), o.end());
            auto groups = std::lower_bound(o.begin(), o.end(), (uint32_t)faces.size());
            if (groups != o.begin())
                std::rotate(o.begin(), groups - 1, groups);
        }
        return Shape(std::move(kept), owners, std::move(props), wires.size());
    }

    // the regions as one group if the faces of the operands have the same properties, else every region gets them as
    // assign decides
    template <class F>
    Shape Result(const AxisSystem &axes, Wire region, const ShapeList &operands, F assign) {
        region.SetAxes(axes);
        if (std::optional<Properties> props = operands.CommonFaceProps())
            return Shape(std::move(region), *props);
        FaceList regions = region.Faces();
        std::invoke(assign, regions, operands.FaceLists());
        return Shape(axes, regions);
    }

    // as a balanced tree of pairwise unions; a region takes the merged properties of the faces it holds, in list order
    inline Shape Unify(const ShapeList &shapes, bool multithreaded);

    // the length of the boundary two faces share: the edges of a congruent to one of b's
    inline double SharedLength(const Face &a, const Face &b) {
        double sum = 0.0;
        for (const Edge &e : a.Edges()) {
            const auto [p, q, m, l] = std::visit([](const auto &x) { return std::tuple{x.Start(), x.End(), x.MidPoint(), x.Length()}; }, e);
            const double tol = Tol(std::max({NormInf(p), NormInf(q), 1.0}));
            for (const Edge &f : b.Edges()) {
                const auto [r, t, n] = std::visit([](const auto &x) { return std::tuple{x.Start(), x.End(), x.MidPoint()}; }, f);
                if (m.IsClose(n, tol) && ((p.IsClose(r, tol) && q.IsClose(t, tol)) || (p.IsClose(t, tol) && q.IsClose(r, tol)))) {
                    sum += l;
                    break;
                }
            }
        }
        return sum;
    }
    inline Shape Shape::WithoutSmallFaces(double area, bool merge) const {
        FaceList kept, small;
        for (const Face &f : Faces())
            (std::abs(f.Area()) < area ? small : kept).push_back(f);
        if (small.empty())
            return *this;
        for (const Face &f : small) {
            if (!merge)
                break;
            size_t best = kept.size();
            double longest = 0.0;
            for (size_t k = 0; k < kept.size(); ++k)
                if (const double shared = SharedLength(f, kept[k]); shared > longest) {
                    longest = shared;
                    best = k;
                }
            if (best == kept.size())
                continue;
            const FaceList united = Unify({Shape(kept[best]), Shape(f)}, false).Faces();
            if (united.size() == 1) {
                Face merged = united.front();
                merged.SetProps(kept[best].Props());
                kept[best] = merged;
            }
        }
        // the faces keep the edges they share
        const Wire wires = WireEdges();
        return SharingFaces(Axes(), kept, wires.Empty() ? WireList{} : WireList{wires});
    }

    inline Shape Unify(const ShapeList &shapes, bool multithreaded = false) {
        shapes.CheckParts(true);
        const AxisSystem axes = shapes.CommonAxes();
        for (const Shape &s : shapes)
            if (s.WireCount() > 0)
                return WireUnion(shapes);
        return Result(axes, BalancedBoolean(shapes.Outlines(), BooleanOp::Union, multithreaded), shapes, &FaceList::MergeCovered);
    }

    // every face of the first shape on its own against the rest, the faces sharing their common edges again; the
    // properties as in Result
    template <class F>
    Shape FaceByFace(const AxisSystem &axes, const ShapeList &shapes, const Wire &rest, BooleanOp op, F assign) {
        FaceList out;
        const std::vector<FaceList> others = shapes.Rest().FaceLists();
        for (const Face &f : shapes.front().Faces()) {
            FaceList regions = Boolean(f.Edges(), rest, op).Faces();
            std::vector<FaceList> operands{{f}};
            operands.insert(operands.end(), others.begin(), others.end());
            std::invoke(assign, regions, operands);
            out.insert(out.end(), regions.begin(), regions.end());
        }
        return SharingFaces(axes, out);
    }

    inline Shape Intersect(const ShapeList &shapes, bool multithreaded = false) {
        // one shape of wires with faces: its pieces in all the faces
        if (std::count_if(shapes.begin(), shapes.end(), std::mem_fn(&Shape::HasOnlyWires)) == 1 && shapes.size() > 1 &&
            std::all_of(shapes.begin(), shapes.end(), [](const Shape &s) { return s.HasOnlyWires() || s.WireCount() == 0; })) {
            const auto w = std::find_if(shapes.begin(), shapes.end(), std::mem_fn(&Shape::HasOnlyWires));
            WireList faces;
            for (auto s = shapes.begin(); s != shapes.end(); ++s)
                if (s != w)
                    faces.push_back(s->FaceEdges());
            shapes.CommonAxes();
            return WiresIn(*w, faces.size() == 1 ? faces.front() : BalancedBoolean(std::move(faces), BooleanOp::Intersection, multithreaded), true);
        }
        shapes.CheckParts(false);
        const AxisSystem axes = shapes.CommonAxes();
        if (!shapes.empty() && shapes.front().HasSharedFaces())
            return FaceByFace(axes, shapes, BalancedBoolean(shapes.Rest().Outlines(), BooleanOp::Intersection, multithreaded),
                              BooleanOp::Intersection, &FaceList::InheritFirstOperandProps);
        return Result(axes, BalancedBoolean(shapes.Outlines(), BooleanOp::Intersection, multithreaded), shapes,
                      &FaceList::InheritFirstOperandProps);
    }

    // the first minus the union of all others; the regions keep the properties of the first shape's faces
    inline Shape Subtract(const ShapeList &shapes, bool multithreaded = false) {
        // a shape of wires minus faces: its pieces outside them
        if (shapes.size() > 1 && shapes.front().HasOnlyWires() &&
            std::all_of(shapes.begin() + 1, shapes.end(), [](const Shape &s) { return s.WireCount() == 0; })) {
            shapes.CommonAxes();
            return WiresIn(shapes.front(), BalancedBoolean(shapes.Rest().Outlines(), BooleanOp::Union, multithreaded), false);
        }
        shapes.CheckParts(false);
        const AxisSystem axes = shapes.CommonAxes();
        if (shapes.empty())
            return Shape();
        WireList rest = shapes.Rest().Outlines();
        if (shapes.front().HasSharedFaces())
            return FaceByFace(axes, shapes, BalancedBoolean(std::move(rest), BooleanOp::Union, multithreaded), BooleanOp::Difference,
                              [](FaceList &regions, const std::vector<FaceList> &operands) {
                                  for (Face &r : regions)
                                      r.SetProps(operands.front().front().Props());
                              });
        Wire region = rest.empty() ? shapes.front().FaceEdges()
                                   : Boolean(shapes.front().FaceEdges(), BalancedBoolean(std::move(rest), BooleanOp::Union, multithreaded),
                                             BooleanOp::Difference);
        return Result(axes, std::move(region), {shapes.front()}, &FaceList::InheritProps);
    }

    inline Shape Boolean(const Shape &a, const Shape &b, BooleanOp op) {
        switch (op) {
            case BooleanOp::Union: return Unify({a, b});
            case BooleanOp::Intersection: return Intersect({a, b});
            case BooleanOp::Difference: return Subtract({a, b});
        }
        return Shape();
    }

}
