// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>

#include "planex/sketch/ref.hpp"

namespace planex2d::sketch {

    struct ArmCut {
        size_t point;
        Ref cut, corner;
    };
    struct CornerParts {
        size_t made;
        std::array<size_t, 2> arms;
        std::array<ArmCut, 2> at;
    };

}
