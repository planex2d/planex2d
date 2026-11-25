// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/sketch/element.hpp"
#include "planex/sketch/offsetcurvedata.hpp"
#include "planex/sketch/ref.hpp"
#include "planex/sketch/splinedata.hpp"
#include "planex/core/bbox.hpp"
#include "planex/curves/blend.hpp"
#include "planex/curves/primitivefit.hpp"

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
    inline size_t HeldCoordinate(Kind k) { return k == Kind::FixV ? 1 : 0; }
    inline bool HasValue(Kind k) {
        return k == Kind::Distance || k == Kind::Radius || k == Kind::Diameter || k == Kind::Angle || k == Kind::Parameter || k == Kind::CurvatureRadius;
    }

    // the ties of an offset, of its point j (the first reference) to source element x (the second) at distance d (its side
    // the sign, +1 to the left of x): Point: j = p + d n, n x's unit normal at its point p (the third); Foot: j on x's normal
    // through p; Carrier: j on x's offset line or circle (x a point: on the circle of radius d around it); Center: the centre
    // of an arc (the first reference) where the point s (the second) is along its chord's bisector; Radius: a circle (the
    // first) of the radius of the circle x plus d; Along: j at its end of the offset curve x, whose chord length there is an
    // unknown of the curve; Spur: j on the tangent of x at its point p, offset by d (a sharp corner's straight piece along a
    // curve); Beyond: j d beyond x's end p, along x's way there times the sign (a square cap)
    enum class OffsetTie { Point, Foot, Carrier, Center, Radius, Along, Spur, Beyond };

    // every sketch starts with its axes H and V and its origin: the elements 0, 1 and 2
    inline constexpr size_t ownElements = 3;

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
        void MovePoint(size_t p, const Vertex &q) {
            for (size_t k : {coordsOf[p], ownCoordsOf[p]}) {
                values[k] = q.x;
                values[k + 1] = q.y;
            }
        }

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
                         bool closed, SplineStyle style = SplineStyle::Averaged, const std::map<size_t, TangentSetting<double>> &settings = {});
        // end == start: closed
        size_t AddFittedSpline(size_t start, size_t end, FittedShape f, const Vertex &origin, double angle) {
            f.origin = NewPoint(origin);
            f.angle = NewValue(angle, true);
            const bool closed = start == end;
            splines.push_back({closed ? std::vector<size_t>{start} : std::vector<size_t>{start, end}, {}, {}, closed, SplineStyle::Averaged, std::move(f)});
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        size_t AddBlend(size_t start, size_t end, const BlendShape &b) {
            splines.push_back({{start, end}, {}, {}, false, SplineStyle::Averaged, std::nullopt, b});
            return Added({ElementKind::Spline, {splines.size() - 1}});
        }
        size_t AddPoledSpline(const std::vector<size_t> &points, std::vector<double> knots, size_t degree, bool closed);
        size_t AddSplinePiece(size_t spline, const SplinePiece &piece) {
            pieces.push_back(piece);
            return Added({ElementKind::Spline, {spline, pieces.size()}});
        }
        size_t AddPattern(double step, bool linear, std::optional<double> step2 = std::nullopt) {
            const size_t first = NewValue(step, !linear);
            return Added({ElementKind::Pattern, {first, linear, step2 ? NewValue(*step2) : none}});
        }
        size_t AddOffset(double d) { return Added({ElementKind::Offset, {NewValue(d)}}); }
        size_t AddOffsetCurve(const OffsetCurveData &c) {
            offsetCurves.push_back(c);
            return Added({ElementKind::OffsetCurve, {offsetCurves.size() - 1}});
        }

        size_t LegOf(const Element &e) const;

        double DrawingTol() const;
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

        std::vector<std::pair<size_t, Vertex>> Mirror(const std::vector<size_t> &which, const Vertex &a, const Vertex &u);
        void Scale(double f);
        void MoveOrigin(const Vertex &p);
        bool IsPointUnused(size_t pointElement) const;
        std::set<size_t> CentrePoints() const;
        std::vector<size_t> LegsGoingWith(size_t k) const;
        void BlendsFollow(size_t a, const std::vector<size_t> &into);
        std::optional<size_t> BlendOn(size_t e) const;

        bool IsEndPointOf(size_t point, size_t line) const {
            return elements[point].kind == ElementKind::Point && (elements[point].Point() == elements[line].Start() || elements[point].Point() == elements[line].End());
        }
        std::optional<Ref> PieceEndAt(const std::vector<size_t> &pieces, size_t point) const;
        std::vector<Vertex> PointPlaces() const;
        std::vector<double> Headings() const;
        std::pair<double, size_t> MostTurned(const std::vector<double> &before, const std::vector<double> &after) const;

        struct Mark {
            size_t values, points, elements, splines, pieces, offsetCurves;
        };
        Mark Marked() const { return {values.size(), ownCoordsOf.size(), elements.size(), splines.size(), pieces.size(), offsetCurves.size()}; }
        void Truncate(const Mark &m);

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
        bool IsCentreShared(size_t point, size_t except) const;
        bool IsRadiusShared(size_t radius, size_t except) const {
            for (size_t k = 0; k < elements.size(); ++k)
                if (k != except && elements[k].IsRound() && elements[k].Radius() == radius)
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

}

namespace planex2d::sketch {

    inline size_t Element::Spline(const Geometry &g) const { return kind == ElementKind::OffsetCurve ? g.offsetCurves[OffsetCurveIndex()].spline : SplineIndex(); }
    inline std::string Element::KindName(const Geometry &g) const { return IsFitted(g) ? "Curve" : IsPoled(g) ? "BSpline" : ElementKindName(kind); }
    inline bool Element::IsFitted(const Geometry &g) const { return kind == ElementKind::Spline && g.splines[SplineIndex()].fitted.has_value(); }
    inline bool Element::IsPoled(const Geometry &g) const { return kind == ElementKind::Spline && g.splines[SplineIndex()].poled.has_value(); }
    inline bool Element::IsPeriodicBSpline(const Geometry &g) const { return IsPoled(g) && !IsPiece(g) && g.splines[SplineIndex()].closed; }
    inline bool Element::IsClosedSpline(const Geometry &g) const { return kind == ElementKind::Spline && !IsPiece(g) && g.splines[SplineIndex()].closed; }
    inline bool Element::IsBlend(const Geometry &g) const { return kind == ElementKind::Spline && g.splines[SplineIndex()].blend.has_value(); }
    inline const SplinePiece *Element::Piece(const Geometry &g) const {
        return kind == ElementKind::Spline && PieceIndex() ? &g.pieces[PieceIndex() - 1] : nullptr;
    }
    inline std::array<Vertex, 2> Element::EndCoords(const Geometry &g) const {
        // a periodic spline by its poles runs through none of them: its ends where its curve starts
        if (IsPeriodicBSpline(g)) {
            const Vertex start = SplinePoints(g).front();
            return {start, start};
        }
        const std::array<size_t, 2> ends = Ends(g);
        return {g.Coords(ends[0]), g.Coords(ends[1])};
    }
    inline bool Element::IsClosed(const Geometry &g) const {
        const std::array<size_t, 2> ends = Ends(g);
        return ends[0] == ends[1];
    }
    inline std::vector<size_t> Element::PointsAndCentre(const Geometry &g) const {
        std::vector<size_t> out = PointsOn(g);
        if (IsRound())
            out.push_back(Center());
        if (kind == ElementKind::Circle && OriginPoint() != none)
            out.push_back(OriginPoint());
        return out;
    }
    inline bool Element::IsHeld(const Geometry &g) const {
        for (size_t u : Values(g))
            if (!g.held[u])
                return false;
        return true;
    }

    inline double Element::Length(const Geometry &g) const { return Norm(g.Coords(End()) - g.Coords(Start())); }
    inline std::pair<Vertex, Vertex> Element::Carrier(const Geometry &g) const {
        if (kind == ElementKind::Line) {
            const Vertex a = g.Coords(Start()), d = g.Coords(End()) - a;
            return {a, d * (1.0 / Norm(d))};
        }
        const double t = g.values[AxisAngle()];
        return {Vertex{std::cos(t), std::sin(t)} * g.values[AxisDistance()], Vertex{-std::sin(t), std::cos(t)}};
    }
    inline Vertex Element::Origin(const Geometry &g) const {
        if (OriginPoint() != none)
            return g.Coords(OriginPoint());
        const double a = g.values[OriginAngle()];
        return g.Coords(Center()) + Vertex{std::cos(a), std::sin(a)} * g.values[Radius()];
    }

    inline double Element::AxisShift(const Geometry &g, const Vertex &p) const {
        const double t = g.values[AxisAngle()];
        return std::cos(t) * p.x + std::sin(t) * p.y;
    }

    inline planex2d::Curve<double, Vertex> Element::Curve(const Geometry &g) const { return g.splines[Spline(g)].Interpolated(g); }
    inline SplineStyle Element::Style(const Geometry &g) const { return g.splines[Spline(g)].style; }
    inline std::vector<Vertex> Element::SplinePoints(const Geometry &g) const { return g.splines[SplineIndex()].Points(g); }
    inline Vertex Element::TangentAtFraction(const Geometry &g, double t) const {
        const planex2d::Curve<double, Vertex> f = Curve(g);
        return f.At(t * f.params.back(), 1);
    }
    inline std::array<double, 2> Element::Range(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const {
        const double end = f.params.back();
        const SplinePiece *piece = Piece(g);
        const double from = piece && piece->from != none ? g.values[piece->from] : 0.0;
        const double to = piece && piece->to != none ? g.values[piece->to] : end;
        return {from, to < from ? to + end : to};
    }
    // the points of the curve at (a parameter to its point) after (a, pa) up to (b, pb): halved while the curve's middle
    // lies farther than tol from the chord's middle, 10 times at most
    template <class F>
    inline void AddHalved(const F &at, double a, const Vertex &pa, double b, const Vertex &pb, double tol, int depth, std::vector<Vertex> &out) {
        const double m = 0.5 * (a + b);
        const Vertex pm = at(m);
        if (depth < 10 && !pm.IsClose((pa + pb) * 0.5, tol)) {
            AddHalved(at, a, pa, m, pm, tol, depth + 1, out);
            AddHalved(at, m, pm, b, pb, tol, depth + 1, out);
        } else
            out.push_back(pb);
    }
    // the points of the curve at after the node from up to the node to, for drawing it within tol: in quarters first, so
    // that a bend between two nodes is seen, each quarter halved where it needs it
    template <class F>
    inline void AddDrawn(const F &at, const std::pair<double, Vertex> &from, const std::pair<double, Vertex> &to, double tol, std::vector<Vertex> &out) {
        double a = from.first;
        Vertex pa = from.second;
        for (int i = 1; i <= 4; ++i) {
            const double b = i == 4 ? to.first : from.first + (to.first - from.first) * (double)i / 4.0;
            const Vertex pb = i == 4 ? to.second : at(b);
            AddHalved(at, a, pa, b, pb, tol, 0, out);
            a = b;
            pa = pb;
        }
    }
    inline std::vector<Vertex> Element::SplinePath(const Geometry &g, double tol) const {
        const planex2d::Curve<double, Vertex> f = Curve(g);
        const double end = f.params.back();
        const auto at = [&](double u) { return f.At(u > end ? u - end : u, 0); };
        const std::vector<std::pair<double, Vertex>> nodes = Nodes(g, f);
        std::vector<Vertex> out{nodes.front().second};
        for (size_t k = 0; k + 1 < nodes.size(); ++k)
            AddDrawn(at, nodes[k], nodes[k + 1], tol, out);
        return out;
    }

    inline CurveEval Element::Evaluator(const Geometry &g) const {
        CurveEval e{Curve(g), 0.0, 0.0};
        if (kind == ElementKind::OffsetCurve) {
            e.side = g.offsetCurves[OffsetCurveIndex()].side;
            e.d = g.values[g.offsetCurves[OffsetCurveIndex()].distance];
        }
        return e;
    }
    inline SplineSpan Element::Span(const Geometry &g) const {
        const CurveEval e = Evaluator(g);
        if (kind == ElementKind::OffsetCurve) {
            const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
            const auto [from, to] = c.Range(g, e.f);
            return {c.range, IsClosed(g), e, from, to, e.f.params.back()};
        }
        const auto [from, to] = Range(g, e.f);
        return {IsPiece(g) ? *Piece(g) : SplinePiece{}, g.splines[SplineIndex()].closed && !IsPiece(g), e, from, to, e.f.params.back()};
    }
    inline OffsetRun Element::Run(const Geometry &g) const {
        const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
        const CurveEval e = Evaluator(g);
        const std::array<double, 2> range = c.Range(g, e.f);
        return {e, c.reversed, range[0], range[1], e.f.params.back()};
    }
    inline std::vector<Vertex> Element::OffsetCurvePath(const Geometry &g, double tol) const {
        const OffsetRun run = Run(g);
        const auto at = [&](double w) { return run.At(w); };
        const std::vector<std::pair<double, Vertex>> nodes = OffsetNodes(g, run);
        std::vector<Vertex> out{nodes.front().second};
        for (size_t k = 0; k + 1 < nodes.size(); ++k)
            AddDrawn(at, nodes[k], nodes[k + 1], tol, out);
        return out;
    }

    inline void Element::AddPointValues(const Geometry &g, size_t point, std::vector<size_t> &out) {
        out.push_back(g.coordsOf[point]);
        out.push_back(g.coordsOf[point] + 1);
    }
    inline void Element::AddUsed(std::array<size_t, 2> ends, std::vector<size_t> &out) {
        for (size_t u : ends)
            if (u != none)
                out.push_back(u);
    }
    inline void Element::AddPointNames(const std::string &part, Names &out) {
        out.push_back({part, "x"});
        out.push_back({part, "y"});
    }
    inline void Element::AddRangeNames(const SplinePiece &range, Names &out) {
        if (range.from != none)
            out.push_back({"", "from"});
        if (range.to != none)
            out.push_back({"", "to"});
    }

    template <> inline void Element::AddValues<ElementKind::Point>(const Geometry &g, std::vector<size_t> &out) const { AddPointValues(g, Point(), out); }
    template <> inline void Element::AddValues<ElementKind::Line>(const Geometry &g, std::vector<size_t> &out) const {
        AddPointValues(g, Start(), out);
        AddPointValues(g, End(), out);
    }
    template <> inline void Element::AddValues<ElementKind::Circle>(const Geometry &g, std::vector<size_t> &out) const {
        AddPointValues(g, Center(), out);
        out.push_back(Radius());
    }
    template <> inline void Element::AddValues<ElementKind::Arc>(const Geometry &g, std::vector<size_t> &out) const {
        AddPointValues(g, Center(), out);
        out.push_back(Radius());
        AddPointValues(g, Start(), out);
        AddPointValues(g, End(), out);
    }
    template <> inline void Element::AddValues<ElementKind::Axis>(const Geometry &, std::vector<size_t> &out) const {
        out.insert(out.end(), {AxisAngle(), AxisDistance()});
    }
    template <> inline void Element::AddValues<ElementKind::Pattern>(const Geometry &, std::vector<size_t> &out) const {
        out.push_back(Step());
        if (HasStep2())
            out.push_back(Step2());
    }
    template <> inline void Element::AddValues<ElementKind::Offset>(const Geometry &, std::vector<size_t> &out) const { out.push_back(Distance()); }
    template <> inline void Element::AddValues<ElementKind::Spline>(const Geometry &g, std::vector<size_t> &out) const {
        const SplineData &d = g.splines[SplineIndex()];
        for (size_t point : d.points)
            AddPointValues(g, point, out);
        out.insert(out.end(), d.tangent.begin(), d.tangent.end());
        for (size_t c : d.curvature)
            if (c != none)
                out.push_back(c);
        if (d.fitted) {
            AddPointValues(g, d.fitted->origin, out);
            out.push_back(d.fitted->angle);
        }
        if (d.blend)
            for (size_t arm : d.blend->arms) {
                const std::vector<size_t> theirs = g.elements[arm].Values(g);
                out.insert(out.end(), theirs.begin(), theirs.end());
            }
        if (const SplinePiece *piece = Piece(g))
            AddUsed({piece->from, piece->to}, out);
    }
    template <> inline void Element::AddValues<ElementKind::OffsetCurve>(const Geometry &g, std::vector<size_t> &out) const {
        const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
        Element{ElementKind::Spline, {c.spline, c.piece}}.AddValues<ElementKind::Spline>(g, out);
        out.push_back(c.distance);
        AddUsed({c.range.from, c.range.to}, out);
    }

    template <> inline void Element::AddValueNames<ElementKind::Point>(const Geometry &, Names &out) const { AddPointNames("", out); }
    template <> inline void Element::AddValueNames<ElementKind::Line>(const Geometry &, Names &out) const {
        AddPointNames(".start", out);
        AddPointNames(".end", out);
    }
    template <> inline void Element::AddValueNames<ElementKind::Circle>(const Geometry &, Names &out) const {
        AddPointNames(".center", out);
        out.push_back({"", "r"});
    }
    template <> inline void Element::AddValueNames<ElementKind::Arc>(const Geometry &, Names &out) const {
        AddPointNames(".center", out);
        out.push_back({"", "r"});
        AddPointNames(".start", out);
        AddPointNames(".end", out);
    }
    template <> inline void Element::AddValueNames<ElementKind::Axis>(const Geometry &, Names &out) const {
        out.insert(out.end(), {{"", "angle"}, {"", "d"}});
    }
    template <> inline void Element::AddValueNames<ElementKind::Pattern>(const Geometry &, Names &out) const {
        out.push_back({"", "step"});
        if (HasStep2())
            out.push_back({"", "step2"});
    }
    template <> inline void Element::AddValueNames<ElementKind::Offset>(const Geometry &, Names &out) const { out.push_back({"", "d"}); }
    template <> inline void Element::AddValueNames<ElementKind::Spline>(const Geometry &g, Names &out) const {
        const SplineData &d = g.splines[SplineIndex()];
        for (size_t k = 0; k < d.points.size(); ++k)
            AddPointNames(!d.fitted && !d.blend ? ".p" + std::to_string(k) : k == 0 ? ".start" : ".end", out);
        if (d.fitted) {
            AddPointNames(".origin", out);
            out.push_back({"", "angle"});
        }
        if (d.blend)
            for (size_t i = 0; i < 2; ++i)
                for (const auto &[part, coordinate] : g.elements[d.blend->arms[i]].ValueNames(g))
                    out.push_back({".arm" + std::to_string(i + 1) + part, coordinate});
        for (size_t k : d.tangentAt)
            out.push_back({".p" + std::to_string(k), "angle"});
        for (size_t k = 0; k < d.curvature.size(); ++k)
            if (d.curvature[k] != none)
                out.push_back({".p" + std::to_string(k), "curvature"});
        if (const SplinePiece *piece = Piece(g))
            AddRangeNames(*piece, out);
    }
    template <> inline void Element::AddValueNames<ElementKind::OffsetCurve>(const Geometry &g, Names &out) const {
        const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
        Names source;
        Element{ElementKind::Spline, {c.spline, c.piece}}.AddValueNames<ElementKind::Spline>(g, source);
        for (const auto &[part, coordinate] : source)
            out.push_back({".source" + part, coordinate});
        out.push_back({".source", "d"});
        AddRangeNames(c.range, out);
    }

    inline std::array<double, 2> OffsetCurveData::Range(const Geometry &g, const Curve<double, Vertex> &f) const {
        const SplinePiece *source = piece ? &g.pieces[piece - 1] : nullptr;
        const double end = f.params.back(), from = RangeEnd(g, range.from, source ? source->from : none, 0.0);
        const double to = RangeEnd(g, range.to, source ? source->to : none, end);
        return {from, to < from ? to + end : to};
    }
    inline Vertex OffsetCurveData::Point(const Geometry &g, const Curve<double, Vertex> &f, double t) const {
        const Vertex d1 = f.At(t, 1);
        return f.At(t, 0) + Vertex{-d1.y, d1.x} * (side * g.values[distance] / Norm(d1));
    }
    inline double OffsetCurveData::RangeEnd(const Geometry &g, size_t mine, size_t inherited, double otherwise) {
        return mine != none ? g.values[mine] : inherited != none ? g.values[inherited] : otherwise;
    }

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

    inline double SplineData::BlendSense(const Geometry &g, size_t i) const {
        const Element &arm = g.elements[blend->arms[i]];
        const bool atEnd = g.coordsOf[arm.Ends(g)[1]] == g.coordsOf[points[i]];
        const double travel = arm.kind == ElementKind::Arc && !arm.ccw ? -1.0 : 1.0;
        return atEnd == (i == 0) ? travel : -travel;
    }

}
