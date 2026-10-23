// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "core/properties.hpp"

namespace planex2d::sketch {

    // what a drawing call gives the elements it makes
    struct Options {
        std::optional<std::string> id;
        Properties props;
        bool construction = false;
    };
    // a span: a cut element kept as the carrier and ends of all its pieces, drawn by its pieces
    enum class Role : char { Drawn, Construction, Span };
    // what an id names: an element or a constraint
    struct IdTarget {
        bool isElement;
        size_t index;
    };

}
