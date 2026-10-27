// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/primitiveedge.hpp"
#include "core/timer.hpp"
#include "curves/curve.hpp"
#include "curves/interpolate.hpp"
#include "curves/primitivefit.hpp"

namespace planex2d {

    class Wire;

    struct SplineData {
        Curve<double, Vertex> curve;
        std::vector<PointOnCurve> nodes;
        std::vector<PrimitiveEdge> approx;
        std::vector<double> params;  // curve parameter at each approximation vertex, approx.size() + 1 of them
    };

    class Spline {
        friend class PlanexWriter;
        friend class PlanexReader;

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
            // the joint j of the approximation, within tol of x, moves onto x: the pieces on both sides end exactly there
            static size_t SnapJoint(SplineData &d, size_t j, const Vertex &x) {
                if (j > 0)
                    std::visit([&](auto &e) { e.Trim(e.Start(), x); }, d.approx[j - 1]);
                if (j < d.approx.size())
                    std::visit([&](auto &e) { e.Trim(x, e.End()); }, d.approx[j]);
                return j;
            }
            static size_t InsertVertex(SplineData &d, size_t first, size_t &last, const Vertex &x, double tol) {
                for (size_t k = first; k < last; ++k) {
                    Location l = std::visit([&](const auto &e) { return e.Locate(x, tol); }, d.approx[k]);
                    if (l == Location::AtStart)
                        return SnapJoint(d, k, x);
                    if (l == Location::AtEnd)
                        return SnapJoint(d, k + 1, x);
                    if (l == Location::Inside) {
                        SplitPiece(d, k, x);
                        ++last;
                        return k + 1;
                    }
                }
                throw std::runtime_error("split point not on spline");
            }
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
                                              const std::vector<Vertex> &nodes, double atol) {
                auto d = std::make_shared<SplineData>();
                d->curve = c;
                d->params.push_back(u.front());
                for (size_t i = 0; i < u.size(); ++i) {
                    d->nodes.emplace_back(u[i], nodes[i]);
                    if (i + 1 < u.size())
                        FitPrimitiveEdges(c, u[i], u[i + 1], nodes[i], nodes[i + 1], atol, d->approx, d->params, 0);
                }
                if (d->approx.empty())
                    throw std::invalid_argument("degenerate spline: zero length");
                for (auto &e : d->approx)
                    std::visit([](auto &x) { x.fromSpline = true; }, e);
                return {Spline(d, 0, d->approx.size(), {})};
            }
            // a closed spline starting and ending at x on it: the same curve, its approximation and parameters taken round
            // from x, the stretch past the old start one period on
            Spline Rebased(const Vertex &x, double tol) const {
                if (x.IsClose(Start(), tol))
                    return *this;
                SplineData d = *data;
                size_t end = d.approx.size();
                const size_t j = InsertVertex(d, 0, end, x, tol);
                const double u = d.params[j], begin = d.params.front(), period = d.params.back() - begin;
                auto r = std::make_shared<SplineData>();
                r->curve = d.curve.Segment(u, d.curve.Last()).Joined(d.curve.Segment(d.curve.First(), u).Shifted(period));
                r->approx.assign(d.approx.begin() + (std::ptrdiff_t)j, d.approx.end());
                r->approx.insert(r->approx.end(), d.approx.begin(), d.approx.begin() + (std::ptrdiff_t)j);
                r->params.assign(d.params.begin() + (std::ptrdiff_t)j, d.params.end());
                for (size_t k = 1; k <= j; ++k)
                    r->params.push_back(d.params[k] + period);
                for (const PointOnCurve &n : d.nodes)
                    if (n.t > u)
                        r->nodes.push_back(n);
                for (const PointOnCurve &n : d.nodes)
                    if (n.t > begin && n.t < u)
                        r->nodes.push_back({n.t + period, n.p});
                return Spline(r, 0, r->approx.size(), props);
            }
            std::vector<Spline> SplitOpen(const SplitPoints &pts, double callerTol) const {
                static Timer t("planex Spline Split");
                Region reg(t);
                auto d = std::make_shared<SplineData>(*data);
                size_t end = last;
                double tol = std::max(Tol(LengthScale()), callerTol);
                std::vector<size_t> cuts{first};
                for (const PointOnCurve &x : pts) {
                    size_t before = d->approx.size();
                    size_t v = InsertVertex(*d, first, end, x.p, tol);
                    if (d->approx.size() > before)
                        for (size_t &c : cuts)
                            if (c >= v)
                                ++c;
                    cuts.push_back(v);
                }
                cuts.push_back(end);
                std::sort(cuts.begin(), cuts.end());
                cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
                std::vector<Spline> pieces;
                for (size_t i = 0; i + 1 < cuts.size(); ++i)
                    pieces.push_back(Spline(d, cuts[i], cuts[i + 1], props));
                KeepOuterEnds(pieces, ends);
                return pieces;
            }
            // averaged takes the tangents' directions only; clamped scales them as OCC does, whatever their length
            static Curve<double, Vertex> Interpolated(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic,
                                                      double tol, SplineStyle style) {
                CheckSpline(pts, tangents, periodic, tol);
                std::vector<size_t> at;
                std::vector<Vertex> given;
                for (const auto &[i, v] : tangents) {
                    at.push_back(i);
                    given.push_back(style == SplineStyle::Clamped ? v : Normalized(v));
                }
                return Interpolate(pts, at, given, periodic, style);
            }
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
            // the approximation piece at the fraction t of the length, and the fraction within it
            std::pair<size_t, double> PieceAtFraction(double t) const {
                std::span<const PrimitiveEdge> approx = Approximation();
                double rest = t * Length();
                for (size_t k = 0; k + 1 < approx.size(); ++k) {
                    double l = approx[k].Length();
                    if (rest <= l)
                        return {k, l > 0.0 ? rest / l : 0.0};
                    rest -= l;
                }
                double l = approx.back().Length();
                return {approx.size() - 1, l > 0.0 ? std::min(rest / l, 1.0) : 0.0};
            }
            double ParamWithin(const Vertex &p, double tol) const {
                std::span<const PrimitiveEdge> approx = Approximation();
                for (size_t k = 0; k < approx.size(); ++k) {
                    double f = std::visit([&](const auto &x) -> double {
                        Location l = x.Locate(p, tol);
                        if (l == Location::Off)
                            return -1.0;
                        if (l == Location::AtStart)
                            return 0.0;
                        if (l == Location::AtEnd)
                            return 1.0;
                        return x.Param(p) / x.Param(x.End());
                    }, approx[k]);
                    if (f >= 0.0)
                        return (double)k + f;
                }
                return -1.0;
            }

