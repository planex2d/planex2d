// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/split.hpp"

#include <algorithm>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>

namespace planex2d {

    FaceSplit SplitFace(const Face &f, const Wire &cutter) {
        Wire c(f.Axes());
        std::vector<CutState> state;
        std::vector<uint32_t> owners;
        for (const Edge &e : f.Edges()) {
            c.Add(e);
            state.push_back({1});
            owners.push_back(0);
        }
        for (const Edge &e : cutter) {
            c.Add(e);
            state.push_back({0});
            owners.push_back(1);
        }
        c.SetOwners(std::move(owners), 1);
        Arrange(c, state);
        const Wire &region = f.Edges();
        const BBox box = region.Box();
        std::vector<bool> outside(c.Size(), false);
        for (size_t k = 0; k < c.Size(); ++k)
            if (state[k].mask == 0) {
                const Vertex m = MidPointOf(c[k]);
                outside[k] = !box.IsInside(m) || !region.Contains(m);
            }
        c.Erase(outside);
        Erase(state, outside);
        for (bool loose = true; loose;) {
            std::vector<size_t> degree = EndDegrees(c);
            std::vector<bool> drop(c.Size(), false);
            loose = false;
            for (size_t k = 0; k < c.Size(); ++k)
                if (state[k].mask == 0 && (degree[2 * k] == 0 || degree[2 * k + 1] == 0))
                    loose = drop[k] = true;
            c.Erase(drop);
            Erase(state, drop);
        }
        FaceSplit out{c, {}};
        if (std::none_of(state.begin(), state.end(), [](const CutState &s) { return s.mask == 0; }))
            return out;
        out.edges.MergeOnCarriers();
        for (HalfEdges &piece : ArrangementFaces(out.edges)) {
            std::optional<Vertex> p = EdgesOf(out.edges, piece).InteriorPoint();
            if (p && box.IsInside(*p) && region.Contains(*p))
                out.pieces.push_back(std::move(piece));
        }
        if (out.pieces.size() < 2)
            out.pieces.clear();
        return out;
    }

    // a piece lies on the left of the cutter if it runs along every cutter edge it has in the cutter's direction, on the
    // right if against it; a cutter edge it has on both sides (a stretch that separates nothing) does not count
    int SideOf(const FaceSplit &s, const HalfEdges &piece) {
        std::vector<std::pair<size_t, int>> along;
        for (auto [i, f] : piece)
            if (s.edges.Owner(i) == 1)
                along.push_back({i, f ? 1 : 2});
        std::sort(along.begin(), along.end());
        bool forward = false, backward = false;
        for (size_t k = 0; k < along.size();) {
            int ways = 0;
            const size_t i = along[k].first;
            for (; k < along.size() && along[k].first == i; ++k)
                ways |= along[k].second;
            forward = forward || ways == 1;
            backward = backward || ways == 2;
        }
        return forward == backward ? 0 : forward ? 1 : -1;
    }

    Wire UnionOfPieces(const FaceSplit &s, const std::vector<size_t> &chosen) {
        std::vector<int> count(s.edges.Size(), 0);
        for (size_t k : chosen)
            for (auto [i, f] : s.pieces[k])
                ++count[i];
        Wire w(s.edges.Axes());
        for (size_t i = 0; i < s.edges.Size(); ++i)
            if (count[i] == 1)
                w.Add(s.edges[i]);
        w.MergeOnCarriers();
        return w;
    }

    // the arrangement gives the wire's pieces and the cutter's the same vertex wherever they meet, so a point is on the
    // cutter exactly when a cutter piece ends there (a distance would miss a spline's cut points, which lie on its
    // approximation)
    WireSplit SplitWire(const Wire &w, const Wire &cutter) {
        Wire c = w;
        std::vector<CutState> state(c.Size(), CutState{1});
        for (const Edge &e : cutter) {
            c.Append(e, 0);
            state.push_back({2});
        }
        Arrange(c, state);
        std::vector<Vertex> onCutter;
        std::vector<bool> other(c.Size());
        for (size_t k = 0; k < c.Size(); ++k) {
            other[k] = !(state[k].mask & 1);
            if (state[k].mask & 2) {
                onCutter.push_back(StartOf(c[k]));
                onCutter.push_back(EndOf(c[k]));
            }
        }
        std::sort(onCutter.begin(), onCutter.end(), VertexLess);
        c.Erase(other);
        const std::vector<Vertex> pts = c.Endpoints();
        DisjointSets run(c.Size());
        Grid grid = Grid::Of(pts, 0.0);
        for (size_t k = 0; k < pts.size(); ++k)
            if (!std::binary_search(onCutter.begin(), onCutter.end(), pts[k], VertexLess))
                grid.ForCell(pts[k], [&](size_t j) {
                    if (pts[j] == pts[k])
                        run.Join(j / 2, k / 2);
                });
        WireSplit out{c, {}};
        std::vector<size_t> index(c.Size(), c.Size());
        for (size_t k = 0; k < c.Size(); ++k) {
            size_t r = run.Find(k);
            if (index[r] == c.Size()) {
                index[r] = out.runs.size();
                out.runs.emplace_back();
            }
            out.runs[index[r]].push_back(k);
        }
        if (out.runs.size() < 2)
            out.runs.clear();
        return out;
    }

