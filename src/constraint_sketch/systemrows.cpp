// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/system.hpp"

namespace planex2d::cs {

    void System::Evaluate(size_t c, const std::vector<double> &v, double *r, double *J) const {
        thread_local std::vector<Dual> res;
        if (IsPointwise(At(c))) {
            std::array<Dual, 2> d;
            for (size_t k = 0, row = 0; k < blocks[c].size(); row += blocks[c][k].rows, ++k) {
                const RowBlock &b = blocks[c][k];
                HeldRows(At(c), Eval{*this, sharedCurves, v, b.indices, builtSize, levers[c]}, k, d.data());
                for (size_t i = 0; i < b.rows; ++i) {
                    r[row + i] = d[i].v;
                    for (size_t j = 0; j < b.indices.size(); ++j)
                        *J++ = d[i].g[j];
                }
            }
            return;
        }
        const std::vector<size_t> &idx = indices[c];
        const size_t n = Residuals(At(c), Eval{*this, sharedCurves, v, idx, builtSize, levers[c]}, res, joints[c]);
        for (size_t i = 0; i < n; ++i) {
            r[i] = res[i].v;
            for (size_t k = 0; k < idx.size(); ++k)
                J[i * idx.size() + k] = res[i].g[k];
        }
    }

    // a Fix of an element that is no point or arc, a FixH or FixV of an element and a rigid group: their rows are
    // evaluated per held point and value, each with only its own unknowns (and the group's place), so that they take
    // any number of points
    bool System::IsPointwise(const Constraint &c) const {
        return c.kind == Kind::RigidGroup || (IsFix(c.kind) && c.refs.size() == 1 && !c.refs[0].IsPoint(*this) &&
                                              (c.kind != Kind::Fix || !c.refs[0].IsArc(*this)));
    }

    // a point that is an end of both (the same unknowns), as where a line or arc runs on into an arc; of a spline
    // any of its points (JoinPoints); of two that meet at more than one, the one where they turn least
    std::optional<size_t> System::Joint(const Ref &a, const Ref &b) const {
        if (a.part != Part::Whole || b.part != Part::Whole)
            return std::nullopt;
        std::vector<size_t> shared;
        for (size_t p : JoinPoints(elements[a.element]))
            for (size_t q : JoinPoints(elements[b.element]))
                if (coordsOf[p] == coordsOf[q])
                    shared.push_back(p);
        if (shared.size() < 2)
            return shared.empty() ? std::nullopt : std::optional(shared[0]);
        const Eval e = PlainEval();
        const auto turn = [&](size_t p) {
            const DualVector ta = e.TangentAt(a, p), tb = e.TangentAt(b, p);
            return std::abs(Cross(ta, tb).v) / (Norm(ta).v * Norm(tb).v);
        };
        return *std::min_element(shared.begin(), shared.end(), [&](size_t p, size_t q) { return turn(p) < turn(q); });
    }

    // the points another element can join e at: of a spline by its poles its ends, its other poles lie off it (a
    // periodic one's all)
    std::vector<size_t> System::JoinPoints(const Element &e) const {
        if (e.kind != ElementKind::Spline || !splines[e.SplineIndex()].poled)
            return e.PointsOn(*this);
        if (!e.IsPiece(*this) && splines[e.SplineIndex()].closed)
            return {};
        const std::array<size_t, 2> ends = e.Ends(*this);
        return {ends[0], ends[1]};
    }

    bool System::HasEnd(const Ref &r, size_t point) const {
        if (r.part != Part::Whole)
            return false;
        for (size_t p : elements[r.element].Ends(*this))
            if (p != sketch::none && coordsOf[p] == coordsOf[point])
                return true;
        return false;
    }

    // where a tangent touches, if known: an end both share, or an end of one constrained onto the other. There the
    // radius standing on the line (or the centres in line with the point) replaces "the centre at distance r",
    // which is singular when the point is on both curves anyway
    std::optional<size_t> System::JointOf(const Constraint &c) const {
        if ((c.kind != Kind::Tangent && c.kind != Kind::Curvature) || c.refs.size() != 2)
            return std::nullopt;
        const Ref &a = c.refs[0], &b = c.refs[1];
        if (const std::optional<size_t> j = Joint(a, b))
            return j;
        for (const Constraint &k : constraints) {
            if (k.kind != Kind::Coincident || k.refs.size() != 2)
                continue;
            for (size_t s = 0; s < 2; ++s) {
                const Ref &p = k.refs[s], &o = k.refs[1 - s];
                if (!p.IsRealPoint(*this) || !o.IsRound(*this))
                    continue;
                const size_t q = p.PointAt(*this);
                if ((o.element == b.element && HasEnd(a, q)) || (o.element == a.element && HasEnd(b, q)))
                    return q;
            }
        }
        return std::nullopt;
    }

