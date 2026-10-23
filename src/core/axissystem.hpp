// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>

#include "core/config.hpp"
#include "core/vector3.hpp"
#include "core/vertex.hpp"

namespace planex2d {

    // a line in 3d: a point on it and its unit direction
    struct Axis {
        Vector3 origin;
        Vector3 direction{0.0, 0.0, 1.0};
    };

    // the part of d perpendicular to the unit normal n, as a unit vector; none if d lies along n: within 1e-6 rad the
    // projection would be mostly rounding
    inline std::optional<Vector3> InPlane(const Vector3 &n, const Vector3 &d) {
        const Vector3 u = d - n * Dot(d, n);
        if (!(Norm(u) > 1e-6 * Norm(d)))
            return std::nullopt;
        return Normalized(u);
    }

    // the plane of a sketch in 3d: its origin, its u and v directions and its normal, always right-handed
    // (v = normal x u), so a mirror in 3d reverses v again
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
            // the xy plane
            AxisSystem() = default;
            // x need not be perpendicular to the normal: its part along the normal is left out
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
            // p projected onto the plane
            Vertex ToHV(const Vector3 &p) const {
                const Vector3 d = p - origin;
                return {Dot(d, xDirection), Dot(d, yDirection)};
            }

            AxisSystem &Translate(const Vector3 &v) {
                origin = origin + v;
                return *this;
            }
            // along the normal
            AxisSystem &Offset(double d) { return Translate(normal * d); }
            AxisSystem &Rotate(const Axis &axis, double degrees) {
                const double rad = Radians(degrees), c = std::cos(rad), s = std::sin(rad);
                const Vector3 k = Normalized(axis.direction);
                origin = axis.origin + Rotated(origin - axis.origin, k, c, s);
                SetDirections(Rotated(normal, k, c, s), Rotated(xDirection, k, c, s));
                return *this;
            }
            // in the plane of another axis system (its origin and normal)
            AxisSystem &Mirror(const AxisSystem &plane) {
                const Vector3 &m = plane.normal;
                origin = plane.origin + Mirrored(origin - plane.origin, m);
                SetDirections(Mirrored(normal, m), Mirrored(xDirection, m));
                return *this;
            }
            // in the plane of another axis system, the coordinates in it kept: the x and y directions mirrored, so the
            // normal turns against its mirror image
            AxisSystem &MirrorKeepingCoordinates(const AxisSystem &plane) {
                const Vector3 &m = plane.normal;
                origin = plane.origin + Mirrored(origin - plane.origin, m);
                SetDirections(-Mirrored(normal, m), Mirrored(xDirection, m));
                return *this;
            }
            // about p: only the origin moves
            AxisSystem &Scale(const Vector3 &p, double s) {
                origin = p + (origin - p) * s;
                return *this;
            }

            // origins within distance, the normals and the x directions each within angle (radians)
            bool IsClose(const AxisSystem &o, double distance = config::axes_distance, double angle = config::axes_angle) const {
                return Norm(origin - o.origin) <= distance && AngleBetween(normal, o.normal) <= angle &&
                       AngleBetween(xDirection, o.xDirection) <= angle;
            }
    };

    // the x direction along the global x axis, else along the global y axis
    inline AxisSystem AxesAlong(const Vector3 &origin, const Vector3 &n) {
        const Vector3 u = Normalized(n);
        const std::optional<Vector3> x = InPlane(u, {1.0, 0.0, 0.0});
        return AxisSystem(origin, u, x ? *x : *InPlane(u, {0.0, 1.0, 0.0}));
    }

    // the plane of axes with its origin moved to origin (projected onto the plane) and its x direction along axis
    inline AxisSystem SketchAxes(const AxisSystem &axes, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis,
                                 const std::string &call) {
        const Vector3 &n = axes.Normal(), &p0 = axes.Origin();
        const Vector3 o = origin ? *origin - n * Dot(*origin - p0, n) : p0;
        Vector3 u = axes.XDirection();
        if (axis) {
            std::optional<Vector3> d = InPlane(n, *axis);
            if (!d)
                throw std::invalid_argument(call + ": the axis is perpendicular to the plane");
            u = *d;
        }
        return AxisSystem(o, n, u);
    }

}
