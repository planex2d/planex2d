// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/cut.hpp"

namespace planex2d::sketch {

    // the pieces between the cuts by their parameters: from the element's start (from) to its end (to); round (a circle,
    // a closed spline) from each cut to the next, the last one on to the first one a period later
    std::vector<std::pair<double, double>> PieceRanges(const std::vector<CutPoint> &cuts, bool round, double from, double to, double period) {
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

    // only the cuts that bound the piece removed: the rest stays one piece; removed becomes its place among the pieces left
    std::vector<CutPoint> BoundingCuts(const std::vector<CutPoint> &cuts, size_t &removed, bool round) {
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
