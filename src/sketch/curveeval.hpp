// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/bbox.hpp"
#include "core/config.hpp"
#include "core/vertex.hpp"
#include "curves/interpolate.hpp"
#include "sketch/splinedata.hpp"

namespace planex2d::sketch {

    // a spline (side 0) or its offset by side * d, along the spline's chord length t: its point (k = 0) and derivative
    // (k = 1)
    struct CurveEval {
        Curve<double, Vertex> f;
        double side = 0.0, d = 0.0;

        Vertex At(double t, size_t k) const {
            if (side == 0.0)
                return f.At(t, k);
            const Vertex d1 = f.At(t, 1);
            const double speed = Norm(d1);
            if (k > 0)
                return d1 * (1.0 - side * d * Cross(d1, f.At(t, 2)) / (speed * speed * speed));
            return f.At(t, 0) + Vertex{-d1.y, d1.x} * (side * d / speed);
        }
        // the chord length in [t0, t1] where it comes nearest p: its nearest sample, then the foot of p on the spline
        // from there (the same place while the curve has no cusp)
        double Nearest(double t0, double t1, const Vertex &p) const {
            double best = t0, nearest = std::numeric_limits<double>::infinity();
            for (size_t k = 0; k + 1 < f.params.size(); ++k) {
                const double a = std::max(f.params[k], t0), b = std::min(f.params[k + 1], t1);
                if (!(b > a))
                    continue;
                for (size_t i = 0; i <= 32; ++i)
                    if (const double t = a + (b - a) * (double)i / 32.0, dist = Norm(At(t, 0) - p); dist < nearest) {
                        nearest = dist;
                        best = t;
                    }
            }
            return f.Project(p, best);
        }
        // its unit directions at ta and tb, running from ta to tb
        std::array<Vertex, 2> EndDirections(double ta, double tb) const {
            const double sense = tb > ta ? 1.0 : -1.0;
            return {At(ta, 1) * (sense / Norm(At(ta, 1))), At(tb, 1) * (sense / Norm(At(tb, 1)))};
        }
        // the chord lengths from ta (its point a) to tb (b) an ordinary spline ('averaged') through its points there, with its
        // directions at the ends (closed, a == b: periodic), passes to stay within spline_tol of it: first the places
        // over the spline's points, then the spans halved where it is not yet within spline_tol
        std::vector<double> FitParams(double ta, double tb, const Vertex &a, const Vertex &b, bool closed) const {
            const double sense = tb > ta ? 1.0 : -1.0, same = 1e-9 * f.params.back();
            const auto ordered = [&](std::vector<double> &t) { std::sort(t.begin(), t.end(), [&](double x, double y) { return (x - y) * sense < 0.0; }); };
            std::vector<double> t{ta, tb};
            for (double u : f.params)
                if ((u - ta) * sense > same && (tb - u) * sense > same)
                    t.push_back(u);
            ordered(t);
            const auto place = [&](size_t i) { return i == 0 ? a : i + 1 == t.size() ? b : At(t[i], 0); };
            const std::array<Vertex, 2> ends = EndDirections(ta, tb);
            for (int round = 0; round < 12; ++round) {
                std::vector<Vertex> points;
                BBox box;
                for (size_t i = 0; i + (closed ? 1 : 0) < t.size(); ++i) {
                    points.push_back(place(i));
                    box.Grow(points.back());
                }
                const Curve<double, Vertex> spline = Interpolate(points, closed ? std::vector<size_t>{} : std::vector<size_t>{0, points.size() - 1},
                                                                 closed ? std::vector<Vertex>{} : std::vector<Vertex>{ends[0], ends[1]}, closed, SplineStyle::Averaged);
                const double atol = config::state->spline_tol * Norm(box.Size());
                std::vector<double> more;
                for (size_t i = 0; i + 1 < t.size(); ++i)
                    if (MaxDeviation(spline, {t[i], t[i + 1]}, {spline.params[i], spline.params[i + 1]}) > atol)
                        more.push_back(0.5 * (t[i] + t[i + 1]));
                if (more.empty())
                    break;
                t.insert(t.end(), more.begin(), more.end());
                ordered(t);
            }
            return t;
        }

    private:
        // its distance at the fraction q of its span ts from the spline, whose span us is that fraction too
        double Deviation(const Curve<double, Vertex> &spline, std::array<double, 2> ts, std::array<double, 2> us, double q) const {
            const Vertex c = At(ts[0] + (ts[1] - ts[0]) * q, 0);
            return Norm(spline.At(spline.Project(c, us[0] + (us[1] - us[0]) * q), 0) - c);
        }
        // the largest Deviation over a span: 16 samples, then a ternary search for the maximum between the neighbours of
        // the largest sample; the 40 steps shrink that bracket to 1e-7 of the sample spacing
        double MaxDeviation(const Curve<double, Vertex> &spline, std::array<double, 2> ts, std::array<double, 2> us) const {
            const int n = 16;
            double best = 0.0;
            int kmax = 1;
            for (int k = 1; k < n; ++k)
                if (const double dist = Deviation(spline, ts, us, (double)k / n); dist > best) {
                    best = dist;
                    kmax = k;
                }
            double lo = (double)(kmax - 1) / n, hi = (double)(kmax + 1) / n;
            for (int it = 0; it < 40; ++it) {
                const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
                const double d1 = Deviation(spline, ts, us, m1), d2 = Deviation(spline, ts, us, m2);
                best = std::max(best, std::max(d1, d2));
                if (d1 < d2)
                    lo = m1;
                else
                    hi = m2;
            }
            return best;
        }
    };

