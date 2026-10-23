// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace planex2d::sketch {

    struct Copies {
        // (original, copy) point elements, and the other elements; per copied spline one (original, copy) element
        std::vector<std::pair<size_t, size_t>> points, elements, splines;
        // the positions along a spline its pieces end at: (original, copy) unknowns, and the elements on that spline
        struct Position {
            size_t original, copy, originalElement, copyElement;
        };
        std::vector<Position> positions;
        // point elements a copy places on its spline by a position (a cut point): no other tie for their copies
        std::set<size_t> placed;
    };

}
