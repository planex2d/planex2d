// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/curve.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/curves/spline.hpp"

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
    std::vector<Vertex> FitPoints(std::span<const PrimitiveEdge> chain, double spacing, size_t arcPieces = 2);
    std::optional<Edge> FitArc(const std::vector<PrimitiveEdge> &chain, double tol, double ignore);

}
