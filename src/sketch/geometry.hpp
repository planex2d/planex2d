// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
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
        Distance, Radius, Diameter, Angle, SymmetricCarriers, OnCircle, Copy, RigidGroup, Expression, Value, Offset, OnCurve, Parameter,
        Curvature, FixH, FixV, CurvatureRadius
    };

    inline const char *KindName(Kind k) {
        constexpr const char *names[] = {"Coincident", "Horizontal", "Vertical", "Parallel", "Perpendicular", "Tangent",
                                         "Equal", "Symmetric", "Concentric", "Fix", "Distance", "Radius", "Diameter",
                                         "Angle", "SymmetricCarriers", "OnCircle", "Copy", "RigidGroup", "Expression", "Value",
                                         "Offset", "OnCurve", "Parameter", "Curvature", "FixH", "FixV",
                                         "CurvatureRadius"};
        return names[(size_t)k];
    }

    inline bool IsFix(Kind k) { return k == Kind::Fix || k == Kind::FixH || k == Kind::FixV; }
    // the coordinate a FixH (h) or FixV (v) holds
    inline size_t HeldCoordinate(Kind k) { return k == Kind::FixV ? 1 : 0; }

    // the ties of an offset, of its point j (the first reference) to source element x (the second) at distance d (its
    // side the sign, +1 to the left of x): Point: j = p + d n, n x's unit normal at its point p (the third); Foot: j on
    // x's normal through p; Carrier: j on x's offset line or circle (x a point: on the circle of radius d around it);
    // Center: the centre of an arc (the first reference) where the point s (the second) is along its chord's bisector;
    // Radius: a circle (the first) of the radius of the circle x plus d; Along: j at its end of the offset curve x, whose
    // chord length there is an unknown of the curve; Spur: j on the tangent of x at its point p, offset by d (decision
    // 0202: a sharp corner's straight piece along a curve); Beyond: j d beyond x's end p, along x's way there times the
    // sign (a square cap)
    enum class OffsetTie { Point, Foot, Carrier, Center, Radius, Along, Spur, Beyond };

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
                         bool closed, SplineStyle style = SplineStyle::Averaged, const std::map<size_t, TangentSetting<double>> &settings = {}) {
            SplineData d{points, tangentAt, {}, closed, style};
            for (double a : angles)
                d.tangent.push_back(NewValue(a, true));
            for (const auto &[k, setting] : settings) {
                if (setting.tension != 1.0) {
                    d.tension.resize(points.size(), 1.0);
                    d.tension[k] = setting.tension;
                }
                if (setting.curvature) {
                    d.curvature.resize(points.size(), none);
                    d.curvature[k] = NewValue(*setting.curvature);
                }
            }
            splines.push_back(std::move(d));
            // raises for points and tangents that give no spline
            splines.back().Interpolated(*this);
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        // a spline fitted to a curve, from the point start to the point end (end == start: closed), its frame at origin
        // turned by angle
        size_t AddFittedSpline(size_t start, size_t end, FittedShape f, const Vertex &origin, double angle) {
            f.origin = NewPoint(origin);
            f.angle = NewValue(angle, true);
            const bool closed = start == end;
            splines.push_back({closed ? std::vector<size_t>{start} : std::vector<size_t>{start, end}, {}, {}, closed, SplineStyle::Averaged, std::move(f)});
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        // a spline computed from the elements it joins, from the point start (an end of b.arms[0]) to end (of b.arms[1])
        size_t AddBlend(size_t start, size_t end, const BlendShape &b) {
            splines.push_back({{start, end}, {}, {}, false, SplineStyle::Averaged, std::nullopt, b});
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        // a spline by its poles, the points (a closed one periodic), knots and degree; raises for ones that give no
        // b-spline. Its knots start at 0, as the elements' ranges take them
        size_t AddPoledSpline(const std::vector<size_t> &points, std::vector<double> knots, size_t degree, bool closed) {
            std::vector<Vertex> q;
            for (size_t p : points)
                q.push_back(Coords(p));
            BSplineCurve({q, knots, {}, (int)degree}, closed);
            const double first = knots.front();
            for (double &k : knots)
                k -= first;
            SplineData d{points, {}, {}, closed};
            d.poled = PoleShape{std::move(knots), degree};
            splines.push_back(std::move(d));
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        size_t AddSplinePiece(size_t spline, const SplinePiece &piece) {
            pieces.push_back(piece);
            return Added({ElementKind::Spline, {spline, pieces.size()}});
        }
        // step2: the step along a linear pattern's second direction, if it has one
        size_t AddPattern(double step, bool linear, std::optional<double> step2 = std::nullopt) {
            const size_t first = NewValue(step, !linear);
            return Added({ElementKind::Pattern, {first, linear, step2 ? NewValue(*step2) : none}});
        }
        size_t AddOffset(double d) { return Added({ElementKind::Offset, {NewValue(d)}}); }
        size_t AddOffsetCurve(const OffsetCurveData &c) {
            offsetCurves.push_back(c);
            return Added({ElementKind::OffsetCurve, {offsetCurves.size() - 1}});
        }

        // the spline (its index in splines) whose control polygon has line e as a leg, e joining two neighbouring poles of
        // a b-spline; none for every other element
        size_t LegOf(const Element &e) const {
            if (e.kind != ElementKind::Line)
                return none;
            for (size_t i = 0; i < splines.size(); ++i) {
                const SplineData &d = splines[i];
                if (!d.poled)
                    continue;
                const size_t n = d.points.size();
                for (size_t k = 0; k + 1 < n + (d.closed ? 1 : 0); ++k) {
                    const size_t a = d.points[k], b = d.points[(k + 1) % n];
                    if ((e.Start() == a && e.End() == b) || (e.Start() == b && e.End() == a))
                        return i;
                }
            }
            return none;
        }

        double Size() const {
            double s = 0.0;
            for (size_t u = 0; u < values.size(); ++u)
                if (!isAngle[u] && !IsCurvature(u))
                    s = std::max(s, std::abs(values[u]));
            return s > 0.0 ? s : 1.0;
        }
        bool IsCurvature(size_t u) const {
            for (const SplineData &d : splines)
                if (std::find(d.curvature.begin(), d.curvature.end(), u) != d.curvature.end())
                    return true;
            return false;
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
    template <> inline void Geometry::HoldOwnValues<ElementKind::Pattern>(size_t element) {
        held[elements[element].Step()] = 1;
        if (elements[element].HasStep2())
            held[elements[element].Step2()] = 1;
    }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Offset>(size_t element) { held[elements[element].Distance()] = 1; }
    template <> inline void Geometry::HoldOwnValues<ElementKind::Spline>(size_t element) {
        const size_t spline = elements[element].SplineIndex();
        if (!IsOnlyElementOf(spline, element))
            return;
        for (size_t a : splines[spline].tangent)
            held[a] = 1;
        if (const std::optional<FittedShape> &f = splines[spline].fitted) {
            HoldPoint(f->origin);
            held[f->angle] = 1;
        }
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
        for (SplineData &d : splines)
            if (d.blend)
                for (size_t *k : {&d.blend->arms[0], &d.blend->arms[1], &d.blend->helper})
                    if (*k != none && *k > element)
                        --*k;
    }

}

#include "sketch/element.cpp"
#include "sketch/offsetcurvedata.cpp"
#include "sketch/ref.cpp"
#include "sketch/splinedata.cpp"
