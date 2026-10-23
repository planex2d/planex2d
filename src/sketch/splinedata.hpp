// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <vector>

#include "core/vertex.hpp"
#include "curves/interpolate.hpp"

namespace planex2d::sketch {

    inline constexpr size_t none = (size_t)-1;

    class Geometry;

    struct SplinePiece {
        size_t from = none, to = none, fromPoint = none, toPoint = none;
    };

    struct SplineData {
        std::vector<size_t> points, tangentAt, tangent;
        bool closed = false;
        SplineStyle style = SplineStyle::Px;

        Curve<double, Vertex> Interpolated(const Geometry &g) const;
        std::vector<Vertex> Points(const Geometry &g) const;
    };

}
