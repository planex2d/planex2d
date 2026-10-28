// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "core/vertex.hpp"
#include "curves/interpolate.hpp"
#include "sketch/curveformula.hpp"

namespace planex2d::sketch {

    inline constexpr size_t none = (size_t)-1;

    class Geometry;

    struct SplinePiece {
        size_t from = none, to = none, fromPoint = none, toPoint = none;
    };

    // a spline fitted to a curve (Curve): its shape in a frame of its own, placed by the point origin (the frame's (0, 0))
    // and the angle value angle (the turn of its h axis), its v axis turned over first if mirrored. formula: what it
    // refits from when a dimension in it changes (none for a callable's); copyOf: the spline whose shape a copy takes.
    // scale: what the sketch was scaled by since, applied to each refit
    struct FittedShape {
        Curve<double, Vertex> shape;
        size_t origin = none, angle = none;
        bool mirrored = false;
        double scale = 1.0;
        std::optional<CurveFormula> formula;
        size_t copyOf = none;

        // a new fit, at the scale the sketch has now
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

    // a spline computed from the elements it joins (Fillet with px.Curvature, Connect): from the spline's first point,
    // an end of element arms[0], to its second, an end of arms[1], matching their directions there and, if curvature,
    // their curvatures (a quintic, else a cubic). helper: the hidden arc of a curvature fillet, which sets where it
    // starts and ends; none for Connect
    struct BlendShape {
        std::array<size_t, 2> arms{};
        size_t helper = none;
        bool curvature = true;
    };

    struct SplineData {
        std::vector<size_t> points, tangentAt, tangent;
        bool closed = false;
        SplineStyle style = SplineStyle::Averaged;
        std::optional<FittedShape> fitted;
        std::optional<BlendShape> blend;

        Curve<double, Vertex> Interpolated(const Geometry &g) const;
        // a blend's way at its point i along the direction (DirectionAt) of the element it joins there: +1 the same,
        // -1 the other; into the blend at its start, out of it at its end
        double BlendSense(const Geometry &g, size_t i) const;
        // the points it runs through; of a fitted spline its ends and its shape's points at the knots between
        std::vector<Vertex> Points(const Geometry &g) const;
    };

}