        public:
            Properties props;
            Ends ends;

            // this stretch of the curve as a curve of its own, its end poles exactly at its ends
            Curve<double, Vertex> Segment() const {
                Curve<double, Vertex> c = data->curve.Segment(data->params[first], data->params[last]);
                c.poles.front() = Start();
                c.poles.back() = End();
                return c;
            }

            static std::vector<Spline> Through(const std::vector<Vertex> &pts, bool periodic, double tol,
                                               const std::map<size_t, Vertex> &tangents, SplineStyle style = config::state->spline_style) {
                static Timer t("planex Spline Through");
                Region reg(t);
                const Curve<double, Vertex> c = Interpolated(pts, tangents, periodic, tol, style);
                std::vector<Vertex> nodes = pts;
                if (periodic)
                    nodes.push_back(pts.front());
                return Pieces(c, ChordParams(nodes), nodes, ApproximationTol(pts));
            }
            static std::optional<Spline> Fitted(const std::vector<PrimitiveEdge> &chain, const std::vector<Vertex> &pts, double tol,
                                                const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent);

            // atol: the approximation's tolerance, if not that of the nodes (a piece fitted like the whole it is part of)
            static std::vector<Spline> FromNodes(const Curve<double, Vertex> &c, const std::vector<double> &u,
                                                 const std::vector<Vertex> &nodes, std::optional<double> atol = std::nullopt) {
                return Pieces(c, u, nodes, atol.value_or(ApproximationTol(nodes)));
            }

