// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/system.hpp"

#include <limits>
#include <set>

namespace planex2d::cs {

    size_t System::Add(Constraint c) {
        for (const Ref &r : c.refs)
            CheckRef(r, c.kind);
        if (Sideless(c) && !c.reference && !(c.value > 0.0))
            throw std::invalid_argument(std::string(KindName(c.kind)) + ": a length or radius must be positive");
        if (const char *outside = c.reference ? nullptr : SweepOutside(c, c.value))
            throw std::invalid_argument(std::string("Angle: ") + outside);
        if (c.kind == Kind::CurvatureRadius && !c.reference && (c.value == 0.0 || std::isnan(c.value)))
            throw std::invalid_argument("CurvatureRadius: the radius is nonzero, math.inf for straight");
        if (c.kind == Kind::Distance && !c.reference && c.refs.size() == 2 && c.refs[0].IsPoint(*this) && c.refs[1].IsPoint(*this))
            MovePointsApart(c);
        builtSize = Size();
        if (!c.reference) {
            GiveTangents(c);
            GiveParams(c);
        }
        if (c.kind == Kind::RigidGroup)
            GiveFrame(c);
        const std::vector<size_t> idx = LocalsOf(c);
        const Eval e{*this, sharedCurves, values, idx, builtSize};
        Freeze(c, e);
        if (c.reference)
            CheckMeasurable(c);
        std::vector<Dual> r;
        Residuals(c, e, r, JointOf(c));
        const bool merge = IsMerge(c);
        constraints.push_back(std::move(c));
        if (merge)
            Remerge();
        return constraints.size() - 1;
    }

    // two points at one place have no direction to move apart in: one starts at the distance along h
    void System::MovePointsApart(const Constraint &c) {
        if (c.refs[0].IsRealPoint(*this) && c.refs[1].IsRealPoint(*this) && coordsOf[c.refs[0].PointAt(*this)] == coordsOf[c.refs[1].PointAt(*this)])
            throw std::invalid_argument("Distance: the two points are coincident, one point");
        const Vertex p = c.refs[0].PointValue(*this), q = c.refs[1].PointValue(*this);
        if (Norm(p - q) <= Tol(std::max({NormInf(p), NormInf(q), c.value}))) {
            const auto movable = [&](const Ref &r) { return r.IsRealPoint(*this) && !held[coordsOf[r.PointAt(*this)]]; };
            const Ref *m = movable(c.refs[1]) ? &c.refs[1] : movable(c.refs[0]) ? &c.refs[0] : nullptr;
            if (!m)
                throw std::invalid_argument("Distance: the two points are at one place and neither can move apart");
            values[coordsOf[m->PointAt(*this)]] += c.value;
        }
    }

    void System::Refreeze(size_t c) {
        const std::vector<size_t> idx = LocalsOf(constraints[c]);
        const Eval e{*this, sharedCurves, values, idx, Size()};
        Freeze(constraints[c], e);
    }

    void System::RemoveConstraint(size_t c) {
        for (const auto &[spline, k] : constraints[c].madeCurvatures) {
            SplineData &d = splines[spline];
            held[d.curvature[k]] = 1;
            d.curvature[k] = sketch::none;
            if (std::all_of(d.curvature.begin(), d.curvature.end(), [](size_t u) { return u == sketch::none; }))
                d.curvature.clear();
        }
        for (const auto &[spline, k] : constraints[c].madeTangents) {
            SplineData &d = splines[spline];
            const size_t t = (size_t)(std::find(d.tangentAt.begin(), d.tangentAt.end(), k) - d.tangentAt.begin());
            held[d.tangent[t]] = 1;
            d.tangentAt.erase(d.tangentAt.begin() + (std::ptrdiff_t)t);
            d.tangent.erase(d.tangent.begin() + (std::ptrdiff_t)t);
        }
        for (size_t u : constraints[c].params)
            held[u] = 1;
        if (constraints[c].valueAt != sketch::none)
            held[constraints[c].valueAt] = 1;
        constraints.erase(constraints.begin() + (std::ptrdiff_t)c);
        for (SplineData &d : splines)
            if (d.fitted && d.fitted->formula)
                for (size_t &k : d.fitted->formula->dimensions)
                    k = k == c ? sketch::none : k != sketch::none && k > c ? k - 1 : k;
        Remerge();
    }

