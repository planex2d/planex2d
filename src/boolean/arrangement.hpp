// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <deque>
#include <limits>
#include <numeric>
#include <vector>

#include "wire/wire.hpp"
#include "core/disjointsets.hpp"

namespace planex2d {

    using HalfEdges = std::vector<std::pair<size_t, bool>>;

    inline Wire EdgesOf(const Wire &w, const HalfEdges &half) {
        Wire out(w.Axes());
        for (auto [i, forward] : half)
            out.Add(w[i]);
        return out;
    }

    // the bounded faces of an arrangement whose edges meet only at shared endpoints and never end loose: a counterclockwise
    // cycle each, with the clockwise cycles of the parts inside it as holes
    inline std::vector<HalfEdges> ArrangementFaces(const Wire &w) {
        std::vector<Loop> cycles = w.FaceCycles();
        const size_t n = cycles.size();
        // cycles sharing an edge belong to one connected part
        DisjointSets part(n);
        std::vector<size_t> cycleOf(2 * w.Size());
        for (size_t c = 0; c < n; ++c)
            for (auto [i, forward] : cycles[c].edges)
                cycleOf[2 * i + (forward ? 0 : 1)] = c;
        for (size_t i = 0; i < w.Size(); ++i)
            part.Join(cycleOf[2 * i], cycleOf[2 * i + 1]);
        std::vector<double> area(n);
        WireList region(n);
        std::vector<BBox> box(n);
        for (size_t c = 0; c < n; ++c) {
            area[c] = w.LoopArea(cycles[c].edges);
            region[c] = EdgesOf(w, cycles[c].edges);
            box[c] = region[c].Box();
        }
        std::vector<HalfEdges> faces;
        std::vector<size_t> faceOf(n, n);
        for (size_t c = 0; c < n; ++c)
            if (area[c] > 0.0) {
                faceOf[c] = faces.size();
                faces.push_back(cycles[c].edges);
            }
        // a clockwise cycle is a hole of the smallest face of another part around it, or bounds the outside
        for (size_t c = 0; c < n; ++c) {
            if (area[c] > 0.0)
                continue;
            const Vertex p = StartOf(w[cycles[c].edges.front().first]);
            size_t best = n;
            for (size_t o = 0; o < n; ++o)
                if (area[o] > 0.0 && part.Find(o) != part.Find(c) && box[o].IsInside(p) && region[o].Contains(p) &&
                    (best == n || area[o] < area[best]))
                    best = o;
            if (best < n)
                for (const auto &h : cycles[c].edges)
                    faces[faceOf[best]].push_back(h);
        }
        return faces;
    }

    // per face of the arrangement (and the outside, last): the fewest edges with cut[i] = false crossed from outside
    inline std::vector<size_t> FaceDepths(const Wire &w, const std::vector<HalfEdges> &faces, std::vector<size_t> &faceOf,
                                          const std::vector<bool> &cut) {
        const size_t outside = faces.size();
        faceOf.assign(2 * w.Size(), outside);
        for (size_t k = 0; k < faces.size(); ++k)
            for (auto [i, forward] : faces[k])
                faceOf[2 * i + (forward ? 0 : 1)] = k;
        std::vector<std::vector<std::pair<size_t, size_t>>> next(faces.size() + 1);
        for (size_t i = 0; i < w.Size(); ++i)
            if (size_t f = faceOf[2 * i], g = faceOf[2 * i + 1]; f != g) {
                next[f].emplace_back(g, cut[i] ? 0 : 1);
                next[g].emplace_back(f, cut[i] ? 0 : 1);
            }
        // 0-1 breadth first
        std::vector<size_t> depth(faces.size() + 1, std::numeric_limits<size_t>::max());
        std::deque<size_t> queue{outside};
        depth[outside] = 0;
        while (!queue.empty()) {
            size_t f = queue.front();
            queue.pop_front();
            for (auto [g, cost] : next[f])
                if (depth[f] + cost < depth[g]) {
                    depth[g] = depth[f] + cost;
                    cost == 0 ? queue.push_front(g) : queue.push_back(g);
                }
        }
        return depth;
    }

    // the faces of a region bounded by the edges cut[i] = false, cut by those with cut[i] = true: the faces reached from
    // outside across an odd number of bounding edges, as edge indices
    inline std::vector<std::vector<size_t>> RegionFaces(const Wire &w, const std::vector<bool> &cut) {
        const std::vector<HalfEdges> faces = ArrangementFaces(w);
        std::vector<size_t> faceOf;
        const std::vector<size_t> depth = FaceDepths(w, faces, faceOf, cut);
        std::vector<std::vector<size_t>> out;
        for (size_t k = 0; k < faces.size(); ++k) {
            if (depth[k] == std::numeric_limits<size_t>::max() || depth[k] % 2 == 0)
                continue;
            std::vector<size_t> edges;
            for (auto [i, forward] : faces[k])
                edges.push_back(i);
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
            out.push_back(std::move(edges));
        }
        return out;
    }

    // the edges between a face reached from outside across an odd and one reached across an even number of edges: a
    // stretch inside the region, a line across it or a loose end falls away. dropped: the edges that fall away
    inline Wire Boundary(const Wire &w, std::vector<size_t> *dropped = nullptr) {
        const std::vector<HalfEdges> faces = ArrangementFaces(w);
        std::vector<size_t> faceOf;
        const std::vector<size_t> depth = FaceDepths(w, faces, faceOf, std::vector<bool>(w.Size(), false));
        auto odd = [&](size_t f) { return depth[f] != std::numeric_limits<size_t>::max() && depth[f] % 2 == 1; };
        Wire out(w.Axes());
        for (size_t i = 0; i < w.Size(); ++i)
            if (odd(faceOf[2 * i]) != odd(faceOf[2 * i + 1]))
                out.Add(w[i]);
            else if (dropped)
                dropped->push_back(i);
        return out;
    }

}
