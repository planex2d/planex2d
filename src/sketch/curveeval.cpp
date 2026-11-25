// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/curveeval.hpp"

#include <cmath>
#include <limits>

namespace planex2d::sketch {

    template <class C>
    static double MeetingLine(const C &curve, double end, double t, const Vertex &o, const Vertex &u) {
        for (int i = 0; i < 30; ++i) {
            const Vertex p = curve.At(t, 0), d = curve.At(t, 1);
            if (Cross(u, d) == 0.0)
                break;
            const double step = Cross(u, p - o) / Cross(u, d);
            t -= step;
            if (std::abs(step) <= 1e-15 * end)
                break;
        }
        return t;
    }

    template <class C>
    static double MeetingCircle(const C &curve, double end, double t, const Vertex &centre, double radius) {
        for (int i = 0; i < 30; ++i) {
            const Vertex p = curve.At(t, 0), d = curve.At(t, 1);
            const double dist = Norm(p - centre), slope = Dot(p - centre, d) / dist;
            if (slope == 0.0)
                break;
            const double step = (dist - radius) / slope;
            t -= step;
            if (std::abs(step) <= 1e-15 * end)
                break;
        }
        return t;
    }

    double CurveEval::MeetLine(double t, const Vertex &o, const Vertex &u) const { return MeetingLine(*this, f.params.back(), t, o, u); }
    double CurveEval::MeetCircle(double t, const Vertex &centre, double radius) const { return MeetingCircle(*this, f.params.back(), t, centre, radius); }

    Vertex CurveEval::At(double t, size_t k) const {
        if (f.closed && t > f.params.back())
            t -= f.params.back();
        if (side == 0.0)
            return f.At(t, k);
        const Vertex d1 = f.At(t, 1);
        const double speed = Norm(d1);
        if (k > 0)
            return d1 * (1.0 - side * d * Cross(d1, f.At(t, 2)) / (speed * speed * speed));
        return f.At(t, 0) + Vertex{-d1.y, d1.x} * (side * d / speed);
    }

    // 32 per piece between its points
    std::vector<double> CurveEval::Samples() const {
        std::vector<double> u;
        for (size_t k = 0; k + 1 < f.params.size(); ++k)
            for (size_t i = 0; i < 32; ++i)
                u.push_back(f.params[k] + (f.params[k + 1] - f.params[k]) * (double)i / 32.0);
        u.push_back(f.params.back());
        return u;
    }

    double CurveEval::Nearest(double t0, double t1, const Vertex &p) const {
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

    std::vector<double> CurveEval::FitParams(double ta, double tb, const Vertex &a, const Vertex &b, bool closed, SplineStyle style) const {
        const double sense = tb > ta ? 1.0 : -1.0, same = samePlace * f.params.back();
        const auto ordered = [&](std::vector<double> &t) { std::sort(t.begin(), t.end(), [&](double x, double y) { return (x - y) * sense < 0.0; }); };
        std::vector<double> t{ta, tb};
        const auto add = [&](double u) {
            if ((u - ta) * sense > same && (tb - u) * sense > same)
                t.push_back(u);
        };
        for (double u : f.params)
            add(u);
        // past a closed spline's seam: its first place (0) is its last
        for (size_t k = 1; k < f.params.size(); ++k)
            add(f.params[k] + f.params.back());
        ordered(t);
        for (int round = 0; round < 12; ++round) {
            const std::vector<Vertex> points = Places(t, a, b, closed);
            BBox box;
            for (const Vertex &p : points)
                box.Grow(p);
            const Curve<double, Vertex> spline = Through(t, points, closed, style);
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

    // the largest Deviation over a span: 16 samples, then a ternary search for the maximum between the neighbours of
    // the largest sample; the 40 steps shrink that bracket to 1e-7 of the sample spacing
    double CurveEval::MaxDeviation(const Curve<double, Vertex> &spline, std::array<double, 2> ts, std::array<double, 2> us) const {
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

    double SplineSpan::Along(const Vertex &x) const {
        const size_t n = 64 * g.f.params.size();
        double u = from;
        for (size_t k = 0; k <= n; ++k)
            if (const double t = from + (to - from) * (double)k / (double)n; Norm(At(t, 0) - x) < Norm(At(u, 0) - x))
                u = t;
        for (int i = 0; i < 30; ++i) {
            const Vertex c = g.f.At(Wrap(u), 0) - x, d1 = g.f.At(Wrap(u), 1), d2 = g.f.At(Wrap(u), 2);
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

    double SplineSpan::MeetLine(double t, const Vertex &o, const Vertex &u) const { return MeetingLine(*this, end, t, o, u); }
    double SplineSpan::MeetCircle(double t, const Vertex &centre, double radius) const { return MeetingCircle(*this, end, t, centre, radius); }

    // both positions step; the other curve's starts at the nearest of 64 samples per point
    double SplineSpan::MeetCurve(double t, const CurveEval &other) const {
        const double otherEnd = other.f.params.back();
        const size_t n = 64 * other.f.params.size();
        double w = 0.0;
        for (size_t i = 0; i <= n; ++i)
            if (const double v = otherEnd * (double)i / (double)n; Norm(other.At(v, 0) - At(t, 0)) < Norm(other.At(w, 0) - At(t, 0)))
                w = v;
        for (int i = 0; i < 30; ++i) {
            const Vertex p = At(t, 0), d = At(t, 1);
            const Vertex q = other.At(w, 0), e = other.At(w, 1), r = p - q;
            const double det = Cross(d, e);
            if (det == 0.0)
                break;
            const double step = Cross(r, e) / det;
            w += Cross(d, r) / det;
            t -= step;
            if (std::abs(step) <= 1e-15 * end)
                break;
        }
        return t;
    }

    // the tightest bend of the spline f towards the side (+1 its left) between the chord lengths t0 and t1: its radius
    // and where; nothing if it bends only away from that side. Sampled 32 times per piece, then a golden-section search
    std::optional<std::pair<double, Vertex>> TightestBend(const Curve<double, Vertex> &f, double side, double t0, double t1) {
        const auto bend = [&](double t) { return side * f.Curvature(t); };
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

}
