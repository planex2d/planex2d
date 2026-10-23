// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "core/vertex.hpp"

namespace planex2d::sketch {

    // a cut of an element: its place along it (the fraction of a line or arc, a turn of a circle, the chord length of
    // a spline), its point, and the cutter it lies on (none: whoever called holds it)
    struct CutPoint {
        // cut points this close in the parameter are one, and this close to an end are that end
        static constexpr double same = 1e-9;
        double t;
        Vertex p;
        size_t by;
    };
    // the point elements of the cuts and the elements of the pieces, along the cut element
    struct CutMade {
        std::vector<size_t> cuts, pieces;
    };


    // along the element, cuts closer than same in the parameter one
    inline void SortCuts(std::vector<CutPoint> &cuts, double same) {
        std::sort(cuts.begin(), cuts.end(), [](const CutPoint &x, const CutPoint &y) { return x.t < y.t; });
        cuts.erase(std::unique(cuts.begin(), cuts.end(), [&](const CutPoint &x, const CutPoint &y) { return y.t - x.t <= same; }), cuts.end());
    }
    // the pieces between the cuts by their parameters: from the element's start (from) to its end (to); round (a circle,
    // a closed spline) from each cut to the next, the last one on to the first one a period later
    inline std::vector<std::pair<double, double>> PieceRanges(const std::vector<CutPoint> &cuts, bool round, double from, double to, double period) {
        std::vector<double> t;
        if (!round)
            t.push_back(from);
        for (const CutPoint &c : cuts)
            t.push_back(c.t);
        t.push_back(round ? cuts.front().t + period : to);
        std::vector<std::pair<double, double>> out;
        for (size_t i = 0; i + 1 < t.size(); ++i)
            out.push_back({t[i], t[i + 1]});
        return out;
    }
    // the piece nearest x by 65 samples along each (at: the point at a parameter); reverse: the next nearest
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
    // only the cuts that bound the piece removed: the rest stays one piece; removed becomes its place among the pieces left
    inline std::vector<CutPoint> BoundingCuts(const std::vector<CutPoint> &cuts, size_t &removed, bool round) {
        std::vector<CutPoint> out;
        if (round)
            out = removed + 1 < cuts.size() ? std::vector<CutPoint>{cuts[removed], cuts[removed + 1]} : std::vector<CutPoint>{cuts.front(), cuts.back()};
        else {
            if (removed > 0)
                out.push_back(cuts[removed - 1]);
            if (removed < cuts.size())
                out.push_back(cuts[removed]);
        }
        removed = round ? (removed + 1 == cuts.size() ? 1 : 0) : (removed > 0 ? 1 : 0);
        return out;
    }

}
