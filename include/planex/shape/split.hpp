// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <vector>

#include "planex/shape/measure.hpp"
#include "planex/core/disjointsets.hpp"

namespace planex2d {

    inline std::vector<size_t> EndDegrees(const Wire &w) {
        const std::vector<Vertex> pts = w.Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        std::vector<size_t> degree(pts.size(), 0);
        for (size_t k = 0; k < pts.size(); ++k) {
            const bool closed = IsClosedEdge(w[k / 2]);
            grid.ForCell(pts[k], [&](size_t j) { degree[k] += j != k && (closed || j / 2 != k / 2) && pts[j] == pts[k]; });
        }
        return degree;
    }

    // owner 0: the face's edges, 1: the cutter's; the pieces as half edges with the face on their left
    struct FaceSplit {
        Wire edges;
        std::vector<HalfEdges> pieces;
    };

    FaceSplit SplitFace(const Face &f, const Wire &cutter);

    int SideOf(const FaceSplit &s, const HalfEdges &piece);

    Wire UnionOfPieces(const FaceSplit &s, const std::vector<size_t> &chosen);

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

    WireSplit SplitWire(const Wire &w, const Wire &cutter);

    struct TrimRule {
        enum Kind { KeepClosest, RemoveClosest, KeepLargest, KeepSmallest, KeepLeft, KeepRight } kind;
        Vertex point;
    };

    ShapeList SplitShape(const Shape &s, const Wire &cutter);
    Shape TrimmedShape(const Shape &s, const Wire &cutter, const TrimRule &r);

}