    template <class D, class S>
    static std::vector<size_t> Chosen(size_t n, const TrimRule &r, D distance, S size) {
        std::vector<size_t> all(n);
        std::iota(all.begin(), all.end(), 0);
        auto best = [&](auto key) { return *std::min_element(all.begin(), all.end(), [&](size_t a, size_t b) { return key(a) < key(b); }); };
        switch (r.kind) {
            case TrimRule::KeepClosest: return {best(distance)};
            case TrimRule::RemoveClosest: {
                size_t k = best(distance);
                all.erase(all.begin() + (std::ptrdiff_t)k);
                return all;
            }
            case TrimRule::KeepLargest: return {best([&](size_t k) { return -size(k); })};
            case TrimRule::KeepSmallest: return {best(size)};
            default: return {};
        }
    }

    static std::optional<Wire> TrimmedFace(const Face &f, const Wire &cutter, const TrimRule &r) {
        FaceSplit s = SplitFace(f, cutter);
        if (s.pieces.empty())
            return std::nullopt;
        std::vector<MassProps> mass;
        for (size_t k = 0; k < s.pieces.size(); ++k)
            mass.push_back(MassOf(UnionOfPieces(s, {k}), Wire(f.Axes())));
        std::vector<size_t> chosen;
        if (r.kind == TrimRule::KeepLeft || r.kind == TrimRule::KeepRight) {
            for (size_t k = 0; k < s.pieces.size(); ++k)
                if (SideOf(s, s.pieces[k]) == (r.kind == TrimRule::KeepLeft ? 1 : -1))
                    chosen.push_back(k);
            if (chosen.empty())
                throw std::invalid_argument(std::string("Trim(...): no piece lies wholly on the ") + (r.kind == TrimRule::KeepLeft ? "left" : "right") +
                                      " of the cutter");
        } else {
            chosen = Chosen(s.pieces.size(), r, [&](size_t k) { return Norm(mass[k].center - r.point); },
                            [&](size_t k) { return mass[k].mass; });
        }
        return UnionOfPieces(s, chosen);
    }

    static std::optional<Wire> TrimmedWire(const Wire &w, const Wire &cutter, const TrimRule &r) {
        if (r.kind == TrimRule::KeepLeft || r.kind == TrimRule::KeepRight)
            throw std::invalid_argument("Trim(...): keep_left and keep_right apply to faces");
        WireSplit split = SplitWire(w, cutter);
        if (split.runs.empty())
            return std::nullopt;
        WireList runs;
        for (size_t k = 0; k < split.runs.size(); ++k)
            runs.push_back(split.Run(k));
        Wire out(w.Axes());
        for (size_t k : Chosen(runs.size(), r, [&](size_t k) { return NearestTo(runs[k], r.point).d; },
                               [&](size_t k) { return runs[k].Length(); }))
            for (const Edge &e : runs[k])
                out.Add(e);
        return out;
    }

    ShapeList SplitShape(const Shape &s, const Wire &cutter) {
        ShapeList out;
        for (const Face &f : s.Faces()) {
            FaceSplit split = SplitFace(f, cutter);
            if (split.pieces.empty())
                out.emplace_back(f);
            for (size_t k = 0; k < split.pieces.size(); ++k)
                out.emplace_back(Face(UnionOfPieces(split, {k}), f.Props()));
        }
        for (const Wire &w : s.WireParts()) {
            WireSplit split = SplitWire(w, cutter);
            if (split.runs.empty())
                out.push_back(Shape(s.Axes(), {}, {w}));
            for (size_t k = 0; k < split.runs.size(); ++k)
                out.push_back(Shape(s.Axes(), {}, {split.Run(k)}));
        }
        for (Shape &piece : out) {
            piece.Edges().AssignIds(true);
            piece.Edges().ClearVertexIds();
            piece.Ids() = s.Ids();
        }
        return out;
    }

    Shape TrimmedShape(const Shape &s, const Wire &cutter, const TrimRule &r) {
        FaceList faces;
        WireList wires;
        for (const Face &f : s.Faces()) {
            std::optional<Wire> t = TrimmedFace(f, cutter, r);
            faces.push_back(t ? Face(std::move(*t), f.Props()) : f);
        }
        const bool side = r.kind == TrimRule::KeepLeft || r.kind == TrimRule::KeepRight;
        if (side && !s.HasFaces())
            throw std::invalid_argument("Trim(...): keep_left and keep_right apply to faces");
        for (const Wire &w : s.WireParts()) {
            std::optional<Wire> t = side ? std::nullopt : TrimmedWire(w, cutter, r);
            wires.push_back(t ? std::move(*t) : w);
        }
        Shape trimmed(s.Axes(), faces, wires);
        trimmed.Ids() = s.Ids();
        trimmed.Edges().AssignIds(true);
        trimmed.Edges().ClearVertexIds();
        return trimmed;
    }

}
