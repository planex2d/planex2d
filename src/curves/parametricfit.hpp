// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/bbox.hpp"
#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/curve.hpp"
#include "curves/interpolate.hpp"

namespace planex2d {

    // a point of a parametric curve with its first and second derivative in the parameter
    struct CurveJet {
        Vertex p, d, dd;

        bool IsSmooth() const { return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(dd.x) && std::isfinite(dd.y); }
    };

    // the quintic B-spline through jets[k] at the parameters u[k]: a knot of multiplicity 3 at each inner one, so the
    // point and both derivatives there fix the three poles both pieces share; C2 by construction
    inline Curve<double, Vertex> QuinticThrough(const std::vector<double> &u, const std::vector<CurveJet> &jets) {
        constexpr size_t degree = 5;
        const size_t n = u.size() - 1;
        Curve<double, Vertex> c;
        c.degree = degree;
        c.params = u;
        c.knots.assign(degree + 1, u.front());
        for (size_t j = 1; j < n; ++j)
            c.knots.insert(c.knots.end(), 3, u[j]);
        c.knots.insert(c.knots.end(), degree + 1, u.back());
        c.poles.assign(3 * n + 3, Vertex{});
        for (size_t j = 0; j <= n; ++j) {
            const size_t span = FindSpan(c.knots, degree, c.poles.size(), u[j]), offset = 3 * j - (span - degree);
            const std::vector<std::vector<double>> d = BasisDerivatives(c.knots, degree, span, u[j], 2);
            std::vector<std::vector<double>> a(3, std::vector<double>(3));
            for (size_t k = 0; k < 3; ++k)
                for (size_t m = 0; m < 3; ++m)
                    a[k][m] = d[k][offset + m];
            std::vector<Vertex> b{jets[j].p, jets[j].d, jets[j].dd};
            SolveDense(std::move(a), b);
            std::copy(b.begin(), b.end(), c.poles.begin() + (std::ptrdiff_t)(3 * j));
        }
        c.poles.front() = jets.front().p;
        c.poles.back() = jets.back().p;
        return c;
    }

    // a parametric curve (jets(t): its point and derivatives at t) over [t0, t1] as a quintic B-spline within about
    // 1e-12 of its size: samples placed adaptively, a quintic piece between two from their points and derivatives, a
    // piece halved where it leaves the curve between them. Its parameter runs from 0 over about the curve's length
    // (u = (t - t0) * s); its end meeting its start to rounding closes it, as one curve (smooth there where the
    // derivatives meet as well)
    template <class F>
    Curve<double, Vertex> ParametricFit(F jets, double t0, double t1, const std::string &call) {
        // half the decision's 1e-12 of the size, so the curve between the check points stays within it too
        constexpr double fitTol = 0.5e-12;
        constexpr size_t most = 4096, first = 16;
        IsFinite(call.c_str(), {t0, t1});
        if (t0 == t1)
            throw std::invalid_argument(call + ": the range (" + NumberText(t0) + ", " + NumberText(t1) + ") is empty");
        // a piece of 2^-20 of the range or less is not halved: what is left there is the formula's own rounding (as of
        // 1 - cos(t) near 0, about sqrt(eps)), quiet up to 1e-7 of the size, or a corner or cusp
        const double shortest = std::ldexp(std::abs(t1 - t0), -20), rounding = 1e-7;
        BBox box;
        double length = 0.0;
        Vertex before = jets(t0).p;
        box.Grow(before);
        for (size_t k = 1; k <= first; ++k) {
            const Vertex p = jets(t0 + (t1 - t0) * (double)k / (double)first).p;
            length += Norm(p - before);
            box.Grow(p);
            before = p;
        }
        if (!(length > 0.0))
            throw std::invalid_argument(call + ": the curve has no length, its point is the same for every t");
        const double size = Norm(box.Size()), tol = std::max(fitTol * size, Tol(box.NormInf())), s = length / (t1 - t0);
        // a sample whose derivatives the formula does not give (sqrt of 0): taken from two inside the range, a millionth of
        // it apart, extrapolated to the sample
        const auto node = [&](double t) {
            CurveJet j = jets(t);
            if (j.IsSmooth())
                return j;
            const double h = (t == t1 ? -1e-6 : 1e-6) * (t1 - t0);
            const CurveJet a = jets(t + h), b = jets(t + 2.0 * h);
            j.d = a.d * 2.0 - b.d;
            j.dd = a.dd * 2.0 - b.dd;
            if (!j.IsSmooth())
                throw std::invalid_argument(call + ": the formula has no derivative at t = " + NumberText(t) + " or near it");
            return j;
        };
        // the most a piece's quintic leaves the curve, at 7 points between its ends
        const auto deviation = [&](double ta, const CurveJet &a, double tb, const CurveJet &b) {
            const double h = tb - ta;
            const std::array<Vertex, 6> q{a.p, a.p + a.d * (h / 5.0), a.p + a.d * (2.0 * h / 5.0) + a.dd * (h * h / 20.0),
                                          b.p - b.d * (2.0 * h / 5.0) + b.dd * (h * h / 20.0), b.p - b.d * (h / 5.0), b.p};
            constexpr std::array<double, 6> binomial{1.0, 5.0, 10.0, 10.0, 5.0, 1.0};
            double most = 0.0;
            for (size_t k = 1; k < 8; ++k) {
                const double x = (double)k / 8.0;
                std::array<double, 6> xs{1.0}, ys{1.0};
                for (size_t i = 1; i < 6; ++i) {
                    xs[i] = xs[i - 1] * x;
                    ys[i] = ys[i - 1] * (1.0 - x);
                }
                Vertex at{};
                for (size_t i = 0; i < 6; ++i)
                    at = at + q[i] * (binomial[i] * xs[i] * ys[5 - i]);
                most = std::max(most, Norm(at - jets(ta + h * x).p));
            }
            return most;
        };
        std::vector<double> t;
        std::vector<CurveJet> j;
        for (size_t k = 0; k <= 8; ++k) {
            t.push_back(k == 8 ? t1 : t0 + (t1 - t0) * (double)k / 8.0);
            j.push_back(node(t.back()));
        }
        double worst = 0.0;
        for (bool split = true; split;) {
            split = false;
            worst = 0.0;
            std::vector<double> tn{t.front()};
            std::vector<CurveJet> jn{j.front()};
            size_t added = 0;
            for (size_t k = 0; k + 1 < t.size(); ++k) {
                const double e = deviation(t[k], j[k], t[k + 1], j[k + 1]);
                if (e > tol && t.size() + added < most && std::abs(t[k + 1] - t[k]) > shortest) {
                    tn.push_back(0.5 * (t[k] + t[k + 1]));
                    jn.push_back(node(tn.back()));
                    split = true;
                    ++added;
                } else if (std::abs(t[k + 1] - t[k]) > shortest || e > rounding * size)
                    worst = std::max(worst, e);
                tn.push_back(t[k + 1]);
                jn.push_back(j[k + 1]);
            }
            t = std::move(tn);
            j = std::move(jn);
        }
        if (worst > tol)
            Warning() << call << ": the fit stays within " << worst / size << " of the curve's size, not 1e-12: " << most
                      << " pieces are not enough, or the formula has a corner or a cusp in the range";
        const bool closed = Norm(j.back().p - j.front().p) <= Tol(box.NormInf());
        if (closed)
            j.back().p = j.front().p;
        std::vector<double> u;
        for (size_t k = 0; k < t.size(); ++k) {
            u.push_back(k + 1 == t.size() ? length : (t[k] - t0) * s);
            j[k].d = j[k].d * (1.0 / s);
            j[k].dd = j[k].dd * (1.0 / (s * s));
        }
        Curve<double, Vertex> c = QuinticThrough(u, j);
        c.closed = closed;
        return c;
    }

