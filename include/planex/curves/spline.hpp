// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/core/bbox.hpp"
#include "planex/curves/edge.hpp"
#include "planex/curves/line.hpp"
#include "planex/curves/arc.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/core/timer.hpp"
#include "planex/curves/curve.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/curves/primitivefit.hpp"

namespace planex2d {

    class Wire;

    struct SplineData {
        Curve<double, Vertex> curve;
        std::vector<PointOnCurve> nodes;
        std::vector<PrimitiveEdge> approx;
        std::vector<double> params;  // curve parameter at each approximation vertex, approx.size() + 1 of them
    };

    class Spline {
        private:
            std::shared_ptr<const SplineData> data;
            size_t first = 0;
            size_t last = 0;
            BBox box;

            template <class P, class E>
            void Transform(P point, E edge) {
                auto d = std::make_shared<SplineData>(*data);
                for (Vertex &q : d->curve.poles)
                    q = point(q);
                for (auto &[u, p] : d->nodes)
                    p = point(p);
                for (auto &e : d->approx)
                    std::visit(edge, e);
                data = std::move(d);
                UpdateBox();
            }
            static void SplitPiece(SplineData &d, size_t k, const Vertex &x) {
                PrimitiveEdge rest = d.approx[k];
                std::visit([&](auto &e) { e.Trim(e.Start(), x); }, d.approx[k]);
                std::visit([&](auto &e) { e.Trim(x, e.End()); }, rest);
                d.approx.insert(d.approx.begin() + (long)k + 1, rest);
                d.params.insert(d.params.begin() + (long)k + 1, d.curve.Project(x, d.params[k], d.params[k + 1]));
            }
            static size_t SnapJoint(SplineData &d, size_t j, const Vertex &x) {
                if (j > 0)
                    std::visit([&](auto &e) { e.Trim(e.Start(), x); }, d.approx[j - 1]);
                if (j < d.approx.size())
                    std::visit([&](auto &e) { e.Trim(x, e.End()); }, d.approx[j]);
                return j;
            }
            static size_t InsertVertex(SplineData &d, size_t first, size_t &last, const Vertex &x, double tol);
            Spline(std::shared_ptr<const SplineData> data, size_t first, size_t last, const Properties &props)
                : data(std::move(data)), first(first), last(last), props(props) {
                UpdateBox();
            }
            void UpdateBox() {
                box = BBox{};
                for (const PrimitiveEdge &e : Approximation())
                    box.Grow(BoxOf(e));
            }
            static std::vector<Spline> Pieces(const Curve<double, Vertex> &c, const std::vector<double> &u,
                                              const std::vector<Vertex> &nodes, double atol);
            Spline Rebased(const Vertex &x, double tol) const {
                if (x.IsClose(Start(), tol))
                    return *this;
                SplineData d = *data;
                size_t end = d.approx.size();
                const size_t j = InsertVertex(d, 0, end, x, tol);
                return Rebased(d, j, props);
            }
            static Spline Rebased(const SplineData &d, size_t j, const Properties &props);
            std::vector<Spline> SplitOpen(const SplitPoints &pts, double callerTol) const;
            static Curve<double, Vertex> Interpolated(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic,
                                                      double tol, SplineStyle style, const std::map<size_t, TangentSetting<double>> &settings = {});
            bool IsOffChord(const Vertex &a, const Vertex &b, double u, double tol) const {
                const Vertex p = data->curve.At(u), d = b - a;
                const double l = Norm(d);
                return l > 0.0 ? std::abs(Cross(d, p - a)) / l > tol : Norm(p - a) > tol;
            }
            void SampleSpan(std::vector<Vertex> &out, double ua, const Vertex &a, double ub, const Vertex &b, double tol, int depth) const {
                const double um = 0.5 * (ua + ub);
                // 2^40 chords: far below any tol
                if (depth < 40 && (IsOffChord(a, b, um, tol) || IsOffChord(a, b, 0.5 * (ua + um), tol) || IsOffChord(a, b, 0.5 * (um + ub), tol))) {
                    const Vertex m = data->curve.At(um);
                    SampleSpan(out, ua, a, um, m, tol, depth + 1);
                    SampleSpan(out, um, m, ub, b, tol, depth + 1);
                } else
                    out.push_back(b);
            }
            // chord length parameters, as Geom2dAPI_Interpolate builds them
            static std::vector<double> ChordParams(const std::vector<Vertex> &nodes) {
                std::vector<double> u{0.0};
                for (size_t i = 1; i < nodes.size(); ++i)
                    u.push_back(u.back() + Norm(nodes[i] - nodes[i - 1]));
                return u;
            }
            static double ApproximationTol(const std::vector<Vertex> &pts) {
                BBox all;
                for (const Vertex &p : pts)
                    all.Grow(p);
                return config::state->spline_tol * Norm(all.Size());
            }
            std::pair<size_t, double> PieceAtFraction(double t) const;
            double ParamWithin(const Vertex &p, double tol) const;

