// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

#include "planex/core/config.hpp"
#include "planex/core/vector3.hpp"
#include "planex/core/vertex.hpp"

namespace planex2d {

    struct Axis {
        Vector3 origin;
        Vector3 direction{0.0, 0.0, 1.0};
    };

    inline std::optional<Vector3> InPlane(const Vector3 &unitNormal, const Vector3 &d) {
        const Vector3 u = d - unitNormal * Dot(d, unitNormal);
        // within 1e-6 rad of the normal the projection would be mostly rounding
        if (!(Norm(u) > 1e-6 * Norm(d)))
            return std::nullopt;
        return Normalized(u);
    }

    // always right-handed (yDirection = normal x xDirection), so a mirror in 3d reverses yDirection again
    class AxisSystem {
        private:
            Vector3 origin;
            Vector3 xDirection{1.0, 0.0, 0.0};
            Vector3 yDirection{0.0, 1.0, 0.0};
            Vector3 normal{0.0, 0.0, 1.0};

            void SetDirections(const Vector3 &n, const Vector3 &x) {
                normal = n;
                xDirection = x;
                yDirection = Cross(n, x);
            }

        public:
            AxisSystem() = default;
            AxisSystem(const Vector3 &origin, const Vector3 &normal, const Vector3 &x) : origin(origin) {
                if (!(Norm(normal) > 0.0))
                    throw std::invalid_argument("AxisSystem: the normal has no length");
                const Vector3 n = Normalized(normal);
                const std::optional<Vector3> u = InPlane(n, x);
                if (!u)
                    throw std::invalid_argument("AxisSystem: the x direction lies along the normal");
                SetDirections(n, *u);
            }

            const Vector3 &Origin() const { return origin; }
            const Vector3 &XDirection() const { return xDirection; }
            const Vector3 &YDirection() const { return yDirection; }
            const Vector3 &Normal() const { return normal; }

            Vector3 ToXYZ(const Vertex &uv) const { return origin + xDirection * uv.x + yDirection * uv.y; }
            Vertex ToHV(const Vector3 &p) const {
                const Vector3 d = p - origin;
                return {Dot(d, xDirection), Dot(d, yDirection)};
            }

            double Height(const Vector3 &p) const { return Dot(p - origin, normal); }
            // the least tolerance is the rounding of the point's own coordinates
            bool IsOn(const Vector3 &p, double tol = 0.0) const { return std::abs(Height(p)) <= std::max(tol, Tol(Norm(p) + Norm(origin))); }

            AxisSystem &Translate(const Vector3 &v) {
                origin = origin + v;
                return *this;
            }
            AxisSystem &Offset(double d) { return Translate(normal * d); }
            AxisSystem &Rotate(const Axis &axis, double degrees) {
                const double rad = Radians(degrees), c = std::cos(rad), s = std::sin(rad);
                const Vector3 k = Normalized(axis.direction);
                origin = axis.origin + Rotated(origin - axis.origin, k, c, s);
                SetDirections(Rotated(normal, k, c, s), Rotated(xDirection, k, c, s));
                return *this;
            }
            AxisSystem &Mirror(const AxisSystem &plane) {
                const Vector3 &m = plane.normal;
                origin = plane.origin + Mirrored(origin - plane.origin, m);
                SetDirections(Mirrored(normal, m), Mirrored(xDirection, m));
                return *this;
            }
            // the x and y directions mirrored, so the normal is the negative of its mirror image
            AxisSystem &MirrorKeepingCoordinates(const AxisSystem &plane) {
                const Vector3 &m = plane.normal;
                origin = plane.origin + Mirrored(origin - plane.origin, m);
                SetDirections(-Mirrored(normal, m), Mirrored(xDirection, m));
                return *this;
            }
            AxisSystem &Scale(const Vector3 &p, double s) {
                origin = p + (origin - p) * s;
                return *this;
            }

            bool IsClose(const AxisSystem &o, double distance = config::state->axes_distance, double angle = config::state->axes_angle) const {
                return Norm(origin - o.origin) <= distance && AngleBetween(normal, o.normal) <= angle &&
                       AngleBetween(xDirection, o.xDirection) <= angle;
            }
    };

    inline AxisSystem AxesAlong(const Vector3 &origin, const Vector3 &n) {
        const Vector3 u = Normalized(n);
        const std::optional<Vector3> x = InPlane(u, {1.0, 0.0, 0.0});
        return AxisSystem(origin, u, x ? *x : *InPlane(u, {0.0, 1.0, 0.0}));
    }

    AxisSystem SketchAxes(const AxisSystem &axes, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis,
                          const std::string &call);

    // the plane through a point with a normal, through two lines that meet or are parallel, a line and a point, three
    // points, or moved by d along its normal; origin and axis as SketchAxes takes them
    AxisSystem AxesThrough(const Vector3 &p, const Vector3 &n, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis);
    AxisSystem AxesThroughLines(const Axis &a, const Axis &b, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis);
    AxisSystem AxesThroughLineAndPoint(const Axis &l, const Vector3 &q, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis);
    AxisSystem AxesThroughPoints(const Vector3 &p1, const Vector3 &p2, const Vector3 &p3, const std::optional<Vector3> &origin,
                                 const std::optional<Vector3> &axis);
    AxisSystem AxesMoved(const AxisSystem &a, double d, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis);

}
