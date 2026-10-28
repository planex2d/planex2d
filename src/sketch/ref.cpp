// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <stdexcept>

#include "sketch/geometry.hpp"

namespace planex2d::sketch {

    inline bool Ref::IsSpline(const Geometry &g) const { return part == Part::Whole && g.elements[element].kind == ElementKind::Spline; }
    inline bool Ref::IsCurve(const Geometry &g) const {
        const ElementKind k = g.elements[element].kind;
        return part == Part::Whole && (k == ElementKind::Spline || k == ElementKind::OffsetCurve);
    }
    inline bool Ref::HasPart(const Geometry &g) const {
        const ElementKind e = g.elements[element].kind;
        return part == Part::Whole ||
               ((part == Part::Start || part == Part::End || part == Part::Mid) &&
                (e == ElementKind::Line || e == ElementKind::Arc || e == ElementKind::Spline || e == ElementKind::OffsetCurve)) ||
               (part == Part::Center && (e == ElementKind::Circle || e == ElementKind::Arc)) ||
               ((part == Part::Point || part == Part::Tangent) && e == ElementKind::Spline && !g.elements[element].IsFitted(g) &&
                index < g.splines[g.elements[element].SplineIndex()].points.size());
    }
    inline bool Ref::IsPoint(const Geometry &g) const {
        return (part != Part::Whole && part != Part::Tangent) || g.elements[element].kind == ElementKind::Point;
    }
    inline bool Ref::IsRealPoint(const Geometry &g) const { return IsPoint(g) && part != Part::Mid; }
    inline bool Ref::IsLine(const Geometry &g) const {
        const ElementKind k = g.elements[element].kind;
        return part == Part::Tangent || (part == Part::Whole && (k == ElementKind::Line || k == ElementKind::Axis));
    }
    inline bool Ref::IsSegment(const Geometry &g) const { return part == Part::Whole && g.elements[element].kind == ElementKind::Line; }
    inline bool Ref::IsRound(const Geometry &g) const {
        const ElementKind k = g.elements[element].kind;
        return part == Part::Whole && (k == ElementKind::Circle || k == ElementKind::Arc);
    }
    inline bool Ref::IsArc(const Geometry &g) const { return part == Part::Whole && g.elements[element].kind == ElementKind::Arc; }
    inline bool Ref::IsPattern(const Geometry &g) const { return part == Part::Whole && g.elements[element].kind == ElementKind::Pattern; }
    inline bool Ref::IsOffset(const Geometry &g) const { return part == Part::Whole && g.elements[element].kind == ElementKind::Offset; }

    inline size_t Ref::PointAt(const Geometry &g) const {
        const Element &e = g.elements[element];
        if (e.kind == ElementKind::Spline)
            return part == Part::Point || part == Part::Tangent ? g.splines[e.SplineIndex()].points[index] : e.Ends(g)[part == Part::End];
        if (e.kind == ElementKind::Point)
            return e.Point();
        if (part == Part::Center)
            return e.Center();
        if (e.kind == ElementKind::OffsetCurve)
            return e.Ends(g)[part == Part::End];
        if (e.kind == ElementKind::Line)
            return part == Part::End ? e.End() : e.Start();
        return part == Part::Start ? e.Start() : e.End();
    }
    inline Vertex Ref::PointValue(const Geometry &g) const {
        if (!IsPoint(g) && part != Part::Tangent)
            throw std::invalid_argument("not a point");
        if (part != Part::Mid)
            return g.Coords((part == Part::Tangent ? Ref{element, Part::Point, index} : *this).PointAt(g));
        const Element &e = g.elements[element];
        if (e.kind == ElementKind::OffsetCurve) {
            const OffsetCurveData &c = g.offsetCurves[e.OffsetCurveIndex()];
            const Curve<double, Vertex> f = e.Curve(g);
            const std::array<double, 2> t = c.Range(g, f);
            return c.Point(g, f, 0.5 * (t[0] + t[1]));
        }
        if (e.kind == ElementKind::Spline) {
            const Curve<double, Vertex> f = e.Curve(g);
            const auto [from, to] = e.Range(g, f);
            const double mid = 0.5 * (from + to), end = f.params.back();
            return f.At(mid > end ? mid - end : mid, 0);
        }
        if (e.kind == ElementKind::Line)
            return (g.Coords(e.Start()) + g.Coords(e.End())) * 0.5;
        // on the arc's side of its chord, also for arcs over 180 degrees
        const Vertex chord = g.Coords(e.End()) - g.Coords(e.Start());
        const Vertex side = e.ccw ? Vertex{chord.y, -chord.x} : Vertex{-chord.y, chord.x};
        return g.Coords(e.Center()) + Normalized(side) * g.values[e.Radius()];
    }

}
