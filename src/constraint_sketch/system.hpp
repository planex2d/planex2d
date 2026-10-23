// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "constraint_sketch/analysis.hpp"
#include "constraint_sketch/constraint.hpp"
#include "constraint_sketch/dual.hpp"
#include "constraint_sketch/eval.hpp"
#include "constraint_sketch/expression.hpp"
#include "constraint_sketch/solver.hpp"
#include "core/disjointsets.hpp"
#include "core/vertex.hpp"
#include "curves/interpolate.hpp"
#include "sketch/curveeval.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::cs {

    using sketch::Element, sketch::ElementKind, sketch::Kind, sketch::KindName, sketch::OffsetCurveData, sketch::OffsetTie, sketch::Part,
        sketch::Ref, sketch::SplineData, sketch::SplinePiece;

    // elements on unknowns and the constraints between them, the System the solver takes
    class System : public sketch::Geometry {
    public:
        System() = default;
        // the geometry of a QuickSketch, without constraints
        explicit System(const sketch::Geometry &g) : sketch::Geometry(g) {}
        std::vector<Constraint> constraints;
        // per value, for the solver: the change that counts as much as a unit change of a length, and whether it is
        // fixed (held, or a point merged into another); set by Build
        std::vector<double> scale;
        std::vector<char> fixed;

        // checks the references, freezes the choices from the current geometry; the index of the new constraint
        size_t Add(Constraint c) {
            for (const Ref &r : c.refs)
                CheckRef(r, c.kind);
            if (Sideless(c) && !c.reference && !(c.value > 0.0))
                throw std::invalid_argument(std::string(KindName(c.kind)) + ": a length or radius must be positive");
            if (const char *outside = c.reference ? nullptr : SweepOutside(c, c.value))
                throw std::invalid_argument(std::string("Angle: ") + outside);
            if (c.kind == Kind::Distance && !c.reference && c.refs.size() == 2 && c.refs[0].IsPoint(*this) && c.refs[1].IsPoint(*this))
                MovePointsApart(c);
            size_ = Size();
            if (!c.reference) {
                GiveTangents(c);
                GiveParams(c);
            }
            if (c.kind == Kind::RigidGroup)
                GiveFrame(c);
            const std::vector<size_t> idx = IndicesOf(c);
            const Eval e{*this, sharedCurves, values, idx, size_};
            Freeze(c, e);
            if (c.reference)
                Measured(c);
            std::vector<Dual> r;
            Residuals(c, e, r, JointOf(c));
            const bool merge = IsMerge(c);
            constraints.push_back(std::move(c));
            if (merge)
                Remerge();
            return constraints.size() - 1;
        }
        // two points at one place have no direction to move apart in: one starts at the distance along h
        void MovePointsApart(const Constraint &c) {
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
        // the choices of constraint c taken anew from the geometry now (after a mirror)
        void Refreeze(size_t c) {
            const std::vector<size_t> idx = IndicesOf(constraints[c]);
            const Eval e{*this, sharedCurves, values, idx, Size()};
            Freeze(constraints[c], e);
        }
        void RemoveConstraint(size_t c) {
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
            Remerge();
        }
        // a spline in a Tangent gets a tangent direction (an angle unknown) at the joint where it has none, so the
        // constraint turns it there instead of moving its points: it starts along the other element (two such
        // splines: along their bisector), so the spline bends to meet it and nothing else moves. So does a spline whose
        // tangent line (id.tK) a constraint names, starting as it runs. Not a spline the constraints already determine
        // (as CATIA): its shape stays, the other element turns
        void GiveTangents(Constraint &c) {
            std::optional<ConstraintAnalysis> analysis;
            const auto free = [&](size_t element) {
                if (!analysis)
                    analysis = Analyze();
                const std::vector<size_t> u = elements[element].Values(*this);
                return std::any_of(u.begin(), u.end(), [&](size_t k) { return !analysis->determined[k]; });
            };
            const auto lacks = [&](size_t element, size_t k) {
                const SplineData &d = splines[elements[element].SplineIndex()];
                return std::find(d.tangentAt.begin(), d.tangentAt.end(), k) == d.tangentAt.end();
            };
            const auto give = [&](size_t element, size_t k, const Vertex &t) {
                const size_t spline = elements[element].SplineIndex();
                SplineData &d = splines[spline];
                d.tangentAt.push_back(k);
                d.tangent.push_back(NewValue(std::atan2(t.y, t.x), true));
                c.madeTangents.push_back({spline, k});
            };
            const std::vector<size_t> none;
            const auto direction = [&](const Ref &r, size_t point) {
                const DualVector t = Eval{*this, sharedCurves, values, none, size_}.TangentAt(r, point);
                const Vertex v{t.x.v, t.y.v};
                return v * (1.0 / Norm(v));
            };
            for (const Ref &r : c.refs)
                if (r.part == Part::Tangent && lacks(r.element, r.index) && free(r.element))
                    give(r.element, r.index, direction({r.element}, r.PointAt(*this)));
            if (c.kind != Kind::Tangent || c.refs.size() != 2)
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
                    if (coordsOf[d.points[k]] == coordsOf[*joint] && lacks(r.element, k) && free(r.element))
                        at[i] = k;
            }
            if (!at[0] && !at[1])
                return;
            const Eval e{*this, sharedCurves, values, none, size_};
            const double sense = Sense(c.refs[0], c.refs[1], *joint, e);
            const Vertex ta = direction(c.refs[0], *joint), tb = direction(c.refs[1], *joint) * sense, both = ta + tb;
            const Vertex dir = !at[1] ? tb : !at[0] ? ta : Norm(both) > 0.0 ? both * (1.0 / Norm(both)) : ta;
            for (size_t i = 0; i < 2; ++i)
                if (at[i])
                    give(c.refs[i].element, *at[i], i == 0 ? dir : dir * sense);
        }
        // a point on a spline, its distance from it, a line, circle or spline touching it away from its points: the
        // positions along the spline(s) become unknowns, started where the drawing comes closest
        void GiveParams(Constraint &c) {
            if (!c.params.empty() || c.refs.size() != 2 || !(c.refs[0].IsCurve(*this) || c.refs[1].IsCurve(*this)))
                return;
            const bool tangent = c.kind == Kind::Tangent;
            if (!(c.kind == Kind::Coincident || c.kind == Kind::Distance || tangent) || (tangent && JointOf(c)))
                return;
            const std::vector<size_t> none;
            const Eval e{*this, sharedCurves, values, none, size_};
            // a point's foot on an offset curve is its foot on the spline
            const auto curve = [&](const Ref &r) { return elements[r.element].Evaluator(*this); };
            // the samples of a curve: 32 per piece between its points
            const auto samples = [](const sketch::CurveEval &along) {
                const Curve<double, Vertex> &f = along.f;
                std::vector<double> u;
                for (size_t k = 0; k + 1 < f.params.size(); ++k)
                    for (size_t i = 0; i < 32; ++i)
                        u.push_back(f.params[k] + (f.params[k + 1] - f.params[k]) * (double)i / 32.0);
                u.push_back(f.params.back());
                return u;
            };
            const auto best = [&](const sketch::CurveEval &f, const auto &score) {
                const std::vector<double> u = samples(f);
                return *std::min_element(u.begin(), u.end(), [&](double a, double b) { return score(f.At(a, 0)) < score(f.At(b, 0)); });
            };
            Ref a = c.refs[0], b = c.refs[1];
            if (a.IsCurve(*this) && b.IsCurve(*this)) {
                if (!tangent)
                    return;
                const sketch::CurveEval f = curve(a), g = curve(b);
                const std::vector<double> u = samples(f), w = samples(g);
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
        // an element no constraint refers to and, for a point, no line or arc ends at
        void RemoveElement(size_t element) {
            Geometry::RemoveElement(element);
            for (Constraint &c : constraints)
                for (Ref &r : c.refs)
                    if (r.element > element)
                        --r.element;
            Remerge();
        }

        // a coincidence of two points that are unknowns themselves (no mid): they share their unknowns
        bool IsMerge(const Constraint &c) const {
            return c.kind == Kind::Coincident && c.refs.size() == 2 && c.refs[0].IsRealPoint(*this) && c.refs[1].IsRealPoint(*this);
        }
        // the points merged by the coincidences, each into the held one or else the oldest of its group; a point keeps
        // where it is when it leaves a group
        void Remerge() {
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

        // undo everything added after the mark
        // the values too: a solve moves those there were
        struct Mark {
            Geometry::Mark geometry;
            size_t constraints;
            std::vector<double> values;
        };
        Mark Marked() const { return {Geometry::Marked(), constraints.size(), values}; }
        void Truncate(const Mark &m) {
            Geometry::Truncate(m.geometry);
            constraints.erase(constraints.begin() + (std::ptrdiff_t)m.constraints, constraints.end());
            Remerge();
            values = m.values;
        }

        // a length, radius or diameter, which has no side: only positive values
        bool Sideless(const Constraint &c) const {
            const size_t n = c.refs.size();
            return c.kind == Kind::Radius || c.kind == Kind::Diameter ||
                   (c.kind == Kind::Distance && (n == 1 || (n == 2 && c.refs[0].IsPoint(*this) && c.refs[1].IsPoint(*this))));
        }
        // an Angle of one arc: its sweep, in degrees, negative for a clockwise arc
        bool IsSweep(const Constraint &c) const { return c.kind == Kind::Angle && c.refs.size() == 1 && c.refs[0].IsArc(*this); }
        // why v cannot be the sweep c holds; nullptr if it can, or c is no sweep
        const char *SweepOutside(const Constraint &c, double v) const {
            if (!IsSweep(c))
                return nullptr;
            if (elements[c.refs[0].element].ccw)
                return v > 0.0 && v < 360.0 ? nullptr : "the sweep of a counterclockwise arc lies in (0, 360) degrees";
            return v < 0.0 && v > -360.0 ? nullptr : "the sweep of a clockwise arc lies in (-360, 0) degrees";
        }


        // a rigid group holds the points of its elements (followed through merges), their radii and tangent directions
        // where they are now in a frame of its own, which moves and turns: three unknowns, its place (the first
        // point's) and its turn
        void GiveFrame(Constraint &c) {
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
                HeldOf(e, p, o);
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
        double Measured(const Constraint &c) const {
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
                return a;
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
            std::vector<Dual> r;
            if (Residuals(m, e, r, JointOf(m)) != 1)
                throw std::invalid_argument(std::string(KindName(c.kind)) + ": cannot measure this as a reference");
            return r[0].v;
        }
        // what a Fix of a whole element holds: its points, and its other unknowns
        void HeldOf(const Element &e, std::vector<size_t> &points, std::vector<size_t> &others) const {
            points.clear();
            others.clear();
            switch (e.kind) {
            case ElementKind::Point:
                points = {e.Point()};
                break;
            case ElementKind::Line:
                points = {e.Start(), e.End()};
                break;
            case ElementKind::Circle:
                points = {e.Center()};
                others = {e.Radius()};
                break;
            case ElementKind::Arc:
                points = {e.Center(), e.Start(), e.End()};
                others = {e.Radius()};
                break;
            case ElementKind::Axis:
                others = {e.AxisAngle(), e.AxisDistance()};
                break;
            case ElementKind::Pattern:
                others = {e.Step()};
                break;
            case ElementKind::Offset:
                others = {e.Distance()};
                break;
            case ElementKind::Spline:
                points = splines[e.SplineIndex()].points;
                others = splines[e.SplineIndex()].tangent;
                if (const SplinePiece *piece = e.Piece(*this))
                    for (size_t u : {piece->from, piece->to})
                        if (u != sketch::none)
                            others.push_back(u);
                break;
            case ElementKind::OffsetCurve:
                throw std::invalid_argument("Fix: an offset curve follows its source; fix its source and the offset's distance");
            }
        }

        // the solver's view: the constraints, then the arcs' own equations
        void Build() {
            fixed.assign(held.begin(), held.end());
            for (size_t p = 0; p < ownCoordsOf.size(); ++p)
                if (coordsOf[p] != ownCoordsOf[p])
                    fixed[ownCoordsOf[p]] = fixed[ownCoordsOf[p] + 1] = 1;
            structural.clear();
            owner.clear();
            AddArcEquations();
            AddValueEquations();
            size_ = Size();
            scale.resize(values.size());
            for (size_t u = 0; u < values.size(); ++u)
                scale[u] = isAngle[u] ? 1.0 / size_ : 1.0;
            sharedCurves.Clear();
            const size_t count = Count();
            indices.assign(count, {});
            rows.assign(count, 0);
            joints.assign(count, std::nullopt);
            levers.assign(count, size_);
            std::vector<Dual> r;
            for (size_t c = 0; c < count; ++c) {
                indices[c] = IndicesOf(At(c));
                joints[c] = JointOf(At(c));
                levers[c] = LeverOf(At(c));
                rows[c] = Residuals(At(c), Eval{*this, sharedCurves, values, indices[c], size_, levers[c]}, r, joints[c]);
            }
        }
        // an end on a circle once: arcs cut from one circle share its centre, radius and the points where they meet
        void AddArcEquations() {
            std::set<std::array<size_t, 3>> onCircle;
            for (size_t k = 0; k < elements.size(); ++k)
                if (elements[k].kind == ElementKind::Arc) {
                    const Element &e = elements[k];
                    // a fixed arc (projected) is where it is, its own equations would be constant ones
                    if (fixed[coordsOf[e.Center()]] && fixed[e.Radius()] && fixed[coordsOf[e.Start()]] && fixed[coordsOf[e.End()]])
                        continue;
                    int ends = 0;
                    for (int i = 0; i < 2; ++i)
                        if (onCircle.insert({coordsOf[e.Center()], e.Radius(), coordsOf[i == 0 ? e.Start() : e.End()]}).second)
                            ends |= 1 << i;
                    if (!ends)
                        continue;
                    Constraint c{Kind::OnCircle, {{k}}};
                    c.variant = ends;
                    structural.push_back(c);
                    owner.push_back(k);
                }
        }
        // the values of the dimensions an expression uses but does not drive, held
        void AddValueEquations() {
            valueRows.clear();
            std::vector<size_t> driven;
            for (const Constraint &expression : constraints)
                if (expression.kind == Kind::Expression && expression.drives != sketch::none)
                    driven.push_back(expression.drives);
            for (size_t k = 0; k < constraints.size(); ++k) {
                const Constraint &d = constraints[k];
                if (d.valueAt == sketch::none || d.reference || std::find(driven.begin(), driven.end(), d.valueAt) != driven.end())
                    continue;
                Constraint keep{Kind::Value};
                keep.valueAt = d.valueAt;
                keep.value = d.value;
                valueRows.push_back({constraints.size() + structural.size(), k});
                structural.push_back(std::move(keep));
                owner.push_back(k);
            }
        }
        // a direction residual times the shortest of the lengths its directions are taken over is how far a point
        // moves: the length of a line, the radius of an arc or circle, the shortest chord of a spline. Fixed per Build,
        // so a short element far from the origin solves as it does near it
        double LeverOf(const Constraint &c) const {
            if (c.kind == Kind::Expression)
                return Weight(c);
            const std::vector<size_t> none;
            const Eval e{*this, sharedCurves, values, none, size_};
            double lever = size_;
            for (const Ref &r : c.refs) {
                if (r.part != Part::Whole && r.part != Part::Tangent)
                    continue;
                const Element &el = elements[r.element];
                double l = 0.0;
                if (el.kind == ElementKind::Line)
                    l = Norm(e.At(el.End()) - e.At(el.Start())).v;
                else if (el.kind == ElementKind::Arc || el.kind == ElementKind::Circle)
                    l = std::abs(values[el.Radius()]);
                else if (el.kind == ElementKind::Spline || el.kind == ElementKind::OffsetCurve) {
                    const std::vector<size_t> &p = splines[el.Spline(*this)].points;
                    l = size_;
                    for (size_t k = 0; k + 1 < p.size(); ++k)
                        l = std::min(l, Norm(e.At(p[k + 1]) - e.At(p[k])).v);
                }
                if (l > 0.0)
                    lever = std::min(lever, l);
            }
            return lever;
        }
        // what makes an expression's residual a length: one over the most a unit change of one of its (scaled) values
        // changes it, at the values now; 1 if nothing changes it
        double Weight(const Constraint &c) const {
            const std::vector<size_t> idx = IndicesOf(c);
            const Eval e{*this, sharedCurves, values, idx, size_};
            const Dual f = Evaluated<Dual>(c.equation, [&](size_t which) { return e.Var(which); });
            double most = 0.0;
            for (size_t k = 0; k < idx.size(); ++k)
                most = std::max(most, std::abs(f.g[k] * scale[idx[k]]));
            return std::isfinite(most) && most > 0.0 ? 1.0 / most : 1.0;
        }
        // per constraint: it, or the equation that keeps its value (a driving dimension an expression uses), takes no
        // rank; a is the analysis of the last Build
        std::vector<char> Redundancies(const ConstraintAnalysis &a) const {
            std::vector<char> out(a.redundant.begin(), a.redundant.begin() + (std::ptrdiff_t)constraints.size());
            for (const auto &[row, c] : valueRows)
                out[c] = out[c] || a.redundant[row];
            return out;
        }
        // the expression that drives the dimension c
        std::optional<size_t> DriverOf(size_t c) const {
            if (constraints[c].valueAt == sketch::none)
                return std::nullopt;
            for (size_t k = 0; k < constraints.size(); ++k)
                if (constraints[k].kind == Kind::Expression && constraints[k].drives == constraints[c].valueAt)
                    return k;
            return std::nullopt;
        }
        // the expression c out of its domain, or on its edge, at the values now: what is wrong there; empty if nothing
        std::string DomainError(const Constraint &c) const {
            const std::vector<size_t> idx = IndicesOf(c);
            const Eval e{*this, sharedCurves, values, idx, Size()};
            std::string error;
            Evaluated<Dual>(c.equation, [&](size_t which) { return e.Var(which); }, &error);
            return error;
        }
        // the dimension c holds its value in an unknown from now on, starting at v (an angle in degrees)
        void Link(size_t c, double v) {
            const bool turn = constraints[c].kind == Kind::Angle;
            constraints[c].valueAt = NewValue(turn ? Radians(v) : v, turn);
        }
        // its value is fixed again, value
        void Unlink(size_t c) {
            held[constraints[c].valueAt] = 1;
            constraints[c].valueAt = sketch::none;
        }
        // the value of a dimension an expression uses as the unknown holds it now (an angle in degrees)
        double Linked(const Constraint &c) const {
            const double v = values[c.valueAt];
            return isAngle[c.valueAt] ? Degrees(v) : v;
        }
        size_t Count() const { return constraints.size() + structural.size(); }
        // the arc of each of the arcs' own equations, after the constraints
        const std::vector<size_t> &StructuralOwners() const { return owner; }
        // the value equations among Count() and the dimensions whose values they hold
        const std::vector<std::pair<size_t, size_t>> &ValueRows() const { return valueRows; }
        size_t Rows(size_t c) const { return rows[c]; }
        const std::vector<size_t> &Indices(size_t c) const { return indices[c]; }
        void Evaluate(size_t c, const std::vector<double> &v, double *r, double *J) const {
            thread_local std::vector<Dual> res;
            const std::vector<size_t> &idx = indices[c];
            const size_t n = Residuals(At(c), Eval{*this, sharedCurves, v, idx, size_, levers[c]}, res, joints[c]);
            for (size_t i = 0; i < n; ++i) {
                r[i] = res[i].v;
                for (size_t k = 0; k < idx.size(); ++k)
                    J[i * idx.size() + k] = res[i].g[k];
            }
        }

        // the change of row c's residuals per unit of its value: lever x pi/180 for an angle dimension (per degree),
        // the value equation of an angle the size x pi/180, else 1 (a length, or no value)
        double RowScale(size_t c) const {
            const Constraint &k = At(c);
            constexpr double degree = std::numbers::pi / 180.0;
            if (k.kind == Kind::Value)
                return isAngle[k.valueAt] ? size_ * degree : 1.0;
            if (k.kind != Kind::Angle || k.valueAt != sketch::none)
                return 1.0;
            return (k.refs.size() == 1 && k.refs[0].IsPattern(*this) ? size_ : levers[c]) * degree;
        }

        // predict: with the linear prediction of the move (SolveResult::predicted)
        SolveResult Solve(bool predict = false, const std::vector<double> *origin = nullptr) {
            Build();
            Unknowns x = SolverUnknowns();
            const SolveResult r = SolveConstraints(*this, x, size_, predict, origin);
            values = std::move(x.value);
            return r;
        }
        // built: nothing changed since Build (a solve that took no step)
        ConstraintAnalysis Analyze(bool complete = true, bool built = false) {
            if (!built)
                Build();
            return AnalyzeConstraints(*this, SolverUnknowns(), size_, complete, &analysisCache);
        }

    private:
        std::vector<Constraint> structural;
        std::vector<size_t> owner;
        // the value equations among the constraints and structural ones (their index in Count()) and their dimensions
        std::vector<std::pair<size_t, size_t>> valueRows;
        std::vector<std::vector<size_t>> indices;
        std::vector<size_t> rows;
        std::vector<double> levers;
        std::vector<std::optional<size_t>> joints;
        double size_ = 1.0;
        AnalysisCache analysisCache;
        mutable SharedCurves sharedCurves;

        // the solver works on a copy of the values, with their scales and what is fixed
        Unknowns SolverUnknowns() const {
            Unknowns x(values);
            x.scale = scale;
            x.fixed = fixed;
            return x;
        }
        const Constraint &At(size_t c) const { return c < constraints.size() ? constraints[c] : structural[c - constraints.size()]; }

        void CheckRef(const Ref &r, Kind k) const {
            if (r.element >= elements.size())
                throw std::invalid_argument(std::string(KindName(k)) + ": no element " + std::to_string(r.element));
            if (!r.HasPart(*this))
                throw std::invalid_argument(std::string(KindName(k)) + ": element " + std::to_string(r.element) +
                                            " has no such point");
        }


        std::vector<size_t> IndicesOf(const Constraint &c) const {
            const bool angles = c.kind == Kind::Copy && !c.held.empty();
            std::vector<size_t> out = angles ? c.held : std::vector<size_t>{};
            if (IsMerge(c))
                return out;
            for (size_t i = angles ? 2 : 0; i < c.refs.size(); ++i) {
                const Ref &r = c.refs[i];
                std::vector<size_t> u = elements[r.element].Values(*this);
                if (r.part == Part::Start || r.part == Part::End || r.part == Part::Center || r.part == Part::Point) {
                    const size_t p = r.PointAt(*this);
                    u = {coordsOf[p], coordsOf[p] + 1};
                }
                out.insert(out.end(), u.begin(), u.end());
            }
            out.insert(out.end(), c.params.begin(), c.params.end());
            if (c.valueAt != sketch::none)
                out.push_back(c.valueAt);
            for (const Node &node : c.equation)
                if (node.op == Op::Value)
                    out.push_back(node.unknown);
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            // held for good: constants, as projected geometry
            std::erase_if(out, [&](size_t u) { return held[u]; });
            if (out.size() > maxLocal)
                throw std::invalid_argument("a constraint on more than about 60 points (a spline's, a rigid group's)");
            return out;
        }

        // a point that is an end of both (the same unknowns), as where a line or arc runs on into an arc; of a spline
        // any of its points
        std::optional<size_t> Joint(const Ref &a, const Ref &b) const {
            if (a.part != Part::Whole || b.part != Part::Whole)
                return std::nullopt;
            for (size_t p : elements[a.element].PointsOn(*this))
                for (size_t q : elements[b.element].PointsOn(*this))
                    if (coordsOf[p] == coordsOf[q])
                        return p;
            return std::nullopt;
        }

        bool HasEnd(const Ref &r, size_t point) const {
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
        std::optional<size_t> JointOf(const Constraint &c) const {
            if (c.kind != Kind::Tangent || c.refs.size() != 2)
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
        // inner point, a closed spline's seam) the drawing decides
        double Sense(const Ref &a, const Ref &b, size_t joint, const Eval &e) const {
            const auto leaves = [&](const Ref &r) {
                const Element &el = elements[r.element];
                const std::array<size_t, 2> ends = el.Ends(*this);
                if (ends[0] == sketch::none || coordsOf[ends[0]] == coordsOf[ends[1]])
                    return 0.0;
                const double travel = el.kind == ElementKind::Arc && !el.ccw ? -1.0 : 1.0;
                return coordsOf[ends[0]] == coordsOf[joint] ? travel : coordsOf[ends[1]] == coordsOf[joint] ? -travel : 0.0;
            };
            const double la = leaves(a), lb = leaves(b);
            if (la != 0.0 && lb != 0.0)
                return -la * lb;
            return Side(Dot(e.TangentAt(a, joint), e.TangentAt(b, joint)));
        }

        // the residual rows of c, lengths; a combination of elements the kind does not relate is an error
        size_t Residuals(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
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
        std::optional<size_t> RowsOf(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
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
            case Kind::Equal:
                return EqualRows(c, e, r);
            case Kind::Symmetric:
                return SymmetricRows(c, e, r);
            case Kind::SymmetricCarriers:
                return SymmetricCarriersRows(c, e, r);
            case Kind::Concentric:
                return ConcentricRows(c, e, r);
            case Kind::Fix:
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
            }
            return std::nullopt;
        }
        std::optional<size_t> CoincidentRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual size(e.size);
            Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
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
        std::optional<size_t> HorizontalVerticalRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual size(e.size);
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
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
        std::optional<size_t> ParallelPerpendicularRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual lever(e.lever);
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
            if (n == 2 && a.IsLine(*this) && b.IsLine(*this)) {
                const DualVector u = e.CarrierOf(a).u, w = e.CarrierOf(b).u;
                r[0] = (c.kind == Kind::Parallel ? Cross(u, w) : Dot(u, w)) * lever;
                return 1;
            }
            return std::nullopt;
        }
        std::optional<size_t> TangentRows(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual lever(e.lever), sign(c.sign);
            Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
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
        std::optional<size_t> EqualRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
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
        std::optional<size_t> SymmetricRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
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
        std::optional<size_t> SymmetricCarriersRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
            if (n == 3 && a.IsSegment(*this) && b.IsLine(*this) && q[2].IsLine(*this)) {
                const Carrier m = e.CarrierOf(q[2]), l = e.CarrierOf(b);
                const std::array<DualVector, 2> p = e.EndsOf(a);
                r[0] = Cross(l.u, m.Mirrored(p[0]) - l.a);
                r[1] = Cross(l.u, m.Mirrored(p[1]) - l.a);
                return 2;
            }
            return std::nullopt;
        }
        std::optional<size_t> ConcentricRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
            if (n == 2 && (a.IsRound(*this) || a.IsPoint(*this)) && (b.IsRound(*this) || b.IsPoint(*this))) {
                const DualVector d = e.CenterOf(a) - e.CenterOf(b);
                r[0] = d.x;
                r[1] = d.y;
                return 2;
            }
            return std::nullopt;
        }
        std::optional<size_t> FixRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual size(e.size);
            const Ref a = n > 0 ? q[0] : Ref{};
            if (n != 1)
                return std::nullopt;
            if (a.IsPoint(*this)) {
                const DualVector p = e.PointOf(a);
                r[0] = p.x - Dual(c.target[0]);
                r[1] = p.y - Dual(c.target[1]);
                return 2;
            } else if (a.IsArc(*this)) {
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
            } else {
                const std::vector<size_t> &u = c.held;
                const size_t m = 2 * c.heldPoints.size();
                r.resize(std::max(r.size(), m + u.size()));
                for (size_t k = 0; k < c.heldPoints.size(); ++k) {
                    const DualVector p = e.At(c.heldPoints[k]);
                    r[2 * k] = p.x - Dual(c.target[2 * k]);
                    r[2 * k + 1] = p.y - Dual(c.target[2 * k + 1]);
                }
                for (size_t k = 0; k < u.size(); ++k)
                    r[m + k] = isAngle[u[k]] ? (e.Var(u[k]) - Dual(c.target[m + k])) * size : e.Var(u[k]) - Dual(c.target[m + k]);
                return m + u.size();
            }
        }
        std::optional<size_t> DistanceRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual sign(c.sign);
            const Dual v = c.valueAt == sketch::none ? Dual(c.value) : e.Var(c.valueAt);
            Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
            if (n == 1 && (a.IsPattern(*this) || a.IsOffset(*this))) {
                const Element &el = elements[a.element];
                r[0] = e.Var(a.IsPattern(*this) ? el.Step() : el.Distance()) - v;
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
        std::optional<size_t> RadiusRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual v = c.valueAt == sketch::none ? Dual(c.value) : e.Var(c.valueAt);
            const Ref a = n > 0 ? q[0] : Ref{};
            if (n == 1 && (a.IsRound(*this) || a.IsOffset(*this))) {
                const Dual size = a.IsOffset(*this) ? e.Var(elements[a.element].Distance()) : e.RoundOf(a).r;
                r[0] = size * Dual(c.kind == Kind::Radius ? 1.0 : 2.0) - v;
                return 1;
            }
            return std::nullopt;
        }
        std::optional<size_t> AngleRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual size(e.size), lever(e.lever), sign(c.sign);
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
            const bool linked = c.valueAt != sketch::none;
            const auto rotated = [&](const DualVector &u) { return linked ? TurnedRadians(u, e.Var(c.valueAt)) : Turned(u, c.value); };
            if (n == 1 && a.IsPattern(*this)) {
                r[0] = (e.Var(elements[a.element].Step()) - (linked ? e.Var(c.valueAt) : Dual(Radians(c.value)))) * size;
                return 1;
            }
            if (n == 2 && a.IsLine(*this) && b.IsLine(*this)) {
                // the angle itself, on the branch the drawing chose (the angle or the angle + 180 degrees)
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
        std::optional<size_t> RigidGroupRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const Dual size(e.size);
            const Dual turn = e.Var(c.params[2]), cs = Cos(turn), sn = Sin(turn);
            const DualVector o{e.Var(c.params[0]), e.Var(c.params[1])};
            const size_t m = 2 * c.heldPoints.size();
            r.resize(std::max(r.size(), m + c.held.size()));
            for (size_t k = 0; k < c.heldPoints.size(); ++k) {
                const Dual lx(c.target[2 * k]), ly(c.target[2 * k + 1]);
                const DualVector p = e.At(c.heldPoints[k]);
                r[2 * k] = p.x - (o.x + lx * cs - ly * sn);
                r[2 * k + 1] = p.y - (o.y + lx * sn + ly * cs);
            }
            for (size_t k = 0; k < c.held.size(); ++k) {
                const size_t u = c.held[k];
                if (isAngle[u]) {
                    const Dual d = e.Var(u) - (Dual(c.target[m + k]) + turn);
                    r[m + k] = Atan2(Sin(d), Cos(d)) * size;
                } else
                    r[m + k] = e.Var(u) - Dual(c.target[m + k]);
            }
            return m + c.held.size();
        }
        std::optional<size_t> CopyRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual size(e.size);
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
            if (c.held.size() == 2 && n == 2) {
                // a copied piece's end at the original's position along its spline
                r[0] = e.Var(c.held[1]) - e.Var(c.held[0]);
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
            if (n != 4 || !a.IsPoint(*this) || !b.IsPoint(*this) || !q[2].IsPattern(*this))
                return std::nullopt;
            const DualVector p = e.PointOf(a), copy = e.PointOf(b);
            const Dual step = e.Var(elements[q[2].element].Step()) * Dual((double)c.variant);
            DualVector to;
            if (elements[q[2].element].IsLinear()) {
                if (!q[3].IsLine(*this))
                    return std::nullopt;
                to = p + e.CarrierOf(q[3]).u * step;
            } else {
                if (!q[3].IsPoint(*this))
                    return std::nullopt;
                const DualVector o = e.PointOf(q[3]), d = p - o;
                const Dual cs = Cos(step), sn = Sin(step);
                to = o + DualVector{d.x * cs - d.y * sn, d.x * sn + d.y * cs};
            }
            r[0] = copy.x - to.x;
            r[1] = copy.y - to.y;
            return 2;
        }
        std::optional<size_t> OnCircleRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Ref a = n > 0 ? q[0] : Ref{};
            const Round o = e.RoundOf(a);
            const std::array<DualVector, 2> p = e.EndsOf(a);
            size_t k = 0;
            for (int i = 0; i < 2; ++i)
                if (c.variant & (1 << i))
                    r[k++] = Norm(p[i] - o.c) - o.r;
            return k;
        }
        std::optional<size_t> OffsetRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const std::vector<Ref> &q = c.refs;
            const size_t n = q.size();
            const Dual sign(c.sign);
            const Ref a = n > 0 ? q[0] : Ref{}, b = n > 1 ? q[1] : Ref{};
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
        std::optional<size_t> ExpressionRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const Dual lever(e.lever);
            // lever: what makes it a length, fixed per Build (LeverOf)
            r[0] = Evaluated<Dual>(c.equation, [&](size_t which) { return e.Var(which); }) * lever;
            return 1;
        }
        std::optional<size_t> ValueRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const {
            const Dual size(e.size);
            r[0] = isAngle[c.valueAt] ? (e.Var(c.valueAt) - Dual(Radians(c.value))) * size : e.Var(c.valueAt) - Dual(c.value);
            return 1;
        }

        // the choices with two or more solutions, taken from the geometry as it is now
        void Freeze(Constraint &c, const Eval &e) const {
            switch (c.kind) {
            case Kind::Distance:
            case Kind::Tangent:
                FreezeSide(c, e);
                return;
            case Kind::Angle:
                FreezeAngle(c, e);
                return;
            case Kind::Symmetric:
                FreezeSymmetric(c, e);
                return;
            case Kind::Fix:
                FreezeFix(c, e);
                return;
            default:
                return;
            }
        }
        // the variant with the smallest residuals
        void FreezeSmallest(Constraint &c, const Eval &e, int variants) const {
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
        void FreezeSide(Constraint &c, const Eval &e) const {
            const size_t n = c.refs.size();
            Ref a = n > 0 ? c.refs[0] : Ref{}, b = n > 1 ? c.refs[1] : Ref{};
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
        void FreezeAngle(Constraint &c, const Eval &e) const {
            const std::vector<Ref> &q = c.refs;
            if (q.size() == 2 && q[0].IsLine(*this) && q[1].IsLine(*this))
                c.sign = Side(Dot(Turned(e.CarrierOf(q[0]).u, c.value), e.CarrierOf(q[1]).u));
        }
        void FreezeSymmetric(Constraint &c, const Eval &e) const {
            const std::vector<Ref> &q = c.refs;
            if (q.size() == 3 && ((q[0].IsSegment(*this) && q[1].IsSegment(*this)) || (q[0].IsArc(*this) && q[1].IsArc(*this))))
                FreezeSmallest(c, e, 2);
        }
        void FreezeFix(Constraint &c, const Eval &e) const {
            if (c.refs.size() != 1)
                return;
            if (c.refs[0].IsPoint(*this)) {
                const DualVector p = e.PointOf(c.refs[0]);
                c.target = {p.x.v, p.y.v};
            } else {
                HeldOf(elements[c.refs[0].element], c.heldPoints, c.held);
                for (size_t p : c.heldPoints)
                    c.target.insert(c.target.end(), {values[coordsOf[p]], values[coordsOf[p] + 1]});
                for (size_t u : c.held)
                    c.target.push_back(values[u]);
            }
        }
    };

}
