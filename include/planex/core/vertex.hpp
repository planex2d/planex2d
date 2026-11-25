// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace planex2d {

    // what comes from outside: nan or inf would spread through every computation
    inline void IsFinite(const char *what, std::initializer_list<double> values) {
        for (double v : values)
            if (!std::isfinite(v))
                throw std::invalid_argument(std::string(what) + ": a value is not finite (" + std::to_string(v) + ")");
    }

    constexpr double Radians(double degrees) { return degrees * std::numbers::pi / 180.0; }
    constexpr double Degrees(double radians) { return radians * 180.0 / std::numbers::pi; }

    struct Vertex {
        double x = 0.0;
        double y = 0.0;

        Vertex operator+(const Vertex &o) const { return {x + o.x, y + o.y}; }
        Vertex operator-(const Vertex &o) const { return {x - o.x, y - o.y}; }
        Vertex operator-() const { return {-x, -y}; }
        Vertex operator*(double s) const { return {x * s, y * s}; }
        Vertex operator/(double s) const { return {x / s, y / s}; }
        bool operator==(const Vertex &o) const = default;
        bool IsClose(const Vertex &o, double tol) const {
            double dx = x - o.x;
            double dy = y - o.y;
            return dx * dx + dy * dy <= tol * tol;
        }
    };

    inline double Dot(const Vertex &a, const Vertex &b) { return a.x * b.x + a.y * b.y; }
    inline double Cross(const Vertex &a, const Vertex &b) { return a.x * b.y - a.y * b.x; }
    inline double Norm(const Vertex &a) { return std::sqrt(Dot(a, a)); }
    inline Vertex Normalized(const Vertex &a) { return a * (1.0 / Norm(a)); }
    inline double NormInf(const Vertex &a) {
        double ax = std::abs(a.x);
        double ay = std::abs(a.y);
        return ax > ay ? ax : ay;
    }

    inline std::string NumberText(double v) {
        char buf[32];
        const auto r = std::to_chars(buf, buf + sizeof(buf), v);
        return std::string(buf, r.ptr);
    }
    inline std::string NumberText(double v, int digits) {
        char buf[32];
        const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general, digits);
        return std::string(buf, r.ptr);
    }
    // 12 significant digits: below them a solve's rounding would show
    inline std::string SolvedText(double v) { return NumberText(v, 12); }
    std::optional<double> NumberOf(std::string_view text);

    inline std::string PointText(const Vertex &p) { return "(" + SolvedText(p.x) + ", " + SolvedText(p.y) + ")"; }

    inline Vertex Rotated(const Vertex &p, const Vertex &c, double cosVal, double sinVal) {
        Vertex r = p - c;
        return c + Vertex{r.x * cosVal - r.y * sinVal, r.x * sinVal + r.y * cosVal};
    }

    Vertex Rotated(const Vertex &p, const Vertex &c, double deg);

    inline Vertex Rotated(const Vertex &p, double deg) { return Rotated(p, Vertex{}, deg); }

    // reflected in the line through a with unit direction d
    inline Vertex Mirrored(const Vertex &p, const Vertex &a, const Vertex &d) {
        Vertex r = p - a;
        return a + d * (2.0 * Dot(r, d)) - r;
    }
    inline Vertex Scaled(const Vertex &p, const Vertex &c, double s) { return c + (p - c) * s; }

}