    // a spline in a Tangent gets a tangent direction (an angle unknown) at the joint where it has none, so the
    // constraint turns it there instead of moving its points: it starts along the other element (two such
    // splines: along their bisector), so the spline bends to meet it and nothing else moves. So does a spline whose
    // tangent line (id.tK) a constraint names, starting as it runs. Not a spline the constraints already determine:
    // its shape stays, the other element turns
    void System::GiveTangents(Constraint &c) {
        std::optional<ConstraintAnalysis> analysis;
        const auto free = [&](size_t element) {
            if (!analysis)
                analysis = Analyze();
            const std::vector<size_t> u = elements[element].Values(*this);
            return std::any_of(u.begin(), u.end(), [&](size_t k) { return !analysis->determined[k]; });
        };
        for (const Ref &r : c.refs)
            if (r.part == Part::Tangent && LacksTangent(r.element, r.index) && free(r.element))
                GiveTangent(c, r.element, r.index, DirectionAt({r.element}, r.PointAt(*this)));
        if (c.kind == Kind::CurvatureRadius && c.refs.size() == 1 && c.refs[0].part == Part::Point) {
            const Ref &r = c.refs[0];
            const SplineData &d = splines[elements[r.element].SplineIndex()];
            if (d.style != SplineStyle::Quintic || d.fitted || d.blend || d.poled)
                throw std::invalid_argument("CurvatureRadius: a curvature radius needs a spline of style 'quintic'");
            if (LacksTangent(r.element, r.index))
                GiveTangent(c, r.element, r.index, DirectionAt({r.element}, r.PointAt(*this)));
            if (d.CurvatureAt(r.index) == sketch::none)
                GiveCurvature(c, r.element, r.index, std::isinf(c.value) ? 0.0 : 1.0 / c.value);
            return;
        }
        if ((c.kind != Kind::Tangent && c.kind != Kind::Curvature) || c.refs.size() != 2 || c.ownTangents)
            return;
        const std::optional<size_t> joint = JointOf(c);
        if (!joint)
            return;
        std::array<std::optional<size_t>, 2> at;
        for (size_t i = 0; i < 2; ++i) {
            const Ref &r = c.refs[i];
            if (!r.IsSpline(*this))
                continue;
            const SplineData &d = splines[elements[r.element].SplineIndex()];
            for (size_t k = 0; k < d.points.size() && !at[i]; ++k)
                if (coordsOf[d.points[k]] == coordsOf[*joint] && LacksTangent(r.element, k) && free(r.element))
                    at[i] = k;
        }
        if (!at[0] && !at[1])
            return;
        const Eval e = PlainEval();
        const double sense = Sense(c.refs[0], c.refs[1], *joint, e);
        const Vertex ta = DirectionAt(c.refs[0], *joint), tb = DirectionAt(c.refs[1], *joint) * sense, both = ta + tb;
        const Vertex dir = !at[1] ? tb : !at[0] ? ta : Norm(both) > 0.0 ? both * (1.0 / Norm(both)) : ta;
        for (size_t i = 0; i < 2; ++i)
            if (at[i]) {
                GiveTangent(c, c.refs[i].element, *at[i], i == 0 ? dir : dir * sense);
                if (c.kind == Kind::Curvature && splines[elements[c.refs[i].element].SplineIndex()].style == SplineStyle::Quintic)
                    GiveCurvature(c, c.refs[i].element, *at[i], CurvatureAt({c.refs[i].element}, *joint));
            }
    }

    // a fitted spline keeps its shape; a spline by its poles takes its tangents from them
    bool System::LacksTangent(size_t element, size_t k) const {
        const SplineData &d = splines[elements[element].SplineIndex()];
        return !d.fitted && !d.blend && !d.poled && std::find(d.tangentAt.begin(), d.tangentAt.end(), k) == d.tangentAt.end();
    }

    void System::GiveTangent(Constraint &c, size_t element, size_t k, const Vertex &t) {
        const size_t spline = elements[element].SplineIndex();
        SplineData &d = splines[spline];
        d.tangentAt.push_back(k);
        d.tangent.push_back(NewValue(std::atan2(t.y, t.x), true));
        c.madeTangents.push_back({spline, k});
    }

