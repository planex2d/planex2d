// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/sketch/curveeval.hpp"
#include "planex/sketch/splinedata.hpp"

namespace planex2d::sketch {

    class Geometry;

    enum class ElementKind { Point, Line, Circle, Arc, Axis, Pattern, Spline, Offset, OffsetCurve };

    // an offset curve is a spline to the user
    inline const char *ElementKindName(ElementKind k) {
        constexpr const char *names[] = {"Point", "Line", "Circle", "Arc", "Axis", "Pattern", "Spline", "Offset", "Spline"};
        return names[(size_t)k];
    }

    struct Element {
        ElementKind kind;
        std::array<size_t, 4> p{};
        bool ccw = true;

        using Names = std::vector<std::pair<std::string, std::string>>;

        size_t Point() const { return p[0]; }
        size_t Center() const { return p[0]; }
        size_t Radius() const { return p[1]; }
        size_t Start() const { return kind == ElementKind::Line ? p[0] : p[2]; }
        size_t End() const { return kind == ElementKind::Line ? p[1] : p[3]; }
        // the circle's edge starts at OriginPoint, or where OriginAngle points if it has none
        size_t OriginAngle() const { return p[2]; }
        size_t OriginPoint() const { return p[3]; }
        // the axis is the line n.x = d with n = (cos a, sin a)
        size_t AxisAngle() const { return p[0]; }
        size_t AxisDistance() const { return p[1]; }
        size_t Step() const { return p[0]; }
        bool IsLinear() const { return p[1] != 0; }
        // none for a pattern of one direction
        size_t Step2() const { return p[2]; }
        bool HasStep2() const { return p[2] != none; }
        size_t Distance() const { return p[0]; }
        size_t SplineIndex() const { return p[0]; }
        // 0: the whole spline, else 1 + its index among the pieces
        size_t PieceIndex() const { return p[1]; }
        size_t OffsetCurveIndex() const { return p[0]; }
        void SetEnds(size_t start, size_t end) {
            p[kind == ElementKind::Line ? 0 : 2] = start;
            p[kind == ElementKind::Line ? 1 : 3] = end;
        }
        void SetPieceIndex(size_t piece) { p[1] = piece; }
        void SetOffsetCurveIndex(size_t curve) { p[0] = curve; }

        bool IsRound() const { return kind == ElementKind::Arc || kind == ElementKind::Circle; }
        bool IsEdge() const { return kind == ElementKind::Line || IsRound() || kind == ElementKind::Spline || kind == ElementKind::OffsetCurve; }
        bool HasEnds() const { return IsEdge() && kind != ElementKind::Circle; }

        size_t Spline(const Geometry &g) const;
        // its kind as the ids and sk.elements name it: a fitted spline is a Curve, a spline by its poles a BSpline
        std::string KindName(const Geometry &g) const;
        bool IsFitted(const Geometry &g) const;
        bool IsPoled(const Geometry &g) const;
        bool IsPeriodicBSpline(const Geometry &g) const;
        bool IsClosedSpline(const Geometry &g) const;
        bool IsBlend(const Geometry &g) const;
        const SplinePiece *Piece(const Geometry &g) const;
        bool IsPiece(const Geometry &g) const { return Piece(g) != nullptr; }
        std::array<size_t, 2> Ends(const Geometry &g) const;
        std::array<Vertex, 2> EndCoords(const Geometry &g) const;
        bool IsClosed(const Geometry &g) const;
        std::vector<size_t> PointsOn(const Geometry &g) const;
        std::vector<size_t> PointsAndCentre(const Geometry &g) const;
        std::vector<size_t> Values(const Geometry &g) const;
        Names ValueNames(const Geometry &g) const;
        bool IsHeld(const Geometry &g) const;

        double Length(const Geometry &g) const;
        std::pair<Vertex, Vertex> Carrier(const Geometry &g) const;
        // where x lies along a line (0 at its start, 1 at its end), an arc (in its direction) or a circle (a turn from h)
        double Param(const Geometry &g, const Vertex &x) const;
        Vertex At(const Geometry &g, double t) const;
        // a line's direction, an arc's or circle's radius at the point turned left, a spline's derivative there
        Vertex DirectionAt(const Geometry &g, size_t point) const;
        // signed along DirectionAt, a left turn positive
        double CurvatureAt(const Geometry &g, size_t point) const;
        Vertex Origin(const Geometry &g) const;
        double AxisShift(const Geometry &g, const Vertex &p) const;
        bool PassesThrough(const Geometry &g, const Vertex &p, double tol) const;

        planex2d::Curve<double, Vertex> Curve(const Geometry &g) const;
        SplineStyle Style(const Geometry &g) const;
        std::vector<Vertex> SplinePoints(const Geometry &g) const;
        Vertex TangentAtFraction(const Geometry &g, double t) const;
        // the chord lengths along its spline it runs between: a piece over a closed spline's seam ends beyond the end
        std::array<double, 2> Range(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const;
        std::vector<std::pair<double, Vertex>> Nodes(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const;
        // within tol of the curve, every point it runs through among them exactly
        std::vector<Vertex> SplinePath(const Geometry &g, double tol) const;
        CurveEval Evaluator(const Geometry &g) const;
        SplineSpan Span(const Geometry &g) const;

        OffsetRun Run(const Geometry &g) const;
        std::vector<std::pair<double, Vertex>> OffsetNodes(const Geometry &g, const OffsetRun &run) const;
        std::vector<Vertex> OffsetCurvePath(const Geometry &g, double tol) const;
        // within spline_tol (atol, if asked) of the exact curve
        std::vector<PrimitiveEdge> OffsetCurveArcs(const Geometry &g, double *tolerance = nullptr) const;

        EdgeList Edges(const Geometry &g, const std::string &id) const;
        std::vector<planex2d::Spline> SplineEdges(const Geometry &g) const;
        std::vector<planex2d::Spline> DirectedSplineEdges(const Geometry &g, bool along) const;
        EdgeList OffsetCurveEdges(const Geometry &g, const std::string &id) const;
        planex2d::Spline OffsetCurveSpline(const Geometry &g) const;

    private:
        double SplineParamAt(const Geometry &g, const planex2d::Curve<double, Vertex> &f, size_t point) const;
        static void AddPointValues(const Geometry &g, size_t point, std::vector<size_t> &out);
        static void AddUsed(std::array<size_t, 2> ends, std::vector<size_t> &out);
        template <ElementKind K> void AddValues(const Geometry &g, std::vector<size_t> &out) const;
        static void AddPointNames(const std::string &part, Names &out);
        static void AddRangeNames(const SplinePiece &range, Names &out);
        template <ElementKind K> void AddValueNames(const Geometry &g, Names &out) const;
    };

    // angle in degrees; 0 where it has none
    struct ElementValues {
        const Element &element;
        double r = 0.0, angle = 0.0, d = 0.0;
    };

}
