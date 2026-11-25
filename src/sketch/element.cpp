// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/geometry.hpp"

#include <cmath>
#include <numbers>

namespace planex2d::sketch {

    std::array<size_t, 2> Element::Ends(const Geometry &g) const {
        if (kind == ElementKind::Line || kind == ElementKind::Arc)
            return {Start(), End()};
        if (kind == ElementKind::Spline) {
            const SplineData &d = g.splines[SplineIndex()];
            const SplinePiece *piece = Piece(g);
            const size_t from = piece && piece->fromPoint != none ? piece->fromPoint : d.points.front();
            const size_t to = piece && piece->toPoint != none ? piece->toPoint : d.closed ? d.points.front() : d.points.back();
            return {from, to};
        }
        if (kind == ElementKind::OffsetCurve) {
            const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
            return c.reversed ? std::array<size_t, 2>{c.range.toPoint, c.range.fromPoint} : std::array<size_t, 2>{c.range.fromPoint, c.range.toPoint};
        }
        return {none, none};
    }

    std::vector<size_t> Element::PointsOn(const Geometry &g) const {
        if (kind == ElementKind::Spline) {
            std::vector<size_t> out = g.splines[SplineIndex()].points;
            if (const SplinePiece *piece = Piece(g))
                for (size_t q : {piece->fromPoint, piece->toPoint})
                    if (q != none)
                        out.push_back(q);
            return out;
        }
        if (kind == ElementKind::Line || kind == ElementKind::Arc || kind == ElementKind::OffsetCurve) {
            const std::array<size_t, 2> ends = Ends(g);
            return {ends[0], ends[1]};
        }
        return {};
    }

    double Element::Param(const Geometry &g, const Vertex &x) const {
        if (kind == ElementKind::Line) {
            const Vertex a = g.Coords(Start()), d = g.Coords(End()) - a;
            return Dot(x - a, d) / Dot(d, d);
        }
        const double turn = 2.0 * std::numbers::pi;
        const auto from = [&](double a, double b) { return std::fmod(std::fmod(b - a, turn) + turn, turn); };
        const Vertex c = g.Coords(Center());
        const double t = std::atan2(x.y - c.y, x.x - c.x);
        if (kind == ElementKind::Circle)
            return from(0.0, t) / turn;
        const Vertex a = g.Coords(Start()), b = g.Coords(End());
        const double ta = std::atan2(a.y - c.y, a.x - c.x), tb = std::atan2(b.y - c.y, b.x - c.x);
        return ccw ? from(ta, t) / from(ta, tb) : from(t, ta) / from(tb, ta);
    }

    Vertex Element::At(const Geometry &g, double t) const {
        if (kind == ElementKind::Line)
            return g.Coords(Start()) + (g.Coords(End()) - g.Coords(Start())) * t;
        const double turn = 2.0 * std::numbers::pi;
        const Vertex c = g.Coords(Center());
        const double r = g.values[Radius()];
        double angle = turn * t;
        if (kind == ElementKind::Arc) {
            const Vertex a = g.Coords(Start()), b = g.Coords(End());
            const double ta = std::atan2(a.y - c.y, a.x - c.x), tb = std::atan2(b.y - c.y, b.x - c.x);
            const auto from = [&](double x, double y) { return std::fmod(std::fmod(y - x, turn) + turn, turn); };
            angle = ccw ? ta + t * from(ta, tb) : ta - t * from(tb, ta);
        }
        return c + Vertex{std::cos(angle), std::sin(angle)} * r;
    }

    Vertex Element::DirectionAt(const Geometry &g, size_t point) const {
        if (kind == ElementKind::Line)
            return g.Coords(End()) - g.Coords(Start());
        if (kind == ElementKind::Spline) {
            const planex2d::Curve<double, Vertex> f = Curve(g);
            return f.At(SplineParamAt(g, f, point), 1);
        }
        if (kind == ElementKind::OffsetCurve) {
            const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
            const CurveEval e = Evaluator(g);
            const Vertex d = e.At(c.Range(g, e.f)[g.coordsOf[c.range.toPoint] == g.coordsOf[point]], 1);
            return c.reversed ? d * -1.0 : d;
        }
        const Vertex r = g.Coords(point) - g.Coords(Center());
        return {-r.y, r.x};
    }

