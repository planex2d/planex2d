// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>

#include "sketch/ref.hpp"

namespace planex2d::sketch {

    // where an arm was cut: the point of the cut, the cut as a reference (the point element, or for an element cut off
    // whole its far end) and the sharp corner as a reference (the end of the piece cut off, or of that whole element)
    struct ArmCut {
        size_t point;
        Ref cut, corner;
    };
    // a corner cut: the new element, the two elements cut (the first one first) and where each was cut; the constraint
    // sketch holds it by constraints
    struct CornerParts {
        size_t made;
        std::array<size_t, 2> arms;
        std::array<ArmCut, 2> at;
    };

}
