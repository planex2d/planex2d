// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>

namespace planex2d {

    // a point, a vector or a direction in 3d
    struct Vector3 {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        Vector3 operator+(const Vector3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vector3 operator-(const Vector3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vector3 operator-() const { return {-x, -y, -z}; }
        Vector3 operator*(double s) const { return {x * s, y * s, z * s}; }
        Vector3 operator/(double s) const { return {x / s, y / s, z / s}; }
        bool operator==(const Vector3 &o) const = default;
    };

    inline double Dot(const Vector3 &a, const Vector3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vector3 Cross(const Vector3 &a, const Vector3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    inline double Norm(const Vector3 &a) { return std::sqrt(Dot(a, a)); }
    inline Vector3 Normalized(const Vector3 &a) { return a * (1.0 / Norm(a)); }
    // the angle between two directions, in radians: atan2 stays exact for nearly equal directions, acos does not
    inline double AngleBetween(const Vector3 &a, const Vector3 &b) { return std::atan2(Norm(Cross(a, b)), Dot(a, b)); }

    // about the line through the origin with unit direction k (Rodrigues)
    inline Vector3 Rotated(const Vector3 &v, const Vector3 &k, double cosVal, double sinVal) {
        return v * cosVal + Cross(k, v) * sinVal + k * (Dot(k, v) * (1.0 - cosVal));
    }
    // reflected in the plane through the origin with unit normal n
    inline Vector3 Mirrored(const Vector3 &v, const Vector3 &n) { return v - n * (2.0 * Dot(v, n)); }

}
