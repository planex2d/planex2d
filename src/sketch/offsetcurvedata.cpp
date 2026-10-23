// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "sketch/geometry.hpp"

namespace planex2d::sketch {

    inline std::array<double, 2> OffsetCurveData::Range(const Geometry &g, const Curve<double, Vertex> &f) const {
        const SplinePiece *source = piece ? &g.pieces[piece - 1] : nullptr;
        return {RangeEnd(g, range.from, source ? source->from : none, 0.0),
                RangeEnd(g, range.to, source ? source->to : none, f.params.back())};
    }
    inline Vertex OffsetCurveData::Point(const Geometry &g, const Curve<double, Vertex> &f, double t) const {
        const Vertex d1 = f.At(t, 1);
        return f.At(t, 0) + Vertex{-d1.y, d1.x} * (side * g.values[distance] / Norm(d1));
    }
    inline double OffsetCurveData::RangeEnd(const Geometry &g, size_t mine, size_t inherited, double otherwise) {
        return mine != none ? g.values[mine] : inherited != none ? g.values[inherited] : otherwise;
    }

}