    double Element::CurvatureAt(const Geometry &g, size_t point) const {
        if (kind == ElementKind::Line)
            return 0.0;
        if (IsRound())
            return 1.0 / g.values[Radius()];
        if (kind == ElementKind::OffsetCurve) {
            const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
            const CurveEval source{Curve(g)};
            const double u = c.Range(g, source.f)[g.coordsOf[c.range.toPoint] == g.coordsOf[point]];
            const Vertex d1 = source.At(u, 1);
            const double speed = Norm(d1), k = Cross(d1, source.At(u, 2)) / (speed * speed * speed);
            // offset by d to the left, a curve bending by k bends by k / (1 - d k)
            return k / (1.0 - g.values[c.distance] * c.side * k) * (c.reversed ? -1.0 : 1.0);
        }
        const planex2d::Curve<double, Vertex> f = Curve(g);
        return f.Curvature(SplineParamAt(g, f, point));
    }

    double Element::SplineParamAt(const Geometry &g, const planex2d::Curve<double, Vertex> &f, size_t point) const {
        if (const SplinePiece *piece = Piece(g)) {
            if (piece->fromPoint != none && g.coordsOf[piece->fromPoint] == g.coordsOf[point])
                return g.values[piece->from];
            if (piece->toPoint != none && g.coordsOf[piece->toPoint] == g.coordsOf[point])
                return g.values[piece->to];
        }
        const SplineData &d = g.splines[SplineIndex()];
        for (size_t k = 0; k < d.points.size(); ++k)
            if (g.coordsOf[d.points[k]] == g.coordsOf[point])
                return d.fitted || d.poled ? (k == 0 ? f.params.front() : f.params.back()) : f.params[k];
        return f.params.back();
    }

    // an arc: its whole circle
    bool Element::PassesThrough(const Geometry &g, const Vertex &p, double tol) const {
        if (kind == ElementKind::Line) {
            const Vertex a = g.Coords(Start()), d = g.Coords(End()) - a;
            const double t = Dot(p - a, d) / Dot(d, d);
            return std::abs(Cross(d, p - a)) <= tol * Norm(d) && t > -tol && t < 1.0 + tol;
        }
        if (IsRound())
            return std::abs(Norm(p - g.Coords(Center())) - g.values[Radius()]) <= tol;
        if (kind == ElementKind::Axis)
            return std::abs(AxisShift(g, p) - g.values[AxisDistance()]) <= tol;
        return false;
    }

    std::vector<size_t> Element::Values(const Geometry &g) const {
        std::vector<size_t> out;
        switch (kind) {
        case ElementKind::Point: AddValues<ElementKind::Point>(g, out); break;
        case ElementKind::Line: AddValues<ElementKind::Line>(g, out); break;
        case ElementKind::Circle: AddValues<ElementKind::Circle>(g, out); break;
        case ElementKind::Arc: AddValues<ElementKind::Arc>(g, out); break;
        case ElementKind::Axis: AddValues<ElementKind::Axis>(g, out); break;
        case ElementKind::Pattern: AddValues<ElementKind::Pattern>(g, out); break;
        case ElementKind::Spline: AddValues<ElementKind::Spline>(g, out); break;
        case ElementKind::Offset: AddValues<ElementKind::Offset>(g, out); break;
        case ElementKind::OffsetCurve: AddValues<ElementKind::OffsetCurve>(g, out); break;
        }
        return out;
    }

    Element::Names Element::ValueNames(const Geometry &g) const {
        Names out;
        switch (kind) {
        case ElementKind::Point: AddValueNames<ElementKind::Point>(g, out); break;
        case ElementKind::Line: AddValueNames<ElementKind::Line>(g, out); break;
        case ElementKind::Circle: AddValueNames<ElementKind::Circle>(g, out); break;
        case ElementKind::Arc: AddValueNames<ElementKind::Arc>(g, out); break;
        case ElementKind::Axis: AddValueNames<ElementKind::Axis>(g, out); break;
        case ElementKind::Pattern: AddValueNames<ElementKind::Pattern>(g, out); break;
        case ElementKind::Spline: AddValueNames<ElementKind::Spline>(g, out); break;
        case ElementKind::Offset: AddValueNames<ElementKind::Offset>(g, out); break;
        case ElementKind::OffsetCurve: AddValueNames<ElementKind::OffsetCurve>(g, out); break;
        }
        return out;
    }

}
