// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/system.hpp"

#include <limits>

namespace planex2d::cs {

    void System::Freeze(Constraint &c, const Eval &e) const {
        switch (c.kind) {
        case Kind::Distance:
        case Kind::Tangent:
            FreezeSide(c, e);
            return;
        case Kind::Curvature:
            if (const std::optional<size_t> j = JointOf(c))
                c.sign = Sense(c.refs[0], c.refs[1], *j, e);
            return;
        case Kind::Angle:
            FreezeAngle(c, e);
            return;
        case Kind::Symmetric:
            FreezeSymmetric(c, e);
            return;
        case Kind::Fix:
        case Kind::FixH:
        case Kind::FixV:
            FreezeFix(c, e);
            return;
        default:
            return;
        }
    }

    // the variant with the smallest residuals
    void System::FreezeSmallest(Constraint &c, const Eval &e, int variants) const {
        double best = 0.0;
        int choice = 0;
        for (int k = 0; k < variants; ++k) {
            c.variant = k;
            std::vector<Dual> r;
            const size_t rows = Residuals(c, e, r, JointOf(c));
            double sum = 0.0;
            for (size_t i = 0; i < rows; ++i)
                sum += r[i].v * r[i].v;
            if (k == 0 || sum < best) {
                best = sum;
                choice = k;
            }
        }
        c.variant = choice;
    }

    void System::FreezeSide(Constraint &c, const Eval &e) const {
        const size_t n = c.refs.size();
        Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n == 2 && (a.IsCurve(*this) || b.IsCurve(*this))) {
            if (c.kind == Kind::Distance && c.params.size() == 1) {
                if (a.IsCurve(*this))
                    std::swap(a, b);
                const Dual t = e.Var(c.params[0]);
                const DualVector d = e.PointOf(a) - e.CurveAt(elements[b.element], t, 0);
                c.sign = Side(Cross(e.CurveAt(elements[b.element], t, 1), d));
            } else if (const std::optional<size_t> j = JointOf(c))
                c.sign = Sense(a, b, *j, e);
            return;
        }
        if (n == 3 && a.IsPoint(*this) && b.IsPoint(*this) && c.refs[2].IsLine(*this)) {
            c.sign = Side(Cross(e.CarrierOf(c.refs[2]).u, e.PointOf(b) - e.PointOf(a)));
            return;
        }
        if (n != 2 || (a.IsPoint(*this) && b.IsPoint(*this)))
            return;
        if (b.IsPoint(*this) || (a.IsRound(*this) && b.IsLine(*this)))
            std::swap(a, b);
        if (a.IsPoint(*this) && b.IsLine(*this)) {
            const Carrier l = e.CarrierOf(b);
            c.sign = Side(Cross(l.u, e.PointOf(a) - l.a));
        } else if (a.IsPoint(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(b);
            c.sign = Side(Norm(e.PointOf(a) - o.c) - o.r);
        } else if (a.IsLine(*this) && b.IsLine(*this)) {
            if (a.IsSegment(*this) && !b.IsSegment(*this))
                std::swap(a, b);
            const Carrier l = e.CarrierOf(a);
            c.sign = Side(Cross(l.u, e.Measured(b) - l.a));
        } else if (a.IsLine(*this) && b.IsRound(*this)) {
            const Carrier l = e.CarrierOf(a);
            c.sign = Side(Cross(l.u, e.RoundOf(b).c - l.a));
        } else if (a.IsRound(*this) && b.IsRound(*this)) {
            if (c.kind == Kind::Tangent) {
                FreezeSmallest(c, e, 3);
                return;
            }
            const Round o = e.RoundOf(a), p = e.RoundOf(b);
            const double d = Norm(o.c - p.c).v;
            c.variant = d + p.r.v <= o.r.v ? 1 : d + o.r.v <= p.r.v ? 2 : 0;
        }
    }

    // two lines: of the value turned either way, the angle or the angle + 180 degrees, the one nearest the second line as drawn
    void System::FreezeAngle(Constraint &c, const Eval &e) const {
        const std::vector<Ref> &q = c.refs;
        if (q.size() != 2 || !q[0].IsLine(*this) || !q[1].IsLine(*this))
            return;
        const DualVector u = e.CarrierOf(q[0]).u, w = e.CarrierOf(q[1]).u;
        double best = -std::numeric_limits<double>::infinity();
        for (const int side : {1, -1}) {
            const double d = Dot(Turned(u, side * c.value), w).v;
            if (std::abs(d) > best + 1e-12) {
                best = std::abs(d);
                c.variant = side;
                c.sign = Side(Dual(d));
            }
        }
    }

    void System::FreezeSymmetric(Constraint &c, const Eval &e) const {
        const std::vector<Ref> &q = c.refs;
        if (q.size() == 3 && ((q[0].IsSegment(*this) && q[1].IsSegment(*this)) || (q[0].IsArc(*this) && q[1].IsArc(*this))))
            FreezeSmallest(c, e, 2);
    }

    void System::FreezeFix(Constraint &c, const Eval &e) const {
        if (c.refs.size() != 1)
            return;
        if (c.refs[0].IsPoint(*this)) {
            const DualVector p = e.PointOf(c.refs[0]);
            c.target = {p.x.v, p.y.v};
        } else {
            HeldOf(elements[c.refs[0].element], c.kind, c.heldPoints, c.held);
            for (size_t p : c.heldPoints)
                c.target.insert(c.target.end(), {values[coordsOf[p]], values[coordsOf[p] + 1]});
            for (size_t u : c.held)
                c.target.push_back(values[u]);
        }
    }

}