    // whether two elements' directions (TangentAt) at their joint are the same (1) or opposite (-1) when they
    // join smoothly: one ending there and the other starting there run on the same way; elsewhere (a spline's
    // inner point, a closed spline's seam) and where they are tangent already (a cusp too) the drawing decides
    double System::Sense(const Ref &a, const Ref &b, size_t joint, const Eval &e) const {
        const auto leaves = [&](const Ref &r) {
            const Element &el = elements[r.element];
            const std::array<size_t, 2> ends = el.Ends(*this);
            if (ends[0] == sketch::none || coordsOf[ends[0]] == coordsOf[ends[1]])
                return 0.0;
            const double travel = el.kind == ElementKind::Arc && !el.ccw ? -1.0 : 1.0;
            return coordsOf[ends[0]] == coordsOf[joint] ? travel : coordsOf[ends[1]] == coordsOf[joint] ? -travel : 0.0;
        };
        const DualVector ta = e.TangentAt(a, joint), tb = e.TangentAt(b, joint);
        const double la = leaves(a), lb = leaves(b);
        if (la != 0.0 && lb != 0.0 && std::abs(Cross(ta, tb).v) > Tol(1.0) * Norm(ta).v * Norm(tb).v)
            return -la * lb;
        return Side(Dot(ta, tb));
    }

    // the residual rows of c, lengths; a combination of elements the kind does not relate is an error
    size_t System::Residuals(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
        // the most rows of any constraint but the Fix of a spline, which grows r itself
        constexpr size_t rowsAtMost = 8;
        if (r.size() < rowsAtMost)
            r.resize(rowsAtMost);
        if ((c.reference && c.valueAt == sketch::none) || c.suspended)
            return 0;
        if (const std::optional<size_t> rows = RowsOf(c, e, r, joint))
            return *rows;
        throw std::invalid_argument(std::string(KindName(c.kind)) + ": cannot relate these elements");
    }

