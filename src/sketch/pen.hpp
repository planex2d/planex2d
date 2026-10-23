// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/vertex.hpp"

namespace planex2d::sketch {

    // what a construction reads of the pen: where it is and heads, and the tolerance there
    struct Pen {
        Vertex pos, dir;
        double tol;
    };

    // where the pen is and heads, where its chain started; the point elements there and the chain's last element; the
    // chain's lines, arcs and splines and its wire id (empty before its first element)
    struct PenState {
        Vertex pos{}, dir{1.0, 0.0}, start{};
        std::optional<size_t> point, first, last;
        std::vector<size_t> chain;
        std::string wire;
    };

}
