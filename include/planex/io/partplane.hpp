// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/core/config.hpp"

namespace planex2d {

    inline constexpr const char *readsCurvedShapes = "; netgen.occ reads the file (OCCGeometry(path).shape), px.Intersect3d(plane, shape) cuts a section from it, px.Project(shape, plane) projects it";

    // OCC's confusion, plus the rounding of the 12 digits other programs write
    inline double PlaneSlack(const AxisSystem &axes, const Vector3 &p) {
        return config::state->axes_distance + 1e-10 * Norm(p - axes.Origin());
    }

    inline bool IsSamePlane(const AxisSystem &a, const AxisSystem &axes) {
        return std::abs(std::abs(Dot(a.Normal(), axes.Normal())) - 1.0) <= 1e-10 &&
               std::abs(Dot(a.Origin() - axes.Origin(), axes.Normal())) <= PlaneSlack(axes, a.Origin());
    }

    inline std::optional<AxisSystem> PlaneThrough(const std::vector<Vector3> &pts) {
        for (size_t i = 1; i < pts.size(); ++i)
            for (size_t j = i + 1; j < pts.size(); ++j)
                if (const Vector3 n = Cross(pts[i] - pts[0], pts[j] - pts[0]); Norm(n) > 1e-9 * Dot(pts[i] - pts[0], pts[i] - pts[0]))
                    return AxisSystem(pts[0], n, pts[i] - pts[0]);
        return std::nullopt;
    }

}