    Vertex System::DirectionAt(const Ref &r, size_t point) const {
        const DualVector t = PlainEval().TangentAt(r, point);
        const Vertex v{t.x.v, t.y.v};
        return v * (1.0 / Norm(v));
    }

    void System::GiveCurvature(Constraint &c, size_t element, size_t k, double curvature) {
        const size_t spline = elements[element].SplineIndex();
        SplineData &d = splines[spline];
        d.curvature.resize(d.points.size(), sketch::none);
        d.curvature[k] = NewValue(curvature);
        c.madeCurvatures.push_back({spline, k});
    }

    double System::CurvatureAt(const Ref &r, size_t point) {
        return PlainEval().CurvatureAt(r, point).v;
    }

    // a point on a spline, its distance from it, a line, circle or spline touching it away from its points: the
    // positions along the spline(s) become unknowns, started where the drawing comes closest
    void System::GiveParams(Constraint &c) {
        if (!c.params.empty() || c.refs.size() != 2 || !(c.refs[0].IsCurve(*this) || c.refs[1].IsCurve(*this)))
            return;
        const bool tangent = c.kind == Kind::Tangent;
        if (!(c.kind == Kind::Coincident || c.kind == Kind::Distance || tangent) || (tangent && JointOf(c)))
            return;
        const Eval e = PlainEval();
        // a point's foot on an offset curve is its foot on the spline
        const auto curve = [&](const Ref &r) { return elements[r.element].Evaluator(*this); };
        const auto best = [&](const sketch::CurveEval &f, const auto &score) {
            const std::vector<double> u = f.Samples();
            return *std::min_element(u.begin(), u.end(), [&](double a, double b) { return score(f.At(a, 0)) < score(f.At(b, 0)); });
        };
        Ref a = c.refs[0], b = c.refs[1];
        if (a.IsCurve(*this) && b.IsCurve(*this)) {
            if (!tangent)
                return;
            const sketch::CurveEval f = curve(a), g = curve(b);
            const std::vector<double> u = f.Samples(), w = g.Samples();
            double ta = 0.0, tb = 0.0, nearest = std::numeric_limits<double>::infinity();
            for (double i : u)
                for (double k : w)
                    if (const double d = Norm(f.At(i, 0) - g.At(k, 0)); d < nearest) {
                        nearest = d;
                        ta = i;
                        tb = k;
                    }
            c.params = {NewValue(ta), NewValue(tb)};
            return;
        }
        if (a.IsCurve(*this))
            std::swap(a, b);
        const sketch::CurveEval f = curve(b);
        if (a.IsPoint(*this) && !tangent) {
            const DualVector q = e.PointOf(a);
            c.params = {NewValue(f.f.Project({q.x.v, q.y.v}))};
        } else if (tangent && a.IsLine(*this)) {
            const Carrier l = e.CarrierOf(a);
            const Vertex o{l.a.x.v, l.a.y.v}, u{l.u.x.v, l.u.y.v};
            c.params = {NewValue(best(f, [&](const Vertex &v) { return std::abs(Cross(u, v - o)); }))};
        } else if (tangent && a.IsRound(*this)) {
            const Round o = e.RoundOf(a);
            const Vertex m{o.c.x.v, o.c.y.v};
            c.params = {NewValue(best(f, [&](const Vertex &v) { return std::abs(Norm(v - m) - o.r.v); }))};
        }
    }

    void System::RemoveElement(size_t element) {
        Geometry::RemoveElement(element);
        for (Constraint &c : constraints)
            for (Ref &r : c.refs)
                if (r.element > element)
                    --r.element;
        Remerge();
    }

    // a coincidence of two points that are unknowns themselves (no mid): they share their unknowns
    bool System::IsMerge(const Constraint &c) const {
        return c.kind == Kind::Coincident && c.refs.size() == 2 && c.refs[0].IsRealPoint(*this) && c.refs[1].IsRealPoint(*this);
    }

