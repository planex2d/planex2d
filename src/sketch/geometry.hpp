// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "sketch/element.hpp"
#include "sketch/offsetcurvedata.hpp"
#include "sketch/ref.hpp"
#include "sketch/splinedata.hpp"

namespace planex2d::sketch {

    enum class Kind {
        Coincident, Horizontal, Vertical, Parallel, Perpendicular, Tangent, Equal, Symmetric, Concentric, Fix,
        Distance, Radius, Diameter, Angle, SymmetricCarriers, OnCircle, Copy, RigidGroup, Expression, Value, Offset
    };

    inline const char *KindName(Kind k) {
        constexpr const char *names[] = {"Coincident", "Horizontal", "Vertical", "Parallel", "Perpendicular", "Tangent",
                                         "Equal", "Symmetric", "Concentric", "Fix", "Distance", "Radius", "Diameter",
                                         "Angle", "SymmetricCarriers", "OnCircle", "Copy", "RigidGroup", "Expression", "Value",
                                         "Offset"};
        return names[(size_t)k];
    }

    // the ties of an offset, of its point j (the first reference) to source element x (the second) at distance d (its
    // side the sign, +1 to the left of x): Point: j = p + d n, n x's unit normal at its point p (the third); Foot: j on
    // x's normal through p; Carrier: j on x's offset line or circle (x a point: on the circle of radius d around it);
    // Center: the centre of an arc (the first reference) where the point s (the second) is along its chord's bisector;
    // Radius: a circle (the first) of the radius of the circle x plus d; Along: j at its end of the offset curve x, whose
    // chord length there is an unknown of the curve
    enum class OffsetTie { Point, Foot, Carrier, Center, Radius, Along };

    class Geometry {
    public:
        std::vector<double> values;
        std::vector<char> isAngle;
        std::vector<char> held;
        std::vector<size_t> ownCoordsOf, coordsOf;
        std::vector<Element> elements;
        std::vector<SplineData> splines;
        std::vector<SplinePiece> pieces;
        std::vector<OffsetCurveData> offsetCurves;

        size_t NewValue(double v, bool angle = false, bool hold = false) {
            values.push_back(v);
            isAngle.push_back(angle);
            held.push_back(hold);
            return values.size() - 1;
        }
        size_t NewPoint(const Vertex &p, bool hold = false) {
            const size_t u = NewValue(p.x, false, hold);
            NewValue(p.y, false, hold);
            ownCoordsOf.push_back(u);
            coordsOf.push_back(u);
            return ownCoordsOf.size() - 1;
        }
        Vertex Coords(size_t point) const { return {values[coordsOf[point]], values[coordsOf[point] + 1]}; }

