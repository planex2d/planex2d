// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"

namespace planex2d {

    inline double ValueOf(double v) { return v; }
    inline bool ExactlyZero(double v) { return v == 0.0; }

    // the knot span of u among n control points of degree p
    template <class S>
    size_t FindSpan(const std::vector<S> &t, size_t p, size_t n, const S &u) {
        const double v = ValueOf(u);
        if (v >= ValueOf(t[n]))
            return n - 1;
        if (v <= ValueOf(t[p]))
            return p;
        size_t lo = p, hi = n, mid = (lo + hi) / 2;
        while (v < ValueOf(t[mid]) || v >= ValueOf(t[mid + 1])) {
            if (v < ValueOf(t[mid]))
                hi = mid;
            else
                lo = mid;
            mid = (lo + hi) / 2;
        }
        return mid;
    }

    // d[k][j]: the k-th derivative (k <= kmax) of the basis function span - p + j of degree p at u (The NURBS Book, A2.3)
    template <class S>
    std::vector<std::vector<S>> BasisDerivatives(const std::vector<S> &t, size_t p, size_t span, const S &u, size_t kmax) {
        std::vector<std::vector<S>> ndu(p + 1, std::vector<S>(p + 1, S(0.0))), a(2, std::vector<S>(p + 1, S(0.0)));
        std::vector<S> left(p + 1, S(0.0)), right(p + 1, S(0.0));
        ndu[0][0] = S(1.0);
        for (size_t j = 1; j <= p; ++j) {
            left[j] = u - t[span + 1 - j];
            right[j] = t[span + j] - u;
            S saved(0.0);
            for (size_t r = 0; r < j; ++r) {
                ndu[j][r] = right[r + 1] + left[j - r];
                const S tmp = ndu[r][j - 1] / ndu[j][r];
                ndu[r][j] = saved + right[r + 1] * tmp;
                saved = left[j - r] * tmp;
            }
            ndu[j][j] = saved;
        }
        std::vector<std::vector<S>> d(kmax + 1, std::vector<S>(p + 1, S(0.0)));
        for (size_t j = 0; j <= p; ++j)
            d[0][j] = ndu[j][p];
        for (size_t r = 0; r <= p; ++r) {
            size_t s1 = 0, s2 = 1;
            a[0][0] = S(1.0);
            for (size_t k = 1; k <= std::min(kmax, p); ++k) {
                S sum(0.0);
                const long rk = (long)r - (long)k, pk = (long)p - (long)k;
                if ((long)r >= (long)k) {
                    a[s2][0] = a[s1][0] / ndu[pk + 1][rk];
                    sum = a[s2][0] * ndu[rk][pk];
                }
                const long j1 = rk >= -1 ? 1 : -rk, j2 = (long)r - 1 <= pk ? (long)k - 1 : (long)p - (long)r;
                for (long j = j1; j <= j2; ++j) {
                    a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j];
                    sum = sum + a[s2][j] * ndu[rk + j][pk];
                }
                if ((long)r <= pk) {
                    a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r];
                    sum = sum + a[s2][k] * ndu[r][pk];
                }
                d[k][r] = sum;
                std::swap(s1, s2);
            }
        }
        double f = (double)p;
        for (size_t k = 1; k <= std::min(kmax, p); ++k) {
            for (size_t j = 0; j <= p; ++j)
                d[k][j] = d[k][j] * S(f);
            f *= (double)(p - k);
        }
        return d;
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

    // a B-spline by its poles: S its numbers (double, or dual numbers that carry the derivatives with respect to the
    // points and tangents it was made from), P its points (with +, - and * S)
    template <class S, class P>
    struct Curve {
        size_t degree = 3;
        bool closed = false;
        std::vector<S> knots;
        // of the points, a closed curve's end (back at the first point) last
        std::vector<S> params;
        std::vector<size_t> tangentAt;
        std::vector<P> poles;

        // the k-th derivative (k <= 2) at u
        P At(const S &u, size_t k) const {
            const size_t span = FindSpan(knots, degree, poles.size(), u);
            const std::vector<std::vector<S>> d = BasisDerivatives(knots, degree, span, u, k);
            P sum = poles[span - degree] * d[k][0];
            for (size_t j = 1; j <= degree; ++j)
                sum = sum + poles[span - degree + j] * d[k][j];
            return sum;
        }
        // the parameter nearest p: the best of 32 samples per point (or guess), then Newton steps
        S Project(const P &p, std::optional<S> guess = std::nullopt) const {
            const S end = params.back();
            const size_t n = 32 * params.size();
            S u = guess.value_or(S(0.0));
            for (size_t k = 0; k <= n && !guess; ++k)
                if (const S t = end * (double)k / (double)n; Norm(At(t, 0) - p) < Norm(At(u, 0) - p))
                    u = t;
            for (int i = 0; i < 30; ++i) {
                const P c = At(u, 0) - p, d1 = At(u, 1), d2 = At(u, 2);
                const S h = Dot(d1, d1) + Dot(c, d2);
                if (!(h > 0.0))
                    break;
                const S step = Dot(c, d1) / h;
                u = std::clamp(u - step, S(0.0), end);
                if (std::abs(step) <= 1e-15 * end)
                    break;
            }
            return u;
        }
    };

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

    // the knot u inserted once (Boehm), the curve the same
    template <class S, class P>
    void InsertKnot(std::vector<S> &t, std::vector<P> &q, size_t p, const S &u) {
        size_t k = 0, s = 0;
        for (size_t i = 0; i < t.size(); ++i)
            if (ValueOf(t[i]) <= ValueOf(u))
                k = i;
        for (size_t i = k + 1; i-- > 0 && ValueOf(t[i]) == ValueOf(u);)
            ++s;
        std::vector<P> r;
        for (size_t i = 0; i <= q.size(); ++i) {
            if (i + p <= k)
                r.push_back(q[i]);
            else if (i + s <= k) {
                const S alpha = (u - t[i]) / (t[i + p] - t[i]);
                r.push_back(q[i] * alpha + q[i - 1] * (S(1.0) - alpha));
            } else
                r.push_back(q[i - 1]);
        }
        t.insert(t.begin() + (std::ptrdiff_t)k + 1, u);
        q = std::move(r);
    }

    // OCC's Geom2dAPI_Interpolate (the style "occ") as netgen calls it, its quirks kept: a point without a tangent
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
    // first. style Occ: InterpolateOcc
    template <class S, class P, class L>
    Curve<S, P> Interpolate(const std::vector<P> &points, const std::vector<size_t> &tangentAt, const std::vector<P> &tangents, bool closed,
                            L length, SplineStyle style = SplineStyle::Px) {
        if (style == SplineStyle::Occ)
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
                                             const std::vector<Vertex> &tangents, bool closed, SplineStyle style = SplineStyle::Px) {
        return Interpolate<double>(points, tangentAt, tangents, closed, [](const Vertex &d) { return Norm(d); }, style);
    }

}
