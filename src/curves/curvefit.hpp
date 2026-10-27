// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/curve.hpp"
#include "curves/primitiveedge.hpp"
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
    inline bool Fits(const Curve<double, Vertex> &c, std::span<const PrimitiveEdge> chain, double tol) {
        double u0 = c.First(), u1 = c.Last();
        if (!WithinTol([&](double s) { return c.At(u0 + (u1 - u0) * s); }, chain, tol))
            return false;
        return Dot(c.Derivative(u0), chain.front().StartTangent()) > 0.0 && Dot(c.Derivative(u1), chain.back().EndTangent()) > 0.0;
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

    // the fitted curve between the chain's two ends, its approximation within spline_tol of it as every spline's (the
    // chain's pieces are only within tol)
    inline std::optional<Spline> Spline::Fitted(const std::vector<PrimitiveEdge> &chain, const std::vector<Vertex> &pts, double tol,
                                                const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent) {
        if (!config::state->spline_fitter)
            throw std::logic_error("Spline::Fitted: no spline fit loaded");
        std::optional<Curve<double, Vertex>> c = config::state->spline_fitter(chain, pts, tol, startTangent, endTangent);
        if (!c)
            return std::nullopt;
        // a node in the middle: a closed curve's two ends are one point
        const double mid = 0.5 * (c->First() + c->Last());
        Spline s = Pieces(*c, {c->First(), mid, c->Last()}, {chain.front().Start(), c->At(mid), chain.back().End()}, ApproximationTol(pts)).front();
        s.props = PropsOf(chain.front());
        return s;
    }

}
