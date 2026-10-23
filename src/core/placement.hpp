// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <stdexcept>

#include "core/vertex.hpp"

namespace planex2d {

    inline Vertex MirrorDirection(const Vertex &d) {
        if (!(Norm(d) > 0.0))
            throw std::invalid_argument("mirror direction must not be zero");
        return Normalized(d);
    }
    inline void CheckScale(double s) {
        if (!(s > 0.0) || !std::isfinite(s))
            throw std::invalid_argument("scale factor must be positive and finite");
    }

}