    std::optional<size_t> System::RowsOf(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
        switch (c.kind) {
        case Kind::Coincident:
            return CoincidentRows(c, e, r);
        case Kind::Horizontal:
        case Kind::Vertical:
            return HorizontalVerticalRows(c, e, r);
        case Kind::Parallel:
        case Kind::Perpendicular:
            return ParallelPerpendicularRows(c, e, r);
        case Kind::Tangent:
            return TangentRows(c, e, r, joint);
        case Kind::Curvature:
            return CurvatureRows(c, e, r, joint);
        case Kind::CurvatureRadius:
            return CurvatureRadiusRows(c, e, r);
        case Kind::Equal:
            return EqualRows(c, e, r);
        case Kind::Symmetric:
            return SymmetricRows(c, e, r);
        case Kind::SymmetricCarriers:
            return SymmetricCarriersRows(c, e, r);
        case Kind::Concentric:
            return ConcentricRows(c, e, r);
        case Kind::Fix:
        case Kind::FixH:
        case Kind::FixV:
            return FixRows(c, e, r);
        case Kind::Distance:
            return DistanceRows(c, e, r);
        case Kind::Radius:
        case Kind::Diameter:
            return RadiusRows(c, e, r);
        case Kind::Angle:
            return AngleRows(c, e, r);
        case Kind::RigidGroup:
            return RigidGroupRows(c, e, r);
        case Kind::Copy:
            return CopyRows(c, e, r);
        case Kind::OnCircle:
            return OnCircleRows(c, e, r);
        case Kind::Offset:
            return OffsetRows(c, e, r);
        case Kind::Expression:
            return ExpressionRows(c, e, r);
        case Kind::Value:
            return ValueRows(c, e, r);
        case Kind::OnCurve:
            return OnCurveRows(c, e, r);
        case Kind::Parameter:
            return 0;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::CoincidentRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Dual size(e.size);
        Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n != 2)
            return std::nullopt;
        if (c.params.size() == 1) {
            if (a.IsCurve(*this))
                std::swap(a, b);
            const DualVector d = e.PointOf(a) - e.CurveAt(elements[b.element], e.Var(c.params[0]), 0);
            r[0] = d.x;
            r[1] = d.y;
            return 2;
        }
        if (a.IsRealPoint(*this) && b.IsRealPoint(*this))
            return 0;
        if (a.IsPoint(*this) && b.IsPoint(*this)) {
            const DualVector d = e.PointOf(a) - e.PointOf(b);
            r[0] = d.x;
            r[1] = d.y;
            return 2;
        }
        if (b.IsPoint(*this))
            std::swap(a, b);
        if (a.IsPoint(*this) && b.IsLine(*this)) {
            const Carrier l = e.CarrierOf(b);
            r[0] = Cross(l.u, e.PointOf(a) - l.a);
            return 1;
        }
        if (a.IsPoint(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(b);
            r[0] = Norm(e.PointOf(a) - o.c) - o.r;
            return 1;
        }
        if (a.IsLine(*this) && b.IsLine(*this)) {
            if (a.IsSegment(*this) && !b.IsSegment(*this))
                std::swap(a, b);
            const Carrier l = e.CarrierOf(a);
            if (b.IsSegment(*this)) {
                const std::array<DualVector, 2> p = e.EndsOf(b);
                r[0] = Cross(l.u, p[0] - l.a);
                r[1] = Cross(l.u, p[1] - l.a);
                return 2;
            }
            const Carrier m = e.CarrierOf(b);
            r[0] = Cross(l.u, m.u) * size;
            r[1] = Cross(l.u, m.a - l.a);
            return 2;
        }
        if (a.IsRound(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(a), p = e.RoundOf(b);
            r[0] = o.c.x - p.c.x;
            r[1] = o.c.y - p.c.y;
            r[2] = o.r - p.r;
            return 3;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::HorizontalVerticalRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Dual size(e.size);
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        const bool h = c.kind == Kind::Horizontal;
        if (n == 2 && a.IsPoint(*this) && b.IsPoint(*this)) {
            const DualVector d = e.PointOf(b) - e.PointOf(a);
            r[0] = h ? d.y : d.x;
            return 1;
        }
        if (n == 1 && a.IsSegment(*this)) {
            const std::array<DualVector, 2> p = e.EndsOf(a);
            r[0] = h ? p[1].y - p[0].y : p[1].x - p[0].x;
            return 1;
        }
        if (n == 1 && a.IsLine(*this)) {
            const Carrier l = e.CarrierOf(a);
            r[0] = (h ? l.u.y : l.u.x) * size;
            return 1;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::ParallelPerpendicularRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Dual lever(e.lever);
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n == 2 && a.IsLine(*this) && b.IsLine(*this)) {
            const DualVector u = e.CarrierOf(a).u, w = e.CarrierOf(b).u;
            r[0] = (c.kind == Kind::Parallel ? Cross(u, w) : Dot(u, w)) * lever;
            return 1;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::TangentRows(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
        const size_t n = c.refs.size();
        const Dual lever(e.lever), sign(c.sign);
        Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n != 2)
            return std::nullopt;
        if (!c.params.empty()) {
            // touching away from the spline's points: there on the other element and parallel to it
            if (a.IsCurve(*this) && b.IsCurve(*this)) {
                const Dual ta = e.Var(c.params[0]), tb = e.Var(c.params[1]);
                const DualVector d = e.CurveAt(elements[a.element], ta, 0) - e.CurveAt(elements[b.element], tb, 0);
                r[0] = d.x;
                r[1] = d.y;
                r[2] = Cross(Unit(e.CurveAt(elements[a.element], ta, 1)), Unit(e.CurveAt(elements[b.element], tb, 1))) * lever;
                return 3;
            }
            if (a.IsCurve(*this))
                std::swap(a, b);
            const Dual t = e.Var(c.params[0]);
            const DualVector p = e.CurveAt(elements[b.element], t, 0), w = Unit(e.CurveAt(elements[b.element], t, 1));
            if (a.IsLine(*this)) {
                const Carrier l = e.CarrierOf(a);
                r[0] = Cross(l.u, p - l.a);
                r[1] = Cross(l.u, w) * lever;
                return 2;
            }
            const Round o = e.RoundOf(a);
            r[0] = Norm(p - o.c) - o.r;
            r[1] = Cross(Unit(Left(p - o.c)), w) * lever;
            return 2;
        }
        if (a.IsCurve(*this) || b.IsCurve(*this)) {
            // only where the two share a point: the angle between their directions there (sign: the same
            // or opposite ones), linear in what turns them, so steps do not overshoot as with its sine
            if (!joint)
                return std::nullopt;
            const DualVector ta = e.TangentAt(a, *joint), tb = e.TangentAt(b, *joint);
            r[0] = Atan2(Cross(ta, tb), Dot(ta, tb) * sign) * lever;
            return 1;
        }
        if (a.IsRound(*this) && b.IsLine(*this))
            std::swap(a, b);
        if (a.IsLine(*this) && b.IsRound(*this)) {
            const Carrier l = e.CarrierOf(a);
            const Round o = e.RoundOf(b);
            if (joint) {
                r[0] = Dot(e.At(*joint) - o.c, l.u);
                return 1;
            }
            r[0] = Cross(l.u, o.c - l.a) * sign - o.r;
            return 1;
        }
        if (a.IsRound(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(a), p = e.RoundOf(b);
            if (joint) {
                const DualVector t = e.At(*joint);
                r[0] = Cross(Unit(t - o.c), t - p.c);
                return 1;
            }
            const Dual d = Norm(o.c - p.c);
            r[0] = c.variant == 0 ? d - o.r - p.r : c.variant == 1 ? d - (o.r - p.r) : d - (p.r - o.r);
            return 1;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::CurvatureRows(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
        if (c.refs.size() != 2 || !joint)
            return std::nullopt;
        const Ref &a = c.refs[0], &b = c.refs[1];
        const Dual lever(e.lever), sign(c.sign);
        const DualVector ta = e.TangentAt(a, *joint), tb = e.TangentAt(b, *joint);
        r[0] = Atan2(Cross(ta, tb), Dot(ta, tb) * sign) * lever;
        r[1] = (e.CurvatureAt(a, *joint) - e.CurvatureAt(b, *joint) * sign) * lever * lever;
        return 2;
    }

    // the curvature of a spline at its point: 1 / the radius, 0 for math.inf (straight); the spline's own there when
    // it is given one (GiveTangents)
    std::optional<size_t> System::CurvatureRadiusRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        if (c.refs.size() != 1 || c.refs[0].part != Part::Point || elements[c.refs[0].element].kind != ElementKind::Spline)
            return std::nullopt;
        const Ref &a = c.refs[0];
        const Dual inverse = c.valueAt == sketch::none ? Dual(std::isinf(c.value) ? 0.0 : 1.0 / c.value) : Dual(1.0) / e.Var(c.valueAt);
        const Dual lever(e.lever);
        r[0] = (e.CurvatureAt({a.element}, a.PointAt(*this)) - inverse) * lever * lever;
        return 1;
    }

    std::optional<size_t> System::EqualRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n == 2 && a.IsSegment(*this) && b.IsSegment(*this)) {
            const std::array<DualVector, 2> p = e.EndsOf(a), s = e.EndsOf(b);
            r[0] = Norm(p[1] - p[0]) - Norm(s[1] - s[0]);
            return 1;
        }
        if (n == 2 && a.IsRound(*this) && b.IsRound(*this)) {
            r[0] = e.RoundOf(a).r - e.RoundOf(b).r;
            return 1;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::SymmetricRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const std::vector<Ref> &q = c.refs;
        const size_t n = q.size();
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n != 3 || !q[2].IsLine(*this))
            return std::nullopt;
        const Carrier m = e.CarrierOf(q[2]);
        const auto pair = [&](const DualVector &p, const DualVector &s, size_t k) {
            r[k] = Cross(m.u, (p + s) * Dual(0.5) - m.a);
            r[k + 1] = Dot(m.u, s - p);
        };
        if (a.IsPoint(*this) && b.IsPoint(*this)) {
            pair(e.PointOf(a), e.PointOf(b), 0);
            return 2;
        }
        // arcs: the ends and the radius; with the ends on their circles that leaves the centre on the
        // mirrored side of the chord, a mirrored centre on top would be redundant
        const bool arcs = a.IsArc(*this) && b.IsArc(*this);
        if ((a.IsSegment(*this) && b.IsSegment(*this)) || arcs) {
            const std::array<DualVector, 2> p = e.EndsOf(a), s = e.EndsOf(b);
            pair(p[0], s[c.variant], 0);
            pair(p[1], s[1 - c.variant], 2);
            if (!arcs)
                return 4;
            r[4] = e.RoundOf(a).r - e.RoundOf(b).r;
            return 5;
        }
        if (a.IsRound(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(a), p = e.RoundOf(b);
            pair(o.c, p.c, 0);
            r[2] = o.r - p.r;
            return 3;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::SymmetricCarriersRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const std::vector<Ref> &q = c.refs;
        const size_t n = q.size();
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n == 3 && a.IsSegment(*this) && b.IsLine(*this) && q[2].IsLine(*this)) {
            const Carrier m = e.CarrierOf(q[2]), l = e.CarrierOf(b);
            const std::array<DualVector, 2> p = e.EndsOf(a);
            r[0] = Cross(l.u, m.Mirrored(p[0]) - l.a);
            r[1] = Cross(l.u, m.Mirrored(p[1]) - l.a);
            return 2;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::ConcentricRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n == 2 && (a.IsRound(*this) || a.IsPoint(*this)) && (b.IsRound(*this) || b.IsPoint(*this))) {
            const DualVector d = e.CenterOf(a) - e.CenterOf(b);
            r[0] = d.x;
            r[1] = d.y;
            return 2;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::FixRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Ref a = c.RefAt(0);
        if (n != 1)
            return std::nullopt;
        if (a.IsPoint(*this)) {
            const DualVector p = e.PointOf(a);
            if (c.kind != Kind::Fix) {
                const size_t i = HeldCoordinate(c.kind);
                r[0] = (i == 0 ? p.x : p.y) - Dual(c.target[i]);
                return 1;
            }
            r[0] = p.x - Dual(c.target[0]);
            r[1] = p.y - Dual(c.target[1]);
            return 2;
        } else if (c.kind == Kind::Fix && a.IsArc(*this)) {
            // its five values: the ends' own equations keep them on the circle, so only their angles; the
            // target is held points (center, start, end), then the radius
            const Round o = e.RoundOf(a);
            const std::array<DualVector, 2> p = e.EndsOf(a);
            const Vertex tc{c.target[0], c.target[1]};
            r[0] = o.c.x - Dual(tc.x);
            r[1] = o.c.y - Dual(tc.y);
            r[2] = o.r - Dual(c.target[6]);
            for (size_t k = 0; k < 2; ++k) {
                const Vertex t = Vertex{c.target[2 + 2 * k], c.target[3 + 2 * k]} - tc;
                const double length = Norm(t);
                r[3 + k] = Cross(DualVector{Dual(t.x / length), Dual(t.y / length)}, p[k] - DualVector{Dual(tc.x), Dual(tc.y)});
            }
            return 5;
        }
        return AllHeldRows(c, e, r);
    }

    std::optional<size_t> System::DistanceRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const std::vector<Ref> &q = c.refs;
        const size_t n = q.size();
        const Dual sign(c.sign);
        const Dual v = c.valueAt == sketch::none ? Dual(c.value) : e.Var(c.valueAt);
        Ref a = c.RefAt(0), b = c.RefAt(1);
        if (n == 1 && (a.IsPattern(*this) || a.IsOffset(*this))) {
            const Element &el = elements[a.element];
            r[0] = e.Var(a.IsPattern(*this) ? (c.variant2 == 2 ? el.Step2() : el.Step()) : el.Distance()) - v;
            return 1;
        }
        if (n == 1 && a.IsSegment(*this)) {
            const std::array<DualVector, 2> p = e.EndsOf(a);
            r[0] = Norm(p[1] - p[0]) - v;
            return 1;
        }
        if (n == 1 && a.IsRound(*this)) {
            r[0] = e.RoundOf(a).r - v;
            return 1;
        }
        if (n == 3 && a.IsPoint(*this) && b.IsPoint(*this) && q[2].IsLine(*this)) {
            r[0] = Cross(e.CarrierOf(q[2]).u, e.PointOf(b) - e.PointOf(a)) * sign - v;
            return 1;
        }
        if (n != 2)
            return std::nullopt;
        if (c.params.size() == 1) {
            // from the foot on the spline, along its normal there
            if (a.IsCurve(*this))
                std::swap(a, b);
            const Dual t = e.Var(c.params[0]);
            const DualVector d = e.PointOf(a) - e.CurveAt(elements[b.element], t, 0), u = Unit(e.CurveAt(elements[b.element], t, 1));
            r[0] = Dot(d, u);
            r[1] = Cross(u, d) * sign - v;
            return 2;
        }
        if (a.IsPoint(*this) && b.IsPoint(*this)) {
            r[0] = Norm(e.PointOf(a) - e.PointOf(b)) - v;
            return 1;
        }
        if (b.IsPoint(*this))
            std::swap(a, b);
        if (a.IsPoint(*this) && b.IsLine(*this)) {
            const Carrier l = e.CarrierOf(b);
            r[0] = Cross(l.u, e.PointOf(a) - l.a) * sign - v;
            return 1;
        }
        if (a.IsPoint(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(b);
            r[0] = (Norm(e.PointOf(a) - o.c) - o.r) * sign - v;
            return 1;
        }
        if (a.IsRound(*this) && b.IsLine(*this))
            std::swap(a, b);
        if (a.IsLine(*this) && b.IsLine(*this)) {
            if (a.IsSegment(*this) && !b.IsSegment(*this))
                std::swap(a, b);
            const Carrier l = e.CarrierOf(a);
            r[0] = Cross(l.u, e.Measured(b) - l.a) * sign - v;
            return 1;
        }
        if (a.IsLine(*this) && b.IsRound(*this)) {
            const Carrier l = e.CarrierOf(a);
            const Round o = e.RoundOf(b);
            r[0] = Cross(l.u, o.c - l.a) * sign - o.r - v;
            return 1;
        }
        if (a.IsRound(*this) && b.IsRound(*this)) {
            const Round o = e.RoundOf(a), p = e.RoundOf(b);
            const Dual d = Norm(o.c - p.c);
            r[0] = (c.variant == 0 ? d - o.r - p.r : c.variant == 1 ? o.r - p.r - d : p.r - o.r - d) - v;
            return 1;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::RadiusRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Dual v = c.valueAt == sketch::none ? Dual(c.value) : e.Var(c.valueAt);
        const Ref a = c.RefAt(0);
        if (n == 1 && (a.IsRound(*this) || a.IsOffset(*this))) {
            const Dual size = a.IsOffset(*this) ? e.Var(elements[a.element].Distance()) : e.RoundOf(a).r;
            r[0] = size * Dual(c.kind == Kind::Radius ? 1.0 : 2.0) - v;
            return 1;
        }
        return std::nullopt;
    }

    std::optional<size_t> System::AngleRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const size_t n = c.refs.size();
        const Dual size(e.size), lever(e.lever), sign(c.sign);
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        const bool linked = c.valueAt != sketch::none;
        // two lines turn the side the drawing showed (variant -1: clockwise by the value)
        const bool back = c.variant < 0 && n == 2 && a.IsLine(*this) && b.IsLine(*this);
        const auto rotated = [&](const DualVector &u) {
            if (linked)
                return TurnedRadians(u, back ? Dual(0.0) - e.Var(c.valueAt) : e.Var(c.valueAt));
            return Turned(u, back ? -c.value : c.value);
        };
        if (n == 1 && a.IsPattern(*this)) {
            r[0] = (e.Var(elements[a.element].Step()) - (linked ? e.Var(c.valueAt) : Dual(Radians(c.value)))) * size;
            return 1;
        }
        if (n == 2 && a.IsLine(*this) && b.IsLine(*this)) {
            // the angle between the lines, on the branch the drawing chose: the side it turns to and the angle or the
            // angle + 180 degrees (the lines' directions do not count)
            const DualVector turned = rotated(e.CarrierOf(a).u), w = e.CarrierOf(b).u;
            r[0] = Atan2(Cross(turned, w), Dot(turned, w) * sign) * lever;
            return 1;
        }
        if (IsSweep(c)) {
            // the start turned by the sweep is the end; the arc's direction makes it the sweep, not one 360 off
            const DualVector o = e.RoundOf(a).c;
            const std::array<DualVector, 2> p = e.EndsOf(a);
            const DualVector turned = rotated(p[0] - o), w = p[1] - o;
            r[0] = Atan2(Cross(turned, w), Dot(turned, w)) * lever;
            return 1;
        }
        return std::nullopt;
    }

    // the k-th held point (two rows, one of a FixH or FixV) or, after the points, held value (one row) of a Fix or a
    // rigid group at its target; a rigid group's targets lie in its frame: params are the place of its first point
    // and its turn
    size_t System::HeldRows(const Constraint &c, const Eval &e, size_t k, Dual *r) const {
        const size_t m = 2 * c.heldPoints.size();
        const bool rigid = c.kind == Kind::RigidGroup;
        if (k < c.heldPoints.size()) {
            const DualVector p = e.At(c.heldPoints[k]);
            const Dual lx(c.target[2 * k]), ly(c.target[2 * k + 1]);
            if (c.kind == Kind::FixH || c.kind == Kind::FixV) {
                r[0] = c.kind == Kind::FixH ? p.x - lx : p.y - ly;
                return 1;
            }
            if (!rigid) {
                r[0] = p.x - lx;
                r[1] = p.y - ly;
                return 2;
            }
            const Dual turn = e.Var(c.params[2]), cs = Cos(turn), sn = Sin(turn);
            r[0] = p.x - (e.Var(c.params[0]) + lx * cs - ly * sn);
            r[1] = p.y - (e.Var(c.params[1]) + lx * sn + ly * cs);
            return 2;
        }
        const size_t j = k - c.heldPoints.size(), u = c.held[j];
        const Dual target(c.target[m + j]), size(e.size);
        if (rigid && isAngle[u]) {
            const Dual d = e.Var(u) - (target + e.Var(c.params[2]));
            r[0] = Atan2(Sin(d), Cos(d)) * size;
        } else
            r[0] = isAngle[u] && !rigid ? (e.Var(u) - target) * size : e.Var(u) - target;
        return 1;
    }

    size_t System::AllHeldRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        r.resize(std::max(r.size(), 2 * c.heldPoints.size() + c.held.size()));
        size_t rows = 0;
        for (size_t k = 0; k < c.heldPoints.size() + c.held.size(); ++k)
            rows += HeldRows(c, e, k, &r[rows]);
        return rows;
    }

    std::optional<size_t> System::CopyRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const std::vector<Ref> &q = c.refs;
        const size_t n = q.size();
        const Dual size(e.size);
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        if (c.held.size() == 2 && n == 2) {
            // a copied piece's end at the original's position along its spline
            r[0] = e.Var(c.held[1]) - e.Var(c.held[0]);
            return 1;
        }
        if (c.held.size() == 2 && n >= 3 && !isAngle[c.held[0]]) {
            const Dual lever(e.lever);
            r[0] = (e.Var(c.held[1]) - e.Var(c.held[0]) * Dual(q[2].IsLine(*this) ? -1.0 : 1.0)) * lever * lever;
            return 1;
        }
        if (c.held.size() == 2 && n >= 3) {
            // a copied spline's tangent direction: mirrored at the axis q[2], or turned by the pattern's step
            const Dual from = e.Var(c.held[0]);
            Dual to = from;
            if (q[2].IsLine(*this)) {
                const DualVector u = e.CarrierOf(q[2]).u;
                to = Dual(2.0) * Atan2(u.y, u.x) - from;
            } else if (q[2].IsPattern(*this) && !elements[q[2].element].IsLinear())
                to = from + e.Var(elements[q[2].element].Step()) * Dual((double)c.variant);
            const Dual d = e.Var(c.held[1]) - to;
            r[0] = Atan2(Sin(d), Cos(d)) * size;
            return 1;
        }
        // a copied fitted spline's frame: its place where the copy takes its original's
        if (c.heldPoints.size() == 2) {
            const std::optional<DualVector> to = CopiedPoint(c, e, e.At(c.heldPoints[0]));
            if (!to)
                return std::nullopt;
            const DualVector w = e.At(c.heldPoints[1]) - *to;
            r[0] = w.x;
            r[1] = w.y;
            return 2;
        }
        if ((n != 4 && n != 5) || !a.IsPoint(*this) || !b.IsPoint(*this))
            return std::nullopt;
        const std::optional<DualVector> to = CopiedPoint(c, e, e.PointOf(a));
        if (!to)
            return std::nullopt;
        const DualVector copy = e.PointOf(b);
        r[0] = copy.x - to->x;
        r[1] = copy.y - to->y;
        return 2;
    }

    // where the copy c puts the point p: mirrored at the line refs[2], or moved by the pattern refs[2] along the line
    // refs[3] (a pattern of two directions also along refs[4]) or turned about the point refs[3]
    std::optional<DualVector> System::CopiedPoint(const Constraint &c, const Eval &e, const DualVector &p) const {
        const std::vector<Ref> &q = c.refs;
        const size_t n = q.size();
        if (n == 3 && q[2].IsLine(*this))
            return e.CarrierOf(q[2]).Mirrored(p);
        if ((n != 4 && n != 5) || !q[2].IsPattern(*this))
            return std::nullopt;
        const Element &pattern = elements[q[2].element];
        const Dual step = e.Var(pattern.Step()) * Dual((double)c.variant);
        if (pattern.IsLinear()) {
            if (!q[3].IsLine(*this) || (n == 5 && (!q[4].IsLine(*this) || !pattern.HasStep2())))
                return std::nullopt;
            DualVector to = p + e.CarrierOf(q[3]).u * step;
            if (n == 5)
                to = to + e.CarrierOf(q[4]).u * (e.Var(pattern.Step2()) * Dual((double)c.variant2));
            return to;
        }
        if (!q[3].IsPoint(*this))
            return std::nullopt;
        const DualVector o = e.PointOf(q[3]), d = p - o;
        const Dual cs = Cos(step), sn = Sin(step);
        return o + DualVector{d.x * cs - d.y * sn, d.x * sn + d.y * cs};
    }

    std::optional<size_t> System::OnCircleRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const Ref a = c.RefAt(0);
        const Round o = e.RoundOf(a);
        const std::array<DualVector, 2> p = e.EndsOf(a);
        size_t k = 0;
        for (int i = 0; i < 2; ++i)
            if (c.variant & (1 << i))
                r[k++] = Norm(p[i] - o.c) - o.r;
        return k;
    }

    std::optional<size_t> System::OnCurveRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const SplineData &d = splines[elements[c.refs[0].element].SplineIndex()];
        size_t k = 0;
        for (size_t i = 0; i < d.points.size(); ++i) {
            const DualVector w = e.At(d.points[i]) - e.PlacedEnd(*d.fitted, i == 1);
            r[k++] = w.x;
            r[k++] = w.y;
        }
        return k;
    }

    std::optional<size_t> System::OffsetRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const std::vector<Ref> &q = c.refs;
        const size_t n = q.size();
        const Dual sign(c.sign);
        const Ref a = c.RefAt(0), b = c.RefAt(1);
        const Dual dist = e.Var(elements[q[n - 1].element].Distance());
        const auto normal = [&](const Ref &of, const DualVector &p) {
            if (of.IsLine(*this))
                return Left(e.CarrierOf(of).u);
            if (of.IsSpline(*this))
                return Left(Unit(e.TangentAt(of, q[2].PointAt(*this))));
            const DualVector out = Unit(p - e.RoundOf(of).c);
            return elements[of.element].ccw ? out * Dual(-1.0) : out;
        };
        switch (static_cast<OffsetTie>(c.variant)) {
        case OffsetTie::Point: {
            const DualVector p = e.PointOf(q[2]), w = e.PointOf(a) - (p + normal(b, p) * (dist * sign));
            r[0] = w.x;
            r[1] = w.y;
            return 2;
        }
        case OffsetTie::Foot: {
            const DualVector p = e.PointOf(q[2]);
            r[0] = Dot(e.PointOf(a) - p, Left(normal(b, p)));
            return 1;
        }
        case OffsetTie::Carrier: {
            const DualVector p = e.PointOf(a);
            if (b.IsPoint(*this)) {
                r[0] = Norm(p - e.PointOf(b)) - dist;
                return 1;
            }
            if (b.IsLine(*this)) {
                const Carrier l = e.CarrierOf(b);
                r[0] = Cross(l.u, p - l.a) - dist * sign;
                return 1;
            }
            const Round o = e.RoundOf(b);
            r[0] = (Norm(p - o.c) - o.r) * Dual(elements[b.element].ccw ? -1.0 : 1.0) - dist * sign;
            return 1;
        }
        case OffsetTie::Center: {
            const std::array<DualVector, 2> ends = e.EndsOf(a);
            r[0] = Dot(e.RoundOf(a).c - e.PointOf(b), Unit(Left(ends[1] - ends[0])));
            return 1;
        }
        case OffsetTie::Radius:
            r[0] = e.RoundOf(a).r - e.RoundOf(b).r - dist * sign;
            return 1;
        case OffsetTie::Spur: {
            const DualVector p = e.PointOf(q[2]), n = normal(b, p);
            r[0] = Dot(e.PointOf(a) - (p + n * (dist * sign)), n);
            return 1;
        }
        case OffsetTie::Beyond: {
            const DualVector p = e.PointOf(q[2]), n = normal(b, p);
            r[0] = Dot(e.PointOf(a) - p, Right(n)) * sign - dist;
            return 1;
        }
        case OffsetTie::Along: {
            const OffsetCurveData &o = offsetCurves[elements[b.element].OffsetCurveIndex()];
            const std::array<Dual, 2> t = e.OffsetRange(elements[b.element]);
            const DualVector w = e.PointOf(a) - e.CurveAt(elements[b.element], t[coordsOf[a.PointAt(*this)] == coordsOf[o.range.toPoint]], 0);
            r[0] = w.x;
            r[1] = w.y;
            return 2;
        }
        }
        return std::nullopt;
    }

    std::optional<size_t> System::ExpressionRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const Dual lever(e.lever);
        // lever: what makes it a length, fixed per Build (LeverOf)
        r[0] = Evaluated<Dual>(c.equation, [&](size_t which) { return e.Var(which); }) * lever;
        return 1;
    }

    std::optional<size_t> System::ValueRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
        const Dual size(e.size);
        r[0] = isAngle[c.valueAt] ? (e.Var(c.valueAt) - Dual(Radians(c.value))) * size : e.Var(c.valueAt) - Dual(c.value);
        return 1;
    }

}
