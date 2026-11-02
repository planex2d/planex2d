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

    template <class S>
    struct TangentSetting {
        S tension = S(1.0);
        std::optional<S> curvature;
    };

    inline void CheckSpline(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic, double tol,
                            const std::map<size_t, TangentSetting<double>> &settings = {}) {
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
        for (const auto &[i, s] : settings) {
            if (!tangents.count(i))
                throw std::invalid_argument("Spline: a " + std::string(s.curvature ? "curvature radius" : "tension") + " at point " +
                                            std::to_string(i) + ", which has no tangent; give its tangent too");
            if (!(s.tension > 0.0) || !std::isfinite(s.tension))
                throw std::invalid_argument("Spline: the tension at point " + std::to_string(i) + " is " + std::to_string(s.tension) +
                                            "; it is positive and finite");
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

    template <class S, class P, class L>
    std::vector<S> PointParams(const std::vector<P> &points, bool closed, L length) {
        const size_t n = points.size();
        std::vector<S> params{S(0.0)};
        for (size_t k = 1; k <= (closed ? n : n - 1); ++k) {
            const S d = length(points[k % n] - points[k - 1]);
            if (!(ValueOf(d) > 0.0))
                throw std::invalid_argument("Spline: two following points at one place");
            params.push_back(params.back() + d);
        }
        return params;
    }

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
                               L length, const std::vector<TangentSetting<S>> &settings = {}) {
        const size_t n = points.size();
        if (n < (closed ? 3 : 2))
            throw std::invalid_argument(closed ? "Spline: a closed spline needs three points" : "Spline: two points at least");
        Curve<S, P> f;
        f.closed = closed;
        f.tangentAt = tangentAt;
        f.params = PointParams<S>(points, closed, length);
        const P zero = points[0] * S(0.0);
        const bool request = !tangentAt.empty();
        std::vector<P> t(n, zero);
        std::vector<char> given(n, 0);
        std::vector<S> tension(n, S(1.0));
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
                if (i < settings.size())
                    tension[tangentAt[i]] = settings[i].tension;
            }
            const size_t degree = n == 2 ? 1 : 2;
            size_t from = 0;
            for (size_t i = 0; i < n; ++i) {
                if (!given[i])
                    continue;
                const P d = Lagrange(f.params[i], degree, &points[from], &f.params[from]).second;
                t[i] = t[i] * ((AbsOf(d.x) + AbsOf(d.y)) / (AbsOf(t[i].x) + AbsOf(t[i].y)) * tension[i]);
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

    template <class S, class P>
    struct QuinticSystem {
        std::vector<S> params, knots;
        std::vector<size_t> multiplicity;
        size_t poleCount = 0;
        std::vector<std::vector<S>> a;
        std::vector<S> b;

        size_t Span(size_t i, bool left) const {
            const size_t span = FindSpan(knots, 5, poleCount, params[i]);
            return left && i > 0 && i + 1 < params.size() ? span - multiplicity[i] : span;
        }
        void Put(size_t i, bool left, size_t k, const P &w, double sign) {
            const size_t span = Span(i, left);
            const std::vector<std::vector<S>> d = BasisDerivatives(knots, 5, span, params[i], k);
            std::vector<S> &row = a.back();
            for (size_t j = 0; j <= 5; ++j) {
                const size_t c = 2 * (span - 5 + j);
                row[c] = row[c] + d[k][j] * w.x * S(sign);
                row[c + 1] = row[c + 1] + d[k][j] * w.y * S(sign);
            }
        }
        void Along(size_t i, bool left, size_t k, const P &w, const S &value) {
            a.emplace_back(2 * poleCount, S(0.0));
            b.push_back(value);
            Put(i, left, k, w, 1.0);
        }
        void Is(size_t i, bool left, size_t k, const P &v) {
            Along(i, left, k, P{S(1.0), S(0.0)}, v.x);
            Along(i, left, k, P{S(0.0), S(1.0)}, v.y);
        }
        void Same(size_t i, bool left, size_t j, bool otherLeft, size_t k, const P &w) {
            a.emplace_back(2 * poleCount, S(0.0));
            b.push_back(S(0.0));
            Put(i, left, k, w, 1.0);
            Put(j, otherLeft, k, w, -1.0);
        }
        void Same(size_t i, bool left, size_t j, bool otherLeft, size_t k) {
            Same(i, left, j, otherLeft, k, P{S(1.0), S(0.0)});
            Same(i, left, j, otherLeft, k, P{S(0.0), S(1.0)});
        }
        std::vector<P> Solved() {
            if (!SolveDense(a, b))
                throw std::invalid_argument("Spline: these points and tangents give no spline");
            std::vector<P> poles;
            for (size_t j = 0; j < poleCount; ++j)
                poles.push_back(P{b[2 * j], b[2 * j + 1]});
            a.clear();
            b.clear();
            return poles;
        }
    };

    template <class S, class P>
    std::vector<P> QuinticPoles(QuinticSystem<S, P> &q, const std::vector<P> &points, const std::vector<char> &given,
                                const std::vector<P> &direction, const std::vector<S> &speed, const std::vector<TangentSetting<S>> &setting,
                                bool closed) {
        const size_t n = points.size(), last = q.params.size() - 1;
        const P zero = points[0] * S(0.0);
        for (size_t i = 0; i <= last; ++i)
            q.Is(i, i == last, 0, points[i % n]);
        for (size_t i = 0; i < n; ++i) {
            const P d = direction[i], normal{S(0.0) - d.y, d.x}, derivative = d * speed[i];
            const std::optional<S> curvature = setting[i].curvature;
            if (!closed && (i == 0 || i == n - 1)) {
                const size_t at = i == 0 ? 0 : last;
                const bool left = at == last;
                if (!given[i]) {
                    q.Is(at, left, 3, zero);
                    q.Is(at, left, 4, zero);
                    continue;
                }
                q.Is(at, left, 1, derivative);
                if (curvature) {
                    q.Along(at, left, 2, normal, *curvature * speed[i] * speed[i]);
                    q.Along(at, left, 2, d, S(0.0));
                } else
                    q.Is(at, left, 3, zero);
                continue;
            }
            const bool seam = closed && i == 0;
            const size_t other = seam ? last : i;
            if (!given[i]) {
                if (seam)
                    for (size_t k = 1; k <= 4; ++k)
                        q.Same(i, false, other, true, k);
                continue;
            }
            q.Is(i, false, 1, derivative);
            if (seam)
                q.Is(other, true, 1, derivative);
            if (curvature) {
                const S across = *curvature * speed[i] * speed[i];
                q.Along(i, false, 2, normal, across);
                if (seam) {
                    q.Along(other, true, 2, normal, across);
                    q.Same(i, false, other, true, 2, d);
                }
                q.Along(i, false, 2, d, S(0.0));
            } else if (seam) {
                q.Same(i, false, other, true, 2);
                q.Same(i, false, other, true, 3);
            }
        }
        return q.Solved();
    }

    template <class S, class P, class L>
    Curve<S, P> InterpolateQuintic(const std::vector<P> &points, const std::vector<size_t> &tangentAt, const std::vector<P> &tangents,
                                   const std::vector<TangentSetting<S>> &settings, bool closed, L length) {
        const size_t n = points.size();
        if (n < (closed ? 3 : 2))
            throw std::invalid_argument(closed ? "Spline: a closed spline needs three points" : "Spline: two points at least");
        QuinticSystem<S, P> q;
        q.params = PointParams<S>(points, closed, length);
        const size_t last = q.params.size() - 1;
        std::vector<char> given(n, 0);
        std::vector<P> direction(n, points[0] * S(0.0));
        std::vector<TangentSetting<S>> setting(n);
        for (size_t t = 0; t < tangentAt.size(); ++t) {
            if (tangentAt[t] >= n)
                throw std::invalid_argument("Spline: a tangent at a point it does not have");
            given[tangentAt[t]] = 1;
            direction[tangentAt[t]] = tangents[t] * (S(1.0) / length(tangents[t]));
            if (t < settings.size())
                setting[tangentAt[t]] = settings[t];
        }
        std::vector<S> speed(n, S(0.0));
        if (!tangentAt.empty()) {
            const Curve<S, P> natural = InterpolateQuintic<S>(points, std::vector<size_t>{}, std::vector<P>{}, std::vector<TangentSetting<S>>{}, closed, length);
            for (size_t i = 0; i < n; ++i)
                if (given[i])
                    speed[i] = setting[i].tension * length(natural.At(q.params[!closed && i == n - 1 ? last : i], 1));
        }
        q.multiplicity.assign(q.params.size(), 6);
        q.knots.assign(6, q.params.front());
        for (size_t i = 1; i < last; ++i) {
            q.multiplicity[i] = !given[i] ? 1 : setting[i].curvature ? 3 : 2;
            q.knots.insert(q.knots.end(), q.multiplicity[i], q.params[i]);
        }
        q.knots.insert(q.knots.end(), 6, q.params.back());
        q.poleCount = q.knots.size() - 6;
        Curve<S, P> f;
        f.degree = 5;
        f.closed = closed;
        f.tangentAt = tangentAt;
        f.params = q.params;
        f.knots = q.knots;
        if (n == 2 && tangentAt.empty()) {
            for (size_t j = 0; j <= 5; ++j)
                f.poles.push_back(points[0] + (points[1] - points[0]) * S((double)j / 5.0));
            return f;
        }
        f.poles = QuinticPoles(q, points, given, direction, speed, setting, closed);
        // exact by construction: 5 (q1 - q0) / (u6 - u1) = 5 (q[m-1] - q[m-2]) / (u[m+4] - u[m-1])
        if (closed) {
            const size_t m = f.poles.size();
            f.poles.front() = f.poles.back() = points[0];
            f.poles[m - 2] = f.poles[m - 1] - (f.poles[1] - f.poles[0]) * ((f.knots[m + 4] - f.knots[m - 1]) / (f.knots[6] - f.knots[1]));
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
                            L length, SplineStyle style = SplineStyle::Averaged, const std::vector<TangentSetting<S>> &settings = {}) {
        if (style == SplineStyle::Quintic)
            return InterpolateQuintic<S>(points, tangentAt, tangents, settings, closed, length);
        for (const TangentSetting<S> &s : settings)
            if (s.curvature)
                throw std::invalid_argument("Spline: a curvature radius needs style 'quintic'");
        if (style == SplineStyle::Clamped)
            return InterpolateOcc<S>(points, tangentAt, tangents, closed, length, settings);
        const size_t m = points.size();
        if (m < (closed ? 3 : 2))
            throw std::invalid_argument(closed ? "Spline: a closed spline needs three points" : "Spline: two points at least");
        Curve<S, P> f;
        f.closed = closed;
        f.tangentAt = tangentAt;
        f.params = PointParams<S>(points, closed, length);
        for (size_t t : tangentAt)
            if (t >= m)
                throw std::invalid_argument("Spline: a tangent at a point it does not have");
        std::vector<P> scaled = tangents;
        for (size_t t = 0; t < settings.size(); ++t)
            scaled[t] = scaled[t] * settings[t].tension;
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
            rows.push_back({f.params[tangentAt[t]], 1, &scaled[t]});
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
                                             const std::vector<Vertex> &tangents, bool closed, SplineStyle style = SplineStyle::Averaged,
                                             const std::vector<TangentSetting<double>> &settings = {}) {
        return Interpolate<double>(points, tangentAt, tangents, closed, [](const Vertex &d) { return Norm(d); }, style, settings);
    }

}
