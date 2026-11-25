// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "planex/core/properties.hpp"

namespace planex2d::sketch {

    struct Options {
        std::optional<std::string> id;
        Properties props;
        bool construction = false;
    };
    // a span: a cut element kept as the carrier and ends of all its pieces, drawn by its pieces; a helper: an element
    // only the solver sees (the arc of a curvature fillet)
    enum class Role : char { Drawn, Construction, Span, Helper };
    struct PatternDir2 {
        std::string ref;
        size_t count = 1;
        double step = 0.0;
        bool reverse = false;
    };

    struct IdTarget {
        bool isElement;
        size_t index;
    };

}
