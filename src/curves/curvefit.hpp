// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <Geom2d_BSplineCurve.hxx>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/primitiveedge.hpp"
#include "curves/occspline.hpp"
#include "curves/spline.hpp"

namespace planex2d {

    // L * a / 8 is the sagitta over a chord of length L turning by a; / 6 leaves a third of slack
    inline std::vector<double> Bands(std::span<const PrimitiveEdge> chain, double tol) {
        std::vector<double> band(chain.size(), tol);
        for (size_t k = 0; k + 1 < chain.size(); ++k) {
            Vertex t0 = chain[k].EndTangent(), t1 = chain[k + 1].StartTangent();
            double a = std::atan2(std::abs(Cross(t0, t1)), Dot(t0, t1));
            band[k] = std::max(band[k], tol + chain[k].Length() * a / 6.0);
            band[k + 1] = std::max(band[k + 1], tol + chain[k + 1].Length() * a / 6.0);
        }
        return band;
    }
    // a sample every tol / 2, at least 8 per piece, at most 1000
    template <class F>
    bool WithinTol(F at, std::span<const PrimitiveEdge> chain, double tol) {
        std::vector<double> band = Bands(chain, tol);
        double length = 0.0;
        for (const PrimitiveEdge &e : chain)
            length += e.Length();
        size_t n = std::min<size_t>(8 * chain.size() + (size_t)std::ceil(2.0 * length / tol), 1000);
        for (size_t k = 0; k <= n; ++k) {
            Vertex q = at((double)k / (double)n);
            bool inside = false;
            for (size_t i = 0; i < chain.size() && !inside; ++i)
                inside = chain[i].Distance(q) <= band[i];
            if (!inside)
                return false;
        }
        return true;
    }
    inline bool Fits(const Handle(Geom2d_BSplineCurve) &c, std::span<const PrimitiveEdge> chain, double tol) {
        double u0 = c->FirstParameter(), u1 = c->LastParameter();
        if (!WithinTol([&](double s) { return OccPoint(c, u0 + (u1 - u0) * s); }, chain, tol))
            return false;
        return Dot(OccDerivative(c, u0), chain.front().StartTangent()) > 0.0 &&
               Dot(OccDerivative(c, u1), chain.back().EndTangent()) > 0.0;
    }
    inline std::vector<Vertex> FitPoints(std::span<const PrimitiveEdge> chain, double spacing, size_t arcPieces = 2) {
        std::vector<Vertex> pts{chain.front().Start()};
        auto add = [&](const Vertex &p) {
            if (Norm(p - pts.back()) >= spacing)
                pts.push_back(p);
        };
        for (const PrimitiveEdge &e : chain) {
            if (const Arc *a = std::get_if<Arc>(&e))
                for (size_t k = 1; k < arcPieces; ++k)
                    add(a->PointAt(a->Sweep() * (double)k / (double)arcPieces));
            add(e.End());
        }
        const Vertex &end = chain.back().End();
        if (!(pts.back() == end)) {
            if (pts.size() > 1)
                pts.pop_back();
            pts.push_back(end);
        }
        return pts;
    }
    inline std::optional<Edge> FitArc(const std::vector<PrimitiveEdge> &chain, double tol, double ignore) {
        std::vector<Vertex> pts = FitPoints(chain, ignore);
        if (pts.size() < 3)
            return std::nullopt;
        const Vertex &p0 = pts.front(), &p1 = pts.back();
        Vertex chord = p1 - p0;
        double l = Norm(chord);
        if (l <= tol)
            return std::nullopt;
        Vertex n{-chord.y / l, chord.x / l};
        const Properties &props = PropsOf(chain.front());
        // every chain edge's midpoint within its band of the curve: the edges are lines and arcs, which cannot stray
        // far from the curve between their ends and midpoint
        const std::vector<double> band = Bands(chain, tol);
        auto close = [&](auto distance) {
            for (size_t i = 0; i < chain.size(); ++i)
                if (distance(chain[i].MidPoint()) > band[i])
                    return false;
            return true;
        };
        double offset = 0.0, aa = 0.0, ab = 0.0;
        for (size_t k = 1; k + 1 < pts.size(); ++k) {
            Vertex d = pts[k] - p0;
            double h = Dot(d, n);
            if (std::abs(h) > std::abs(offset))
                offset = h;
            aa += 4.0 * h * h;
            ab += 2.0 * h * Dot(d, pts[k] - p1);
        }
        if (std::abs(offset) <= tol) {
            if (!close([&](const Vertex &m) { return std::abs(Dot(m - p0, n)); }))
                return std::nullopt;
            Line line(p0, p1);
            line.props = props;
            return Edge(line);
        }
        Arc arc(p0, p1, (p0 + p1) * 0.5 + n * (ab / aa), offset < 0.0);
        arc.props = props;
        // at most a half circle, with slack: the halves of a closed chain are exactly half circles
        if (arc.Sweep() > std::numbers::pi * (1.0 + 1e-9))
            return std::nullopt;
        for (size_t k = 1; k + 1 < pts.size(); ++k)
            if (std::abs(Norm(pts[k] - arc.Center()) - arc.Radius()) > tol || arc.Param(pts[k]) >= arc.Sweep())
                return std::nullopt;
        if (!close([&](const Vertex &m) { return std::abs(Norm(m - arc.Center()) - arc.Radius()); }))
            return std::nullopt;
        return Edge(arc);
    }

