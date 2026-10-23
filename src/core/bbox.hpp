// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <limits>

#include "core/vertex.hpp"

namespace planex2d {

    struct BBox {
        static constexpr double inf = std::numeric_limits<double>::infinity();
        double x0 = inf, y0 = inf, x1 = -inf, y1 = -inf;

        static BBox Of(const Vertex &a, const Vertex &b) {
            BBox box;
            box.Grow(a);
            box.Grow(b);
            return box;
        }
        void Grow(const Vertex &p) {
            x0 = p.x < x0 ? p.x : x0;
            y0 = p.y < y0 ? p.y : y0;
            x1 = p.x > x1 ? p.x : x1;
            y1 = p.y > y1 ? p.y : y1;
        }
        void Grow(const BBox &o) {
            x0 = o.x0 < x0 ? o.x0 : x0;
            y0 = o.y0 < y0 ? o.y0 : y0;
            x1 = o.x1 > x1 ? o.x1 : x1;
            y1 = o.y1 > y1 ? o.y1 : y1;
        }
        BBox Grown(double t) const { return {x0 - t, y0 - t, x1 + t, y1 + t}; }
        bool Overlaps(const BBox &o, double tol) const {
            return x0 - tol <= o.x1 && o.x0 - tol <= x1 && y0 - tol <= o.y1 && o.y0 - tol <= y1;
        }
        bool IsInside(const Vertex &p) const { return x0 <= p.x && p.x <= x1 && y0 <= p.y && p.y <= y1; }
        bool IsEmpty() const { return x0 > x1; }
        Vertex Size() const { return {x1 - x0, y1 - y0}; }
        Vertex Center() const { return {0.5 * (x0 + x1), 0.5 * (y0 + y1)}; }
        double NormInf() const {
            double a = planex2d::NormInf({x0, y0});
            double b = planex2d::NormInf({x1, y1});
            return a > b ? a : b;
        }
    };

}
