// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/eval.hpp"

#include <algorithm>

namespace planex2d::cs {

    std::vector<size_t> SharedCurves::UnknownsOf(const sketch::Geometry &g, const SplineData &d) {
        std::vector<size_t> out(d.tangent);
        for (size_t c : d.curvature)
            if (c != sketch::none)
                out.push_back(c);
        for (size_t p : d.points) {
            out.push_back(g.coordsOf[p]);
            out.push_back(g.coordsOf[p] + 1);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        std::erase_if(out, [&](size_t u) { return g.held[u]; });
        return out;
    }

    // a spline's interpolation with every number a dual one: its chord lengths, knots and poles carry their
    // derivatives with respect to its points and tangents (made once per evaluation)
    const Curve<Dual, DualVector> &Eval::CurveOf(const Element &e) const {
        const size_t of = e.Spline(s);
        for (const auto &[spline, curve] : curves)
            if (spline == of)
                return curve;
        const SplineData &d = s.splines[of];
        if (d.fitted)
            return Store(of, Placed(*d.fitted));
        if (d.blend)
            return Store(of, Blended(d));
        if (d.poled) {
            std::vector<DualVector> poles;
            for (size_t p : d.points)
                poles.push_back(At(p));
            return Store(of, PoleCurve<Dual, DualVector>(poles, d.poled->knots, d.poled->degree, d.closed));
        }
        if (std::all_of(d.points.begin(), d.points.end(), [&](size_t p) { return s.held[s.coordsOf[p]] && s.held[s.coordsOf[p] + 1]; }) &&
            std::all_of(d.tangent.begin(), d.tangent.end(), [&](size_t a) { return bool(s.held[a]); }) &&
            std::all_of(d.curvature.begin(), d.curvature.end(), [&](size_t c) { return c == sketch::none || s.held[c]; })) {
            // held for good (projected): its values, no derivatives
            return Store(of, Converted(d.Interpolated(s), [](double k) { return Dual(k); }));
        }
        if (idx.empty())
            return Store(of, Interpolated(d));
        const SharedCurves::Shared &k = shared.Of(s, of, x, size);
        return Store(of, Remapped(k.curve, k.unknowns));
    }

    Curve<Dual, DualVector> Eval::Blended(const SplineData &d) const {
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

    Curve<Dual, DualVector> Eval::Interpolated(const SplineData &d) const {
        std::vector<DualVector> q, t;
        for (size_t p : d.points)
            q.push_back(At(p));
        for (size_t a : d.tangent) {
            const Dual angle = Var(a);
            t.push_back({Cos(angle), Sin(angle)});
        }
        std::vector<TangentSetting<Dual>> settings;
        for (size_t k : d.tangentAt) {
            const size_t c = d.CurvatureAt(k);
            settings.push_back({Dual(d.TensionAt(k)), c == sketch::none ? std::nullopt : std::optional<Dual>(Var(c))});
        }
        return Interpolate<Dual>(q, d.tangentAt, t, d.closed, d.style, settings);
    }

    // the shared curve, whose derivatives run over the spline's own unknowns, with them over this constraint's
    Curve<Dual, DualVector> Eval::Remapped(const Curve<Dual, DualVector> &g, const std::vector<size_t> &own) const {
        std::vector<std::ptrdiff_t> from(idx.size(), -1);
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

    DualVector Eval::TangentAt(const Ref &r, size_t point) const {
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

    Dual Eval::SplineParamAt(const Element &e, size_t point) const {
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
                return d.fitted || d.poled ? (k == 0 ? u.front() : u.back()) : u[k];
        return u.back();
    }

    // the curvature of a line, arc, circle, spline or offset curve at one of its points, signed along TangentAt (a
    // left turn positive)
    Dual Eval::CurvatureAt(const Ref &r, size_t point) const {
        const Element &e = s.elements[r.element];
        if (e.kind == ElementKind::Line || e.kind == ElementKind::Axis)
            return Dual(0.0);
        if (e.IsRound())
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

    DualVector Eval::PointOf(const Ref &r) const {
        const Element &e = s.elements[r.element];
        if (r.part != Part::Mid)
            return At(r.PointAt(s));
        if (e.kind == ElementKind::OffsetCurve) {
            const std::array<Dual, 2> t = OffsetRange(e);
            const Dual end = CurveOf(e).params.back();
            Dual mid = (t[0] + (t[1].v < t[0].v ? t[1] + end : t[1])) * Dual(0.5);
            if (mid.v > end.v)
                mid = mid - end;
            return CurveAt(e, mid, 0);
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

    Carrier Eval::CarrierOf(const Ref &r) const {
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

    const SharedCurves::Shared &SharedCurves::Of(const sketch::Geometry &g, size_t spline, const std::vector<double> &x, double scale) {
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
        for (size_t c : d.curvature)
            if (c != sketch::none)
                inputs.push_back(x[c]);
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
