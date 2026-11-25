// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "planex/core/grid.hpp"
#include "planex/wire/corner.hpp"
#include "planex/curves/edgelist.hpp"

namespace planex2d {

    enum class Continuity { Point, Tangent, Curvature };

    // leaving: the unit direction into the chain; the curvature signed along it, a left turn positive
    struct ChainEnd {
        Vertex at, leaving;
        double curvature;

        // the chain at o, the end of another chain at the same point, goes on as this one: the same curve passed
        // through leaves the point the opposite way with the opposite curvature; directions within 1e-9 rad as loop
        // building takes them, curvatures within 1e-9 of the larger
        bool IsContinuedBy(const ChainEnd &o, Continuity c) const {
            if (c == Continuity::Point)
                return true;
            if (Dot(leaving, o.leaving) >= 0.0 || std::abs(Cross(leaving, o.leaving)) > 1e-9)
                return false;
            return c == Continuity::Tangent ||
                   std::abs(curvature + o.curvature) <= 1e-9 * std::max(std::abs(curvature), std::abs(o.curvature));
        }
    };

    // a spline piece's direction and curvature at its parameter on the whole spline: its end vertex lies only within the
    // approximation's tolerance of the curve
    inline ChainEnd ChainEndOf(const Edge &e, bool start) {
        const double sign = start ? 1.0 : -1.0;
        if (const Spline *s = std::get_if<Spline>(&e)) {
            const double u = start ? s->StartParam() : s->EndParam();
            return {start ? s->Start() : s->End(), s->ExactDirectionAt(u) * sign, s->ExactCurvatureAt(u) * sign};
        }
        const ExactCurve c(e);
        const double u = start ? c.First() : c.Last();
        return {c.At(u), c.Direction(u) * sign, c.Curvature(u) * sign};
    }

    inline std::optional<std::array<ChainEnd, 2>> ChainEndsOf(const EdgeList &chain, double tol) {
        if (chain.empty() || chain.HasCircle())
            return std::nullopt;
        const ChainEnd start = ChainEndOf(chain.front(), true), end = ChainEndOf(chain.back(), false);
        if (start.at.IsClose(end.at, tol))
            return std::nullopt;
        return std::array{start, end};
    }

    std::vector<size_t> Propagated(const std::vector<EdgeList> &chains, const std::vector<size_t> &seeds,
                                   Continuity c);

}