    // the points merged by the coincidences, each into the held one or else the oldest of its group; a point keeps
    // where it is when it leaves a group
    void System::Remerge() {
        const size_t n = ownCoordsOf.size();
        for (size_t p = 0; p < n; ++p)
            if (coordsOf[p] != ownCoordsOf[p]) {
                values[ownCoordsOf[p]] = values[coordsOf[p]];
                values[ownCoordsOf[p] + 1] = values[coordsOf[p] + 1];
            }
        DisjointSets groups(n);
        for (const Constraint &c : constraints) {
            if (!IsMerge(c))
                continue;
            size_t a = groups.Find(c.refs[0].PointAt(*this)), b = groups.Find(c.refs[1].PointAt(*this));
            if (a == b)
                continue;
            if (held[ownCoordsOf[b]] > held[ownCoordsOf[a]] || (held[ownCoordsOf[b]] == held[ownCoordsOf[a]] && b < a))
                std::swap(a, b);
            groups.Join(b, a);
        }
        for (size_t p = 0; p < n; ++p)
            coordsOf[p] = ownCoordsOf[groups.Find(p)];
    }

    void System::Truncate(const Mark &m) {
        Geometry::Truncate(m.geometry);
        constraints.erase(constraints.begin() + (std::ptrdiff_t)m.constraints, constraints.end());
        Remerge();
        values = m.values;
    }

    // a length, radius or diameter, which has no side: only positive values
    bool System::Sideless(const Constraint &c) const {
        const size_t n = c.refs.size();
        return c.kind == Kind::Radius || c.kind == Kind::Diameter ||
               (c.kind == Kind::Distance && (n == 1 || (n == 2 && c.refs[0].IsPoint(*this) && c.refs[1].IsPoint(*this))));
    }

    // why v cannot be the sweep c holds; nullptr if it can, or c is no sweep
    const char *System::SweepOutside(const Constraint &c, double v) const {
        if (!IsSweep(c))
            return nullptr;
        if (elements[c.refs[0].element].ccw)
            return v > 0.0 && v < 360.0 ? nullptr : "the sweep of a counterclockwise arc lies in (0, 360) degrees";
        return v < 0.0 && v > -360.0 ? nullptr : "the sweep of a clockwise arc lies in (-360, 0) degrees";
    }

    // a rigid group holds the points of its elements (followed through merges), their radii and tangent directions
    // where they are now in a frame of its own, which moves and turns: three unknowns, its place (the first
    // point's) and its turn
    void System::GiveFrame(Constraint &c) {
        std::vector<size_t> points, others;
        for (const Ref &r : c.refs) {
            const Element &e = elements[r.element];
            if (r.part != Part::Whole && r.IsPoint(*this)) {
                points.push_back(r.PointAt(*this));
                continue;
            }
            if (e.kind == ElementKind::Axis || e.kind == ElementKind::Pattern || e.kind == ElementKind::Offset ||
                e.kind == ElementKind::OffsetCurve)
                throw std::invalid_argument("RigidGroup: an axis, pattern, offset or offset curve cannot be part of it");
            std::vector<size_t> p, o;
            HeldOf(e, Kind::Fix, p, o);
            points.insert(points.end(), p.begin(), p.end());
            others.insert(others.end(), o.begin(), o.end());
        }
        for (size_t p : points)
            if (std::none_of(c.heldPoints.begin(), c.heldPoints.end(), [&](size_t q) { return coordsOf[q] == coordsOf[p]; }))
                c.heldPoints.push_back(p);
        for (size_t u : others)
            if (std::find(c.held.begin(), c.held.end(), u) == c.held.end())
                c.held.push_back(u);
        if (c.heldPoints.empty())
            throw std::invalid_argument("RigidGroup: no points to hold");
        const Vertex o{values[coordsOf[c.heldPoints[0]]], values[coordsOf[c.heldPoints[0]] + 1]};
        for (size_t p : c.heldPoints)
            c.target.insert(c.target.end(), {values[coordsOf[p]] - o.x, values[coordsOf[p] + 1] - o.y});
        for (size_t u : c.held)
            c.target.push_back(values[u]);
        c.params = {NewValue(o.x), NewValue(o.y), NewValue(0.0, true)};
    }