    // a curve given by its points only (point(t), called once per t) over [t0, t1]: the cubic through samples placed
    // adaptively, a stretch halved until the curve passes its middle sample within fitTol of its size (an offset: within
    // spline_tol); at most 4096 samples, with a warning if that is not enough. Closed as ParametricFit
    template <class F>
    Curve<double, Vertex> SampledFit(F point, double t0, double t1, const std::string &call, double fitTol = 1e-10) {
        constexpr size_t most = 4096, first = 16;
        IsFinite(call.c_str(), {t0, t1});
        if (t0 == t1)
            throw std::invalid_argument(call + ": the range (" + NumberText(t0) + ", " + NumberText(t1) + ") is empty");
        std::map<double, Vertex> samples, checks;
        for (size_t k = 0; k <= first; ++k) {
            const double t = k == first ? t1 : t0 + (t1 - t0) * (double)k / (double)first;
            samples[t] = point(t);
        }
        BBox box;
        for (const auto &[t, p] : samples)
            box.Grow(p);
        const double size = Norm(box.Size()), tol = std::max(fitTol * size, Tol(box.NormInf()));
        if (!(size > 0.0))
            throw std::invalid_argument(call + ": the curve has no length, its point is the same for every t");
        const bool closed = Norm(samples.rbegin()->second - samples.begin()->second) <= Tol(box.NormInf());
        if (closed)
            samples.rbegin()->second = samples.begin()->second;
        Curve<double, Vertex> c;
        double worst = 0.0;
        for (bool more = true; more;) {
            std::vector<double> t;
            std::vector<Vertex> p;
            for (const auto &[x, q] : samples) {
                t.push_back(x);
                p.push_back(q);
            }
            c = Interpolate(p, {}, {}, false, SplineStyle::Averaged);
            more = false;
            worst = 0.0;
            for (size_t k = 0; k + 1 < t.size(); ++k) {
                const double m = 0.5 * (t[k] + t[k + 1]);
                auto at = checks.find(m);
                if (at == checks.end())
                    at = checks.emplace(m, point(m)).first;
                const double e = Norm(c.At(c.Project(at->second, c.params[k], c.params[k + 1])) - at->second);
                if (e > tol && samples.size() < most) {
                    samples[m] = at->second;
                    more = true;
                } else
                    worst = std::max(worst, e);
            }
        }
        if (worst > tol)
            Warning() << call << ": the curve passes its samples within " << worst / size << " of its size with " << most
                      << " samples, not " << fitTol;
        c.poles.front() = samples.begin()->second;
        c.poles.back() = samples.rbegin()->second;
        c.closed = closed;
        return c;
    }

}
