// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>

#include "core/vertex.hpp"
#include "curves/interpolate.hpp"
#include "sketch/splinedata.hpp"

namespace planex2d::sketch {

    class Geometry;

    struct OffsetCurveData {
        size_t spline = 0, piece = 0, distance = 0;
        double side = 1.0;
        SplinePiece range;
        bool reversed = false;

        std::array<double, 2> Range(const Geometry &g, const Curve<double, Vertex> &f) const;
        Vertex Point(const Geometry &g, const Curve<double, Vertex> &f, double t) const;

    private:
        static double RangeEnd(const Geometry &g, size_t mine, size_t inherited, double otherwise);
    };

}
