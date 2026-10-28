// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "core/vertex.hpp"

namespace planex2d::cs {

    // the most unknowns one constraint depends on: a spline of 60 points and a line
    constexpr size_t maxLocal = 128;
    // the unknowns of the constraint being evaluated: the derivatives in use, the rest is never read
    inline thread_local size_t active = 0;

    // a value and its derivatives with respect to the unknowns of one constraint (forward mode): a dual number
    struct Dual {
        double v = 0.0;
        std::array<double, maxLocal> g;
        Dual() { std::fill_n(g.begin(), active, 0.0); }
        Dual(double value) : v(value) { std::fill_n(g.begin(), active, 0.0); }
    };

    inline Dual operator+(const Dual &a, const Dual &b) {
        Dual r(a.v + b.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = a.g[i] + b.g[i];
        return r;
    }
    inline Dual operator-(const Dual &a, const Dual &b) {
        Dual r(a.v - b.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = a.g[i] - b.g[i];
        return r;
    }
    inline Dual operator-(const Dual &a) {
        Dual r(-a.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = -a.g[i];
        return r;
    }
    inline Dual operator*(const Dual &a, const Dual &b) {
        Dual r(a.v * b.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = a.g[i] * b.v + a.v * b.g[i];
        return r;
    }
    inline Dual operator/(const Dual &a, const Dual &b) {
        Dual r(a.v / b.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = (a.g[i] - r.v * b.g[i]) / b.v;
        return r;
    }
    inline Dual Sqrt(const Dual &a) {
        Dual r(std::sqrt(a.v));
        for (size_t i = 0; i < active; ++i)
            r.g[i] = a.g[i] / (2.0 * r.v);
        return r;
    }
    inline Dual Sin(const Dual &a) {
        Dual r(std::sin(a.v));
        const double c = std::cos(a.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = c * a.g[i];
        return r;
    }
    inline Dual Cos(const Dual &a) {
        Dual r(std::cos(a.v));
        const double s = -std::sin(a.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = s * a.g[i];
        return r;
    }
    inline double ValueOf(const Dual &d) { return d.v; }
    // zero with all its derivatives: a dual number that can be left out of a sum or a product
    inline bool ExactlyZero(const Dual &d) {
        if (d.v != 0.0)
            return false;
        for (size_t i = 0; i < active; ++i)
            if (d.g[i] != 0.0)
                return false;
        return true;
    }
    inline Dual Atan2(const Dual &y, const Dual &x) {
        Dual r(std::atan2(y.v, x.v));
        const double q = x.v * x.v + y.v * y.v;
        for (size_t i = 0; i < active; ++i)
            r.g[i] = (x.v * y.g[i] - y.v * x.g[i]) / q;
        return r;
    }
    inline Dual Tan(const Dual &a) {
        Dual r(std::tan(a.v));
        const double s = 1.0 + r.v * r.v;
        for (size_t i = 0; i < active; ++i)
            r.g[i] = s * a.g[i];
        return r;
    }
    inline Dual Asin(const Dual &a) {
        Dual r(std::asin(a.v));
        const double s = 1.0 / std::sqrt(1.0 - a.v * a.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = s * a.g[i];
        return r;
    }
    inline Dual Acos(const Dual &a) {
        Dual r(std::acos(a.v));
        const double s = -1.0 / std::sqrt(1.0 - a.v * a.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = s * a.g[i];
        return r;
    }
    // f(a) by the chain rule, f1 the derivative of f at a's value
    inline Dual Chained(const Dual &a, double f, double f1) {
        Dual r(f);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = f1 * a.g[i];
        return r;
    }
    inline Dual Exp(const Dual &a) {
        const double e = std::exp(a.v);
        return Chained(a, e, e);
    }
    inline Dual Log(const Dual &a) { return Chained(a, std::log(a.v), 1.0 / a.v); }
    inline Dual Sinh(const Dual &a) { return Chained(a, std::sinh(a.v), std::cosh(a.v)); }
    inline Dual Cosh(const Dual &a) { return Chained(a, std::cosh(a.v), std::sinh(a.v)); }
    inline Dual Tanh(const Dual &a) {
        const double t = std::tanh(a.v);
        return Chained(a, t, 1.0 - t * t);
    }
    inline Dual Asinh(const Dual &a) { return Chained(a, std::asinh(a.v), 1.0 / std::sqrt(1.0 + a.v * a.v)); }
    inline Dual Acosh(const Dual &a) { return Chained(a, std::acosh(a.v), 1.0 / std::sqrt(a.v * a.v - 1.0)); }
    inline Dual Atanh(const Dual &a) { return Chained(a, std::atanh(a.v), 1.0 / (1.0 - a.v * a.v)); }
    inline Dual Abs(const Dual &a) { return a.v < 0.0 ? -a : a; }
    inline Dual AbsOf(const Dual &a) { return Abs(a); }
    inline Dual Min(const Dual &a, const Dual &b) { return b.v < a.v ? b : a; }
    inline Dual Max(const Dual &a, const Dual &b) { return b.v > a.v ? b : a; }
    // a derivative term only where its argument changes: log a is no number for a <= 0, a^(b-1) none at a = 0, b < 1
    inline Dual Pow(const Dual &a, const Dual &b) {
        Dual r(std::pow(a.v, b.v));
        const double da = b.v * std::pow(a.v, b.v - 1.0), db = r.v * std::log(a.v);
        for (size_t i = 0; i < active; ++i)
            r.g[i] = (a.g[i] != 0.0 ? da * a.g[i] : 0.0) + (b.g[i] != 0.0 ? db * b.g[i] : 0.0);
        return r;
    }
    // it depends on the unknowns
    inline bool IsVarying(const Dual &a) {
        for (size_t i = 0; i < active; ++i)
            if (a.g[i] != 0.0)
                return true;
        return false;
    }

    struct DualVector {
        Dual x, y;
    };
    inline DualVector operator+(const DualVector &a, const DualVector &b) { return {a.x + b.x, a.y + b.y}; }
    inline DualVector operator-(const DualVector &a, const DualVector &b) { return {a.x - b.x, a.y - b.y}; }
    inline DualVector operator*(const DualVector &a, const Dual &s) { return {a.x * s, a.y * s}; }
    inline Dual Dot(const DualVector &a, const DualVector &b) { return a.x * b.x + a.y * b.y; }
    inline Dual Cross(const DualVector &a, const DualVector &b) { return a.x * b.y - a.y * b.x; }
    inline Dual Norm(const DualVector &a) { return Sqrt(Dot(a, a)); }
    inline DualVector Unit(const DualVector &a) {
        const Dual n = Norm(a);
        return {a.x / n, a.y / n};
    }
    inline DualVector Left(const DualVector &a) { return {-a.y, a.x}; }
    inline DualVector Right(const DualVector &a) { return {a.y, -a.x}; }
    // u turned by degrees
    inline DualVector Turned(const DualVector &u, double degrees) {
        const double t = Radians(degrees), ct = std::cos(t), st = std::sin(t);
        return {u.x * Dual(ct) - u.y * Dual(st), u.x * Dual(st) + u.y * Dual(ct)};
    }
    inline DualVector TurnedRadians(const DualVector &u, const Dual &t) {
        const Dual ct = Cos(t), st = Sin(t);
        return {u.x * ct - u.y * st, u.x * st + u.y * ct};
    }
    // the sign of q's value, 1 for 0
    inline double Side(const Dual &q) { return q.v < 0.0 ? -1.0 : 1.0; }

    // the same functions of a plain value: the evaluation of an expression runs with double and Dual alike
    inline double ValueOf(double x) { return x; }
    inline bool IsVarying(double) { return false; }
    inline double Sqrt(double x) { return std::sqrt(x); }
    inline double Sin(double x) { return std::sin(x); }
    inline double Cos(double x) { return std::cos(x); }
    inline double Tan(double x) { return std::tan(x); }
    inline double Asin(double x) { return std::asin(x); }
    inline double Acos(double x) { return std::acos(x); }
    inline double Atan2(double y, double x) { return std::atan2(y, x); }
    inline double Abs(double x) { return std::abs(x); }
    inline double Min(double a, double b) { return std::min(a, b); }
    inline double Max(double a, double b) { return std::max(a, b); }
    inline double Pow(double a, double b) { return std::pow(a, b); }
    inline double Exp(double x) { return std::exp(x); }
    inline double Log(double x) { return std::log(x); }
    inline double Sinh(double x) { return std::sinh(x); }
    inline double Cosh(double x) { return std::cosh(x); }
    inline double Tanh(double x) { return std::tanh(x); }
    inline double Asinh(double x) { return std::asinh(x); }
    inline double Acosh(double x) { return std::acosh(x); }
    inline double Atanh(double x) { return std::atanh(x); }

}
