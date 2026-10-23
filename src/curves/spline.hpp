// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <Geom2d_BSplineCurve.hxx>
#include <Geom_Curve.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/primitiveedge.hpp"
#include "core/timer.hpp"
#include "curves/interpolate.hpp"
#include "curves/occspline.hpp"
#include "curves/primitivefit.hpp"

namespace planex2d {

    class Wire;

    struct SplineData {
        Handle(Geom2d_BSplineCurve) curve;
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

            template <class C, class P, class E>
            void Transform(C curve, P point, E edge) {
                auto d = std::make_shared<SplineData>(*data);
                Handle(Geom2d_BSplineCurve) r = Handle(Geom2d_BSplineCurve)::DownCast(d->curve->Copy());
                curve(*r);
                d->curve = r;
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
                d.params.insert(d.params.begin() + (long)k + 1, OccParam(d.curve, x, d.params[k], d.params[k + 1]));
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
            static std::vector<Spline> Pieces(Handle(Geom2d_BSplineCurve) c, const std::vector<double> &u,
                                              const std::vector<Vertex> &nodes, double atol) {
                auto d = std::make_shared<SplineData>();
                d->curve = c;
                d->params.push_back(u.front());
                std::vector<size_t> at;
                for (size_t i = 0; i < u.size(); ++i) {
                    d->nodes.emplace_back(u[i], nodes[i]);
                    at.push_back(d->approx.size());
                    if (i + 1 < u.size())
                        FitPrimitiveEdges(OccCurve{c}, u[i], u[i + 1], nodes[i], nodes[i + 1], atol, d->approx, d->params, 0);
                }
                if (d->approx.empty())
                    throw std::invalid_argument("degenerate spline: zero length");
                for (auto &e : d->approx)
                    std::visit([](auto &x) { x.fromSpline = true; }, e);
                size_t n = d->approx.size();
                // a closed edge would have start == end: split at the middle node
                if (!(nodes.front() == nodes.back()))
                    return {Spline(d, 0, n, {})};
                size_t m = at[u.size() / 2];
                return {Spline(d, 0, m, {}), Spline(d, m, n, {})};
            }
            static Handle(Geom2d_BSplineCurve) InterpolatePx(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents,
                                                             bool periodic, double tol) {
                CheckSpline(pts, tangents, periodic, tol);
                std::vector<size_t> at;
                std::vector<Vertex> unit;
                for (const auto &[i, v] : tangents) {
                    at.push_back(i);
                    unit.push_back(Normalized(v));
                }
                const Curve<double, Vertex> f = Interpolate(pts, at, unit, periodic);
                return MakeBSpline(BSplineData{f.poles, f.knots, {}, (int)f.degree}, false);
            }
            // chord length parameters, as Geom2dAPI_Interpolate builds them
            bool IsOffChord(const Vertex &a, const Vertex &b, double u, double tol) const {
                const Vertex p = OccPoint(data->curve, u), d = b - a;
                const double l = Norm(d);
                return l > 0.0 ? std::abs(Cross(d, p - a)) / l > tol : Norm(p - a) > tol;
            }
            void SampleSpan(std::vector<Vertex> &out, double ua, const Vertex &a, double ub, const Vertex &b, double tol, int depth) const {
                const double um = 0.5 * (ua + ub);
                // 2^40 chords: far below any tol
                if (depth < 40 && (IsOffChord(a, b, um, tol) || IsOffChord(a, b, 0.5 * (ua + um), tol) || IsOffChord(a, b, 0.5 * (um + ub), tol))) {
                    const Vertex m = OccPoint(data->curve, um);
                    SampleSpan(out, ua, a, um, m, tol, depth + 1);
                    SampleSpan(out, um, m, ub, b, tol, depth + 1);
                } else
                    out.push_back(b);
            }
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
                return config::spline_tol * Norm(all.Size());
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

            // this stretch of the curve as its own OCC curve, its end poles exactly at its ends
            Handle(Geom2d_BSplineCurve) Segment() const {
                Handle(Geom2d_BSplineCurve) c = Handle(Geom2d_BSplineCurve)::DownCast(data->curve->Copy());
                c->Segment(data->params[first], data->params[last]);
                c->SetPole(1, gp_Pnt2d(Start().x, Start().y));
                c->SetPole(c->NbPoles(), gp_Pnt2d(End().x, End().y));
                return c;
            }

            static std::vector<Spline> Through(const std::vector<Vertex> &pts, bool periodic, double tol,
                                               const std::map<size_t, Vertex> &tangents, SplineStyle style = config::spline_style) {
                static Timer t("planex Spline Through");
                Region reg(t);
                Handle(Geom2d_BSplineCurve) c =
                    style == SplineStyle::Occ ? InterpolateOcc(pts, tangents, periodic, tol, true) : InterpolatePx(pts, tangents, periodic, tol);
                std::vector<Vertex> nodes = pts;
                if (periodic)
                    nodes.push_back(pts.front());
                return Pieces(c, ChordParams(nodes), nodes, ApproximationTol(pts));
            }
            static std::optional<Spline> Fitted(std::vector<PrimitiveEdge> chain, const std::vector<Vertex> &pts, double tol,
                                                const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent);

            // atol: the approximation's tolerance, if not that of the nodes (a piece fitted like the whole it is part of)
            static std::vector<Spline> FromNodes(const Handle(Geom2d_BSplineCurve) &c, const std::vector<double> &u,
                                                 const std::vector<Vertex> &nodes, std::optional<double> atol = std::nullopt) {
                return Pieces(c, u, nodes, atol.value_or(ApproximationTol(nodes)));
            }

            static std::vector<Spline> FromCurve(const Handle(Geom2d_BSplineCurve) &c) {
                static Timer t("planex Spline FromCurve");
                Region reg(t);
                std::vector<double> u;
                std::vector<Vertex> nodes;
                BBox all;
                for (int i = 1; i <= c->NbKnots(); ++i) {
                    u.push_back(c->Knot(i));
                    nodes.push_back(OccPoint(c, u.back()));
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
            // b meets this piece at v: one piece over both if they neighbour on one curve, never closing it
            std::optional<Spline> Joined(const Spline &b, const Vertex &) const {
                std::optional<Spline> s = Joined(b);
                if (!s)
                    s = b.Joined(*this);
                if (!s || s->Start() == s->End())
                    return std::nullopt;
                return s;
            }
            std::optional<Spline> Joined(const Spline &next) const {
                if (data != next.data || last != next.first)
                    return std::nullopt;
                Spline s(data, first, next.last, props);
                s.ends = MakeEnds(PropsAtEnd(ends, false), PropsAtEnd(next.ends, true));
                return s;
            }
            // the same curve run backwards: the whole curve, its nodes, approximation and parameters reversed, the stretch the
            // same one from the other end
            Spline Reversed() const {
                auto d = std::make_shared<SplineData>();
                d->curve = Handle(Geom2d_BSplineCurve)::DownCast(data->curve->Copy());
                d->curve->Reverse();
                for (size_t k = data->approx.size(); k-- > 0;)
                    d->approx.push_back(std::visit([](const auto &x) -> PrimitiveEdge { return x.Reversed(); }, data->approx[k]));
                for (size_t k = data->params.size(); k-- > 0;)
                    d->params.push_back(data->curve->ReversedParameter(data->params[k]));
                for (size_t k = data->nodes.size(); k-- > 0;)
                    d->nodes.emplace_back(data->curve->ReversedParameter(data->nodes[k].t), data->nodes[k].p);
                const size_t n = data->approx.size();
                Spline r(d, n - last, n - first, props);
                r.ends = Swapped(ends);
                return r;
            }
            const BBox &Box() const { return box; }
            void Rotate(const Vertex &c, double cs, double sn) {
                Transform([&](Geom2d_BSplineCurve &r) { r.Rotate(gp_Pnt2d(c.x, c.y), std::atan2(sn, cs)); },
                          [&](const Vertex &p) { return Rotated(p, c, cs, sn); }, [&](auto &x) { x.Rotate(c, cs, sn); });
            }
            void Translate(const Vertex &t) {
                Transform([&](Geom2d_BSplineCurve &r) { r.Translate(gp_Vec2d(t.x, t.y)); },
                          [&](const Vertex &p) { return p + t; }, [&](auto &x) { x.Translate(t); });
            }
            void Mirror(const Vertex &a, const Vertex &d) {
                Transform([&](Geom2d_BSplineCurve &r) { r.Mirror(gp_Ax2d(gp_Pnt2d(a.x, a.y), gp_Dir2d(d.x, d.y))); },
                          [&](const Vertex &p) { return Mirrored(p, a, d); }, [&](auto &x) { x.Mirror(a, d); });
            }
            void Scale(const Vertex &c, double s) {
                Transform([&](Geom2d_BSplineCurve &r) { r.Scale(gp_Pnt2d(c.x, c.y), s); },
                          [&](const Vertex &p) { return Scaled(p, c, s); }, [&](auto &x) { x.Scale(c, s); });
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
            // the stretch from the point a to the point b of this spline, cut out of its approximation, its ends exactly a and
            // b; no a or no b: this spline's own start or end
            Spline Part(const std::optional<Vertex> &a, const std::optional<Vertex> &b) const {
                BBox all;
                for (const PrimitiveEdge &e : data->approx)
                    all.Grow(BoxOf(e));
                SplitPoints cuts;
                if (a)
                    cuts.Add(0.0, *a);
                if (b)
                    cuts.Add(0.0, *b);
                for (Spline &p : Split(cuts, config::spline_tol * Norm(all.Size())))
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
                               k + 1 == last ? End() : OccPoint(data->curve, data->params[k + 1]), tol, 0);
                return out;
            }
            BSplineData BSpline() const {
                Handle(Geom2d_BSplineCurve) c = Segment();
                BSplineData b;
                b.degree = c->Degree();
                for (int i = 1; i <= c->NbPoles(); ++i)
                    b.poles.push_back({c->Pole(i).X(), c->Pole(i).Y()});
                for (int i = 1; i <= c->NbKnots(); ++i)
                    for (int m = 0; m < c->Multiplicity(i); ++m)
                        b.knots.push_back(c->Knot(i));
                if (c->IsRational())
                    for (int i = 1; i <= c->NbPoles(); ++i)
                        b.weights.push_back(c->Weight(i));
                return b;
            }
            Wire Offset(double d) const;
    };

    inline std::vector<Spline> Split(const Spline &s, const SplitPoints &pts, double tol) { return s.Split(pts, tol); }

}