    // an offset curve as the biarc fit evaluates it, along its run from w = 0 at its start; a point's parameter from its
    // foot on the spline
    struct OffsetRun {
        CurveEval g;
        bool reversed;
        double from, to;

        double T(double w) const { return reversed ? to - w : from + w; }
        Vertex At(double w) const { return g.At(T(w), 0); }
        Vertex Derivative(double w) const { return g.At(T(w), 1) * (reversed ? -1.0 : 1.0); }
        double Project(const Vertex &x, double wa, double wb) const {
            const double t = g.f.Project(x, T(0.5 * (wa + wb)));
            return std::clamp(reversed ? to - t : t - from, std::min(wa, wb), std::max(wa, wb));
        }
    };

    // where a spline element runs along its spline, by chord length; a piece over a closed spline's seam ends beyond the
    // end
    struct SplineSpan {
        SplinePiece piece;
        bool round;
        Curve<double, Vertex> f;
        double from, to, end;

        double Wrap(double u) const { return u > end ? u - end : u; }
        Vertex At(double u, size_t k) const { return f.At(Wrap(u), k); }
        // the position of x along the element: the nearest of samples, then Newton steps
        double Along(const Vertex &x) const {
            const size_t n = 64 * f.params.size();
            double u = from;
            for (size_t k = 0; k <= n; ++k)
                if (const double t = from + (to - from) * (double)k / (double)n; Norm(At(t, 0) - x) < Norm(At(u, 0) - x))
                    u = t;
            for (int i = 0; i < 30; ++i) {
                const Vertex c = At(u, 0) - x, d1 = At(u, 1), d2 = At(u, 2);
                const double h = Dot(d1, d1) + Dot(c, d2);
                if (!(h > 0.0))
                    break;
                const double step = Dot(c, d1) / h;
                u = std::clamp(u - step, from, to);
                if (std::abs(step) <= 1e-15 * end)
                    break;
            }
            return u;
        }
    };

    // the tightest bend of the spline f towards the side (+1 its left) between the chord lengths t0 and t1: its radius
    // and where; nothing if it bends only away from that side. Sampled 32 times per piece, then a golden-section search
    inline std::optional<std::pair<double, Vertex>> TightestBend(const Curve<double, Vertex> &f, double side, double t0, double t1) {
        const auto bend = [&](double t) {
            const Vertex d1 = f.At(t, 1);
            const double speed = Norm(d1);
            return side * Cross(d1, f.At(t, 2)) / (speed * speed * speed);
        };
        double best = t0, most = bend(t0), step = 0.0;
        for (size_t k = 0; k + 1 < f.params.size(); ++k) {
            const double a = std::max(f.params[k], t0), b = std::min(f.params[k + 1], t1);
            if (!(b > a))
                continue;
            for (size_t i = 0; i <= 32; ++i)
                if (const double t = a + (b - a) * (double)i / 32.0, v = bend(t); v > most) {
                    most = v;
                    best = t;
                    step = (b - a) / 32.0;
                }
        }
        const double golden = 0.5 * (std::sqrt(5.0) - 1.0);
        double lo = std::max(t0, best - step), hi = std::min(t1, best + step);
        for (int i = 0; i < 100 && hi - lo > 1e-15 * (std::abs(t1) + 1.0); ++i) {
            const double p = hi - golden * (hi - lo), q = lo + golden * (hi - lo);
            if (bend(p) < bend(q))
                lo = p;
            else
                hi = q;
        }
        if (const double t = 0.5 * (lo + hi), v = bend(t); v > most) {
            most = v;
            best = t;
        }
        if (!(most > 0.0))
            return std::nullopt;
        return std::pair{1.0 / most, f.At(best, 0)};
    }

    // the cusp the offset of the spline f (id) by d towards the side would have between t0 and t1: where it bends towards
    // that side with a radius d or less
    inline std::optional<std::string> Cusp(const Curve<double, Vertex> &f, double side, double t0, double t1, double d, const std::string &id) {
        const std::optional<std::pair<double, Vertex>> bend = TightestBend(f, side, t0, t1);
        if (!bend || d < bend->first - Tol(std::max(bend->first, d)))
            return std::nullopt;
        return "the offset of " + id + " would have a cusp: d = " + NumberText(d) + " is not below " + NumberText(bend->first) +
               ", the radius of its tightest bend towards that side, at (" + NumberText(bend->second.x) + ", " + NumberText(bend->second.y) + ")";
    }

}
