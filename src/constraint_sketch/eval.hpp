// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "constraint_sketch/dual.hpp"
#include "core/vertex.hpp"
#include "curves/blend.hpp"
#include "curves/interpolate.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::cs {

    using sketch::Element, sketch::ElementKind, sketch::OffsetCurveData, sketch::Part, sketch::Ref, sketch::SplineData, sketch::SplinePiece;

    // a line: a point on it and its direction
    struct Carrier {
        DualVector a, u;

        DualVector Mirrored(const DualVector &p) const { return (a + u * Dot(p - a, u)) * Dual(2.0) - p; }
    };
    struct Round {
        DualVector c;
        Dual r;
    };

    // the splines' interpolations with derivatives over their own unknowns, each made once for all constraints on it
    // while its values stay the same
    class SharedCurves {
    public:
        struct Shared {
            std::vector<size_t> unknowns;
            std::vector<double> inputs;
            Curve<Dual, DualVector> curve;
        };

        const Shared &Of(const sketch::Geometry &g, size_t spline, const std::vector<double> &x, double scale);
        void Clear() { bySpline.clear(); }

    private:
        std::map<size_t, Shared> bySpline;

        // the free unknowns of a spline, sorted
        static std::vector<size_t> UnknownsOf(const sketch::Geometry &g, const SplineData &d) {
            std::vector<size_t> out(d.tangent);
            for (size_t p : d.points) {
                out.push_back(g.coordsOf[p]);
                out.push_back(g.coordsOf[p] + 1);
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            std::erase_if(out, [&](size_t u) { return g.held[u]; });
            return out;
        }
    };

    // the system at the values x, every number a dual one with derivatives with respect to the unknowns idx
    struct Eval {
        const sketch::Geometry &s;
        SharedCurves &shared;
        const std::vector<double> &x;
        const std::vector<size_t> &idx;
        // lever: the length the directions of the constraint's elements are taken over (the size if none)
        double size, lever;
        mutable std::deque<std::pair<size_t, Curve<Dual, DualVector>>> curves;

        Eval(const sketch::Geometry &g, SharedCurves &curves, const std::vector<double> &values, const std::vector<size_t> &unknowns, double scale,
             double arm = 0.0)
            : s(g), shared(curves), x(values), idx(unknowns), size(scale), lever(arm > 0.0 ? arm : scale) {
            active = idx.size();
        }

        Dual Var(size_t u) const {
            Dual d(x[u]);
            for (size_t k = 0; k < idx.size(); ++k)
                if (idx[k] == u) {
                    d.g[k] = 1.0;
                    break;
                }
            return d;
        }
        DualVector At(size_t point) const {
            const size_t u = s.coordsOf[point];
            return {Var(u), Var(u + 1)};
        }
        // a spline's interpolation with every number a dual one: its chord lengths, knots and poles carry their
        // derivatives with respect to its points and tangents (made once per evaluation)
        const Curve<Dual, DualVector> &CurveOf(const Element &e) const {
            const size_t of = e.Spline(s);
            for (const auto &[spline, curve] : curves)
                if (spline == of)
                    return curve;
            const SplineData &d = s.splines[of];
            if (d.fitted)
                return Store(of, Placed(*d.fitted));
            if (d.blend)
                return Store(of, Blended(d));
            if (std::all_of(d.points.begin(), d.points.end(), [&](size_t p) { return s.held[s.coordsOf[p]] && s.held[s.coordsOf[p] + 1]; }) &&
                std::all_of(d.tangent.begin(), d.tangent.end(), [&](size_t a) { return bool(s.held[a]); })) {
                // held for good (projected): its values, no derivatives
                return Store(of, Converted(d.Interpolated(s), [](double k) { return Dual(k); }));
            }
            if (idx.empty())
                return Store(of, Interpolated(d));
            const SharedCurves::Shared &k = shared.Of(s, of, x, size);
            return Store(of, Remapped(k.curve, k.unknowns));
        }
        // a fitted spline's shape placed by its frame: its poles carry the derivatives with respect to the frame's place
        // and turn, the shape is a constant
        Curve<Dual, DualVector> Placed(const sketch::FittedShape &f) const {
            const DualVector o = At(f.origin);
            const Dual turn = Var(f.angle), c = Cos(turn), sn = Sin(turn);
            Curve<Dual, DualVector> g = Converted(f.shape, [](double k) { return Dual(k); });
            for (size_t k = 0; k < g.poles.size(); ++k)
                g.poles[k] = PlacedPoint(f, f.shape.poles[k], o, c, sn);
            return g;
        }
        static DualVector PlacedPoint(const sketch::FittedShape &f, const Vertex &q, const DualVector &o, const Dual &c, const Dual &sn) {
            const Dual x(q.x), y(f.mirrored ? -q.y : q.y);
            return {o.x + c * x - sn * y, o.y + sn * x + c * y};
        }
        // where a fitted spline's shape puts its first (end: last) pole
        DualVector PlacedEnd(const sketch::FittedShape &f, bool end) const {
            const Dual turn = Var(f.angle);
            return PlacedPoint(f, end ? f.shape.poles.back() : f.shape.poles.front(), At(f.origin), Cos(turn), Sin(turn));
        }
        // a blend's curve from the directions and curvatures of the elements it joins at its ends
        Curve<Dual, DualVector> Blended(const SplineData &d) const {
            std::array<DualVector, 2> t;
            std::array<Dual, 2> k;
            for (size_t i = 0; i < 2; ++i) {
                const Ref arm{d.blend->arms[i]};
                const Dual sense(d.BlendSense(s, i));
                t[i] = Unit(TangentAt(arm, d.points[i])) * sense;
                k[i] = CurvatureAt(arm, d.points[i]) * sense;
            }
            const DualVector p0 = At(d.points[0]), p1 = At(d.points[1]);
            return d.blend->curvature ? G2Blend<Dual, DualVector>(p0, t[0], k[0], p1, t[1], k[1]) : G1Blend<Dual, DualVector>(p0, t[0], p1, t[1]);
        }
        const Curve<Dual, DualVector> &Store(size_t of, Curve<Dual, DualVector> &&curve) const {
            curves.emplace_back(of, std::move(curve));
            return curves.back().second;
        }
        Curve<Dual, DualVector> Interpolated(const SplineData &d) const {
            std::vector<DualVector> q, t;
            for (size_t p : d.points)
                q.push_back(At(p));
            for (size_t a : d.tangent) {
                const Dual angle = Var(a);
                t.push_back({Cos(angle), Sin(angle)});
            }
            return Interpolate<Dual>(q, d.tangentAt, t, d.closed, [](const DualVector &v) { return Norm(v); }, d.style);
        }
        // the shared curve, whose derivatives run over the spline's own unknowns, with them over this constraint's
        Curve<Dual, DualVector> Remapped(const Curve<Dual, DualVector> &g, const std::vector<size_t> &own) const {
            std::vector<long> from(idx.size(), -1);
            for (size_t k = 0; k < idx.size(); ++k)
                if (const auto it = std::lower_bound(own.begin(), own.end(), idx[k]); it != own.end() && *it == idx[k])
                    from[k] = it - own.begin();
            const auto dual = [&](const Dual &a) {
                Dual r(a.v);
                for (size_t k = 0; k < idx.size(); ++k)
                    if (from[k] >= 0)
                        r.g[k] = a.g[from[k]];
                return r;
            };
            return Converted(g, dual);
        }
        // g with each of its numbers made a dual one by dual
        template <class S, class P, class F>
        static Curve<Dual, DualVector> Converted(const Curve<S, P> &g, F dual) {
            Curve<Dual, DualVector> f;
            f.degree = g.degree;
            f.closed = g.closed;
            f.tangentAt = g.tangentAt;
            for (const S &k : g.knots)
                f.knots.push_back(dual(k));
            for (const S &u : g.params)
                f.params.push_back(dual(u));
            for (const P &v : g.poles)
                f.poles.push_back({dual(v.x), dual(v.y)});
            return f;
        }
        // the k-th derivative of a spline at the chord length u
        DualVector SplineAt(const Element &e, const Dual &u, size_t k) const { return CurveOf(e).At(u, k); }
        // a spline's or offset curve's point (k = 0) at the chord length u of its spline, and a vector along it as u
        // grows (k = 1; of an offset curve its spline's derivative, which runs the same way while it has no cusp)
        DualVector CurveAt(const Element &e, const Dual &u, size_t k) const {
            const DualVector p = SplineAt(e, u, k);
            if (e.kind != ElementKind::OffsetCurve || k > 0)
                return p;
            const OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
            return p + Left(Unit(SplineAt(e, u, 1))) * (Var(c.distance) * Dual(c.side));
        }
        // where an offset curve starts and ends along its spline: from, to
        std::array<Dual, 2> OffsetRange(const Element &e) const {
            const OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
            const SplinePiece *source = c.piece ? &s.pieces[c.piece - 1] : nullptr;
            const auto end = [&](size_t mine, size_t inherited, const Dual &otherwise) {
                return mine != sketch::none ? Var(mine) : inherited != sketch::none ? Var(inherited) : otherwise;
            };
            return {end(c.range.from, source ? source->from : sketch::none, Dual(0.0)),
                    end(c.range.to, source ? source->to : sketch::none, CurveOf(e).params.back())};
        }
        // the direction of a line, arc, circle or spline at one of its points
        DualVector TangentAt(const Ref &r, size_t point) const {
            const Element &e = s.elements[r.element];
            if (e.kind == ElementKind::OffsetCurve) {
                const OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
                const std::array<Dual, 2> t = OffsetRange(e);
                return CurveAt(e, t[s.coordsOf[c.range.toPoint] == s.coordsOf[point]], 1) * Dual(c.reversed ? -1.0 : 1.0);
            }
            if (e.kind == ElementKind::Spline) {
                const SplineData &d = s.splines[e.SplineIndex()];
                for (size_t t = 0; t < d.tangentAt.size(); ++t)
                    if (s.coordsOf[d.points[d.tangentAt[t]]] == s.coordsOf[point])
                        return {Cos(Var(d.tangent[t])), Sin(Var(d.tangent[t]))};
                return SplineAt(e, SplineParamAt(e, point), 1);
            }
            if (e.kind == ElementKind::Line || e.kind == ElementKind::Axis)
                return CarrierOf(r).u;
            return Left(At(point) - RoundOf(r).c);
        }
        // where one of a spline element's points lies along its spline: a piece's end, or a point it runs through (a
        // fitted spline's points are its ends, its parameters run over the knots between them)
        Dual SplineParamAt(const Element &e, size_t point) const {
            if (const SplinePiece *piece = e.Piece(s)) {
                if (piece->fromPoint != sketch::none && s.coordsOf[piece->fromPoint] == s.coordsOf[point])
                    return Var(piece->from);
                if (piece->toPoint != sketch::none && s.coordsOf[piece->toPoint] == s.coordsOf[point])
                    return Var(piece->to);
            }
            const SplineData &d = s.splines[e.SplineIndex()];
            const std::vector<Dual> &u = CurveOf(e).params;
            for (size_t k = 0; k < d.points.size(); ++k)
                if (s.coordsOf[d.points[k]] == s.coordsOf[point])
                    return d.fitted ? (k == 0 ? u.front() : u.back()) : u[k];
            return u.back();
        }
        // the curvature of a line, arc, circle, spline or offset curve at one of its points, signed along TangentAt (a
        // left turn positive)
        Dual CurvatureAt(const Ref &r, size_t point) const {
            const Element &e = s.elements[r.element];
            if (e.kind == ElementKind::Line || e.kind == ElementKind::Axis)
                return Dual(0.0);
            if (e.kind == ElementKind::Arc || e.kind == ElementKind::Circle)
                return Dual(1.0) / Var(e.Radius());
            if (e.kind == ElementKind::OffsetCurve) {
                const OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
                const Dual u = OffsetRange(e)[s.coordsOf[c.range.toPoint] == s.coordsOf[point]];
                const Dual k = Bending(SplineAt(e, u, 1), SplineAt(e, u, 2));
                // offset by d to the left, a curve bending by k bends by k / (1 - d k)
                return k / (Dual(1.0) - Var(c.distance) * Dual(c.side) * k) * Dual(c.reversed ? -1.0 : 1.0);
            }
            const Dual u = SplineParamAt(e, point);
            return Bending(SplineAt(e, u, 1), SplineAt(e, u, 2));
        }
        static Dual Bending(const DualVector &d1, const DualVector &d2) {
            const Dual speed = Norm(d1);
            return Cross(d1, d2) / (speed * speed * speed);
        }
        DualVector PointOf(const Ref &r) const {
            const Element &e = s.elements[r.element];
            if (r.part != Part::Mid)
                return At(r.PointAt(s));
            if (e.kind == ElementKind::OffsetCurve) {
                const std::array<Dual, 2> t = OffsetRange(e);
                return CurveAt(e, (t[0] + t[1]) * Dual(0.5), 0);
            }
            if (e.kind == ElementKind::Spline) {
                const Dual end = CurveOf(e).params.back();
                const SplinePiece *piece = e.Piece(s);
                if (!piece)
                    return SplineAt(e, end * Dual(0.5), 0);
                // a piece over a closed spline's seam ends before it starts
                const Dual from = piece->from != sketch::none ? Var(piece->from) : Dual(0.0);
                Dual to = piece->to != sketch::none ? Var(piece->to) : end;
                if (to.v < from.v)
                    to = to + end;
                Dual mid = (from + to) * Dual(0.5);
                if (mid.v > end.v)
                    mid = mid - end;
                return SplineAt(e, mid, 0);
            }
            if (e.kind == ElementKind::Line)
                return (At(e.Start()) + At(e.End())) * Dual(0.5);
            // on the arc's side of its chord, also for arcs over 180 degrees
            const DualVector chord = At(e.End()) - At(e.Start());
            return At(e.Center()) + Unit(e.ccw ? Right(chord) : Left(chord)) * Var(e.Radius());
        }
        Carrier CarrierOf(const Ref &r) const {
            const Element &e = s.elements[r.element];
            if (r.part == Part::Tangent) {
                const size_t p = r.PointAt(s);
                return {At(p), Unit(TangentAt({r.element}, p))};
            }
            if (e.kind == ElementKind::Line) {
                const DualVector a = At(e.Start());
                return {a, Unit(At(e.End()) - a)};
            }
            const Dual t = Var(e.AxisAngle());
            const DualVector n{Cos(t), Sin(t)};
            return {n * Var(e.AxisDistance()), Left(n)};
        }
        std::array<DualVector, 2> EndsOf(const Ref &r) const {
            const std::array<size_t, 2> p = s.elements[r.element].Ends(s);
            return {At(p[0]), At(p[1])};
        }
        Round RoundOf(const Ref &r) const {
            const Element &e = s.elements[r.element];
            return {At(e.Center()), Var(e.Radius())};
        }
        DualVector CenterOf(const Ref &r) const { return r.IsRound(s) ? RoundOf(r).c : PointOf(r); }
        // where a distance to a line is measured: a line's midpoint (any point of it while it is parallel to the
        // reference), an axis's point nearest the origin
        DualVector Measured(const Ref &r) const {
            if (!r.IsSegment(s))
                return CarrierOf(r).a;
            const std::array<DualVector, 2> p = EndsOf(r);
            return (p[0] + p[1]) * Dual(0.5);
        }
    };

    inline const SharedCurves::Shared &SharedCurves::Of(const sketch::Geometry &g, size_t spline, const std::vector<double> &x, double scale) {
        const SplineData &d = g.splines[spline];
        Shared &k = bySpline[spline];
        const std::vector<size_t> own = UnknownsOf(g, d);
        std::vector<double> inputs;
        for (size_t p : d.points) {
            inputs.push_back(x[g.coordsOf[p]]);
            inputs.push_back(x[g.coordsOf[p] + 1]);
        }
        for (size_t a : d.tangent)
            inputs.push_back(x[a]);
        if (k.unknowns != own || k.inputs != inputs) {
            const size_t outer = active;
            k.curve = Eval{g, *this, x, own, scale}.Interpolated(d);
            active = outer;
            k.unknowns = own;
            k.inputs = inputs;
        }
        return k;
    }

}