        public:
            Properties props;
            Ends ends;

            Curve<double, Vertex> Segment() const {
                Curve<double, Vertex> c = data->curve.Segment(data->params[first], data->params[last]);
                c.poles.front() = Start();
                c.poles.back() = End();
                return c;
            }

            static std::vector<Spline> Through(const std::vector<Vertex> &pts, bool periodic, double tol,
                                               const std::map<size_t, Vertex> &tangents, SplineStyle style = config::state->spline_style,
                                               const std::map<size_t, TangentSetting<double>> &settings = {});
            static std::optional<Spline> Fitted(const std::vector<PrimitiveEdge> &chain, const std::vector<Vertex> &pts, double tol,
                                                const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent);

            // atol: the approximation's tolerance, if not that of the nodes (a piece fitted like the whole it is part of)
            static std::vector<Spline> FromNodes(const Curve<double, Vertex> &c, const std::vector<double> &u,
                                                 const std::vector<Vertex> &nodes, std::optional<double> atol = std::nullopt) {
                return Pieces(c, u, nodes, atol.value_or(ApproximationTol(nodes)));
            }

            static std::vector<Spline> FromCurve(const Curve<double, Vertex> &c);

            const Vertex &Start() const { return data->approx[first].Start(); }
            const Vertex &End() const { return data->approx[last - 1].End(); }
            std::vector<Vertex> Points() const {
                std::vector<Vertex> pts{Start()};
                double u0 = data->params[first];
                double u1 = data->params[last];
                for (const auto &[u, p] : data->nodes)
                    if (u > u0 && u < u1)
                        pts.push_back(p);
                pts.push_back(End());
                return pts;
            }
            std::span<const PrimitiveEdge> Approximation() const { return {data->approx.data() + first, last - first}; }
            // its index in Approximation(); the first of equally near pieces
            size_t NearestPieceIndex(const Vertex &p) const {
                size_t best = first;
                double nearest = std::numeric_limits<double>::infinity();
                for (size_t k = first; k < last; ++k)
                    if (const double d = data->approx[k].Distance(p); d < nearest) {
                        nearest = d;
                        best = k;
                    }
                return best - first;
            }
            const PrimitiveEdge &NearestPiece(const Vertex &p) const { return data->approx[first + NearestPieceIndex(p)]; }
            std::pair<double, Vertex> ExactNearest(const Vertex &p) const;
            Vertex ExactNearestPoint(const Vertex &p) const { return ExactNearest(p).second; }
            // the core's decisions take Tangent, the approximation's
            Vertex ExactTangent(const Vertex &p) const { return data->curve.Direction(ExactNearest(p).first); }
            std::optional<Vertex> ExactTangentAlong(const Vertex &d, const Vertex &p) const;
            Vertex NearestPoint(const Vertex &p) const { return NearestPiece(p).NearestPoint(p); }
            double Distance(const Vertex &q) const {
                double d = data->approx[first].Distance(q);
                for (size_t k = first + 1; k < last; ++k)
                    d = std::min(d, data->approx[k].Distance(q));
                return d;
            }
            double SignedArea(const Vertex &o) const {
                double a = 0.0;
                for (const PrimitiveEdge &p : Approximation())
                    a += p.SignedArea(o);
                return a;
            }
            // within spline_tol of Start() and End()
            std::pair<Vertex, Vertex> ExactEnds() const { return {data->curve.At(data->params[first]), data->curve.At(data->params[last])}; }
            // f(point, tangent, weight) at the Gauss-Legendre points of the exact curve over this stretch (up to the
            // parameter to), 10 on each stretch between the knots and the approximation's joints: exact for a
            // polynomial of degree 19
            template <class F>
            void ForEachGaussPoint(F f, double to = std::numeric_limits<double>::infinity()) const {
                static constexpr std::array<double, 5> x{0.14887433898163121, 0.43339539412924719, 0.67940956829902441,
                                                         0.86506336668898451, 0.97390652851717172};
                static constexpr std::array<double, 5> w{0.29552422471475287, 0.26926671930999636, 0.21908636251598204,
                                                         0.14945134915058059, 0.066671344308688138};
                const std::vector<double> &knots = data->curve.knots;
                size_t j = 0;
                for (size_t k = first; k < last && data->params[k] < to; ++k) {
                    const double end = std::min(data->params[k + 1], to);
                    for (double a = data->params[k]; a < end;) {
                        while (j < knots.size() && knots[j] <= a)
                            ++j;
                        const double b = j < knots.size() && knots[j] < end ? knots[j] : end;
                        const double m = 0.5 * (a + b), h = 0.5 * (b - a);
                        for (size_t i = 0; i < x.size(); ++i)
                            for (const double u : {m - h * x[i], m + h * x[i]})
                                f(data->curve.At(u), data->curve.At(u, 1), h * w[i]);
                        a = b;
                    }
                }
            }
            double ExactLength() const {
                double l = 0.0;
                ForEachGaussPoint([&](const Vertex &, const Vertex &t, double w) { l += w * Norm(t); });
                return l;
            }
            double ExactLengthTo(double u) const {
                double l = 0.0;
                ForEachGaussPoint([&](const Vertex &, const Vertex &t, double w) { l += w * Norm(t); }, u);
                return l;
            }
            double ExactParamAt(double s) const;
            Vertex ExactPointAt(double u) const { return data->curve.At(u); }
            Vertex ExactDirectionAt(double u) const { return data->curve.Direction(u); }
            double StartParam() const { return data->params[first]; }
            double EndParam() const { return data->params[last]; }
            double ExactCurvatureAt(double u) const { return data->curve.Curvature(u); }
            // closed to Start() and End() by lines, so that the stretches of one loop add up
            double ExactSignedArea(const Vertex &o) const {
                const auto [a, b] = ExactEnds();
                double s = Cross(Start() - o, a - o) + Cross(b - o, End() - o);
                ForEachGaussPoint([&](const Vertex &q, const Vertex &t, double w) { s += w * Cross(q - o, t); });
                return 0.5 * s;
            }
            double LengthFraction(const Vertex &p) const;
            Vertex PointAtFraction(double t) const {
                const auto [k, f] = PieceAtFraction(t);
                return data->approx[first + k].PointAtFraction(f);
            }
            Vertex TangentAtFraction(double t) const {
                const auto [k, f] = PieceAtFraction(t);
                return data->approx[first + k].TangentAtFraction(f);
            }
            std::optional<Spline> Joined(const Spline &b, const Vertex &) const {
                std::optional<Spline> s = Joined(b);
                if (!s)
                    s = b.Joined(*this);
                if (!s || (s->Start() == s->End() && !s->IsClosed()))
                    return std::nullopt;
                return s;
            }
            std::optional<Spline> Joined(const Spline &next) const;
            Spline Reversed() const;
            const BBox &Box() const { return box; }
            void Rotate(const Vertex &c, double cs, double sn) {
                Transform([&](const Vertex &p) { return Rotated(p, c, cs, sn); }, [&](auto &x) { x.Rotate(c, cs, sn); });
            }
            void Translate(const Vertex &t) {
                Transform([&](const Vertex &p) { return p + t; }, [&](auto &x) { x.Translate(t); });
            }
            void Mirror(const Vertex &a, const Vertex &d) {
                Transform([&](const Vertex &p) { return Mirrored(p, a, d); }, [&](auto &x) { x.Mirror(a, d); });
            }
            void Scale(const Vertex &c, double s) {
                Transform([&](const Vertex &p) { return Scaled(p, c, s); }, [&](auto &x) { x.Scale(c, s); });
            }
            double LengthScale() const {
                double s = 0.0;
                for (const PrimitiveEdge &e : Approximation())
                    s = std::max(s, e.LengthScale());
                return s;
            }
            double Length() const {
                double l = 0.0;
                for (const PrimitiveEdge &e : Approximation())
                    l += e.Length();
                return l;
            }
            // the same point for the spline and its Reversed(): the joint in the middle, or of the middle piece the one of its
            // two directions that is lower by (x, y)
            Vertex MidPoint() const {
                const size_t n = last - first;
                const PrimitiveEdge &p = data->approx[first + n / 2];
                if (n % 2 == 0)
                    return std::visit([](const auto &x) { return x.Start(); }, p);
                return std::visit([](const auto &x) {
                    const Vertex a = x.MidPoint(), b = x.Reversed().MidPoint();
                    return std::pair(a.x, a.y) < std::pair(b.x, b.y) ? a : b;
                }, p);
            }
            Location Locate(const Vertex &p, double tol) const {
                if (p.IsClose(Start(), tol))
                    return Location::AtStart;
                if (p.IsClose(End(), tol))
                    return Location::AtEnd;
                for (const PrimitiveEdge &e : Approximation())
                    if (std::visit([&](const auto &x) { return x.Locate(p, tol); }, e) != Location::Off)
                        return Location::Inside;
                return Location::Off;
            }
            bool IsInside(const Vertex &p, double tol) const { return Locate(p, tol) != Location::Off; }
            bool IsInside(const Vertex &p) const { return IsInside(p, Tol(std::max(LengthScale(), NormInf(p)))); }
            Vertex Tangent(const Vertex &p) const {
                for (const PrimitiveEdge &e : Approximation()) {
                    Vertex t = std::visit([&](const auto &x) { return x.Tangent(p); }, e);
                    if (t.x != 0.0 || t.y != 0.0)
                        return t;
                }
                return {0.0, 0.0};
            }
            double Param(const Vertex &p, double callerTol = 0.0) const {
                const double own = Tol(std::max(LengthScale(), NormInf(p)));
                const double t = ParamWithin(p, own);
                return t < 0.0 && callerTol > own ? ParamWithin(p, callerTol) : t;
            }
            std::vector<Spline> Split(const SplitPoints &pts, double callerTol) const {
                if (!IsClosed() || pts.IsEmpty())
                    return SplitOpen(pts, callerTol);
                const double tol = std::max(Tol(LengthScale()), callerTol);
                const Spline r = Rebased(pts[0].p, tol);
                SplitPoints cuts = pts;
                if (std::ranges::all_of(pts, [&](const PointOnCurve &x) { return x.p.IsClose(pts[0].p, tol); }))
                    cuts.Add(0.0, r.PointAtFraction(0.5));
                return r.SplitOpen(cuts, callerTol);
            }
            std::array<Spline, 2> Halves() const {
                const size_t n = data->approx.size();
                size_t m = n / 2;
                if (!data->nodes.empty())
                    if (auto it = std::ranges::find(data->params, data->nodes[data->nodes.size() / 2].t); it != data->params.end())
                        m = (size_t)(it - data->params.begin());
                if (m == 0 || m >= n)
                    m = n / 2;
                return {Spline(data, 0, m, props), Spline(data, m, n, props)};
            }
            double ApproximationTol() const {
                BBox all;
                for (const PrimitiveEdge &e : data->approx)
                    all.Grow(BoxOf(e));
                return config::state->spline_tol * Norm(all.Size());
            }
            bool IsClosed() const { return first == 0 && last == data->approx.size() && Start() == End(); }
            Spline Part(const std::optional<Vertex> &a, const std::optional<Vertex> &b) const;
            std::vector<Spline> SplitAt(std::vector<std::pair<size_t, Vertex>> at, double callerTol) const;
            template <bool recomputeCenter = false>
            void Trim(const Vertex &a, const Vertex &b) {
                if (a == Start() && b == End())
                    return;
                auto d = std::make_shared<SplineData>(*data);
                std::visit([&](auto &x) { x.template Trim<recomputeCenter>(a, x.End()); }, d->approx[first]);
                std::visit([&](auto &x) { x.template Trim<recomputeCenter>(x.Start(), b); }, d->approx[last - 1]);
                data = std::move(d);
                UpdateBox();
            }
            std::vector<Vertex> Sample(double tol) const {
                std::vector<Vertex> out{Start()};
                for (size_t k = first; k < last; ++k)
                    SampleSpan(out, data->params[k], out.back(), data->params[k + 1],
                               k + 1 == last ? End() : data->curve.At(data->params[k + 1]), tol, 0);
                return out;
            }
            BSplineData BSpline() const {
                Curve<double, Vertex> c = Segment();
                return {std::move(c.poles), std::move(c.knots), std::move(c.weights), (int)c.degree};
            }
            Wire Offset(double d) const;
    };

    inline std::vector<Spline> Split(const Spline &s, const SplitPoints &pts, double tol) { return s.Split(pts, tol); }

}
