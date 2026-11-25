// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>

namespace planex2d::cs {

    // a value with its first and second derivative in one parameter (a curve's t): the expression evaluator over it
    // gives a formula's point and its exact derivatives
    struct Jet {
        double v = 0.0, d = 0.0, dd = 0.0;
        Jet() = default;
        Jet(double value) : v(value) {}
        Jet(double value, double first, double second) : v(value), d(first), dd(second) {}
    };

    // f(a) by the chain rule, f1 and f2 the first and second derivative of f at a's value
    inline Jet Chained(const Jet &a, double f, double f1, double f2) { return {f, f1 * a.d, f2 * a.d * a.d + f1 * a.dd}; }

    inline Jet operator+(const Jet &a, const Jet &b) { return {a.v + b.v, a.d + b.d, a.dd + b.dd}; }
    inline Jet operator-(const Jet &a, const Jet &b) { return {a.v - b.v, a.d - b.d, a.dd - b.dd}; }
    inline Jet operator-(const Jet &a) { return {-a.v, -a.d, -a.dd}; }
    inline Jet operator*(const Jet &a, const Jet &b) { return {a.v * b.v, a.d * b.v + a.v * b.d, a.dd * b.v + 2.0 * a.d * b.d + a.v * b.dd}; }
    inline Jet operator/(const Jet &a, const Jet &b) {
        const double v = a.v / b.v, d = (a.d - v * b.d) / b.v;
        return {v, d, (a.dd - 2.0 * d * b.d - v * b.dd) / b.v};
    }

    inline double ValueOf(const Jet &a) { return a.v; }
    inline bool IsVarying(const Jet &a) { return a.d != 0.0 || a.dd != 0.0; }
    inline Jet Sqrt(const Jet &a) {
        const double r = std::sqrt(a.v);
        return Chained(a, r, 0.5 / r, -0.25 / (r * a.v));
    }
    inline Jet Sin(const Jet &a) { return Chained(a, std::sin(a.v), std::cos(a.v), -std::sin(a.v)); }
    inline Jet Cos(const Jet &a) { return Chained(a, std::cos(a.v), -std::sin(a.v), -std::cos(a.v)); }
    inline Jet Tan(const Jet &a) {
        const double t = std::tan(a.v), s = 1.0 + t * t;
        return Chained(a, t, s, 2.0 * t * s);
    }
    inline Jet Asin(const Jet &a) {
        const double q = 1.0 - a.v * a.v, s = 1.0 / std::sqrt(q);
        return Chained(a, std::asin(a.v), s, a.v * s / q);
    }
    inline Jet Acos(const Jet &a) {
        const double q = 1.0 - a.v * a.v, s = 1.0 / std::sqrt(q);
        return Chained(a, std::acos(a.v), -s, -a.v * s / q);
    }
    inline Jet Atan2(const Jet &y, const Jet &x) {
        const double q = x.v * x.v + y.v * y.v, n = x.v * y.d - y.v * x.d, dn = x.v * y.dd - y.v * x.dd, dq = 2.0 * (x.v * x.d + y.v * y.d);
        return {std::atan2(y.v, x.v), n / q, (dn * q - n * dq) / (q * q)};
    }
    inline Jet Exp(const Jet &a) {
        const double e = std::exp(a.v);
        return Chained(a, e, e, e);
    }
    inline Jet Log(const Jet &a) { return Chained(a, std::log(a.v), 1.0 / a.v, -1.0 / (a.v * a.v)); }
    inline Jet Sinh(const Jet &a) { return Chained(a, std::sinh(a.v), std::cosh(a.v), std::sinh(a.v)); }
    inline Jet Cosh(const Jet &a) { return Chained(a, std::cosh(a.v), std::sinh(a.v), std::cosh(a.v)); }
    inline Jet Tanh(const Jet &a) {
        const double t = std::tanh(a.v), s = 1.0 - t * t;
        return Chained(a, t, s, -2.0 * t * s);
    }
    inline Jet Asinh(const Jet &a) {
        const double q = 1.0 + a.v * a.v, s = 1.0 / std::sqrt(q);
        return Chained(a, std::asinh(a.v), s, -a.v * s / q);
    }
    inline Jet Acosh(const Jet &a) {
        const double q = a.v * a.v - 1.0, s = 1.0 / std::sqrt(q);
        return Chained(a, std::acosh(a.v), s, -a.v * s / q);
    }
    inline Jet Atanh(const Jet &a) {
        const double s = 1.0 / (1.0 - a.v * a.v);
        return Chained(a, std::atanh(a.v), s, 2.0 * a.v * s * s);
    }
    // the sign of the value, at 0 of the first derivative that is not 0
    inline Jet Abs(const Jet &a) { return a.v < 0.0 || (a.v == 0.0 && (a.d < 0.0 || (a.d == 0.0 && a.dd < 0.0))) ? -a : a; }
    inline Jet Min(const Jet &a, const Jet &b) { return b.v < a.v ? b : a; }
    inline Jet Max(const Jet &a, const Jet &b) { return b.v > a.v ? b : a; }
    // a constant power by its derivatives, so a^2 has them at a = 0; a varying one as exp(b log a)
    inline Jet Pow(const Jet &a, const Jet &b) {
        const double r = std::pow(a.v, b.v);
        if (IsVarying(b)) {
            const Jet g = b * Jet{std::log(a.v), a.d / a.v, (a.dd * a.v - a.d * a.d) / (a.v * a.v)};
            return {r, r * g.d, r * (g.dd + g.d * g.d)};
        }
        const double p = b.v;
        const double f1 = p == 0.0 ? 0.0 : p * std::pow(a.v, p - 1.0), f2 = p == 0.0 || p == 1.0 ? 0.0 : p * (p - 1.0) * std::pow(a.v, p - 2.0);
        return Chained(a, r, f1, f2);
    }

}
