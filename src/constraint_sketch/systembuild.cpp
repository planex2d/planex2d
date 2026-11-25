// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/system.hpp"

#include <set>

namespace planex2d::cs {

    // the solver's view: the constraints, then the arcs' own equations
    void System::Build() {
        RefitCurves();
        fixed.assign(held.begin(), held.end());
        for (size_t p = 0; p < ownCoordsOf.size(); ++p)
            if (coordsOf[p] != ownCoordsOf[p])
                fixed[ownCoordsOf[p]] = fixed[ownCoordsOf[p] + 1] = 1;
        structural.clear();
        owner.clear();
        AddArcEquations();
        AddCurveEquations();
        AddValueEquations();
        builtSize = Size();
        scale.resize(values.size());
        for (size_t u = 0; u < values.size(); ++u)
            scale[u] = isAngle[u] ? 1.0 / builtSize : 1.0;
        sharedCurves.Clear();
        const size_t count = Count();
        indices.assign(count, {});
        rows.assign(count, 0);
        joints.assign(count, std::nullopt);
        levers.assign(count, builtSize);
        blocks.assign(count, {});
        std::vector<Dual> r;
        for (size_t c = 0; c < count; ++c) {
            if (IsPointwise(At(c))) {
                blocks[c] = BlocksOf(At(c));
                for (const RowBlock &b : blocks[c]) {
                    rows[c] += b.rows;
                    indices[c].insert(indices[c].end(), b.indices.begin(), b.indices.end());
                }
                std::sort(indices[c].begin(), indices[c].end());
                indices[c].erase(std::unique(indices[c].begin(), indices[c].end()), indices[c].end());
                continue;
            }
            indices[c] = IndicesOf(At(c));
            joints[c] = JointOf(At(c));
            levers[c] = LeverOf(At(c));
            rows[c] = Residuals(At(c), Eval{*this, sharedCurves, values, indices[c], builtSize, levers[c]}, r, joints[c]);
            blocks[c] = {{rows[c], indices[c]}};
        }
    }

