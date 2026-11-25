// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <utility>
#include <vector>

#include "planex/wire/wire.hpp"
#include "planex/wire/crossing.hpp"
#include "planex/wire/loops.hpp"
#include "planex/core/timer.hpp"

namespace planex2d {

    template <bool exact>
    double Wire::LoopArea(const std::vector<std::pair<size_t, bool>> &loop) const {
        if (loop.empty())
            return 0.0;
        const Vertex o = StartOf(edges[loop.front().first]);
        double a = 0.0;
        for (auto [i, forward] : loop) {
            if constexpr (exact)
                a += (forward ? 1.0 : -1.0) * ExactSignedArea(edges[i], o);
            else
                a += (forward ? 1.0 : -1.0) * SignedArea(edges[i], o);
        }
        return a;
    }

}