        size_t AddPoint(size_t point) { return Added({ElementKind::Point, {point}}); }
        size_t AddLine(size_t start, size_t end) { return Added({ElementKind::Line, {start, end}}); }
        size_t AddCircle(size_t center, double r, double originAngle = 0.0, bool ccw = true, size_t originPoint = none) {
            return Added({ElementKind::Circle, {center, NewValue(r), NewValue(originAngle, true, true), originPoint}, ccw});
        }
        size_t AddArc(size_t center, double r, size_t start, size_t end, bool ccw) {
            return Added({ElementKind::Arc, {center, NewValue(r), start, end}, ccw});
        }
        size_t AddArcWithRadius(size_t center, size_t radius, size_t start, size_t end, bool ccw) {
            return Added({ElementKind::Arc, {center, radius, start, end}, ccw});
        }
        size_t AddAxis(double a, double d, bool hold) {
            return Added({ElementKind::Axis, {NewValue(a, true, hold), NewValue(d, false, hold)}});
        }
        size_t AddSpline(const std::vector<size_t> &points, const std::vector<size_t> &tangentAt, const std::vector<double> &angles,
                         bool closed, SplineStyle style = SplineStyle::Px) {
            SplineData d{points, tangentAt, {}, closed, style};
            for (double a : angles)
                d.tangent.push_back(NewValue(a, true));
            splines.push_back(std::move(d));
            // raises for points and tangents that give no spline
            splines.back().Interpolated(*this);
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        size_t AddSplinePiece(size_t spline, const SplinePiece &piece) {
            pieces.push_back(piece);
            return Added({ElementKind::Spline, {spline, pieces.size()}});
        }
        size_t AddPattern(double step, bool linear) { return Added({ElementKind::Pattern, {NewValue(step, !linear), linear}}); }
        size_t AddOffset(double d) { return Added({ElementKind::Offset, {NewValue(d)}}); }
        size_t AddOffsetCurve(const OffsetCurveData &c) {
            offsetCurves.push_back(c);
            return Added({ElementKind::OffsetCurve, {offsetCurves.size() - 1}});
        }

        double Size() const {
            double s = 0.0;
            for (size_t u = 0; u < values.size(); ++u)
                if (!isAngle[u])
                    s = std::max(s, std::abs(values[u]));
            return s > 0.0 ? s : 1.0;
        }

        void RemoveElement(size_t element);

        struct Mark {
            size_t values, points, elements, splines, pieces, offsetCurves;
        };
        Mark Marked() const { return {values.size(), ownCoordsOf.size(), elements.size(), splines.size(), pieces.size(), offsetCurves.size()}; }
        void Truncate(const Mark &m) {
            const auto cut = [](auto &v, size_t n) { v.erase(v.begin() + (std::ptrdiff_t)n, v.end()); };
            cut(values, m.values);
            cut(isAngle, m.values);
            cut(held, m.values);
            cut(ownCoordsOf, m.points);
            cut(coordsOf, m.points);
            cut(elements, m.elements);
            cut(splines, m.splines);
            cut(pieces, m.pieces);
            cut(offsetCurves, m.offsetCurves);
            for (SplineData &d : splines)
                while (!d.tangent.empty() && d.tangent.back() >= m.values) {
                    d.tangent.pop_back();
                    d.tangentAt.pop_back();
                }
        }

    protected:
        size_t Added(const Element &e) {
            elements.push_back(e);
            return elements.size() - 1;
        }

    private:
        template <ElementKind K> void HoldOwnValues(size_t) {}
        void HoldOwnRoundValues(size_t element) {
            const Element &e = elements[element];
            if (!IsCentreShared(e.Center(), element))
                HoldPoint(e.Center());
            if (!IsRadiusShared(e.Radius(), element))
                held[e.Radius()] = 1;
        }
        void HoldPoint(size_t point) {
            held[ownCoordsOf[point]] = 1;
            held[ownCoordsOf[point] + 1] = 1;
        }
        bool IsCentreShared(size_t point, size_t except) const {
            for (size_t k = 0; k < elements.size(); ++k) {
                if (k == except)
                    continue;
                const Element &o = elements[k];
                if ((o.kind == ElementKind::Circle || o.kind == ElementKind::Arc) && o.Center() == point)
                    return true;
                const std::vector<size_t> through = o.kind == ElementKind::Point ? std::vector<size_t>{o.Point()} : o.PointsOn(*this);
                if (std::find(through.begin(), through.end(), point) != through.end())
                    return true;
            }
            return false;
        }
        bool IsRadiusShared(size_t radius, size_t except) const {
            for (size_t k = 0; k < elements.size(); ++k)
                if (k != except && (elements[k].kind == ElementKind::Circle || elements[k].kind == ElementKind::Arc) &&
                    elements[k].Radius() == radius)
                    return true;
            return false;
        }
        bool IsOnlyElementOf(size_t spline, size_t element) const {
            for (size_t k = 0; k < elements.size(); ++k)
                if (k != element && elements[k].kind == ElementKind::Spline && elements[k].SplineIndex() == spline)
                    return false;
            return true;
        }
    };

    template <> inline void Geometry::HoldOwnValues<ElementKind::Point>(size_t element) { HoldPoint(elements[element].Point()); }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Circle>(size_t element) { HoldOwnRoundValues(element); }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Arc>(size_t element) { HoldOwnRoundValues(element); }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Axis>(size_t element) {
        held[elements[element].AxisAngle()] = 1;
        held[elements[element].AxisDistance()] = 1;
    }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Pattern>(size_t element) { held[elements[element].Step()] = 1; }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Offset>(size_t element) { held[elements[element].Distance()] = 1; }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Spline>(size_t element) {
        const size_t spline = elements[element].SplineIndex();
        if (IsOnlyElementOf(spline, element))
            for (size_t a : splines[spline].tangent)
                held[a] = 1;
    }
    template <> inline void Geometry::HoldOwnValues<ElementKind::OffsetCurve>(size_t element) {
        const OffsetCurveData &c = offsetCurves[elements[element].OffsetCurveIndex()];
        for (size_t u : {c.range.from, c.range.to})
            if (u != none)
                held[u] = 1;
    }

    inline void Geometry::RemoveElement(size_t element) {
        switch (elements[element].kind) {
        case ElementKind::Point: HoldOwnValues<ElementKind::Point>(element); break;
        case ElementKind::Line: HoldOwnValues<ElementKind::Line>(element); break;
        case ElementKind::Circle: HoldOwnValues<ElementKind::Circle>(element); break;
        case ElementKind::Arc: HoldOwnValues<ElementKind::Arc>(element); break;
        case ElementKind::Axis: HoldOwnValues<ElementKind::Axis>(element); break;
        case ElementKind::Pattern: HoldOwnValues<ElementKind::Pattern>(element); break;
        case ElementKind::Spline: HoldOwnValues<ElementKind::Spline>(element); break;
        case ElementKind::Offset: HoldOwnValues<ElementKind::Offset>(element); break;
        case ElementKind::OffsetCurve: HoldOwnValues<ElementKind::OffsetCurve>(element); break;
        }
        elements.erase(elements.begin() + (std::ptrdiff_t)element);
    }

}

#include "sketch/element.cpp"
#include "sketch/offsetcurvedata.cpp"
#include "sketch/ref.cpp"
#include "sketch/splinedata.cpp"
