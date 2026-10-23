// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/vertex.hpp"
#include "curves/interpolate.hpp"
#include "curves/primitiveedge.hpp"
#include "sketch/curveeval.hpp"
#include "sketch/splinedata.hpp"

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

        size_t Spline(const Geometry &g) const;
        const SplinePiece *Piece(const Geometry &g) const;
        bool IsPiece(const Geometry &g) const { return Piece(g) != nullptr; }
        std::array<size_t, 2> Ends(const Geometry &g) const;
        std::array<Vertex, 2> EndCoords(const Geometry &g) const;
        bool IsClosed(const Geometry &g) const;
        std::vector<size_t> PointsOn(const Geometry &g) const;
        std::vector<size_t> PointsAndCentre(const Geometry &g) const;
        std::vector<size_t> Values(const Geometry &g) const;
        Names ValueNames(const Geometry &g) const;
        // nothing of it can ever move: projected
        bool IsHeld(const Geometry &g) const;

        double Length(const Geometry &g) const;
        // a line's or axis's: a point on it and its unit direction
        std::pair<Vertex, Vertex> Carrier(const Geometry &g) const;
        // where x lies along a line (0 at its start, 1 at its end), an arc (in its direction) or a circle (a turn from h)
        double Param(const Geometry &g, const Vertex &x) const;
        Vertex At(const Geometry &g, double t) const;
        // a line's direction, an arc's or circle's radius at the point turned left
        Vertex DirectionAt(const Geometry &g, size_t point) const;
        Vertex Origin(const Geometry &g) const;

        // a spline's interpolation; an offset curve's is that of its spline
        planex2d::Curve<double, Vertex> Curve(const Geometry &g) const;
        SplineStyle Style(const Geometry &g) const;
        std::vector<Vertex> SplinePoints(const Geometry &g) const;
        Vertex TangentAtFraction(const Geometry &g, double t) const;
        // the chord lengths along its spline it runs between: a piece over a closed spline's seam ends beyond the end
        std::array<double, 2> Range(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const;
        // the points it runs through with their chord lengths, from its start to its end
        std::vector<std::pair<double, Vertex>> Nodes(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const;
        // every point it runs through among them exactly
        std::vector<Vertex> SplinePath(const Geometry &g, size_t perPiece = 16) const;
        // a spline, or an offset curve along its spline
        CurveEval Evaluator(const Geometry &g) const;
        SplineSpan Span(const Geometry &g) const;

        OffsetRun Run(const Geometry &g) const;
        // the places an offset curve runs through, along its run (w): its end points and its spline's points between
        std::vector<std::pair<double, Vertex>> OffsetNodes(const Geometry &g, const OffsetRun &run) const;
        // its ends and its places over its spline's points among them exactly
        std::vector<Vertex> OffsetCurvePath(const Geometry &g, size_t perPiece = 16) const;
        // an offset curve's arcs and lines within spline_tol (atol, if asked) of the exact curve, along its run
        std::vector<PrimitiveEdge> OffsetCurveArcs(const Geometry &g, double *tolerance = nullptr) const;

    private:
        static void AddPointValues(const Geometry &g, size_t point, std::vector<size_t> &out);
        static void AddUsed(std::array<size_t, 2> ends, std::vector<size_t> &out);
        template <ElementKind K> void AddValues(const Geometry &g, std::vector<size_t> &out) const;
        static void AddPointNames(const std::string &part, Names &out);
        static void AddRangeNames(const SplinePiece &range, Names &out);
        template <ElementKind K> void AddValueNames(const Geometry &g, Names &out) const;
    };

    // an element with its values: a circle's or arc's radius r, an axis's normal angle (degrees) and distance d from the
    // origin; 0 where it has none
    struct ElementValues {
        const Element &element;
        double r = 0.0, angle = 0.0, d = 0.0;
    };

}