    // a reference dimension's value: what the geometry has now (an angle in degrees, on its branch)
    double System::Measured(const Constraint &c) const {
        Constraint m = c;
        m.reference = false;
        m.value = 0.0;
        m.valueAt = sketch::none;
        const std::vector<size_t> none;
        const Eval e{*this, sharedCurves, values, none, Size()};
        if (m.kind == Kind::Angle && m.refs.size() == 2 && m.refs[0].IsLine(*this) && m.refs[1].IsLine(*this)) {
            const DualVector u = e.CarrierOf(m.refs[0]).u, w = e.CarrierOf(m.refs[1]).u;
            double a = Degrees(std::atan2(Cross(u, w).v, Dot(u, w).v));
            if (c.sign < 0.0)
                a += a > 0.0 ? -180.0 : 180.0;
            return c.variant < 0 ? -a : a;
        }
        if (m.kind == Kind::Angle && m.refs.size() == 1 && m.refs[0].IsPattern(*this))
            return Degrees(values[elements[m.refs[0].element].Step()]);
        if (IsSweep(m)) {
            const DualVector o = e.RoundOf(m.refs[0]).c;
            const std::array<DualVector, 2> p = e.EndsOf(m.refs[0]);
            const DualVector u = p[0] - o, w = p[1] - o;
            const double turn = Degrees(std::atan2(Cross(u, w).v, Dot(u, w).v));
            if (elements[m.refs[0].element].ccw)
                return turn <= 0.0 ? turn + 360.0 : turn;
            return turn >= 0.0 ? turn - 360.0 : turn;
        }
        if (m.kind == Kind::CurvatureRadius && m.refs.size() == 1 && m.refs[0].part == Part::Point) {
            const double k = e.CurvatureAt({m.refs[0].element}, m.refs[0].PointAt(*this)).v;
            return k == 0.0 ? std::numeric_limits<double>::infinity() : 1.0 / k;
        }
        std::vector<Dual> r;
        if (Residuals(m, e, r, JointOf(m)) != 1)
            throw std::invalid_argument(std::string(KindName(c.kind)) + ": cannot measure this as a reference");
        return r[0].v;
    }

    // what a Fix of a whole element holds: its points, and its other unknowns; a FixH or FixV one coordinate of
    // its points, and of a curve given by a formula also its turn
    void System::HeldOf(const Element &e, Kind kind, std::vector<size_t> &points, std::vector<size_t> &others) const {
        const bool all = kind == Kind::Fix;
        points.clear();
        others.clear();
        if (!all && (e.kind == ElementKind::Axis || e.kind == ElementKind::Pattern || e.kind == ElementKind::Offset))
            throw std::invalid_argument(std::string(KindName(kind)) + ": an axis, a pattern or an offset has no points to hold");
        switch (e.kind) {
        case ElementKind::Point:
            points = {e.Point()};
            break;
        case ElementKind::Line:
            points = {e.Start(), e.End()};
            break;
        case ElementKind::Circle:
            points = {e.Center()};
            if (all)
                others = {e.Radius()};
            break;
        case ElementKind::Arc:
            points = {e.Center(), e.Start(), e.End()};
            if (all)
                others = {e.Radius()};
            break;
        case ElementKind::Axis:
            others = {e.AxisAngle(), e.AxisDistance()};
            break;
        case ElementKind::Pattern:
            others = {e.Step()};
            if (e.HasStep2())
                others.push_back(e.Step2());
            break;
        case ElementKind::Offset:
            others = {e.Distance()};
            break;
        case ElementKind::Spline:
            if (splines[e.SplineIndex()].blend)
                throw std::invalid_argument(std::string(KindName(kind)) + ": a blend follows the elements it joins; fix them");
            // a fitted spline: its frame's place and turn
            if (const std::optional<sketch::FittedShape> &f = splines[e.SplineIndex()].fitted) {
                points = {f->origin};
                others = {f->angle};
            } else {
                points = splines[e.SplineIndex()].points;
                if (all)
                    others = splines[e.SplineIndex()].tangent;
            }
            if (const SplinePiece *piece = e.Piece(*this))
                for (size_t u : {piece->from, piece->to})
                    if (u != sketch::none)
                        others.push_back(u);
            break;
        case ElementKind::OffsetCurve:
            throw std::invalid_argument(std::string(KindName(kind)) + ": an offset curve follows its source; fix its source and the offset's distance");
        }
    }

    std::optional<size_t> System::DriverOf(size_t c) const {
        if (constraints[c].valueAt == sketch::none)
            return std::nullopt;
        for (size_t k = 0; k < constraints.size(); ++k)
            if (constraints[k].kind == Kind::Expression && constraints[k].drives == constraints[c].valueAt)
                return k;
        return std::nullopt;
    }

