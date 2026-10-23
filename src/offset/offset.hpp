// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <numbers>
#include <vector>

#include "boolean/boolean.hpp"

namespace planex2d {

    inline Wire Line::Offset(double d) const {
        if (!(d > 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be positive and finite");
        Vertex u = (end - start) * (1.0 / Norm(end - start));
        Vertex n{-u.y * d, u.x * d};
        Vertex sl = start + n;
        Vertex sr = start - n;
        Vertex el = end + n;
        Vertex er = end - n;
        Line right(sr, er), left(el, sl);
        Arc endcap(er, el, end, true), startcap(sl, sr, start, true);
        right.fromSpline = fromSpline;
        left.fromSpline = fromSpline;
        right.props = left.props = endcap.props = startcap.props = WithoutPartition(props);
        Wire w;
        w.Add(right);
        w.Add(endcap);
        w.Add(left);
        w.Add(startcap);
        return w;
    }

    inline Wire Arc::Offset(double d) const {
        if (!(d > 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be positive and finite");
        Vertex a = (start - center) * (1.0 / Norm(start - center));
        Vertex b = (end - center) * (1.0 / Norm(end - center));
        if (d >= radius) {
            // the inner side is covered: every point of the slice of radius r + d over the arc's angle is within d of
            // it, and a point outside that angle is nearest to an end
            const Properties p = WithoutPartition(props);
            const Vertex so = center + a * (radius + d), eo = center + b * (radius + d);
            Line in(center, so), out(eo, center);
            Arc rim(so, eo, center, ccw);
            rim.fromSpline = fromSpline;
            in.props = out.props = rim.props = p;
            Wire slice;
            slice.Add(in);
            slice.Add(rim);
            slice.Add(out);
            WireList parts{slice};
            for (const Vertex &q : {start, end}) {
                Arc upper(q + Vertex{d, 0.0}, q - Vertex{d, 0.0}, q, true), lower(q - Vertex{d, 0.0}, q + Vertex{d, 0.0}, q, true);
                upper.props = lower.props = p;
                Wire disc;
                disc.Add(upper);
                disc.Add(lower);
                parts.push_back(std::move(disc));
            }
            return BalancedBoolean(std::move(parts), BooleanOp::Union);
        }
        Vertex so = center + a * (radius + d);
        Vertex si = center + a * (radius - d);
        Vertex eo = center + b * (radius + d);
        Vertex ei = center + b * (radius - d);
        Arc outer(so, eo, center, ccw), inner(ei, si, center, !ccw);
        Arc endcap(eo, ei, end, ccw), startcap(si, so, start, ccw);
        outer.fromSpline = fromSpline;
        inner.fromSpline = fromSpline;
        outer.props = inner.props = endcap.props = startcap.props = WithoutPartition(props);
        Wire w;
        w.Add(outer);
        w.Add(endcap);
        w.Add(inner);
        w.Add(startcap);
        return w;
    }

    // the band within d of the circle: the concentric circle of radius r + d, and r - d inside it unless d reaches r
    inline Wire Circle::Offset(double d) const {
        if (!(d > 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be positive and finite");
        const Vertex u = (origin - center) * (1.0 / radius);
        Circle outer(center, center + u * (radius + d), ccw);
        outer.props = WithoutPartition(props);
        Wire w;
        w.Add(outer);
        if (d < radius) {
            Circle inner(center, center + u * (radius - d), !ccw);
            inner.props = WithoutPartition(props);
            w.Add(inner);
        }
        return w;
    }

    inline Wire Spline::Offset(double d) const {
        WireList parts;
        for (const PrimitiveEdge &part : Approximation())
            parts.push_back(std::visit([&](auto e) {
                e.props = WithoutPartition(props);
                return e.Offset(d);
            }, part));
        return BalancedBoolean(std::move(parts), BooleanOp::Union);
    }

    inline Wire Wire::Offset(double d) const {
        WireList parts;
        for (const Edge &e : edges)
            parts.push_back(std::visit([&](const auto &x) { return x.Offset(d); }, e));
        Wire result = BalancedBoolean(std::move(parts), BooleanOp::Union);
        result.SetAxes(axes);
        return result;
    }

}
