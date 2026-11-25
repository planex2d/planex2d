// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/spline.hpp"

#include <stdexcept>

namespace planex2d {

    size_t Spline::InsertVertex(SplineData &d, size_t first, size_t &last, const Vertex &x, double tol) {
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

    std::vector<Spline> Spline::Pieces(const Curve<double, Vertex> &c, const std::vector<double> &u,
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

    // clamped scales the tangents as OCC does, whatever their length
    Curve<double, Vertex> Spline::Interpolated(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic,
                                               double tol, SplineStyle style, const std::map<size_t, TangentSetting<double>> &settings) {
        CheckSpline(pts, tangents, periodic, tol, settings);
        std::vector<size_t> at;
        std::vector<Vertex> given;
        std::vector<TangentSetting<double>> set;
        for (const auto &[i, v] : tangents) {
            at.push_back(i);
            given.push_back(style == SplineStyle::Clamped ? v : Normalized(v));
            const auto s = settings.find(i);
            set.push_back(s == settings.end() ? TangentSetting<double>{} : s->second);
        }
        return Interpolate(pts, at, given, periodic, style, set);
    }

    std::vector<Spline> Spline::Through(const std::vector<Vertex> &pts, bool periodic, double tol, const std::map<size_t, Vertex> &tangents,
                                        SplineStyle style, const std::map<size_t, TangentSetting<double>> &settings) {
        static Timer t("planex Spline Through");
        Region reg(t);
        const Curve<double, Vertex> c = Interpolated(pts, tangents, periodic, tol, style, settings);
        std::vector<Vertex> nodes = pts;
        if (periodic)
            nodes.push_back(pts.front());
        return Pieces(c, ChordParams(nodes), nodes, ApproximationTol(pts));
    }

    // its approximation is within spline_tol of the fitted curve, as every spline's; the chain's pieces are only within tol
    std::optional<Spline> Spline::Fitted(const std::vector<PrimitiveEdge> &chain, const std::vector<Vertex> &pts, double tol,
                                         const std::optional<Vertex> &startTangent, const std::optional<Vertex> &endTangent) {
        if (!config::state->spline_fitter)
            throw std::logic_error("Spline::Fitted: no spline fit loaded");
        std::optional<Curve<double, Vertex>> c = config::state->spline_fitter(chain, pts, tol, startTangent, endTangent);
        if (!c)
            return std::nullopt;
        // a node in the middle: a closed curve's two ends are one point
        const double mid = 0.5 * (c->First() + c->Last());
        Spline s = Pieces(*c, {c->First(), mid, c->Last()}, {chain.front().Start(), c->At(mid), chain.back().End()}, ApproximationTol(pts)).front();
        s.props = PropsOf(chain.front());
        return s;
    }

    // a point where the curve crosses itself is two joints, so a stretch that starts there names its joint, not the point
    Spline Spline::Rebased(const SplineData &d, size_t j, const Properties &props) {
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

    std::vector<Spline> Spline::SplitOpen(const SplitPoints &pts, double callerTol) const {
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

    std::pair<size_t, double> Spline::PieceAtFraction(double t) const {
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

    double Spline::ParamWithin(const Vertex &p, double tol) const {
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

    std::vector<Spline> Spline::FromCurve(const Curve<double, Vertex> &c) {
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

    std::pair<double, Vertex> Spline::ExactNearest(const Vertex &p) const {
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

    std::optional<Vertex> Spline::ExactTangentAlong(const Vertex &d, const Vertex &p) const {
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

    double Spline::ExactParamAt(double s) const {
        const double lo = data->params[first], hi = data->params[last];
        double u = ExactNearest(PointAtFraction(s / ExactLength())).first;
        for (int it = 0; it < 30; ++it) {
            const double speed = Norm(data->curve.At(u, 1));
            // a parametrization that comes to a stop at an end (FromNACA's trailing edge): no step
            if (!(speed > 0.0))
                break;
            const double step = (ExactLengthTo(u) - s) / speed;
            u = std::clamp(u - step, lo, hi);
            if (std::abs(step) <= 1e-14 * (hi - lo))
                break;
        }
        return u;
    }

    double Spline::LengthFraction(const Vertex &p) const {
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

    std::optional<Spline> Spline::Joined(const Spline &next) const {
        if (data != next.data)
            return std::nullopt;
        const size_t n = data->approx.size();
        std::optional<Spline> s;
        if (last == next.first)
            s = Spline(data, first, next.last, props);
        else if (last == n && next.first == 0 && first > 0 && next.last <= first &&
                 data->approx.front().Start() == data->approx.back().End()) {
            const Spline round = Rebased(*data, first, props);
            s = Spline(round.data, 0, (n - first) + next.last, props);
        }
        if (s)
            s->ends = MakeEnds(PropsAtEnd(ends, false), PropsAtEnd(next.ends, true));
        return s;
    }

    Spline Spline::Reversed() const {
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

    Spline Spline::Part(const std::optional<Vertex> &a, const std::optional<Vertex> &b) const {
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

    std::vector<Spline> Spline::SplitAt(std::vector<std::pair<size_t, Vertex>> at, double callerTol) const {
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

}
