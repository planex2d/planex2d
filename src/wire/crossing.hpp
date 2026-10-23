// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <utility>
#include <variant>

#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/circle.hpp"
#include "curves/spline.hpp"

namespace planex2d {

    template <bool vertical>
    double Along(const Vertex &v) {
        if constexpr (vertical)
            return v.y;
        else
            return v.x;
    }

    template <bool vertical>
    double Across(const Vertex &v) {
        if constexpr (vertical)
            return v.x;
        else
            return v.y;
    }

    template <bool vertical>
    Vertex RayVertex(double along, double across) {
        if constexpr (vertical)
            return {across, along};
        else
            return {along, across};
    }

    template <bool vertical>
    bool RayMisses(const BBox &b, const Vertex &p) {
        Vertex lo{b.x0, b.y0};
        Vertex hi{b.x1, b.y1};
        return Across<vertical>(lo) > Across<vertical>(p) || Across<vertical>(hi) < Across<vertical>(p) ||
               Along<vertical>(hi) < Along<vertical>(p);
    }

    // onboundary skips the crossing at p itself: for p on an edge the parity is the state just beyond p along the ray
    template <bool vertical, bool onboundary>
    bool RayParity(const Line &l, const Vertex &p) {
        if (RayMisses<vertical>(l.Box(), p))
            return false;
        if constexpr (onboundary) {
            if (l.IsInside(p))
                return false;
        }
        auto u = [](const Vertex &v) { return Along<vertical>(v); };
        auto w = [](const Vertex &v) { return Across<vertical>(v); };
        const Vertex &a = l.Start();
        const Vertex &b = l.End();
        if ((w(a) > w(p)) == (w(b) > w(p)))
            return false;
        return u(a) + (w(p) - w(a)) * (u(b) - u(a)) / (w(b) - w(a)) > u(p);
    }

    template <bool vertical, bool onboundary>
    bool RayParity(const Arc &arc, const Vertex &p) {
        if (RayMisses<vertical>(arc.Box(), p))
            return false;
        auto u = [](const Vertex &v) { return Along<vertical>(v); };
        auto w = [](const Vertex &v) { return Across<vertical>(v); };
        double tp = -1.0;
        if constexpr (onboundary) {
            if (arc.IsInside(p))
                tp = arc.Param(p);
        }
        const Vertex &c = arc.Center();
        double r = arc.Radius();
        double sweep = arc.Sweep();
        std::array<PointOnCurve, 4> pts;
        size_t n = 0;
        pts[n++] = {0.0, arc.Start()};
        for (const Vertex &ext : {RayVertex<vertical>(u(c), w(c) + r), RayVertex<vertical>(u(c), w(c) - r)}) {
            double t = arc.Param(ext);
            if (t > 0.0 && t < sweep)
                pts[n++] = {t, ext};
        }
        pts[n++] = {sweep, arc.End()};
        if (n == 4 && pts[2] < pts[1])
            std::swap(pts[1], pts[2]);
        double dw = w(p) - w(c);
        double h2 = (r - dw) * (r + dw);
        double h = h2 > 0.0 ? std::sqrt(h2) : 0.0;
        bool odd = false;
        for (size_t i = 1; i < n; ++i) {
            const PointOnCurve &a = pts[i - 1];
            const PointOnCurve &b = pts[i];
            if constexpr (onboundary) {
                if (tp >= a.t && tp <= b.t)
                    continue;
            }
            if ((w(a.p) > w(p)) == (w(b.p) > w(p)))
                continue;
            bool positive = u(arc.PointAt(0.5 * (a.t + b.t))) > u(c);
            if ((positive ? u(c) + h : u(c) - h) > u(p))
                odd = !odd;
        }
        return odd;
    }

    template <bool vertical, bool onboundary, class... T>
    bool RayParity(const std::variant<T...> &e, const Vertex &p) {
        return std::visit([&](const auto &x) { return RayParity<vertical, onboundary>(x, p); }, e);
    }

    template <bool vertical, bool onboundary>
    bool RayParity(const Circle &c, const Vertex &p) {
        if (RayMisses<vertical>(c.Box(), p))
            return false;
        bool odd = false;
        for (const Arc &half : c.Halves())
            odd ^= RayParity<vertical, onboundary>(half, p);
        return odd;
    }

    template <bool vertical, bool onboundary>
    bool RayParity(const Spline &s, const Vertex &p) {
        if (RayMisses<vertical>(s.Box(), p))
            return false;
        bool odd = false;
        for (const PrimitiveEdge &part : s.Approximation())
            odd ^= RayParity<vertical, onboundary>(part, p);
        return odd;
    }

}
