// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/curve.hpp"

namespace planex2d {

    template <class S>
    struct TangentSetting {
        S tension = S(1.0);
        std::optional<S> curvature;
    };

    void CheckSpline(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic, double tol,
                     const std::map<size_t, TangentSetting<double>> &settings = {});

    template <class S, class P>
    bool SolveDense(std::vector<std::vector<S>> a, std::vector<P> &b);

    inline double AbsOf(double v) { return std::abs(v); }

    template <class S, class P>
    Curve<S, P> Interpolate(const std::vector<P> &points, const std::vector<size_t> &tangentAt, const std::vector<P> &tangents, bool closed,
                            SplineStyle style, const std::vector<TangentSetting<S>> &settings);

    inline Curve<double, Vertex> Interpolate(const std::vector<Vertex> &points, const std::vector<size_t> &tangentAt,
                                             const std::vector<Vertex> &tangents, bool closed, SplineStyle style = SplineStyle::Averaged,
                                             const std::vector<TangentSetting<double>> &settings = {}) {
        return Interpolate<double, Vertex>(points, tangentAt, tangents, closed, style, settings);
    }

}
