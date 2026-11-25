// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "planex/constraint_sketch/dual.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/blend.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/sketch/geometry.hpp"

namespace planex2d::cs {

    using sketch::Element, sketch::ElementKind, sketch::OffsetCurveData, sketch::Part, sketch::Ref, sketch::SplineData, sketch::SplinePiece;

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

        static std::vector<size_t> UnknownsOf(const sketch::Geometry &g, const SplineData &d);
    };

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
        const Curve<Dual, DualVector> &CurveOf(const Element &e) const;
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
        DualVector PlacedEnd(const sketch::FittedShape &f, bool end) const {
            const Dual turn = Var(f.angle);
            return PlacedPoint(f, end ? f.shape.poles.back() : f.shape.poles.front(), At(f.origin), Cos(turn), Sin(turn));
        }
        Curve<Dual, DualVector> Blended(const SplineData &d) const;
        const Curve<Dual, DualVector> &Store(size_t of, Curve<Dual, DualVector> &&curve) const {
            curves.emplace_back(of, std::move(curve));
            return curves.back().second;
        }
        Curve<Dual, DualVector> Interpolated(const SplineData &d) const;
        Curve<Dual, DualVector> Remapped(const Curve<Dual, DualVector> &g, const std::vector<size_t> &own) const;
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
        std::array<Dual, 2> OffsetRange(const Element &e) const {
            const OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
            const SplinePiece *source = c.piece ? &s.pieces[c.piece - 1] : nullptr;
            const auto end = [&](size_t mine, size_t inherited, const Dual &otherwise) {
                return mine != sketch::none ? Var(mine) : inherited != sketch::none ? Var(inherited) : otherwise;
            };
            return {end(c.range.from, source ? source->from : sketch::none, Dual(0.0)),
                    end(c.range.to, source ? source->to : sketch::none, CurveOf(e).params.back())};
        }
        DualVector TangentAt(const Ref &r, size_t point) const;
        Dual SplineParamAt(const Element &e, size_t point) const;
        Dual CurvatureAt(const Ref &r, size_t point) const;
        static Dual Bending(const DualVector &d1, const DualVector &d2) {
            const Dual speed = Norm(d1);
            return Cross(d1, d2) / (speed * speed * speed);
        }
        DualVector PointOf(const Ref &r) const;
        Carrier CarrierOf(const Ref &r) const;
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

}
