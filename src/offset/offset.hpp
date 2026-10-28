// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

#include "boolean/boolean.hpp"
#include "offset/offsetpath.hpp"

namespace planex2d {

    // the band within d of one edge, its ends round
    template <class E>
    Wire EdgeOffset(const E &e, double d) {
        Wire w;
        w.Add(e);
        return w.Offset(d);
    }
    inline Wire Line::Offset(double d) const { return EdgeOffset(*this, d); }
    inline Wire Arc::Offset(double d) const { return EdgeOffset(*this, d); }
    // the band within d of the circle: the concentric circle of radius r + d, and r - d inside it unless d reaches r
    inline Wire Circle::Offset(double d) const { return EdgeOffset(*this, d); }
    inline Wire Spline::Offset(double d) const { return EdgeOffset(*this, d); }

    // the band of width 2 d around the wire's edges: per chain of it (Loops) its raw band, the edges' offsets joined
    // around the corners and capped at the open ends, all of them cleaned at once by their winding
    inline Wire Wire::Offset(double d, OffsetEnd ends, OffsetCorner corners) const {
        if (!(d > 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be positive and finite");
        const double tol = planex2d::Tol(Box().NormInf() + d);
        Wire raw(axes);
        for (const Loop &loop : Loops()) {
            EdgeList chain;
            for (auto [i, along] : loop.edges)
                chain.push_back(along ? edges[i] : ReversedOf(edges[i]));
            for (EdgeList &part : RawBand(std::move(chain), loop.closed, d, ends, corners, false, tol))
                for (Edge &e : part)
                    raw.Add(std::move(e));
        }
        Wire result = PositiveRegion(raw);
        result.SetAxes(axes);
        return result;
    }

}