    inline std::vector<double> ChainParams(std::span<const PrimitiveEdge> chain) {
        std::vector<double> u{0.0};
        Vertex prev = chain.front().Start();
        for (const PrimitiveEdge &e : chain) {
            double l = 0.0;
            if (const Arc *a = std::get_if<Arc>(&e)) {
                l = Norm(a->MidPoint() - prev);
                prev = a->MidPoint();
            }
            l += Norm(e.End() - prev);
            prev = e.End();
            u.push_back(u.back() + l);
        }
        return u;
    }
    inline Handle(Geom2d_BSplineCurve) FittedCurve(std::span<const PrimitiveEdge> chain, const std::vector<Vertex> &pts, double tol,
                                                   const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent) {
        Handle(Geom2d_BSplineCurve) c = Approximate(pts, tol, startTangent, endTangent);
        if (!c.IsNull() && Fits(c, chain, tol))
            return c;
        std::map<size_t, Vertex> tangents;
        if (startTangent)
            tangents[0] = *startTangent;
        if (endTangent)
            tangents[pts.size() - 1] = *endTangent;
        double S = 0.0;
        for (const Vertex &p : pts)
            S = std::max(S, NormInf(p));
        c = InterpolateOcc(pts, tangents, false, Tol(S), false);
        return Fits(c, chain, tol) ? c : Handle(Geom2d_BSplineCurve)();
    }
    inline std::optional<Spline> Spline::Fitted(std::vector<PrimitiveEdge> chain, const std::vector<Vertex> &pts, double tol,
                                                const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent) {
        Handle(Geom2d_BSplineCurve) c = FittedCurve(chain, pts, tol, startTangent, endTangent);
        if (c.IsNull())
            return std::nullopt;
        const std::vector<double> u = ChainParams(chain);
        auto d = std::make_shared<SplineData>();
        d->curve = c;
        double u0 = c->FirstParameter(), u1 = c->LastParameter();
        for (size_t k = 0; k < u.size(); ++k) {
            d->params.push_back(u0 + (u1 - u0) * u[k] / u.back());
            d->nodes.emplace_back(d->params.back(), k == 0 ? chain.front().Start() : chain[k - 1].End());
        }
        Properties props = PropsOf(chain.front());
        d->approx = std::move(chain);
        for (PrimitiveEdge &e : d->approx)
            std::visit([](auto &x) {
                x.fromSpline = true;
                x.ends = nullptr;
            }, e);
        return Spline(d, 0, d->approx.size(), props);
    }

}
