// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
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

    struct BSplineData {
        std::vector<Vertex> poles;
        std::vector<double> knots;
        std::vector<double> weights;  // empty if not rational
        int degree = 0;
    };

    // a B-spline by its poles, rational with weights: S its numbers (double, or dual numbers that carry the derivatives
    // with respect to the points and tangents it was made from), P its points (with +, - and * S). An interpolated
    // curve also keeps the parameters of its points and where it has tangents
    template <class S, class P>
    struct Curve {
        size_t degree = 3;
        bool closed = false;
        std::vector<S> knots;
        // of the points, a closed curve's end (back at the first point) last
        std::vector<S> params;
        std::vector<size_t> tangentAt;
        std::vector<P> poles;
        // one per pole, or none if not rational
        std::vector<S> weights;

        S First() const { return knots[degree]; }
        S Last() const { return knots[poles.size()]; }
        // the k-th derivative (k <= 2) at u
        P At(const S &u, size_t k = 0) const {
            const size_t span = FindSpan(knots, degree, poles.size(), u);
            const std::vector<std::vector<S>> d = BasisDerivatives(knots, degree, span, u, k);
            const size_t i0 = span - degree;
            if (weights.empty()) {
                P sum = poles[i0] * d[k][0];
                for (size_t j = 1; j <= degree; ++j)
                    sum = sum + poles[i0 + j] * d[k][j];
                return sum;
            }
            // the derivatives a of the weighted sum of the poles and w of the weight; the curve is a / w
            std::vector<P> a;
            std::vector<S> w;
            for (size_t i = 0; i <= k; ++i) {
                a.push_back(poles[i0] * (weights[i0] * d[i][0]));
                w.push_back(weights[i0] * d[i][0]);
                for (size_t j = 1; j <= degree; ++j) {
                    a[i] = a[i] + poles[i0 + j] * (weights[i0 + j] * d[i][j]);
                    w[i] = w[i] + weights[i0 + j] * d[i][j];
                }
            }
            const S inverse = S(1.0) / w[0];
            const P c0 = a[0] * inverse;
            if (k == 0)
                return c0;
            const P c1 = (a[1] - c0 * w[1]) * inverse;
            if (k == 1)
                return c1;
            return (a[2] - c1 * (S(2.0) * w[1]) - c0 * w[2]) * inverse;
        }
        P Derivative(const S &u) const { return At(u, 1); }
        // the unit tangent at u; at an end, where the parametrization may come to a stop (FromNACA's cosine spacing at
        // the trailing edge: its derivative vanishes there), the tangent's limit, the direction of the first pole off
        // the end pole by more than rounding
        P Direction(const S &u) const {
            if (u == First() || u == Last()) {
                const bool atStart = u == First();
                const P end = atStart ? poles.front() : poles.back();
                const double tol = Tol(NormInf(end));
                for (size_t k = 1; k < poles.size(); ++k) {
                    const P d = atStart ? poles[k] - end : end - poles[poles.size() - 1 - k];
                    if (NormInf(d) > tol)
                        return d / Norm(d);
                }
            }
            const P d = At(u, 1);
            return d / Norm(d);
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
        // the parameter between ua and ub nearest p: the best of 16 samples, then Newton steps
        S Project(const P &p, const S &ua, const S &ub) const {
            const size_t n = 16;
            S u = ua;
            for (size_t k = 1; k <= n; ++k)
                if (const S t = ua + (ub - ua) * ((double)k / (double)n); Norm(At(t) - p) < Norm(At(u) - p))
                    u = t;
            const S lo = std::min(ua, ub), hi = std::max(ua, ub);
            for (int i = 0; i < 30; ++i) {
                const P c = At(u) - p, d1 = At(u, 1), d2 = At(u, 2);
                const S h = Dot(d1, d1) + Dot(c, d2);
                if (!(h > 0.0))
                    break;
                const S step = Dot(c, d1) / h;
                u = std::clamp(u - step, lo, hi);
                if (std::abs(step) <= 1e-15 * (std::abs(lo) + std::abs(hi)))
                    break;
            }
            return u;
        }
        // the same curve run the other way, on the same parameter range: u here is First() + Last() - u there
        Curve Reversed() const {
            Curve r;
            r.degree = degree;
            r.closed = closed;
            const S sum = First() + Last();
            for (size_t i = knots.size(); i-- > 0;)
                r.knots.push_back(sum - knots[i]);
            r.poles.assign(poles.rbegin(), poles.rend());
            r.weights.assign(weights.rbegin(), weights.rend());
            return r;
        }
        // the stretch from a to b as a curve of its own, its ends knots of full multiplicity; a or b within 1e-9 of the
        // range of a knot is taken as that knot (OCC's Segment does the same), so no span is nearly empty
        Curve Segment(S a, S b) const {
            const S nearTol = 1e-9 * (Last() - First());
            for (const S &t : knots) {
                if (std::abs(t - a) <= nearTol)
                    a = t;
                if (std::abs(t - b) <= nearTol)
                    b = t;
            }
            std::vector<S> t = knots, w = weights;
            std::vector<P> q = poles;
            for (size_t i = 0; i < w.size(); ++i)
                q[i] = q[i] * w[i];
            for (const S &u : {a, b})
                for (size_t m = (size_t)std::count(t.begin(), t.end(), u); m <= degree; ++m) {
                    if (!w.empty()) {
                        std::vector<S> tw = t;
                        InsertKnot(tw, w, degree, u);
                    }
                    InsertKnot(t, q, degree, u);
                }
            const size_t s = (size_t)(std::find(t.begin(), t.end(), a) - t.begin());
            const size_t e = (size_t)(std::find(t.begin(), t.end(), b) - t.begin());
            Curve c;
            c.degree = degree;
            c.knots.assign(t.begin() + (std::ptrdiff_t)s, t.begin() + (std::ptrdiff_t)(e + degree + 1));
            c.poles.assign(q.begin() + (std::ptrdiff_t)s, q.begin() + (std::ptrdiff_t)e);
            if (!w.empty()) {
                c.weights.assign(w.begin() + (std::ptrdiff_t)s, w.begin() + (std::ptrdiff_t)e);
                for (size_t i = 0; i < c.poles.size(); ++i)
                    c.poles[i] = c.poles[i] * (S(1.0) / c.weights[i]);
            }
            return c;
        }
        // the same curve on parameters d further on
        Curve Shifted(const S &d) const {
            Curve r = *this;
            for (S &t : r.knots)
                t = t + d;
            for (S &t : r.params)
                t = t + d;
            return r;
        }
        // this curve followed by next, which starts where this one ends (point and parameter), both with knots of full
        // multiplicity at their ends: one curve, its knot there of multiplicity degree, the shared pole once
        Curve Joined(const Curve &next) const {
            Curve r;
            r.degree = degree;
            r.knots.assign(knots.begin(), knots.end() - 1);
            r.knots.insert(r.knots.end(), next.knots.begin() + (std::ptrdiff_t)(degree + 1), next.knots.end());
            r.poles = poles;
            r.poles.insert(r.poles.end(), next.poles.begin() + 1, next.poles.end());
            if (!weights.empty()) {
                // a rational piece is the same curve with all its weights scaled: next's first weight matches this one's last
                const S scale = weights.back() / next.weights.front();
                r.weights = weights;
                for (size_t i = 1; i < next.weights.size(); ++i)
                    r.weights.push_back(next.weights[i] * scale);
            }
            return r;
        }
    };

    // a b-spline given by its data, checked; periodic, its knots one period (the first knot's copies to the last's, the
    // last as often as the first), unwrapped to its clamped form on the same parameters. Pole 1 goes where OCC puts it:
    // the first span starts degree + 1 - (the first knot's multiplicity) poles later
    inline Curve<double, Vertex> BSplineCurve(const BSplineData &b, bool periodic) {
        const auto fail = [](const std::string &why) { throw std::invalid_argument("invalid b-spline: " + why); };
        for (const Vertex &q : b.poles)
            IsFinite("BSpline", {q.x, q.y});
        for (double x : b.knots)
            IsFinite("BSpline knot", {x});
        for (double x : b.weights)
            IsFinite("BSpline weight", {x});
        std::vector<double> distinct;
        std::vector<size_t> mult;
        for (size_t i = 0; i < b.knots.size(); ++i) {
            if (i > 0 && b.knots[i] < b.knots[i - 1])
                fail("the knots decrease at knot " + std::to_string(i));
            if (!distinct.empty() && b.knots[i] == distinct.back())
                ++mult.back();
            else {
                distinct.push_back(b.knots[i]);
                mult.push_back(1);
            }
        }
        const size_t n = b.poles.size(), nu = distinct.size();
        if (n < 2 || nu < 2 || b.degree < 1 || (!b.weights.empty() && b.weights.size() != n))
            fail(std::to_string(n) + " poles, " + std::to_string(nu) + " distinct knots, degree " + std::to_string(b.degree) +
                 (b.weights.empty() ? "" : ", " + std::to_string(b.weights.size()) + " weights") +
                 "; it needs 2 poles and 2 distinct knots at least, degree 1 at least, a weight per pole");
        const size_t p = (size_t)b.degree;
        for (size_t i = 0; i < b.weights.size(); ++i)
            if (!(b.weights[i] > 0.0))
                fail("weight " + std::to_string(i) + " is not positive");
        for (size_t i = 1; i + 1 < nu; ++i)
            if (mult[i] > p)
                fail("knot " + std::to_string(distinct[i]) + " repeats " + std::to_string(mult[i]) + " times, more than the degree");
        if (!periodic) {
            if (mult.front() > p + 1 || mult.back() > p + 1)
                fail("an end knot repeats more than degree + 1 times");
            if (b.knots.size() != n + p + 1)
                fail(std::to_string(n) + " poles of degree " + std::to_string(p) + " need " + std::to_string(n + p + 1) + " knots, there are " +
                     std::to_string(b.knots.size()));
            Curve<double, Vertex> c;
            c.degree = p;
            c.knots = b.knots;
            c.poles = b.poles;
            c.weights = b.weights;
            return c;
        }
        if (mult.front() != mult.back() || mult.front() > p)
            fail("a periodic b-spline's first and last knot repeat as often, at most the degree times");
        const size_t base = b.knots.size() - mult.back();
        if (base != n)
            fail(std::to_string(n) + " poles of a periodic b-spline need " + std::to_string(n) + " knots before the last, there are " +
                 std::to_string(base));
        const double period = distinct.back() - distinct.front();
        const long ln = (long)n, shift = (long)p + 1 - (long)mult.front();
        const auto wrap = [&](long j) { return (size_t)(((j % ln) + ln) % ln); };
        Curve<double, Vertex> c;
        c.degree = p;
        for (long j = -(long)p; j <= ln + (long)p; ++j)
            c.knots.push_back(b.knots[wrap(j)] + (double)((j - (long)wrap(j)) / ln) * period);
        for (long j = -(long)p; j < ln; ++j) {
            c.poles.push_back(b.poles[wrap(j + shift)]);
            if (!b.weights.empty())
                c.weights.push_back(b.weights[wrap(j + shift)]);
        }
        return c.Segment(c.knots[p], c.knots[n + p]);
    }

}
