// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "core/bbox.hpp"
#include "core/config.hpp"
#include "curves/primitivefit.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::sketch {

    inline size_t Element::Spline(const Geometry &g) const { return kind == ElementKind::OffsetCurve ? g.offsetCurves[OffsetCurveIndex()].spline : SplineIndex(); }
    inline std::string Element::KindName(const Geometry &g) const { return IsFitted(g) ? "Curve" : ElementKindName(kind); }
    inline bool Element::IsFitted(const Geometry &g) const { return kind == ElementKind::Spline && g.splines[SplineIndex()].fitted.has_value(); }
    inline const SplinePiece *Element::Piece(const Geometry &g) const {
        return kind == ElementKind::Spline && PieceIndex() ? &g.pieces[PieceIndex() - 1] : nullptr;
    }
    inline std::array<size_t, 2> Element::Ends(const Geometry &g) const {
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
    inline std::array<Vertex, 2> Element::EndCoords(const Geometry &g) const {
        const std::array<size_t, 2> ends = Ends(g);
        return {g.Coords(ends[0]), g.Coords(ends[1])};
    }
    inline bool Element::IsClosed(const Geometry &g) const {
        const std::array<size_t, 2> ends = Ends(g);
        return ends[0] == ends[1];
    }
    inline std::vector<size_t> Element::PointsOn(const Geometry &g) const {
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
    inline std::vector<size_t> Element::PointsAndCentre(const Geometry &g) const {
        std::vector<size_t> out = PointsOn(g);
        if (kind == ElementKind::Arc || kind == ElementKind::Circle)
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
    inline double Element::Param(const Geometry &g, const Vertex &x) const {
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
    inline Vertex Element::At(const Geometry &g, double t) const {
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
    inline Vertex Element::DirectionAt(const Geometry &g, size_t point) const {
        if (kind == ElementKind::Line)
            return g.Coords(End()) - g.Coords(Start());
        if (kind == ElementKind::Spline) {
            const planex2d::Curve<double, Vertex> f = Curve(g);
            return f.At(SplineParamAt(g, f, point), 1);
        }
        const Vertex r = g.Coords(point) - g.Coords(Center());
        return {-r.y, r.x};
    }
    inline double Element::CurvatureAt(const Geometry &g, size_t point) const {
        if (kind == ElementKind::Line)
            return 0.0;
        if (kind == ElementKind::Arc || kind == ElementKind::Circle)
            return 1.0 / g.values[Radius()];
        const planex2d::Curve<double, Vertex> f = Curve(g);
        const double u = SplineParamAt(g, f, point);
        const Vertex d1 = f.At(u, 1), d2 = f.At(u, 2);
        const double speed = Norm(d1);
        return Cross(d1, d2) / (speed * speed * speed);
    }
    inline double Element::SplineParamAt(const Geometry &g, const planex2d::Curve<double, Vertex> &f, size_t point) const {
        if (const SplinePiece *piece = Piece(g)) {
            if (piece->fromPoint != none && g.coordsOf[piece->fromPoint] == g.coordsOf[point])
                return g.values[piece->from];
            if (piece->toPoint != none && g.coordsOf[piece->toPoint] == g.coordsOf[point])
                return g.values[piece->to];
        }
        const SplineData &d = g.splines[SplineIndex()];
        for (size_t k = 0; k < d.points.size(); ++k)
            if (g.coordsOf[d.points[k]] == g.coordsOf[point])
                return d.fitted ? (k == 0 ? f.params.front() : f.params.back()) : f.params[k];
        return f.params.back();
    }
    inline Vertex Element::Origin(const Geometry &g) const {
        if (OriginPoint() != none)
            return g.Coords(OriginPoint());
        const double a = g.values[OriginAngle()];
        return g.Coords(Center()) + Vertex{std::cos(a), std::sin(a)} * g.values[Radius()];
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
    inline std::vector<std::pair<double, Vertex>> Element::Nodes(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const {
        const auto [from, to] = Range(g, f);
        const double end = f.params.back(), same = 1e-9 * end;
        const std::vector<Vertex> q = SplinePoints(g);
        const std::array<Vertex, 2> ends = EndCoords(g);
        std::vector<std::pair<double, Vertex>> out{{from, ends[0]}};
        for (size_t k = 0; k < f.params.size(); ++k)
            for (double u : {f.params[k], f.params[k] + end})
                if (u > from + same && u < to - same)
                    out.push_back({u, q[k % q.size()]});
        std::sort(out.begin(), out.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
        out.erase(std::unique(out.begin(), out.end(), [&](const auto &x, const auto &y) { return y.first - x.first <= same; }), out.end());
        out.push_back({to, ends[1]});
        return out;
    }
    inline std::vector<Vertex> Element::SplinePath(const Geometry &g, size_t perPiece) const {
        const planex2d::Curve<double, Vertex> f = Curve(g);
        const double end = f.params.back();
        const std::vector<std::pair<double, Vertex>> nodes = Nodes(g, f);
        std::vector<Vertex> out;
        for (size_t k = 0; k < nodes.size(); ++k) {
            out.push_back(nodes[k].second);
            if (k + 1 < nodes.size())
                for (size_t i = 1; i < perPiece; ++i) {
                    const double u = nodes[k].first + (nodes[k + 1].first - nodes[k].first) * (double)i / (double)perPiece;
                    out.push_back(f.At(u > end ? u - end : u, 0));
                }
        }
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
        const planex2d::Curve<double, Vertex> f = Curve(g);
        const auto [from, to] = Range(g, f);
        return {IsPiece(g) ? *Piece(g) : SplinePiece{}, g.splines[SplineIndex()].closed && !IsPiece(g), f, from, to, f.params.back()};
    }
    inline OffsetRun Element::Run(const Geometry &g) const {
        const OffsetCurveData &c = g.offsetCurves[OffsetCurveIndex()];
        const CurveEval e = Evaluator(g);
        const std::array<double, 2> range = c.Range(g, e.f);
        return {e, c.reversed, range[0], range[1]};
    }
    inline std::vector<std::pair<double, Vertex>> Element::OffsetNodes(const Geometry &g, const OffsetRun &run) const {
        const std::array<size_t, 2> ends = Ends(g);
        const double same = 1e-9 * run.g.f.params.back();
        std::vector<double> inner;
        for (double t : run.g.f.params)
            if (t > run.from + same && t < run.to - same)
                inner.push_back(run.reversed ? run.to - t : t - run.from);
        std::sort(inner.begin(), inner.end());
        std::vector<std::pair<double, Vertex>> out{{0.0, g.Coords(ends[0])}};
        for (double w : inner)
            out.push_back({w, run.At(w)});
        out.push_back({run.to - run.from, g.Coords(ends[1])});
        return out;
    }
    inline std::vector<Vertex> Element::OffsetCurvePath(const Geometry &g, size_t perPiece) const {
        const OffsetRun run = Run(g);
        const std::vector<std::pair<double, Vertex>> nodes = OffsetNodes(g, run);
        std::vector<Vertex> out;
        for (size_t k = 0; k < nodes.size(); ++k) {
            out.push_back(nodes[k].second);
            if (k + 1 < nodes.size())
                for (size_t i = 1; i < perPiece; ++i)
                    out.push_back(run.At(nodes[k].first + (nodes[k + 1].first - nodes[k].first) * (double)i / (double)perPiece));
        }
        return out;
    }
    inline std::vector<PrimitiveEdge> Element::OffsetCurveArcs(const Geometry &g, double *tolerance) const {
        const OffsetRun run = Run(g);
        std::vector<double> u;
        std::vector<Vertex> nodes;
        BBox box;
        for (const auto &[w, p] : OffsetNodes(g, run)) {
            u.push_back(w);
            nodes.push_back(p);
            box.Grow(p);
        }
        const double atol = config::state->spline_tol * Norm(box.Size());
        if (tolerance)
            *tolerance = atol;
        return planex2d::Approximated(run, u, nodes, atol);
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
        if (d.fitted) {
            AddPointValues(g, d.fitted->origin, out);
            out.push_back(d.fitted->angle);
        }
        // a blend's curve follows the elements it joins
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
        // a fitted spline's points are its ends; its frame's place and turn follow
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

    inline std::vector<size_t> Element::Values(const Geometry &g) const {
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
    inline Element::Names Element::ValueNames(const Geometry &g) const {
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
