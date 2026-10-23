// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <numeric>
#include <optional>
#include <vector>

#include "shape/measure.hpp"
#include "core/disjointsets.hpp"

namespace planex2d {

    // the number of other endpoints at every endpoint; a circle's start and end meet each other
    inline std::vector<size_t> EndDegrees(const Wire &w) {
        const std::vector<Vertex> pts = w.Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        std::vector<size_t> degree(pts.size(), 0);
        for (size_t k = 0; k < pts.size(); ++k) {
            const bool circle = std::holds_alternative<Circle>(w[k / 2]);
            grid.ForCell(pts[k], [&](size_t j) { degree[k] += j != k && (circle || j / 2 != k / 2) && pts[j] == pts[k]; });
        }
        return degree;
    }

    // a face cut by a cutter: the arrangement (owner 0 the face's edges, 1 the cutter's) and its pieces as half edges,
    // each with the face on their left
    struct FaceSplit {
        Wire edges;
        std::vector<HalfEdges> pieces;
    };

    // empty pieces if the cutter separates nothing: its parts outside the face and its loose ends go
    inline FaceSplit SplitFace(const Face &f, const Wire &cutter) {
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
    inline int SideOf(const FaceSplit &s, const HalfEdges &piece) {
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

    // the region of the chosen pieces: an edge between two of them goes
    inline Wire UnionOfPieces(const FaceSplit &s, const std::vector<size_t> &chosen) {
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

    // a wire cut where it meets the cutter: its pieces (with the owners of the wire's edges) and the connected runs of
    // them between the points on the cutter; no runs if nothing is cut
    struct WireSplit {
        Wire edges;
        std::vector<std::vector<size_t>> runs;

        Wire Run(size_t k) const {
            Wire w(edges.Axes());
            for (size_t i : runs[k])
                w.Add(edges[i]);
            return w;
        }
    };

    inline WireSplit SplitWire(const Wire &w, const Wire &cutter) {
        Wire c = w;
        std::vector<CutState> state(c.Size(), CutState{1});
        for (const Edge &e : cutter) {
            c.Append(e, 0);
            state.push_back({2});
        }
        Arrange(c, state);
        std::vector<bool> other(c.Size());
        for (size_t k = 0; k < c.Size(); ++k)
            other[k] = !(state[k].mask & 1);
        c.Erase(other);
        const double tol = std::max(Planar{Wire(w.Axes()), cutter}.Tolerance(), Planar{Wire(w.Axes()), c}.Tolerance());
        const std::vector<Vertex> pts = c.Endpoints();
        DisjointSets run(c.Size());
        Grid grid = Grid::Of(pts, 0.0);
        for (size_t k = 0; k < pts.size(); ++k)
            if (NearestTo(cutter, pts[k]).d > tol)
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

}
