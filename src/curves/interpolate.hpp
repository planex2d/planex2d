// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/curve.hpp"

namespace planex2d {

    inline void CheckSpline(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic, double tol) {
        const auto point = [](const Vertex &p) { return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")"; };
        for (const Vertex &p : pts)
            IsFinite("Spline", {p.x, p.y});
        for (const auto &[i, t] : tangents)
            IsFinite("Spline tangent", {t.x, t.y});
        if (pts.size() < (periodic ? 3u : 2u))
            throw std::invalid_argument("Spline: " + std::to_string(pts.size()) + " points; a" + (periodic ? " periodic" : "") +
                                        " spline needs at least " + (periodic ? "3" : "2"));
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            if (Norm(pts[i + 1] - pts[i]) <= tol)
                throw std::invalid_argument("Spline: points " + std::to_string(i) + " and " + std::to_string(i + 1) + " coincide at " +
                                            point(pts[i]));
        if (periodic && Norm(pts.back() - pts.front()) <= tol)
            throw std::invalid_argument("Spline: the last point is the first; a periodic spline lists its start once");
        for (const auto &[i, t] : tangents) {
            if (i >= pts.size())
                throw std::invalid_argument("Spline: a tangent at point " + std::to_string(i) + ", but the points are 0 to " +
                                            std::to_string(pts.size() - 1));
            if (!(Norm(t) > 0.0))
                throw std::invalid_argument("Spline: the tangent at point " + std::to_string(i) + " is zero");
        }
    }

    // solves A x = b in place (partial pivoting by value); false if A is singular. A zero value is not skipped, as a dual
    // number it may still carry derivatives; an entry zero with all of them is (ExactlyZero), which leaves the band of
    // an interpolation's matrix to the work and changes no result
    template <class S, class P>
    bool SolveDense(std::vector<std::vector<S>> a, std::vector<P> &b) {
        const size_t n = a.size();
        // the end of row r: past its last entry that is not exactly zero, r + 1 at least
        const auto reach = [&](size_t r) {
            size_t k = n;
            while (k > r + 1 && ExactlyZero(a[r][k - 1]))
                --k;
            return k;
        };
        for (size_t c = 0; c < n; ++c) {
            size_t pivot = c;
            for (size_t r = c + 1; r < n; ++r)
                if (std::abs(ValueOf(a[r][c])) > std::abs(ValueOf(a[pivot][c])))
                    pivot = r;
            if (ValueOf(a[pivot][c]) == 0.0)
                return false;
            std::swap(a[c], a[pivot]);
            std::swap(b[c], b[pivot]);
            const size_t end = reach(c);
            for (size_t r = c + 1; r < n; ++r) {
                if (ExactlyZero(a[r][c]))
                    continue;
                const S f = a[r][c] / a[c][c];
                for (size_t k = c; k < end; ++k)
                    a[r][k] = a[r][k] - f * a[c][k];
                b[r] = b[r] - b[c] * f;
            }
        }
        for (size_t c = n; c-- > 0;) {
            P s = b[c];
            for (size_t r = c + 1, end = reach(c); r < end; ++r)
                s = s - b[r] * a[c][r];
            b[c] = s * (S(1.0) / a[c][c]);
        }
        return true;
    }

    inline double AbsOf(double v) { return std::abs(v); }

    // OCC's PLib::EvalLagrange, its order of operations kept: the value and first derivative at u of the polynomial of
    // degree through points[0..degree] at params[0..degree]
    template <class S, class P>
    std::pair<P, P> Lagrange(const S &u, size_t degree, const P *points, const S *params) {
        std::vector<P> dd(points, points + degree + 1);
        for (size_t i = degree + 1; i-- > 0;)
            for (size_t j = degree; j > degree - i; --j)
                dd[j] = (dd[j] - dd[j - 1]) * (S(1.0) / (params[j] - params[j + i - degree - 1]));
        P value = dd[degree], derivative = points[0] * S(0.0);
        for (size_t i = degree; i >= 1; --i) {
            const S d = u - params[i - 1];
            derivative = derivative * d + value;
            value = value * d + dd[i - 1];
        }
        return {value, derivative};
    }

