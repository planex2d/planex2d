// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>

#include "curves/blend.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::sketch {

    inline Curve<double, Vertex> SplineData::Interpolated(const Geometry &g) const {
        if (fitted)
            return fitted->PlacedAt(g.Coords(fitted->origin), g.values[fitted->angle]);
        if (blend) {
            std::array<Vertex, 2> t;
            std::array<double, 2> k;
            for (size_t i = 0; i < 2; ++i) {
                const Element &arm = g.elements[blend->arms[i]];
                const double sense = BlendSense(g, i);
                t[i] = Normalized(arm.DirectionAt(g, points[i])) * sense;
                k[i] = arm.CurvatureAt(g, points[i]) * sense;
            }
            const Vertex p0 = g.Coords(points[0]), p1 = g.Coords(points[1]);
            return blend->curvature ? G2Blend<double, Vertex>(p0, t[0], k[0], p1, t[1], k[1]) : G1Blend<double, Vertex>(p0, t[0], p1, t[1]);
        }
        std::vector<Vertex> q, t;
        for (size_t p : points)
            q.push_back(g.Coords(p));
        for (size_t a : tangent)
            t.push_back({std::cos(g.values[a]), std::sin(g.values[a])});
        return Interpolate(q, tangentAt, t, closed, style);
    }
    inline double SplineData::BlendSense(const Geometry &g, size_t i) const {
        const Element &arm = g.elements[blend->arms[i]];
        const bool atEnd = g.coordsOf[arm.Ends(g)[1]] == g.coordsOf[points[i]];
        const double travel = arm.kind == ElementKind::Arc && !arm.ccw ? -1.0 : 1.0;
        return atEnd == (i == 0) ? travel : -travel;
    }
    inline std::vector<Vertex> SplineData::Points(const Geometry &g) const {
        std::vector<Vertex> out;
        if (fitted) {
            const Curve<double, Vertex> f = Interpolated(g);
            out.push_back(g.Coords(points.front()));
            for (size_t k = 1; k + 1 < f.params.size(); ++k)
                out.push_back(f.At(f.params[k]));
            if (!closed)
                out.push_back(g.Coords(points.back()));
            return out;
        }
        for (size_t p : points)
            out.push_back(g.Coords(p));
        return out;
    }

}
