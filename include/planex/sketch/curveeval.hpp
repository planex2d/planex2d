// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/bbox.hpp"
#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/sketch/splinedata.hpp"

namespace planex2d::sketch {

    // places along a curve closer than this part of its chord length are one
    inline constexpr double samePlace = 1e-9;

    // side 0: the spline, else its offset by side * d; t: the spline's chord length; k = 0 its point, 1 its derivative; a
    // closed spline's runs on past its end (a piece over its seam)
    struct CurveEval {
        Curve<double, Vertex> f;
        double side = 0.0, d = 0.0;

        Vertex At(double t, size_t k) const;
        double Nearest(double t0, double t1, const Vertex &p) const;
        // t: a place near where they meet
        double MeetLine(double t, const Vertex &o, const Vertex &u) const;
        double MeetCircle(double t, const Vertex &centre, double radius) const;
        std::vector<double> Samples() const;
        std::array<Vertex, 2> EndDirections(double ta, double tb) const {
            const double sense = tb > ta ? 1.0 : -1.0;
            return {At(ta, 1) * (sense / Norm(At(ta, 1))), At(tb, 1) * (sense / Norm(At(tb, 1)))};
        }
        std::vector<double> FitParams(double ta, double tb, const Vertex &a, const Vertex &b, bool closed, SplineStyle style) const;
        std::vector<Vertex> Places(const std::vector<double> &t, const Vertex &a, const Vertex &b, bool closed) const {
            std::vector<Vertex> points;
            for (size_t i = 0; i + (closed ? 1 : 0) < t.size(); ++i)
                points.push_back(i == 0 ? a : i + 1 == t.size() ? b : At(t[i], 0));
            return points;
        }
        Curve<double, Vertex> Through(const std::vector<double> &t, const std::vector<Vertex> &points, bool closed, SplineStyle style) const {
            const std::array<Vertex, 2> ends = EndDirections(t.front(), t.back());
            return Interpolate(points, closed ? std::vector<size_t>{} : std::vector<size_t>{0, points.size() - 1},
                               closed ? std::vector<Vertex>{} : std::vector<Vertex>{ends[0], ends[1]}, closed, style);
        }

    private:
        double Deviation(const Curve<double, Vertex> &spline, std::array<double, 2> ts, std::array<double, 2> us, double q) const {
            const Vertex c = At(ts[0] + (ts[1] - ts[0]) * q, 0);
            return Norm(spline.At(spline.Project(c, us[0] + (us[1] - us[0]) * q), 0) - c);
        }
        double MaxDeviation(const Curve<double, Vertex> &spline, std::array<double, 2> ts, std::array<double, 2> us) const;
    };

    // w: along its run, 0 at its start; a piece over a closed spline's seam ends beyond the end
    struct OffsetRun {
        CurveEval g;
        bool reversed;
        double from, to, end;

        double T(double w) const {
            const double t = reversed ? to - w : from + w;
            return t > end ? t - end : t;
        }
        Vertex At(double w) const { return g.At(T(w), 0); }
        Vertex Derivative(double w) const { return g.At(T(w), 1) * (reversed ? -1.0 : 1.0); }
        double Project(const Vertex &x, double wa, double wb) const {
            double t = g.f.Project(x, T(0.5 * (wa + wb)));
            if (to > end && t < from)
                t += end;
            return std::clamp(reversed ? to - t : t - from, std::min(wa, wb), std::max(wa, wb));
        }
    };

    // by chord length; a piece over a closed spline's seam ends beyond the end
    struct SplineSpan {
        SplinePiece piece;
        bool round;
        CurveEval g;
        double from, to, end;

        // only a span that runs past the end wraps: a place past an open spline's end stays there
        double Wrap(double u) const { return (round || to > end) && u > end ? u - end : u; }
        // k <= 1
        Vertex At(double u, size_t k) const { return g.At(Wrap(u), k); }
        double Along(const Vertex &x) const;
        // t: a place near where they meet
        double MeetLine(double t, const Vertex &o, const Vertex &u) const;
        double MeetCircle(double t, const Vertex &centre, double radius) const;
        double MeetCurve(double t, const CurveEval &other) const;
    };

    std::optional<std::pair<double, Vertex>> TightestBend(const Curve<double, Vertex> &f, double side, double t0, double t1);

    inline std::optional<std::string> Cusp(const Curve<double, Vertex> &f, double side, double t0, double t1, double d, const std::string &id) {
        const std::optional<std::pair<double, Vertex>> bend = TightestBend(f, side, t0, t1);
        if (!bend || d < bend->first - Tol(std::max(bend->first, d)))
            return std::nullopt;
        return "the offset of " + id + " would have a cusp: d = " + NumberText(d) + " is not below " + NumberText(bend->first) +
               ", the radius of its tightest bend towards that side, at (" + NumberText(bend->second.x) + ", " + NumberText(bend->second.y) + ")";
    }

}
