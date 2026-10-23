// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>

#include "sketch/geometry.hpp"

namespace planex2d::sketch {

    inline Curve<double, Vertex> SplineData::Interpolated(const Geometry &g) const {
        std::vector<Vertex> q, t;
        for (size_t p : points)
            q.push_back(g.Coords(p));
        for (size_t a : tangent)
            t.push_back({std::cos(g.values[a]), std::sin(g.values[a])});
        return Interpolate(q, tangentAt, t, closed, style);
    }
    inline std::vector<Vertex> SplineData::Points(const Geometry &g) const {
        std::vector<Vertex> out;
        for (size_t p : points)
            out.push_back(g.Coords(p));
        return out;
    }

}