    // OCC's Geom2dAPI_Interpolate (the style "clamped") as netgen calls it, its quirks kept: a point without a tangent
    // gets the first given one, the given tangents are scaled to the L1 norm of a Lagrange derivative (ScaleTangents),
    // an end without one gets the derivative of the cubic through its four points, a given tangent makes a double knot;
    // a periodic curve is C1 at its start only and comes here in a clamped form
    template <class S, class P, class L>
    Curve<S, P> InterpolateOcc(const std::vector<P> &points, const std::vector<size_t> &tangentAt, const std::vector<P> &tangents, bool closed,
                               L length) {
        const size_t n = points.size();
        if (n < (closed ? 3 : 2))
            throw std::invalid_argument(closed ? "Spline: a closed spline needs three points" : "Spline: two points at least");
        Curve<S, P> f;
        f.closed = closed;
        f.tangentAt = tangentAt;
        f.params.push_back(S(0.0));
        for (size_t k = 1; k <= (closed ? n : n - 1); ++k) {
            const S d = length(points[k % n] - points[k - 1]);
            if (!(ValueOf(d) > 0.0))
                throw std::invalid_argument("Spline: two following points at one place");
            f.params.push_back(f.params.back() + d);
        }
        const P zero = points[0] * S(0.0);
        const bool request = !tangentAt.empty();
        std::vector<P> t(n, zero);
        std::vector<char> given(n, 0);
        if (request) {
            size_t first = 0;
            for (size_t i = 0; i < tangentAt.size(); ++i) {
                if (tangentAt[i] >= n)
                    throw std::invalid_argument("Spline: a tangent at a point it does not have");
                if (tangentAt[i] < tangentAt[first])
                    first = i;
            }
            std::fill(t.begin(), t.end(), tangents[first]);
            for (size_t i = 0; i < tangentAt.size(); ++i) {
                t[tangentAt[i]] = tangents[i];
                given[tangentAt[i]] = 1;
            }
            const size_t degree = n == 2 ? 1 : 2;
            size_t from = 0;
            for (size_t i = 0; i < n; ++i) {
                if (!given[i])
                    continue;
                const P d = Lagrange(f.params[i], degree, &points[from], &f.params[from]).second;
                t[i] = t[i] * ((AbsOf(d.x) + AbsOf(d.y)) / (AbsOf(t[i].x) + AbsOf(t[i].y)));
                if (i != 0)
                    ++from;
                from = std::min(from, n - 1 - degree);
            }
        }
        struct Row {
            S u;
            size_t k;
            P data;
        };
        std::vector<Row> rows;
        if (!closed && n == 2 && !request) {
            f.degree = 1;
            f.knots = {f.params[0], f.params[0], f.params[1], f.params[1]};
            f.poles = points;
            return f;
        }
        if (!closed && n == 3 && !request) {
            f.degree = 2;
            f.knots = {f.params[0], f.params[0], f.params[0], f.params[2], f.params[2], f.params[2]};
            for (size_t i = 0; i < 3; ++i)
                rows.push_back({f.params[i], 0, points[i]});
        } else {
            f.degree = 3;
            const S period = closed ? f.params[n] - f.params[0] : S(0.0);
            const size_t degree = n == 3 ? 2 : 3;
            const auto build = [&](size_t i, size_t from) { t[i] = Lagrange(f.params[i], degree, &points[from], &f.params[from]).second; };
            if (n >= 3 && !given[0])
                build(0, 0);
            if (!closed && n >= 3 && !given[n - 1])
                build(n - 1, n - 1 - degree);
            rows.push_back({f.params[0], 0, points[0]});
            rows.push_back({f.params[0], 1, t[0]});
            f.knots.assign(closed ? 2 : 4, closed ? f.params[n - 1] - period : f.params[0]);
            if (closed)
                f.knots.insert(f.knots.end(), 2, f.params[0]);
            for (size_t i = 1; i < (closed ? n : n - 1); ++i) {
                rows.push_back({f.params[i], 0, points[i]});
                f.knots.push_back(f.params[i]);
                if (given[i]) {
                    rows.push_back({f.params[i], 1, t[i]});
                    f.knots.push_back(f.params[i]);
                }
            }
            const S end = f.params.back();
            rows.push_back({end, 1, closed ? t[0] : t[n - 1]});
            rows.push_back({end, 0, closed ? points[0] : points[n - 1]});
            f.knots.insert(f.knots.end(), closed ? 2 : 4, end);
            if (closed)
                f.knots.insert(f.knots.end(), 2, f.params[1] + period);
        }
        const size_t m = rows.size();
        std::vector<std::vector<S>> a(m, std::vector<S>(m, S(0.0)));
        f.poles.assign(m, zero);
        for (size_t r = 0; r < m; ++r) {
            const size_t span = FindSpan(f.knots, f.degree, m, rows[r].u);
            const std::vector<std::vector<S>> d = BasisDerivatives(f.knots, f.degree, span, rows[r].u, rows[r].k);
            for (size_t j = 0; j <= f.degree; ++j)
                a[r][span - f.degree + j] = d[rows[r].k][j];
            f.poles[r] = rows[r].data;
        }
        if (!SolveDense(std::move(a), f.poles))
            throw std::invalid_argument("Spline: these points and tangents give no spline");
        if (closed) {
            // OCC's periodic curve takes all but the last two poles round again; clamped at both ends by one more
            // knot there, the knot and pole beyond each end dropped
            f.poles[m - 2] = f.poles[0];
            f.poles[m - 1] = f.poles[1];
            InsertKnot(f.knots, f.poles, 3, f.params[n]);
            InsertKnot(f.knots, f.poles, 3, f.params[0]);
            f.knots.erase(f.knots.begin());
            f.knots.front() = f.params[0];
            f.poles.erase(f.poles.begin());
            f.knots.pop_back();
            f.knots.back() = f.params[n];
            f.poles.pop_back();
        }
        return f;
    }

