// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "planex/core/vertex.hpp"

namespace planex2d::sketch {

    // t: the fraction of a line or arc, a turn of a circle, the chord length of a spline; by: the cutter it lies on (none:
    // whoever called holds it)
    struct CutPoint {
        // cut points this close in the parameter are one, and this close to an end are that end
        static constexpr double same = 1e-9;
        double t;
        Vertex p;
        size_t by;
    };
    struct CutMade {
        std::vector<size_t> cuts, pieces;
    };


    inline void SortCuts(std::vector<CutPoint> &cuts, double same) {
        std::sort(cuts.begin(), cuts.end(), [](const CutPoint &x, const CutPoint &y) { return x.t < y.t; });
        cuts.erase(std::unique(cuts.begin(), cuts.end(), [&](const CutPoint &x, const CutPoint &y) { return y.t - x.t <= same; }), cuts.end());
    }
    std::vector<std::pair<double, double>> PieceRanges(const std::vector<CutPoint> &cuts, bool round, double from, double to, double period);
    // 65 samples along each piece; reverse: the next nearest
    template <class At>
    size_t NearestPiece(const std::vector<std::pair<double, double>> &pieces, const Vertex &x, bool reverse, At at) {
        std::vector<std::pair<double, size_t>> rank;
        for (size_t i = 0; i < pieces.size(); ++i) {
            double d = std::numeric_limits<double>::infinity();
            for (int k = 0; k <= 64; ++k)
                d = std::min(d, Norm(at(pieces[i].first + (pieces[i].second - pieces[i].first) * k / 64.0) - x));
            rank.push_back({d, i});
        }
        std::sort(rank.begin(), rank.end());
        return rank[reverse && rank.size() > 1 ? 1 : 0].second;
    }
    std::vector<CutPoint> BoundingCuts(const std::vector<CutPoint> &cuts, size_t &removed, bool round);

}