    std::string System::DomainError(const Constraint &c) const {
        const std::vector<size_t> idx = IndicesOf(c);
        const Eval e{*this, sharedCurves, values, idx, Size()};
        std::string error;
        Evaluated<Dual>(c.equation, [&](size_t which) { return e.Var(which); }, &error);
        return error;
    }

    // the dimension c holds its value in an unknown from now on, starting at v (an angle in degrees)
    void System::Link(size_t c, double v) {
        const bool turn = constraints[c].kind == Kind::Angle;
        constraints[c].valueAt = NewValue(turn ? Radians(v) : v, turn);
    }

    void System::Unlink(size_t c) {
        held[constraints[c].valueAt] = 1;
        constraints[c].valueAt = sketch::none;
    }

    // the value of a dimension an expression uses as the unknown holds it now (an angle in degrees)
    double System::Linked(const Constraint &c) const {
        const double v = values[c.valueAt];
        return isAngle[c.valueAt] ? Degrees(v) : v;
    }

    // Add gives c at most a param per curve and a tangent per spline
    bool System::Fits(const Constraint &c) const {
        size_t given = 0;
        for (const Ref &r : c.refs)
            given += r.IsSpline(*this) ? 2 : r.IsCurve(*this) ? 1 : 0;
        return UnknownsReadBy(c).size() + given <= maxLocal;
    }

    // angles in degrees
    double System::DimensionValue(size_t k) const {
        const Constraint &c = constraints[k];
        if (c.reference)
            return Measured(c);
        if (DriverOf(k))
            return Linked(c);
        return c.value;
    }

    // 0 for any other
    std::vector<double> System::LengthValues() const {
        std::vector<double> out(constraints.size(), 0.0);
        for (size_t k = 0; k < out.size(); ++k)
            if (Sideless(constraints[k]) && !constraints[k].reference)
                out[k] = DimensionValue(k);
        return out;
    }

    bool System::IsFixed(const sketch::Ref &point, sketch::Kind kind) const {
        return std::any_of(constraints.begin(), constraints.end(), [&](const Constraint &k) {
            return (k.kind == sketch::Kind::Fix || k.kind == kind) && k.refs.size() == 1 && k.refs[0].IsPoint(*this) &&
                   k.refs[0].PointAt(*this) == point.PointAt(*this);
        });
    }

    void System::Refix(size_t c) {
        Constraint &k = constraints[c];
        k.heldPoints.clear();
        k.held.clear();
        k.target.clear();
        Refreeze(c);
    }

    bool System::IsReferenced(size_t element) const {
        return std::any_of(constraints.begin(), constraints.end(), [&](const Constraint &c) { return c.RefersTo(element); });
    }

    std::string System::PlaceKeyOf(sketch::Kind kind, const std::vector<sketch::Ref> &refs) const {
        std::vector<std::string> parts;
        for (const sketch::Ref &r : refs)
            parts.push_back(r.IsPoint(*this) ? "p" + std::to_string(coordsOf[r.PointAt(*this)])
                                         : std::to_string(r.element) + ":" + std::to_string((int)r.part) + ":" + std::to_string(r.index));
        std::sort(parts.begin(), parts.end());
        std::string key = std::to_string((int)kind);
        for (const std::string &p : parts)
            key += "," + p;
        return key;
    }

    std::set<std::string> System::ConstraintKeys() const {
        std::set<std::string> out;
        for (const Constraint &c : constraints)
            out.insert(PlaceKeyOf(c.kind, c.refs));
        return out;
    }

    std::optional<size_t> System::CurveUsing(size_t k) const {
        for (size_t e = 0; e < elements.size(); ++e)
            if (const sketch::Element &el = elements[e]; el.IsFitted(*this))
                if (const std::optional<sketch::CurveFormula> &f = splines[el.SplineIndex()].fitted->formula;
                    f && std::find(f->dimensions.begin(), f->dimensions.end(), k) != f->dimensions.end())
                    return e;
        return std::nullopt;
    }

    void System::CheckRef(const Ref &r, Kind k) const {
        if (r.element >= elements.size())
            throw std::invalid_argument(std::string(KindName(k)) + ": no element " + std::to_string(r.element));
        if (!r.HasPart(*this))
            throw std::invalid_argument(std::string(KindName(k)) + ": element " + std::to_string(r.element) +
                                        " has no such point");
    }

}