    // an end on a circle once: arcs cut from one circle share its centre, radius and the points where they meet
    void System::AddArcEquations() {
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

    // a fitted spline's ends on its placed shape, once per spline, as an arc's ends on its circle
    void System::AddCurveEquations() {
        std::set<size_t> done;
        for (size_t k = 0; k < elements.size(); ++k) {
            const Element &e = elements[k];
            if (!e.IsFitted(*this) || !done.insert(e.SplineIndex()).second)
                continue;
            const sketch::FittedShape &f = *splines[e.SplineIndex()].fitted;
            // held for good: its own equations would be constant ones
            const std::vector<size_t> u = e.Values(*this);
            if (std::all_of(u.begin(), u.end(), [&](size_t x) { return bool(fixed[x]); }) && fixed[coordsOf[f.origin]])
                continue;
            structural.push_back({Kind::OnCurve, {{k}}});
            owner.push_back(k);
        }
    }

    // the fitted splines whose formula uses a dimension that changed take the shape it gives now, their copies the
    // shape of their source; a removed curve keeps its shape. Public: a sketch that sets its dimensions back takes the
    // shapes back with them
    void System::RefitCurves() {
        std::vector<char> alive(splines.size(), 0);
        for (const Element &e : elements)
            if (e.kind == ElementKind::Spline)
                alive[e.SplineIndex()] = 1;
        for (size_t k = 0; k < splines.size(); ++k) {
            if (!splines[k].fitted || !alive[k])
                continue;
            sketch::FittedShape &f = *splines[k].fitted;
            if (f.copyOf != sketch::none) {
                f.shape = splines[f.copyOf].fitted->shape;
                continue;
            }
            if (!f.formula)
                continue;
            // a scaled sketch's lengths as the formula knew them: Reshape scales the shape it gives
            std::vector<double> now;
            for (size_t c : f.formula->dimensions)
                now.push_back(constraints[c].kind == Kind::Angle ? constraints[c].value : constraints[c].value / f.scale);
            if (now != f.formula->used) {
                f.Reshape(f.formula->Shape(now));
                f.formula->used = std::move(now);
            }
        }
    }

    void System::AddValueEquations() {
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
    double System::LeverOf(const Constraint &c) const {
        if (c.kind == Kind::Expression)
            return Weight(c);
        const Eval e = PlainEval();
        double lever = builtSize;
        for (const Ref &r : c.refs) {
            if (r.part != Part::Whole && r.part != Part::Tangent)
                continue;
            const Element &el = elements[r.element];
            double l = 0.0;
            if (el.kind == ElementKind::Line)
                l = Norm(e.At(el.End()) - e.At(el.Start())).v;
            else if (el.IsRound())
                l = std::abs(values[el.Radius()]);
            else if (el.kind == ElementKind::Spline || el.kind == ElementKind::OffsetCurve) {
                const std::vector<size_t> &p = splines[el.Spline(*this)].points;
                l = builtSize;
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
    double System::Weight(const Constraint &c) const {
        const std::vector<size_t> idx = IndicesOf(c);
        const Eval e{*this, sharedCurves, values, idx, builtSize};
        const Dual f = Evaluated<Dual>(c.equation, [&](size_t which) { return e.Var(which); });
        double most = 0.0;
        for (size_t k = 0; k < idx.size(); ++k)
            most = std::max(most, std::abs(f.g[k] * scale[idx[k]]));
        return std::isfinite(most) && most > 0.0 ? 1.0 / most : 1.0;
    }

    // the change of row c's residuals per unit of its value: lever x pi/180 for an angle dimension (per degree),
    // the value equation of an angle the size x pi/180, else 1 (a length, or no value)
    double System::RowScale(size_t c) const {
        const Constraint &k = At(c);
        constexpr double degree = std::numbers::pi / 180.0;
        if (k.kind == Kind::Value)
            return isAngle[k.valueAt] ? builtSize * degree : 1.0;
        if (k.kind != Kind::Angle || k.valueAt != sketch::none)
            return 1.0;
        return (k.refs.size() == 1 && k.refs[0].IsPattern(*this) ? builtSize : levers[c]) * degree;
    }

    // predict: with the linear prediction of the move (SolveResult::predicted). The least change keeps the drawn
    // shapes (ShapedSystem): what a constraint moves takes the elements it belongs to along
    SolveResult System::Solve(bool predict, const std::vector<double> *origin) {
        Build();
        const ShapedSystem<System> shaped(*this, origin ? *origin : values);
        Unknowns x = shaped.Extended(SolverUnknowns());
        const std::vector<double> from = shaped.Origin();
        const SolveResult r = SolveConstraints(shaped, x, builtSize, predict, origin ? &from : nullptr);
        x.value.resize(values.size());
        values = std::move(x.value);
        return r;
    }

    SolveResult System::SolveDragged(const std::vector<size_t> &dragged, bool held) {
        // a change of a dragged unknown counts as much as 10 times that change of another, the two scales of ShapedSystem
        // (wider ones crawl in the damping)
        constexpr double pulled = 0.1;
        Build();
        Unknowns x = SolverUnknowns();
        for (size_t u : dragged) {
            if (held)
                x.fixed[u] = 1;
            else
                x.scale[u] *= pulled;
        }
        const SolveResult r = SolveConstraints(*this, x, builtSize);
        values = std::move(x.value);
        return r;
    }

    // built: nothing changed since Build (a solve that took no step)
    ConstraintAnalysis System::Analyze(bool complete, bool built) {
        if (!built)
            Build();
        return AnalyzeConstraints(*this, SolverUnknowns(), builtSize, complete, &analysisCache);
    }

    std::pair<Unknowns, Eigen::MatrixXd> System::Motions() {
        Build();
        Unknowns x = SolverUnknowns();
        Eigen::MatrixXd N = MotionBasis(*this, x, builtSize);
        return {std::move(x), std::move(N)};
    }

    Unknowns System::SolverUnknowns() const {
        Unknowns x(values);
        x.scale = scale;
        x.fixed = fixed;
        return x;
    }

    // the blocks of a pointwise constraint: two rows per held point (one of a FixH or FixV), one per held value, in
    // the order of HeldRows
    std::vector<RowBlock> System::BlocksOf(const Constraint &c) const {
        std::vector<RowBlock> out;
        if ((c.reference && c.valueAt == sketch::none) || c.suspended)
            return out;
        const bool rigid = c.kind == Kind::RigidGroup;
        const auto block = [&](size_t rows, std::vector<size_t> u) {
            std::sort(u.begin(), u.end());
            u.erase(std::unique(u.begin(), u.end()), u.end());
            std::erase_if(u, [&](size_t k) { return held[k]; });
            out.push_back({rows, std::move(u)});
        };
        for (size_t p : c.heldPoints)
            if (rigid)
                block(2, {coordsOf[p], coordsOf[p] + 1, c.params[0], c.params[1], c.params[2]});
            else if (c.kind == Kind::Fix)
                block(2, {coordsOf[p], coordsOf[p] + 1});
            else
                block(1, {coordsOf[p] + HeldCoordinate(c.kind)});
        for (size_t u : c.held)
            block(1, rigid && isAngle[u] ? std::vector<size_t>{u, c.params[2]} : std::vector<size_t>{u});
        return out;
    }

    std::vector<size_t> System::IndicesOf(const Constraint &c) const {
        std::vector<size_t> out = UnknownsReadBy(c);
        if (out.size() > maxLocal)
            throw std::invalid_argument("a constraint on more than about 60 points (a spline's)");
        return out;
    }

    std::vector<size_t> System::UnknownsReadBy(const Constraint &c) const {
        const bool angles = c.kind == Kind::Copy && !c.held.empty();
        std::vector<size_t> out = angles ? c.held : std::vector<size_t>{};
        if (IsMerge(c))
            return out;
        for (size_t i = angles ? 2 : 0; i < c.refs.size(); ++i) {
            const Ref &r = c.refs[i];
            std::vector<size_t> u = elements[r.element].Values(*this);
            if (c.kind != Kind::CurvatureRadius && (r.part == Part::Start || r.part == Part::End || r.part == Part::Center || r.part == Part::Point)) {
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
        return out;
    }

}
