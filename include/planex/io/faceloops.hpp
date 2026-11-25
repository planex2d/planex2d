// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "planex/wire/wire.hpp"

namespace planex2d {

    // a loop of a face of an export: the outer one counterclockwise, a hole clockwise, as edge indices into the export's
    // wire with whether the loop runs along the edge
    struct FaceLoop {
        bool outer;
        std::vector<std::pair<size_t, bool>> edges;
    };

    std::vector<FaceLoop> FaceLoops(const Wire &c, const std::vector<size_t> &faceEdges);

}
