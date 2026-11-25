// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/axissystem.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace planex2d {

    AxisSystem SketchAxes(const AxisSystem &axes, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis,
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

    static double Scale(std::initializer_list<Vector3> points) {
        double s = 0.0;
        for (const Vector3 &p : points)
            s = std::max({s, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
        return s;
    }

    // a normal without a plane to follow: toward +z, in the xy plane toward +x, along y toward +y
    static Vector3 Upward(const Vector3 &n) {
        const Vector3 u = Normalized(n);
        constexpr double flat = 1e-12;
        const double sign = std::abs(u.z) > flat ? u.z : std::abs(u.x) > flat ? u.x : u.y;
        return sign < 0.0 ? -u : u;
    }

    // its origin and h by the rule of a sketch on a face: the global origin projected and the global x axis projected (y if
    // x lies along the normal), unless given
    AxisSystem AxesThrough(const Vector3 &p, const Vector3 &n, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis) {
        return SketchAxes(AxesAlong(p, n), origin.value_or(Vector3{0.0, 0.0, 0.0}), axis, "Plane");
    }

    AxisSystem AxesThroughLines(const Axis &a, const Axis &b, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis) {
        const double size = Scale({a.origin, b.origin});
        Vector3 n = Cross(a.direction, b.direction);
        if (Norm(n) <= 1e-12) {
            n = Cross(a.direction, b.origin - a.origin);
            if (Norm(n) <= Tol(size))
                throw std::invalid_argument("Plane(line, line): the lines lie on one line, they span no plane");
        } else if (std::abs(Dot(b.origin - a.origin, Normalized(n))) > Tol(size))
            throw std::invalid_argument("Plane(line, line): the lines neither meet nor are parallel");
        return AxesThrough(a.origin, Upward(n), origin, axis);
    }

    AxisSystem AxesThroughLineAndPoint(const Axis &l, const Vector3 &q, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis) {
        const Vector3 n = Cross(l.direction, q - l.origin);
        if (Norm(n) <= Tol(Scale({l.origin, q})))
            throw std::invalid_argument("Plane(line, point): the point lies on the line");
        return AxesThrough(l.origin, Upward(n), origin, axis);
    }

    AxisSystem AxesThroughPoints(const Vector3 &p1, const Vector3 &p2, const Vector3 &p3, const std::optional<Vector3> &origin,
                                 const std::optional<Vector3> &axis) {
        const double size = Scale({p1, p2, p3}), l = Norm(p2 - p1);
        const Vector3 n = Cross(p2 - p1, p3 - p1);
        if (!(l > Tol(size)) || Norm(n) / l <= Tol(size))
            throw std::invalid_argument("Plane(p1, p2, p3): the three points lie on one line");
        return AxesThrough(p1, Upward(n), origin, axis);
    }

    AxisSystem AxesMoved(const AxisSystem &a, double d, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis) {
        return SketchAxes(AxisSystem(a.Origin() + a.Normal() * d, a.Normal(), a.XDirection()), origin, axis, "Plane");
    }

}