            // nodes at its knots
            static std::vector<Spline> FromCurve(const Curve<double, Vertex> &c) {
                static Timer t("planex Spline FromCurve");
                Region reg(t);
                std::vector<double> u;
                std::vector<Vertex> nodes;
                BBox all;
                for (size_t i = c.degree; i <= c.poles.size(); ++i) {
                    if (!u.empty() && c.knots[i] == u.back())
                        continue;
                    u.push_back(c.knots[i]);
                    nodes.push_back(c.At(u.back()));
                    all.Grow(nodes.back());
                }
                // ends apart by rounding only (a closed curve converted or projected) make a closed curve
                if (nodes.front() != nodes.back() && nodes.front().IsClose(nodes.back(), Tol(all.NormInf())))
                    nodes.back() = nodes.front();
                return Pieces(c, u, nodes, ApproximationTol(nodes));
            }

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
            // the first of equally near pieces, its index in Approximation()
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
            // the point nearest p on the exact curve within this stretch and its parameter, or its end: the curve
            // projected over the parameters of the nearest piece and its neighbours
            std::pair<double, Vertex> ExactNearest(const Vertex &p) const {
                const size_t k = first + NearestPieceIndex(p);
                std::pair<double, Vertex> best{data->params[first], Start()};
                double d = Norm(best.second - p);
                if (const double e = Norm(End() - p); e < d) {
                    best = {data->params[last], End()};
                    d = e;
                }
                for (size_t i = k > first ? k - 1 : k; i < std::min(k + 2, last); ++i) {
                    const double u = data->curve.Project(p, data->params[i], data->params[i + 1]);
                    const Vertex q = data->curve.At(u);
                    if (const double e = Norm(q - p); e < d) {
                        best = {u, q};
                        d = e;
                    }
                }
                return best;
            }
            Vertex ExactNearestPoint(const Vertex &p) const { return ExactNearest(p).second; }
            // the unit tangent of the exact curve at its point nearest p; the core's decisions take Tangent, the
            // approximation's
            Vertex ExactTangent(const Vertex &p) const {
                const Vertex t = data->curve.At(ExactNearest(p).first, 1);
                return t / Norm(t);
            }
            // the point of the exact curve within this stretch where its tangent runs along d, from near p (Newton steps on
            // the cross product of the tangent and d); none where they run off the stretch
            std::optional<Vertex> ExactTangentAlong(const Vertex &d, const Vertex &p) const {
                const size_t k = first + NearestPieceIndex(p);
                const double lo = data->params[first], hi = data->params[last];
                double u = data->curve.Project(p, data->params[k], data->params[k + 1]);
                for (int i = 0; i < 60; ++i) {
                    const double h = Cross(data->curve.At(u, 1), d), dh = Cross(data->curve.At(u, 2), d);
                    if (!(dh != 0.0))
                        return std::nullopt;
                    const double step = h / dh;
                    u -= step;
                    if (u < lo || u > hi)
                        return std::nullopt;
                    if (std::abs(step) <= 1e-15 * (std::abs(lo) + std::abs(hi)))
                        break;
                }
                return data->curve.At(u);
            }
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
            // the exact curve's points at this stretch's ends, within spline_tol of Start() and End()
            std::pair<Vertex, Vertex> ExactEnds() const { return {data->curve.At(data->params[first]), data->curve.At(data->params[last])}; }
            // f(point, tangent, weight) at the Gauss-Legendre points of the exact curve over this stretch, 10 on each
            // stretch between the knots and the approximation's joints: exact for a polynomial of degree 19
            template <class F>
            void ForEachGaussPoint(F f) const {
                static constexpr std::array<double, 5> x{0.14887433898163121, 0.43339539412924719, 0.67940956829902441,
                                                         0.86506336668898451, 0.97390652851717172};
                static constexpr std::array<double, 5> w{0.29552422471475287, 0.26926671930999636, 0.21908636251598204,
                                                         0.14945134915058059, 0.066671344308688138};
                const std::vector<double> &knots = data->curve.knots;
                size_t j = 0;
                for (size_t k = first; k < last; ++k) {
                    const double end = data->params[k + 1];
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
            // closed to Start() and End() by lines, so that the stretches of one loop add up
            double ExactSignedArea(const Vertex &o) const {
                const auto [a, b] = ExactEnds();
                double s = Cross(Start() - o, a - o) + Cross(b - o, End() - o);
                ForEachGaussPoint([&](const Vertex &q, const Vertex &t, double w) { s += w * Cross(q - o, t); });
                return 0.5 * s;
            }
            double LengthFraction(const Vertex &p) const {
                std::span<const PrimitiveEdge> approx = Approximation();
                size_t nearest = 0;
                for (size_t k = 1; k < approx.size(); ++k)
                    if (approx[k].Distance(p) < approx[nearest].Distance(p))
                        nearest = k;
                double before = 0.0, total = 0.0;
                for (size_t k = 0; k < approx.size(); ++k) {
                    double l = approx[k].Length();
                    before += k < nearest ? l : 0.0;
                    total += l;
                }
                return (before + approx[nearest].LengthFraction(p) * approx[nearest].Length()) / total;
            }
            Vertex PointAtFraction(double t) const {
                const auto [k, f] = PieceAtFraction(t);
                return data->approx[first + k].PointAtFraction(f);
            }
            Vertex TangentAtFraction(double t) const {
                const auto [k, f] = PieceAtFraction(t);
                return data->approx[first + k].TangentAtFraction(f);
            }
            // b meets this piece at v: one piece over both if they neighbour on one curve, closing it only as the whole of a
            // closed curve
            std::optional<Spline> Joined(const Spline &b, const Vertex &) const {
                std::optional<Spline> s = Joined(b);
                if (!s)
                    s = b.Joined(*this);
                if (!s || (s->Start() == s->End() && !s->IsClosed()))
                    return std::nullopt;
                return s;
            }
            // next goes on where this ends on one curve; on a closed curve also over its seam (the curve then taken round
            // from this one's start)
            std::optional<Spline> Joined(const Spline &next) const {
                if (data != next.data)
                    return std::nullopt;
                const size_t n = data->approx.size();
                std::optional<Spline> s;
                if (last == next.first)
                    s = Spline(data, first, next.last, props);
                else if (last == n && next.first == 0 && first > 0 && next.last <= first &&
                         data->approx.front().Start() == data->approx.back().End()) {
                    const Spline round = Spline(data, 0, n, props).Rebased(Start(), 0.0);
                    s = Spline(round.data, 0, (n - first) + next.last, props);
                }
                if (s)
                    s->ends = MakeEnds(PropsAtEnd(ends, false), PropsAtEnd(next.ends, true));
                return s;
            }
            // the same curve run backwards: the whole curve, its nodes, approximation and parameters reversed, the stretch the
            // same one from the other end
            Spline Reversed() const {
                auto d = std::make_shared<SplineData>();
                d->curve = data->curve.Reversed();
                const double sum = data->curve.First() + data->curve.Last();
                for (size_t k = data->approx.size(); k-- > 0;)
                    d->approx.push_back(std::visit([](const auto &x) -> PrimitiveEdge { return x.Reversed(); }, data->approx[k]));
                for (size_t k = data->params.size(); k-- > 0;)
                    d->params.push_back(sum - data->params[k]);
                for (size_t k = data->nodes.size(); k-- > 0;)
                    d->nodes.emplace_back(sum - data->nodes[k].t, data->nodes[k].p);
                const size_t n = data->approx.size();
                Spline r(d, n - last, n - first, props);
                r.ends = Swapped(ends);
                return r;
            }
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
            // this spline cut at the points; a closed one taken round from the first of them, cut also halfway round
            // from it if that is the only one (as a circle)
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
            // a closed spline as two, cut at its middle node: the core's closed spline before it was one edge, as the
            // OCC export takes it
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
            // how far the approximation of the whole curve may lie from it: spline_tol times its size
            double ApproximationTol() const {
                BBox all;
                for (const PrimitiveEdge &e : data->approx)
                    all.Grow(BoxOf(e));
                return config::state->spline_tol * Norm(all.Size());
            }
            // the whole of a closed curve: start and end are one point, no vertex
            bool IsClosed() const { return first == 0 && last == data->approx.size() && Start() == End(); }
            // the stretch from the point a to the point b of this spline, cut out of its approximation, its ends exactly a and
            // b; no a or no b: this spline's own start or end
            Spline Part(const std::optional<Vertex> &a, const std::optional<Vertex> &b) const {
                SplitPoints cuts;
                if (a)
                    cuts.Add(0.0, *a);
                if (b)
                    cuts.Add(0.0, *b);
                for (Spline &p : Split(cuts, ApproximationTol()))
                    if ((!a || p.Start() == *a) && (!b || p.End() == *b))
                        return std::move(p);
                throw std::logic_error("Spline::Part: no piece between the two points");
            }
            std::vector<Spline> SplitAt(std::vector<std::pair<size_t, Vertex>> at, double callerTol) const {
                auto d = std::make_shared<SplineData>(*data);
                const double tol = std::max(Tol(LengthScale()), callerTol);
                auto along = [&](const std::pair<size_t, Vertex> &p) {
                    return std::visit([&](const auto &e) { return e.Param(p.second); }, d->approx[first + p.first]);
                };
                std::sort(at.begin(), at.end(), [&](const auto &p, const auto &q) {
                    return p.first != q.first ? p.first > q.first : along(p) > along(q);
                });
                std::vector<size_t> cuts{first};
                size_t end = last;
                for (const auto &[k, x] : at) {
                    const size_t i = first + k;
                    const Location l = std::visit([&](const auto &e) { return e.Locate(x, tol); }, d->approx[i]);
                    if (l == Location::Off)
                        continue;
                    if (l == Location::Inside) {
                        SplitPiece(*d, i, x);
                        ++end;
                        for (size_t &c : cuts)
                            if (c > i)
                                ++c;
                    }
                    cuts.push_back(l == Location::Inside ? i + 1 : SnapJoint(*d, l == Location::AtStart ? i : i + 1, x));
                }
                cuts.push_back(end);
                std::sort(cuts.begin(), cuts.end());
                cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
                std::vector<Spline> pieces;
                for (size_t i = 0; i + 1 < cuts.size(); ++i)
                    pieces.push_back(Spline(d, cuts[i], cuts[i + 1], props));
                KeepOuterEnds(pieces, ends);
                return pieces;
            }
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