    // a B-spline through points with the derivatives tangents at the points tangentAt. Parameters are chord lengths
    // (length: the distance of two points as an S), so a unit tangent is a derivative of length 1; the knots average the
    // parameters of all conditions, a parameter with a derivative condition counted twice, which keeps the system
    // regular. Cubic, lower with fewer conditions. A closed curve returns to its first point with equal first and
    // second derivatives; without tangents its knots are the parameters: the C2 periodic cubic, whichever point comes
    // first. style Clamped: InterpolateOcc
    template <class S, class P, class L>
    Curve<S, P> Interpolate(const std::vector<P> &points, const std::vector<size_t> &tangentAt, const std::vector<P> &tangents, bool closed,
                            L length, SplineStyle style = SplineStyle::Averaged) {
        if (style == SplineStyle::Clamped)
            return InterpolateOcc<S>(points, tangentAt, tangents, closed, length);
        const size_t m = points.size();
        if (m < (closed ? 3 : 2))
            throw std::invalid_argument(closed ? "Spline: a closed spline needs three points" : "Spline: two points at least");
        Curve<S, P> f;
        f.closed = closed;
        f.tangentAt = tangentAt;
        f.params.push_back(S(0.0));
        for (size_t k = 1; k <= (closed ? m : m - 1); ++k) {
            const S d = length(points[k % m] - points[k - 1]);
            if (!(ValueOf(d) > 0.0))
                throw std::invalid_argument("Spline: two following points at one place");
            f.params.push_back(f.params.back() + d);
        }
        for (size_t t : tangentAt)
            if (t >= m)
                throw std::invalid_argument("Spline: a tangent at a point it does not have");
        // the conditions: the derivative order k of the curve at u is data (a point, a tangent) or, closing, equal at
        // both ends
        struct Row {
            S u;
            size_t k;
            const P *data;
        };
        std::vector<Row> rows;
        for (size_t k = 0; k < f.params.size(); ++k)
            rows.push_back({f.params[k], 0, &points[k % m]});
        for (size_t t = 0; t < tangentAt.size(); ++t)
            rows.push_back({f.params[tangentAt[t]], 1, &tangents[t]});
        const S end = f.params.back();
        if (closed)
            for (size_t k : {1, 2})
                rows.push_back({S(0.0), k, nullptr});
        const size_t n = rows.size();
        f.degree = std::min<size_t>(3, n - 1);
        const size_t p = f.degree;
        std::vector<S> sites;
        for (const Row &r : rows)
            sites.push_back(!r.data && r.k == 2 ? end : r.u);
        std::sort(sites.begin(), sites.end(), [](const S &a, const S &b) { return ValueOf(a) < ValueOf(b); });
        f.knots.assign(p + 1, S(0.0));
        if (closed && tangentAt.empty())
            f.knots.insert(f.knots.end(), f.params.begin() + 1, f.params.end() - 1);
        else
            for (size_t j = 1; j + p < n; ++j) {
                S s(0.0);
                for (size_t i = j; i < j + p; ++i)
                    s = s + sites[i];
                f.knots.push_back(s / S((double)p));
            }
        f.knots.insert(f.knots.end(), p + 1, end);
        const P zero = points[0] * S(0.0);
        std::vector<std::vector<S>> a(n, std::vector<S>(n, S(0.0)));
        f.poles.assign(n, zero);
        for (size_t r = 0; r < n; ++r) {
            const Row &row = rows[r];
            const auto put = [&](const S &u, double sign) {
                const size_t span = FindSpan(f.knots, p, n, u);
                const std::vector<std::vector<S>> d = BasisDerivatives(f.knots, p, span, u, row.k);
                for (size_t j = 0; j <= p; ++j)
                    a[r][span - p + j] = a[r][span - p + j] + d[row.k][j] * S(sign);
            };
            put(row.u, 1.0);
            if (row.data)
                f.poles[r] = *row.data;
            else
                put(end, -1.0);
        }
        if (!SolveDense(std::move(a), f.poles))
            throw std::invalid_argument("Spline: these points and tangents give no spline");
        return f;
    }

    inline Curve<double, Vertex> Interpolate(const std::vector<Vertex> &points, const std::vector<size_t> &tangentAt,
                                             const std::vector<Vertex> &tangents, bool closed, SplineStyle style = SplineStyle::Averaged) {
        return Interpolate<double>(points, tangentAt, tangents, closed, [](const Vertex &d) { return Norm(d); }, style);
    }

}
