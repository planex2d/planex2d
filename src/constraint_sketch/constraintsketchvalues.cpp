// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    ConstraintSketch &ConstraintSketch::AddExpression(const std::string &text, const std::vector<cs::Node> &nodes, const std::vector<cs::ExpressionName> &names,
                                                      const std::optional<std::string> &drives, const std::optional<std::string> &id) {
        const std::string call = "Expression '" + text + "'";
        Flush();
        const std::vector<double> lengths = s.LengthValues();
        sketch::Undoable(*this, [&] {
            const std::vector<size_t> dimensions = LinkedDimensions(names);
            cs::Constraint c = ExpressionOf(text, nodes, names, dimensions, drives, call);
            if (const std::string error = s.DomainError(c); !error.empty())
                throw std::invalid_argument(call + ": " + error);
            if (c.drives != sketch::none)
                if (const std::optional<std::string> wrong = SignChangeOf(c, lengths, names, dimensions))
                    throw std::invalid_argument(call + ": " + *wrong);
            try {
                AddConstraint(std::move(c), id, std::nullopt, true, true);
            } catch (const std::invalid_argument &x) {
                const std::string what = x.what();
                throw std::invalid_argument(what.starts_with(text + ": ") ? call + what.substr(text.size()) : call + ": " + what);
            }
            if (const std::optional<std::string> wrong = OutOfDomain())
                throw std::invalid_argument(call + ": " + *wrong);
            if (const std::optional<std::string> wrong = SignChange(lengths, s.values))
                throw std::invalid_argument(call + ": " + *wrong);
        });
        return *this;
    }

    void ConstraintSketch::CurveDimensions(sketch::CurveFormula &formula) {
        Flush();
        for (const std::string &id : formula.ids) {
            const size_t k = DimensionAt(id);
            if (s.constraints[k].reference)
                throw std::invalid_argument(formula.text + ": " + id + " is a reference dimension, it measures the solved geometry; a curve's "
                                            "shape is known before each solve, from driving dimensions");
            if (const std::optional<size_t> e = s.DriverOf(k))
                throw std::invalid_argument(formula.text + ": " + id + " is driven by " + constraintIds[*e] + " (" + s.constraints[*e].text +
                                            "); a curve's shape is known before each solve, from driving dimensions");
            formula.dimensions.push_back(k);
            formula.used.push_back(s.constraints[k].value);
        }
    }

    std::vector<std::pair<std::string, std::string>> ConstraintSketch::DrivenDimensions() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (size_t k = 0; k < s.constraints.size(); ++k) {
            if (!sketch::HasValue(s.constraints[k].kind))
                continue;
            if (s.constraints[k].reference)
                out.push_back({constraintIds[k], "a reference dimension, it measures the solved geometry"});
            else if (const std::optional<size_t> e = s.DriverOf(k))
                out.push_back({constraintIds[k], "driven by " + constraintIds[*e] + " (" + s.constraints[*e].text + ")"});
        }
        return out;
    }

    std::optional<std::string> ConstraintSketch::CurveUsing(size_t k) const {
        const std::optional<size_t> e = s.CurveUsing(k);
        return e ? std::optional(book.elementIds[*e]) : std::nullopt;
    }

    ConstraintSketch &ConstraintSketch::RemoveExpression(const std::string &id) {
        const auto it = book.byId.find(id);
        if (it == book.byId.end() || it->second.isElement || s.constraints[it->second.index].kind != sketch::Kind::Expression)
            throw std::invalid_argument("RemoveExpression('" + id + "'): no such expression");
        return Remove(id, "RemoveExpression('" + id + "')");
    }

    cs::ExpressionNames ConstraintSketch::ExpressionNames() const {
        cs::ExpressionNames out;
        for (size_t c = 0; c < s.constraints.size(); ++c) {
            const sketch::Kind k = s.constraints[c].kind;
            if (sketch::HasValue(k))
                out.dimensions.push_back({constraintIds[c], k == sketch::Kind::Angle});
            else
                out.others.push_back({constraintIds[c], sketch::KindName(k)});
        }
        for (const auto &[pattern, info] : patterns) {
            out.others.push_back({pattern + ".count", "count"});
            if (info.IsGrid())
                out.others.push_back({pattern + ".count2", "count"});
        }
        return out;
    }

    double ConstraintSketch::Value(const std::string &id) {
        if (const std::optional<std::string> chamfer = ReverseOf(id)) {
            return pending.Flip(*chamfer).value_or(chamfers.at(*chamfer).reverse) ? 1.0 : 0.0;
        }
        if (const std::optional<std::string> pattern = CountOf(id)) {
            if (const std::optional<size_t> n = pending.Count(id))
                return (double)*n;
            return (double)patterns.at(*pattern).CountNow(id.ends_with("2"));
        }
        const size_t k = DimensionAt(id);
        if (s.constraints[k].reference || s.DriverOf(k)) {
            Flush();
            return s.DimensionValue(DimensionAt(id));
        }
        return pending.Value(id).value_or(s.constraints[k].value);
    }

    std::vector<std::pair<std::string, double>> ConstraintSketch::Dimensions() {
        Flush();
        std::vector<std::pair<std::string, double>> out;
        for (size_t c = 0; c < s.constraints.size(); ++c)
            if (sketch::HasValue(s.constraints[c].kind))
                out.push_back({constraintIds[c], Value(constraintIds[c])});
        for (const auto &[id, p] : patterns) {
            out.push_back({id + ".count", Value(id + ".count")});
            if (p.IsGrid())
                out.push_back({id + ".count2", Value(id + ".count2")});
        }
        for (const auto &[id, info] : chamfers)
            out.push_back({id + ".reverse", Value(id + ".reverse")});
        return out;
    }

    // takes effect with Update(), or before the sketch is drawn on or read
    std::optional<std::string> ConstraintSketch::SettingProblem(size_t k, double v, const std::string &id) {
        if (s.constraints[k].kind == sketch::Kind::Parameter)
            return id + " is a global parameter (px.Parameters); its Update sets it in every sketch that uses it";
        if (const std::optional<size_t> e = s.DriverOf(k))
            return id + " is driven by " + constraintIds[*e] + " (" + s.constraints[*e].text + "); remove the expression to set it";
        if (s.constraints[k].reference)
            return "a reference dimension measures, it is not set";
        if (const std::optional<std::string> wrong = OffsetDistanceProblem(k, v))
            return wrong;
        if (const std::optional<std::string> wrong = KeyholeProblem(k, v))
            return wrong;
        if (s.Sideless(s.constraints[k]) && !(v > 0.0))
            return "a length or radius must be positive";
        if (const char *outside = s.SweepOutside(s.constraints[k], v))
            return std::string(outside);
        return std::nullopt;
    }

    void ConstraintSketch::SetValue(const std::string &id, double v) {
        const auto it = book.byId.find(id);
        if (!(std::isinf(v) && it != book.byId.end() && !it->second.isElement && s.constraints[it->second.index].kind == sketch::Kind::CurvatureRadius))
            IsFinite(id.c_str(), {v});
        const auto refused = [&](const std::string &why) { return std::invalid_argument(id + " = " + NumberText(v) + ": " + why); };
        if (const std::optional<std::string> chamfer = ReverseOf(id)) {
            if (v != 0.0 && v != 1.0)
                throw refused("reverse is True (1) or False (0)");
            pending.SetFlip(*chamfer, v == 1.0);
            analysis.reset();
            return;
        }
        if (CountOf(id)) {
            if (!(v >= 1.0) || v != std::floor(v))
                throw refused("a count is a whole number of instances, the original included");
            pending.SetCount(id, (size_t)v);
            analysis.reset();
            return;
        }
        const size_t k = DimensionAt(id);
        if (const std::optional<std::string> wrong = SettingProblem(k, v, id))
            throw refused(*wrong);
        if (id.ends_with(".angle"))
            if (const auto p = patterns.find(id.substr(0, id.size() - 6)); p != patterns.end())
                p->second.equal = false;
        for (auto &[i, w] : pending.values)
            if (i == id) {
                w = v;
                return;
            }
        pending.values.push_back({id, v});
        analysis.reset();
    }

    void ConstraintSketch::SetValues(const std::vector<std::pair<std::string, double>> &values) {
        const cs::Pending savedPending = pending;
        const auto savedPatterns = patterns;
        try {
            for (const auto &[id, v] : values)
                SetValue(id, v);
        } catch (...) {
            pending = savedPending;
            patterns = savedPatterns;
            analysis.reset();
            throw;
        }
    }

    // all set values together, walked there in steps (at least steps of them): a step is halved when it finds no
    // solution, when its linear prediction (the undamped Gauss-Newton step) moves the geometry by more than a
    // quarter of the sketch's size, when the geometry moves more than twice as far as predicted or when a line or
    // arc turns by more than smoothTurn (it would leave its branch, the assembly it was drawn in); a smallest step
    // that still turns one round changes the branch with a warning (errorOnBranchChange: refused); with no
    // solution everything is as before
    ConstraintSketch &ConstraintSketch::Update(const std::vector<std::pair<std::string, double>> &values, size_t steps) {
        UpdateValues(values, steps);
        RetrySuspended();
        return *this;
    }

    ConstraintSketch &ConstraintSketch::Drag(const std::vector<std::string> &points, const std::vector<Vertex> &to) {
        Flush();
        std::vector<size_t> dragged;
        std::vector<double> placed = s.values;
        for (size_t k = 0; k < points.size(); ++k) {
            const sketch::Ref r = RefOf(points[k]);
            if (r.part == sketch::Part::Mid)
                throw std::invalid_argument("Drag: '" + points[k] + "' follows the ends; drag them");
            if (!r.IsRealPoint(s))
                throw std::invalid_argument("Drag: '" + points[k] + "' is no point");
            const size_t c = s.coordsOf[r.PointAt(s)];
            for (size_t i = 0; i < 2; ++i)
                if (!s.held[c + i]) {
                    placed[c + i] = i == 0 ? to[k].x : to[k].y;
                    dragged.push_back(c + i);
                }
        }
        const std::vector<double> before = s.values, lengths = s.LengthValues();
        s.values = placed;
        bool solved = s.SolveDragged(dragged, true).solved;
        if (!solved) {
            s.values = placed;
            solved = s.SolveDragged(dragged, false).solved;
        }
        if (!solved || OutOfDomain(false) || SignChange(lengths, s.values)) {
            s.values = before;
            s.RefitCurves();
            return *this;
        }
        analysis.reset();
        Sync();
        return *this;
    }

    ConstraintSketch &ConstraintSketch::UpdateValues(const std::vector<std::pair<std::string, double>> &values, size_t steps) {
        SetValues(values);
        if (!pending.flips.empty())
            Reversed();
        if (pending.counts.empty())
            return UpdateDimensions(steps);
        UndoableUpdate([&] { ApplyCounts(steps); });
        analysis.reset();
        RewriteExpressions();
        return *this;
    }

    ConstraintSketch &ConstraintSketch::UpdateDimensions(size_t steps) {
        if (pending.values.empty())
            return *this;
        std::vector<std::pair<size_t, double>> change;
        std::string text;
        for (const auto &[id, v] : pending.values) {
            change.push_back({DimensionAt(id), v});
            text += (text.empty() ? "" : ", ") + id + " = " + NumberText(v);
        }
        pending.values.clear();
        analysis.reset();
        std::vector<double> from;
        for (const auto &[k, v] : change)
            from.push_back(s.constraints[k].value);
        const std::vector<double> values0 = s.values, lengths = s.LengthValues();
        std::string domain;
        if (const std::vector<double> target = Following(change, domain); domain.empty())
            if (const std::optional<std::string> wrong = SignChange(lengths, target)) {
                throw std::invalid_argument("Update(" + text + "): " + *wrong + "; nothing changed");
            }
        const auto restore = [&] {
            s.values = values0;
            for (size_t k = 0; k < change.size(); ++k)
                s.constraints[change[k].first].value = from[k];
            s.RefitCurves();
        };
        Walk walk;
        std::optional<std::string> wrong;
        try {
            walk = WalkTo(change, from, steps);
            if (walk.reached == 1.0) {
                wrong = OutOfDomain();
                if (!wrong)
                    wrong = SignChange(lengths, s.values);
            }
        } catch (...) {
            restore();
            throw;
        }
        if (walk.reached == 1.0 && !wrong) {
            for (const std::string &what : walk.branchChanges)
                Warning() << "Update(" << text << "): " << what;
            Sync();
            return *this;
        }
        const std::string hint = walk.reached == 1.0 ? "" : DomainHint(change);
        restore();
        if (wrong)
            throw std::invalid_argument("Update(" + text + "): " + *wrong + "; nothing changed");
        if (walk.refused)
            throw std::invalid_argument("Update(" + text + "): " + *walk.refused + "; nothing changed (error_on_branch_change)");
        throw std::invalid_argument("Update(" + text + "): no solution, nothing changed" + ContradictionText(change) + hint);
    }

    // a dimension's value the fraction t of the way from from to to; a curvature radius by its curvature, which
    // passes straight (an infinite radius) where the radius changes its sign
    double ConstraintSketch::Between(sketch::Kind kind, double from, double to, double t) {
        if (kind != sketch::Kind::CurvatureRadius)
            return from + (to - from) * t;
        return 1.0 / (1.0 / from + (1.0 / to - 1.0 / from) * t);
    }

    ConstraintSketch::Walk ConstraintSketch::WalkTo(const std::vector<std::pair<size_t, double>> &change, const std::vector<double> &from, size_t steps) {
        Walk walk;
        // the smallest step tried: there a solution is taken as it comes, a branch change warned about (or refused)
        constexpr double finest = 1.0 / 4096.0;
        double &t = walk.reached;
        double h = 1.0 / (double)(std::max<size_t>(steps, 1));
        const std::vector<double> origin = s.values;
        while (t < 1.0) {
            const double next = std::min(1.0, t + h);
            for (size_t k = 0; k < change.size(); ++k) {
                cs::Constraint &c = s.constraints[change[k].first];
                c.value = next == 1.0 ? change[k].second : Between(c.kind, from[k], change[k].second, next);
            }
            const std::vector<double> before = s.values, headed = s.Headings();
            std::vector<double> target = origin;
            FollowCopies(target);
            const std::vector<double> placed = s.values;
            const double size = s.Size();
            const SolveResult r = s.Solve(true, &target);
            double moved = 0.0;
            for (size_t u = 0; u < placed.size(); ++u)
                moved = std::max(moved, std::abs(s.values[u] - placed[u]) / s.scale[u]);
            const auto [turned, element] = s.MostTurned(headed, s.Headings());
            const bool steady = r.predicted <= 0.25 * size && moved <= 2.0 * r.predicted + Tol(size) && turned <= smoothTurn;
            if (r.solved && (steady || h <= finest)) {
                if (turned > branchTurn) {
                    const std::string what = book.elementIds[element] + " turned round (" + std::to_string(std::lround(Degrees(turned))) +
                                             " degrees in one smallest step): the sketch leaves the branch it was drawn in";
                    if (errorOnBranchChange) {
                        walk.refused = what;
                        break;
                    }
                    walk.branchChanges.push_back(what);
                }
                t = next;
                h = std::min(2.0 * h, 1.0);
                continue;
            }
            s.values = before;
            h /= 2.0;
            if (h < finest)
                break;
        }
        return walk;
    }

    void ConstraintSketch::Flush() {
        if (!pending.IsEmpty())
            Update();
    }

    ConstraintSketch &ConstraintSketch::UseParameters(const std::vector<std::pair<std::string, double>> &values) {
        Flush();
        for (const auto &[name, v] : values) {
            IsFinite(name.c_str(), {v});
            if (const auto it = book.byId.find(name); it != book.byId.end() && (it->second.isElement || s.constraints[it->second.index].kind != sketch::Kind::Parameter))
                throw std::invalid_argument("Use: the sketch has " + std::string(it->second.isElement ? "an element" : "a constraint") + " '" + name +
                                            "'; a parameter's name must be free in the sketches that use it");
        }
        Atomic([&] {
            for (const auto &[name, v] : values)
                if (!book.byId.contains(name)) {
                    cs::Constraint c{sketch::Kind::Parameter};
                    c.value = v;
                    AddConstraint(c, name);
                }
        });
        return UpdateParameters(values);
    }

    ConstraintSketch &ConstraintSketch::UpdateParameters(const std::vector<std::pair<std::string, double>> &values) {
        Flush();
        for (const auto &[name, v] : values) {
            IsFinite(name.c_str(), {v});
            const auto it = book.byId.find(name);
            if (it == book.byId.end() || it->second.isElement || s.constraints[it->second.index].kind != sketch::Kind::Parameter)
                throw std::invalid_argument("'" + name + "' is no global parameter of this sketch (sk.Use)");
            if (s.constraints[it->second.index].value != v)
                pending.values.push_back({name, v});
        }
        UpdateDimensions(1);
        RetrySuspended();
        return *this;
    }

    std::vector<size_t> ConstraintSketch::LinkedDimensions(const std::vector<cs::ExpressionName> &names) {
        std::vector<size_t> dimensions;
        for (const cs::ExpressionName &n : names)
            dimensions.push_back(n.measure.empty() ? DimensionAt(n.id) : MeasureReference(n.id, n.measure));
        for (size_t k : dimensions)
            if (s.constraints[k].valueAt == sketch::none)
                s.Link(k, s.DimensionValue(k));
        analysis.reset();
        return dimensions;
    }

    cs::Constraint ConstraintSketch::ExpressionOf(const std::string &text, const std::vector<cs::Node> &nodes, const std::vector<cs::ExpressionName> &names,
                                                  const std::vector<size_t> &dimensions, const std::optional<std::string> &drives, const std::string &call) const {
        cs::Constraint c{sketch::Kind::Expression};
        c.text = text;
        c.equation = nodes;
        for (cs::Node &node : c.equation)
            if (node.op == cs::Op::Value) {
                node.unknown = s.constraints[dimensions[node.unknown]].valueAt;
                node.number = s.isAngle[node.unknown] ? Degrees(1.0) : 1.0;
            }
        if (drives && !s.constraints[DimensionAt(*drives)].reference) {
            const size_t k = DimensionAt(*drives);
            if (const std::optional<size_t> e = s.DriverOf(k))
                throw std::invalid_argument(call + ": " + *drives + " is driven by " + constraintIds[*e] + " (" + s.constraints[*e].text +
                                            ") already");
            if (const std::optional<std::string> curve = CurveUsing(k))
                throw std::invalid_argument(call + ": " + *curve + " uses " + *drives + " in its formula, which takes driving dimensions only; "
                                            "it cannot be driven");
            if (s.constraints[k].kind == sketch::Kind::Parameter)
                throw std::invalid_argument(call + ": " + *drives + " is a global parameter (px.Parameters), its Update sets it; it cannot be driven");
            c.drives = s.constraints[k].valueAt;
        }
        for (size_t n = 0; n < names.size(); ++n)
            if (!names[n].measure.empty()) {
                const cs::Constraint &d = s.constraints[dimensions[n]];
                c.written.push_back({names[n].id + "." + names[n].measure, names[n].measure, d.valueAt, d.MeasuredRef().element});
            }
        return c;
    }

    size_t ConstraintSketch::DimensionAt(const std::string &id) const {
        const auto it = book.byId.find(id);
        if (it == book.byId.end() || it->second.isElement) {
            if (const std::optional<std::string> hint = CopyHint(id))
                throw std::invalid_argument("no dimension '" + id + "': " + *hint);
            throw std::invalid_argument("no dimension '" + id + "'");
        }
        const cs::Constraint &c = s.constraints[it->second.index];
        if (!sketch::HasValue(c.kind))
            throw std::invalid_argument("'" + id + "' (px." + sketch::KindName(c.kind) + ") has no value");
        return it->second.index;
    }

    size_t ConstraintSketch::MeasureReference(const std::string &ref, const std::string &measure) {
        const sketch::Ref r = RefOf(ref);
        const sketch::Ref h{0}, v{1};
        cs::Constraint c{measure == "r" ? sketch::Kind::Radius : measure == "angle" || measure == "sweep" ? sketch::Kind::Angle : sketch::Kind::Distance};
        c.refs = measure == "angle" ? std::vector<sketch::Ref>{h, r} : measure == "h" ? std::vector<sketch::Ref>{r, v}
                 : measure == "v"   ? std::vector<sketch::Ref>{r, h} : std::vector<sketch::Ref>{r};
        c.reference = true;
        const double sign = measure == "h" ? -1.0 : 1.0;
        const auto same = [](const sketch::Ref &a, const sketch::Ref &b) { return a.element == b.element && a.part == b.part && a.index == b.index; };
        for (size_t k = 0; k < s.constraints.size(); ++k) {
            const cs::Constraint &o = s.constraints[k];
            if (o.automatic && o.kind == c.kind && o.sign == sign && o.refs.size() == c.refs.size() &&
                std::equal(o.refs.begin(), o.refs.end(), c.refs.begin(), same))
                return k;
        }
        AddConstraint(c, book.NewId("Reference", std::nullopt));
        s.constraints.back().automatic = true;
        s.constraints.back().sign = sign;
        return s.constraints.size() - 1;
    }

    std::map<size_t, std::string> ConstraintSketch::ValueHolders() const {
        std::map<size_t, std::string> out;
        for (size_t c = 0; c < s.constraints.size(); ++c)
            if (s.constraints[c].valueAt != sketch::none)
                out[s.constraints[c].valueAt] = constraintIds[c];
        return out;
    }

    void ConstraintSketch::Prune(const std::string &call, const std::map<size_t, std::string> &held) {
        if (held.empty())
            return;
        const auto holds = [&](size_t u) {
            return std::any_of(s.constraints.begin(), s.constraints.end(), [&](const cs::Constraint &d) { return d.valueAt == u; });
        };
        for (size_t k = s.constraints.size(); k-- > 0;) {
            const cs::Constraint &c = s.constraints[k];
            const auto gone = std::find_if(c.equation.begin(), c.equation.end(), [&](const cs::Node &n) {
                return n.op == cs::Op::Value && !holds(n.unknown);
            });
            if (gone == c.equation.end())
                continue;
            const auto name = held.find(gone->unknown);
            Warning() << call << ": removed " << constraintIds[k] << " (" << c.text << "), it used "
                      << (name != held.end() ? name->second : std::string("a dimension that is gone"));
            DropConstraint(k);
        }
        std::vector<char> used(s.values.size(), 0);
        for (const cs::Constraint &c : s.constraints)
            for (const cs::Node &n : c.equation)
                if (n.op == cs::Op::Value)
                    used[n.unknown] = 1;
        for (size_t k = s.constraints.size(); k-- > 0;) {
            const cs::Constraint &c = s.constraints[k];
            if (c.valueAt == sketch::none || used[c.valueAt])
                continue;
            if (c.automatic)
                DropConstraint(k);
            else
                s.Unlink(k);
        }
        Reindex();
        analysis.reset();
    }

    std::string ConstraintSketch::Renamed(const std::string &text, const std::string &from, const std::string &to) {
        const auto inName = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '.'; };
        std::string out;
        size_t done = 0;
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, done)) {
            const size_t end = at + from.size();
            const bool whole = (at == 0 || !inName(text[at - 1])) && (end == text.size() || !inName(text[end]));
            out += whole ? text.substr(done, at - done) + to : text.substr(done, end - done);
            done = end;
        }
        return out + text.substr(done);
    }

    void ConstraintSketch::RewriteExpressions() {
        for (size_t k = 0; k < s.constraints.size(); ++k)
            for (cs::Written &w : s.constraints[k].written) {
                const auto d = std::find_if(s.constraints.begin(), s.constraints.end(),
                                            [&](const cs::Constraint &o) { return o.automatic && o.valueAt == w.unknown; });
                if (d == s.constraints.end())
                    continue;
                const sketch::Ref r = d->MeasuredRef();
                const std::string now = RefName(r) + "." + w.measure;
                if (now != w.token) {
                    if (r.element != w.element)
                        Warning() << constraintIds[k] << ": " << w.token.substr(0, w.token.size() - w.measure.size() - 1) << " is now "
                                  << RefName(r) << ", the text is rewritten";
                    s.constraints[k].text = Renamed(s.constraints[k].text, w.token, now);
                    w.token = now;
                }
                w.element = r.element;
            }
    }

    std::optional<std::string> ConstraintSketch::SignChangeOf(const cs::Constraint &c, const std::vector<double> &before, const std::vector<cs::ExpressionName> &names,
                                                              const std::vector<size_t> &dimensions) const {
        const size_t k = (size_t)(std::find_if(s.constraints.begin(), s.constraints.end(), [&](const cs::Constraint &d) { return d.valueAt == c.drives; }) -
                                  s.constraints.begin());
        if (k >= before.size() || before[k] == 0.0)
            return std::nullopt;
        const double f = cs::Evaluated<double>(c.equation, [&](size_t u) { return s.values[u]; });
        const double target = s.values[c.drives] - f / c.equation.front().number;
        if (before[k] * target > 0.0)
            return std::nullopt;
        std::string uses;
        for (size_t n = 0; n < names.size(); ++n)
            if (dimensions[n] != k)
                uses += (uses.empty() ? "" : ", ") + (names[n].measure.empty() ? names[n].id : names[n].id + "." + names[n].measure) + " = " +
                        NumberText(s.DimensionValue(dimensions[n]));
        return constraintIds[k] + " = " + NumberText(target) + ", a length must not change sign" + (uses.empty() ? "" : " (" + uses + ")");
    }

    std::optional<std::string> ConstraintSketch::SignChange(const std::vector<double> &before, const std::vector<double> &v, const std::string &uses) const {
        for (size_t k = 0; k < before.size(); ++k)
            if (before[k] != 0.0 && s.DriverOf(k))
                if (const double now = v[s.constraints[k].valueAt]; before[k] * now <= 0.0)
                    return constraintIds[k] + " = " + NumberText(now) + ", a length must not change sign" + (uses.empty() ? "" : " (" + uses + ")");
        return std::nullopt;
    }

    std::optional<std::string> ConstraintSketch::OutOfDomain(bool shape) const {
        for (size_t k = 0; k < s.constraints.size(); ++k)
            if (s.constraints[k].kind == sketch::Kind::Expression)
                if (const std::string error = s.DomainError(s.constraints[k]); !error.empty())
                    return constraintIds[k] + " (" + s.constraints[k].text + "): " + error;
        return OffsetProblem(shape);
    }

    std::vector<double> ConstraintSketch::Following(const std::vector<std::pair<size_t, double>> &change, std::string &error) const {
        std::vector<double> v = s.values;
        for (const auto &[k, value] : change)
            if (const size_t u = s.constraints[k].valueAt; u != sketch::none)
                v[u] = s.isAngle[u] ? Radians(value) : value;
        const auto at = [&](size_t which) { return v[which]; };
        const size_t rounds = Expressions().size();
        // a chain of n driven dimensions follows in n rounds
        for (size_t pass = 0; pass <= rounds; ++pass)
            for (size_t k = 0; k < s.constraints.size(); ++k) {
                const cs::Constraint &c = s.constraints[k];
                if (c.kind != sketch::Kind::Expression)
                    continue;
                std::string what;
                const double f = cs::Evaluated<double>(c.equation, at, &what);
                if (!what.empty()) {
                    error = "; " + constraintIds[k] + " (" + c.text + "): " + what + " there";
                    return v;
                }
                if (c.drives != sketch::none)
                    v[c.drives] -= f / c.equation.front().number;
            }
        return v;
    }

    std::string ConstraintSketch::DomainHint(const std::vector<std::pair<size_t, double>> &change) const {
        std::string error;
        Following(change, error);
        return error;
    }

    std::string ConstraintSketch::DomainHintWithoutExpressions(const std::vector<double> &start) const {
        cs::System rest = s;
        rest.values = start;
        for (size_t k = rest.constraints.size(); k-- > 0;)
            if (rest.constraints[k].kind == sketch::Kind::Expression)
                rest.RemoveConstraint(k);
        if (!rest.Solve().solved)
            return "";
        cs::System there = s;
        there.values = rest.values;
        for (const cs::Constraint &c : rest.constraints)
            if (c.reference && c.valueAt != sketch::none && !rest.isAngle[c.valueAt])
                there.values[c.valueAt] = rest.Measured(c);
        for (size_t k = 0; k < there.constraints.size(); ++k)
            if (there.constraints[k].kind == sketch::Kind::Expression)
                if (const std::string error = there.DomainError(there.constraints[k]); !error.empty())
                    return "; " + constraintIds[k] + " (" + there.constraints[k].text + "): " + error + " there";
        return "";
    }

    std::string ConstraintSketch::DomainHintOfSolve(const std::vector<double> &start) const {
        const std::string hint = DomainHint({});
        return hint.empty() ? DomainHintWithoutExpressions(start) : hint;
    }

}
