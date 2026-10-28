// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/vertex.hpp"
#include "wire/wire.hpp"

namespace planex2d {

    // a point of a projection: an edge seen end-on, or a vertex on its own
    struct ProjectedPoint {
        Vertex p;
        std::string name;
        std::optional<std::string> id;
    };
    struct Projection {
        Wire wire;
        std::vector<ProjectedPoint> points;
    };

}
