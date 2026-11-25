// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/sketch/curveformula.hpp"

namespace planex2d::sketch {

    inline constexpr size_t none = (size_t)-1;

    class Geometry;

    struct SplinePiece {
        size_t from = none, to = none, fromPoint = none, toPoint = none;
    };

    // origin: the frame's (0, 0); angle: the turn of its h axis; mirrored: its v axis turned over first; scale: what the
    // sketch was scaled by since, applied to each refit
    struct FittedShape {
        Curve<double, Vertex> shape;
        size_t origin = none, angle = none;
        bool mirrored = false;
        double scale = 1.0;
        std::optional<CurveFormula> formula;
        size_t copyOf = none;

        void Reshape(Curve<double, Vertex> c) {
            shape = std::move(c);
            Rescale(scale);
        }
        // the shape times f, its parameter too (it runs over about the curve's length)
        void Rescale(double f) {
            for (Vertex &q : shape.poles)
                q = q * f;
            for (double &k : shape.knots)
                k *= f;
            for (double &u : shape.params)
                u *= f;
        }
        Vertex Placed(const Vertex &q, const Vertex &o, double a) const {
            const double c = std::cos(a), s = std::sin(a), y = mirrored ? -q.y : q.y;
            return {o.x + c * q.x - s * y, o.y + s * q.x + c * y};
        }
        Curve<double, Vertex> PlacedAt(const Vertex &o, double a) const {
            Curve<double, Vertex> f = shape;
            for (Vertex &q : f.poles)
                q = Placed(q, o, a);
            return f;
        }
    };

    // from an end of element arms[0] to an end of arms[1]; curvature: a quintic matching their curvatures too, else a cubic;
    // helper: the hidden arc of a curvature fillet, none for Connect
    struct BlendShape {
        std::array<size_t, 2> arms{};
        size_t helper = none;
        bool curvature = true;
    };

    // a closed one is periodic
    struct PoleShape {
        std::vector<double> knots;
        size_t degree = 3;
    };

    struct SplineData {
        std::vector<size_t> points, tangentAt, tangent;
        bool closed = false;
        SplineStyle style = SplineStyle::Averaged;
        std::optional<FittedShape> fitted;
        std::optional<BlendShape> blend;
        std::optional<PoleShape> poled;
        std::vector<double> tension;
        std::vector<size_t> curvature;

        Curve<double, Vertex> Interpolated(const Geometry &g) const;
        double TensionAt(size_t k) const { return tension.empty() ? 1.0 : tension[k]; }
        size_t CurvatureAt(size_t k) const { return curvature.empty() ? none : curvature[k]; }
        // a blend's way at its point i along the direction (DirectionAt) of the element it joins there: +1 the same,
        // -1 the other; into the blend at its start, out of it at its end
        double BlendSense(const Geometry &g, size_t i) const;
        std::vector<Vertex> Points(const Geometry &g) const;
    };

}
