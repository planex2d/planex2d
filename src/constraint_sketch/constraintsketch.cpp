// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    ConstraintSketch &ConstraintSketch::Remove(const std::string &id, const std::string &call) {
        sketch::Undoable(*this, [&] {
            const std::map<size_t, std::string> held = ValueHolders();
            RemoveOne(id);
            Prune(call, held);
        });
        return *this;
    }

    // refs: ids, "id.start" / ".end" / ".center" / ".mid"
    ConstraintSketch &ConstraintSketch::Constrain(const std::vector<std::string> &refs, sketch::Kind kind, std::optional<double> value,
                                                  const std::optional<std::string> &id, bool reference, bool ownTangents) {
        if (value && !(kind == sketch::Kind::CurvatureRadius && std::isinf(*value)))
            IsFinite(sketch::KindName(kind), {*value});
        cs::Constraint c{kind};
        for (const std::string &r : refs)
            c.refs.push_back(RefOf(r));
        c.reference = reference;
        c.ownTangents = ownTangents;
        if (reference && (!sketch::HasValue(kind) || value))
            throw std::invalid_argument(Text(c) + (value ? ": a reference measures, it takes no value" : ": only dimensions can be references"));
        if (!reference && sketch::HasValue(kind) != value.has_value())
            throw std::invalid_argument(Text(c) + (value ? ": takes no value" : ": needs a value"));
        c.value = value.value_or(0.0);
        if (sketch::IsFix(kind) || kind == sketch::Kind::RigidGroup)
            for (size_t k = 0; k < refs.size(); ++k)
                if (c.refs[k].part == sketch::Part::Whole && s.elements[c.refs[k].element].kind == sketch::ElementKind::OffsetCurve)
                    throw std::invalid_argument(Text(c) + ": '" + refs[k] + "' is an offset curve, it follows its source; " +
                                                (sketch::IsFix(kind) ? "fix" : "group") + " its source and the offset's distance instead");
        Atomic([&] {
            RetrySuspended();
            DropImpliedLike(c);
            AddConstraint(c, id, std::nullopt, true, true);
        });
        return *this;
    }

    void ConstraintSketch::DropImpliedLike(const cs::Constraint &c) {
        if (c.reference)
            return;
        const std::string key = s.PlaceKeyOf(c.kind, c.refs);
        for (size_t j = 0; j < constraintIds.size(); ++j) {
            const std::string id = constraintIds[j];
            if (impliedIds.contains(id) && s.PlaceKeyOf(s.constraints[j].kind, s.constraints[j].refs) == key) {
                RemoveImplied(id);
                return;
            }
        }
    }

    // a constraint a drawing call implies (its helpers, auto_constraint): left out quietly when it follows from the
    // others already (it takes no degree of freedom; the drawing is what the user drew); the implied ones it makes
    // redundant are removed
    ConstraintSketch &ConstraintSketch::ConstrainImplied(const std::vector<std::string> &refs, sketch::Kind kind) {
        cs::Constraint c{kind};
        for (const std::string &r : refs)
            c.refs.push_back(RefOf(r));
        return ConstrainImplied(std::move(c));
    }

    ConstraintSketch &ConstraintSketch::ConstrainImplied(const cs::Constraint &c) {
        const size_t dof = Analysis(false).dof;
        AtomicIf([&] {
            const cs::System::Mark before = s.Marked();
            if (AddConstraint(c, std::nullopt, dof) || (analysis->dof == dof && MakesOffsetTieRedundant(before)))
                return false;
            const std::string added = constraintIds.back();
            DropRedundantImplied();
            impliedIds.insert(added);
            return true;
        });
        return *this;
    }

    void ConstraintSketch::WarnLandedOnEnd(const std::string &call, const std::string &id, const std::string &t, const std::string &end, const char *role) {
        if (!book.IsElement(t))
            return;
        // the placement finds a meeting at a tangent point to about sqrt(eps) only
        const double close = 1e-6 * std::max(1.0, Size());
        const Vertex p = Location(id + end);
        for (const char *part : {".start", ".end"})
            if (HasLocation(t + part) && Norm(Location(t + part) - p) <= close)
                Warning() << call << "(\"" << t << "\"): its " << end.substr(1) << " lands on " << t << part << " but only lies on "
                          << t << "; \"" << t << part << "\" as " << role << " makes them one point";
    }

    ConstraintSketch &ConstraintSketch::ConstrainHelped(const std::string &call, const std::string &id, const sketch::HelperTargets &t) {
        const auto add = [&](const std::vector<std::string> &refs, sketch::Kind k) { ConstrainImplied(refs, k); };
        const bool ends = call != "Circle" && ElementOf(id).element.kind != sketch::ElementKind::Circle;
        if (t.from && ends) {
            add({id + ".start", *t.from}, sketch::Kind::Coincident);
            WarnLandedOnEnd(call, id, *t.from, ".start", "from");
        }
        if (t.to && ends) {
            add({id + ".end", *t.to}, sketch::Kind::Coincident);
            WarnLandedOnEnd(call, id, *t.to, ".end", "to");
        }
        if (t.until) {
            const std::vector<std::string> on = Through(Location(id + ".end"), id);
            if (!on.empty())
                add({id + ".end", on.front()}, sketch::Kind::Coincident);
        }
        if (t.parallel)
            add({*t.parallel, id}, sketch::Kind::Parallel);
        if (t.perpendicular)
            add({*t.perpendicular, id}, sketch::Kind::Perpendicular);
        if (t.coincident)
            add({id, *t.coincident}, sketch::Kind::Coincident);
        if (t.through)
            add({*t.through, id}, sketch::Kind::Coincident);
        if (t.center)
            add({id + ".center", *t.center}, sketch::Kind::Coincident);
        if (t.concentric)
            add({id, *t.concentric}, sketch::Kind::Concentric);
        for (const std::string &touched : t.tangent) {
            add({id, touched}, sketch::Kind::Tangent);
            if (ends)
                for (const char *end : {".start", ".end"}) {
                    const std::vector<std::string> on = Through(Location(id + end), id);
                    if (std::find(on.begin(), on.end(), touched) != on.end())
                        add({id + end, touched}, sketch::Kind::Coincident);
                }
        }
        return *this;
    }

    void ConstraintSketch::TieSymmetryLine(std::string a, std::string b, const std::string &axis) {
        const std::string kind = RefKind(a);
        if (kind == "point")
            Constrain({a, b, axis}, sketch::Kind::Symmetric, std::nullopt, std::nullopt);
        else if (kind == "round") {
            Constrain({a + ".center", b + ".center", axis}, sketch::Kind::Symmetric, std::nullopt, std::nullopt);
            Constrain({a, b}, sketch::Kind::Equal, std::nullopt, std::nullopt);
            TieLastConstraintTo(axis);
        } else {
            if (kind == "axis")
                std::swap(a, b);
            Constrain({a, b, axis}, sketch::Kind::SymmetricCarriers, std::nullopt, std::nullopt);
        }
    }

    bool ConstraintSketch::MakesOffsetTieRedundant(const cs::System::Mark &before) const {
        for (size_t j : s.MadeRedundant(s.Redundancies(*analysis), before))
            for (const auto &entry : offsetInfos)
                if (entry.second.Owns(constraintIds[j]))
                    return true;
        return false;
    }

    ConstraintSketch &ConstraintSketch::RigidGroup(const std::vector<std::string> &ids, const std::optional<std::string> &id) {
        if (ids.empty())
            throw std::invalid_argument("RigidGroup: no elements");
        return Constrain(Expanded(ids), sketch::Kind::RigidGroup, std::nullopt, id);
    }

    void ConstraintSketch::Relate(sketch::Kind kind, std::vector<sketch::Ref> refs, std::vector<size_t> params) {
        cs::Constraint c{kind, std::move(refs)};
        c.params = std::move(params);
        AddConstraint(std::move(c), std::nullopt);
    }

    void ConstraintSketch::RelateGiven(sketch::Kind kind, std::vector<sketch::Ref> refs) {
        AddConstraint({kind, std::move(refs)}, std::nullopt, std::nullopt, true);
    }

    // id empty: a new one of the kind's
    void ConstraintSketch::Dimension(sketch::Kind kind, std::vector<sketch::Ref> refs, double value, const std::string &id, bool inOffset) {
        if (inOffset)
            AddUnsolved({kind, std::move(refs), value}, id);
        else
            AddConstraint({kind, std::move(refs), value}, id.empty() ? std::nullopt : std::optional<std::string>(id));
    }

    void ConstraintSketch::Reindex() {
        BasicSketch::Reindex();
        for (size_t c = 0; c < constraintIds.size(); ++c)
            book.byId[constraintIds[c]] = {false, c};
    }

    void ConstraintSketch::RemoveOne(const std::string &id) {
        if (const auto whole = offsetInfos.find(id); whole != offsetInfos.end()) {
            const std::vector<std::string> parts = whole->second.elements;
            for (const std::string &part : parts)
                CheckNoOffsetFrom(id, part);
            offsetInfos.erase(whole);
            for (auto e = parts.rbegin(); e != parts.rend(); ++e)
                if (book.byId.contains(*e))
                    RemoveOne(*e);
            if (book.IsSlot(id))
                RemoveOne(id + ".spine");
        }
        if (const auto w = book.wires.find(id); w != book.wires.end())
            for (const std::string &e : w->second) {
                CheckHoldsNoOffset(e);
                CheckNoOffsetFrom(id, e);
            }
        else {
            CheckHoldsNoOffset(id);
            CheckNoOffsetFrom(id, id);
            pending.EraseValue(id);
            const auto it = book.byId.find(id);
            if (it == book.byId.end()) {
                if (const std::optional<std::string> hint = CopyHint(id))
                    throw std::invalid_argument("Remove('" + id + "'): " + *hint);
                throw std::invalid_argument("Remove('" + id + "'): no such element or constraint");
            }
            if (!it->second.isElement) {
                DropConstraint(it->second.index);
                Reindex();
                analysis.reset();
                RetrySuspended();
                return;
            }
        }
        // a curvature fillet's hidden arc goes after it
        std::optional<std::string> helper;
        if (const std::optional<size_t> it = book.ElementAt(id); it && s.elements[*it].IsBlend(s))
            if (const size_t h = s.splines[s.elements[*it].SplineIndex()].blend->helper; h != sketch::none)
                helper = book.elementIds[h];
        BasicSketch::RemoveOne(id);
        if (helper)
            RemoveOne(*helper);
        if (const std::optional<std::string> offset = OffsetWithoutPieces(id))
            RemoveOne(*offset);
        analysis.reset();
        RetrySuspended();
    }

    void ConstraintSketch::ElementGoes(size_t k, const std::string &id) {
        ++removedInCall;
        patterns.erase(id);
        chamfers.erase(id);
        for (auto &[name, info] : offsetInfos)
            std::erase_if(info.curves, [&](const auto &c) { return c.first == id; });
        pending.Forget(id);
        if (const auto tied = companions.find(id); tied != companions.end()) {
            const std::vector<std::string> others = tied->second;
            companions.erase(tied);
            for (const std::string &c : others)
                if (const std::optional<size_t> at = book.ConstraintAt(c))
                    RemoveOne(c);
        }
        for (size_t c = s.constraints.size(); c-- > 0;)
            if (s.constraints[c].RefersTo(k)) {
                pending.EraseValue(constraintIds[c]);
                DropConstraint(c);
            }
    }

    void ConstraintSketch::AddTie(cs::Constraint c) {
        c.tie = true;
        AddConstraint(std::move(c), std::nullopt);
    }

    void ConstraintSketch::AddUnsolved(cs::Constraint c, const std::string &id) {
        book.CheckId(id);
        const size_t k = s.Add(std::move(c));
        constraintIds.push_back(id);
        book.byId[id] = {false, k};
    }

    const std::string &ConstraintSketch::AddUnsolved(cs::Constraint c) {
        const sketch::Kind kind = c.kind;
        const size_t k = s.Add(std::move(c));
        constraintIds.push_back(book.NewId(sketch::KindName(kind), std::nullopt));
        book.byId[constraintIds.back()] = {false, k};
        return constraintIds.back();
    }

    void ConstraintSketch::RemoveImplied(const std::string &id) {
        Remove(id);
        impliedIds.erase(id);
    }

    std::string ConstraintSketch::Text(const cs::Constraint &c) const {
        if (c.kind == sketch::Kind::Expression)
            return c.text;
        std::string t;
        for (const sketch::Ref &r : c.refs)
            t += RefText(r) + ", ";
        t += "px." + std::string(sketch::KindName(c.kind));
        const bool driven = c.valueAt != sketch::none && std::any_of(s.constraints.begin(), s.constraints.end(), [&](const cs::Constraint &e) {
            return e.kind == sketch::Kind::Expression && e.drives == c.valueAt;
        });
        if (c.reference)
            t += ", reference=True";
        else if (sketch::HasValue(c.kind))
            t += ", " + NumberText(driven ? s.Linked(c) : c.value);
        return t;
    }

    void ConstraintSketch::ReportStillRedundant(const std::vector<std::string> &ids) {
        if (ids.empty())
            return;
        const ConstraintAnalysis &a = Analysis(false);
        const std::vector<char> now = s.Redundancies(a);
        for (const std::string &id : ids)
            if (const std::optional<size_t> at = book.ConstraintAt(id); at && now[*at])
                Warning() << RedundancyText(*at, ListedIds(s.Dependencies(a)[*at]));
    }

    void ConstraintSketch::DropRedundantImplied() {
        for (;;) {
            const std::vector<char> redundant = s.Redundancies(Analysis(false));
            std::optional<std::string> drop;
            for (size_t c = constraintIds.size(); c-- > 0 && !drop;)
                if (redundant[c] && impliedIds.contains(constraintIds[c]))
                    drop = constraintIds[c];
            if (!drop)
                return;
            RemoveImplied(*drop);
        }
    }

    void ConstraintSketch::DropConstraint(size_t k) {
        if (s.constraints[k].kind == sketch::Kind::Parameter)
            throw std::invalid_argument(constraintIds[k] + ": a global parameter (px.Parameters) the sketch uses stays with it");
        if (const std::optional<std::string> curve = CurveUsing(k))
            throw std::invalid_argument(constraintIds[k] + ": " + *curve + " uses it in its formula; remove " + *curve + " first");
        const cs::Constraint &c = s.constraints[k];
        if (c.kind == sketch::Kind::Expression && c.drives != sketch::none)
            for (cs::Constraint &d : s.constraints)
                if (d.valueAt == c.drives)
                    d.value = s.Linked(d);
        s.RemoveConstraint(k);
        conflicts.erase(constraintIds[k]);
        constraintIds.erase(constraintIds.begin() + (std::ptrdiff_t)k);
    }

    std::vector<std::string> ConstraintSketch::ConflictSet(size_t k, const std::vector<double> &start) const {
        std::vector<std::string> out;
        for (size_t j = 0; j < s.constraints.size(); ++j) {
            if (j == k || !s.constraints[j].IsListed() || s.constraints[j].reference)
                continue;
            cs::System rest = s;
            rest.values = start;
            rest.RemoveConstraint(j);
            if (rest.Solve().solved)
                out.push_back(constraintIds[j]);
        }
        return out;
    }

    std::string ConstraintSketch::ContradictionText(const std::vector<std::pair<size_t, double>> &change) const {
        std::vector<std::string> changed, others;
        for (size_t j = 0; j < s.constraints.size(); ++j) {
            if (!s.constraints[j].IsListed() || s.constraints[j].reference)
                continue;
            cs::System rest = s;
            for (const auto &[k, v] : change)
                rest.constraints[k].value = v;
            rest.RemoveConstraint(j);
            if (!rest.Solve().solved)
                continue;
            const bool isChanged = std::any_of(change.begin(), change.end(), [&](const auto &x) { return x.first == j; });
            (isChanged ? changed : others).push_back(constraintIds[j]);
        }
        if (changed.empty() || others.empty())
            return "";
        return "; " + Joined(changed) + (changed.size() > 1 ? " contradict " : " contradicts ") + Joined(others);
    }

    std::vector<std::string> ConstraintSketch::IdsOf(const std::vector<size_t> &constraints) const {
        std::vector<std::string> out;
        for (size_t c : constraints)
            out.push_back(constraintIds[c]);
        return out;
    }

    void ConstraintSketch::Suspend(size_t k, const std::vector<double> &start, const std::string &what) {
        std::vector<std::string> with = ConflictSet(k, start);
        s.constraints[k].suspended = true;
        s.values = start;
        analysis.reset();
        Warning() << what << (with.empty() ? "" : " (" + Joined(with) + ")") << "; " << constraintIds[k]
                  << " is kept but not solved, the sketch is over-constrained";
        conflicts[constraintIds[k]] = std::move(with);
    }

    std::vector<std::string> ConstraintSketch::ListedIds(const std::vector<size_t> &constraints) const {
        std::vector<std::string> out;
        for (size_t c : constraints)
            if (!s.constraints[c].IsCopyTie())
                out.push_back(constraintIds[c]);
        return out;
    }

    std::string ConstraintSketch::RedundancyText(size_t k, const std::vector<std::string> &from) const {
        const std::string own = constraintIds[k] + " (" + Text(s.constraints[k]) + ")";
        return from.empty() ? own + " is redundant (consistent)" : own + " follows from " + Joined(from) + "; remove one of them";
    }

    void ConstraintSketch::TryActivate(size_t k) {
        const std::vector<double> start = s.values;
        s.constraints[k].suspended = false;
        analysis.reset();
        if (s.Solve().solved) {
            conflicts.erase(constraintIds[k]);
            analysis = s.Analyze(false);
            if (const std::vector<char> now = s.Redundancies(*analysis); now[k])
                Warning() << RedundancyText(k, ListedIds(s.Dependencies(*analysis)[k]));
            return;
        }
        s.constraints[k].suspended = true;
        s.values = start;
    }

    void ConstraintSketch::RetrySuspended() {
        for (size_t k = 0; k < s.constraints.size(); ++k)
            if (s.constraints[k].suspended)
                TryActivate(k);
    }

    std::set<size_t> ConstraintSketch::OverConstraints() const {
        std::set<size_t> out;
        for (const auto &[id, with] : conflicts) {
            out.insert(book.byId.at(id).index);
            for (const std::string &w : with)
                if (const std::optional<size_t> at = book.ConstraintAt(w))
                    out.insert(*at);
        }
        return out;
    }

    std::set<size_t> ConstraintSketch::OverElements() const {
        std::set<size_t> out;
        for (size_t c : OverConstraints())
            for (const sketch::Ref &r : s.constraints[c].refs)
                out.insert(r.element);
        return out;
    }

    void ConstraintSketch::CheckNothingShrinks(const std::string &text) const {
        for (size_t e = 0; e < s.elements.size(); ++e) {
            const sketch::Element &el = s.elements[e];
            const bool line = el.kind == sketch::ElementKind::Line, arc = el.kind == sketch::ElementKind::Arc;
            if ((line || arc) && s.coordsOf[el.Start()] == s.coordsOf[el.End()])
                throw std::invalid_argument(text + ": " + book.elementIds[e] + " would shrink to a point");
        }
    }

    void ConstraintSketch::CheckBranch(const std::vector<double> &drawn, const std::string &name, const std::string &text) const {
        if (const auto [turned, element] = s.MostTurned(drawn, s.Headings()); turned > branchTurn) {
            const std::string what = book.elementIds[element] + " turned round (" + std::to_string(std::lround(Degrees(turned))) +
                                     " degrees): the solution is not the branch drawn";
            if (errorOnBranchChange)
                throw std::invalid_argument(text + ": " + what + " (error_on_branch_change)");
            Warning() << name << " (" << text << "): " << what;
        }
    }

    bool ConstraintSketch::DropImpliedMadeRedundant(const std::vector<char> &now, const cs::System::Mark &before) {
        const std::vector<size_t> made = s.MadeRedundant(now, before);
        if (made.empty())
            return false;
        for (size_t j : made)
            if (!impliedIds.contains(constraintIds[j]) || s.IsMerge(s.constraints[j]))
                return false;
        for (size_t m = made.size(); m-- > 0;) {
            const std::string id = constraintIds[made[m]];
            RemoveImplied(id);
        }
        analysis = s.Analyze(false);
        return true;
    }

    void ConstraintSketch::ReportMadeRedundant(const std::vector<char> &now, const cs::System::Mark &before) const {
        const std::vector<size_t> made = s.MadeRedundant(now, before);
        if (made.empty())
            return;
        const std::vector<std::vector<size_t>> dependencies = s.Dependencies(*analysis);
        std::vector<std::pair<std::vector<size_t>, std::vector<std::string>>> groups;
        for (size_t m : made) {
            const std::vector<std::string> from = ListedIds(dependencies[m]);
            const auto same = std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.second == from; });
            if (same == groups.end())
                groups.push_back({{m}, from});
            else
                same->first.push_back(m);
        }
        for (const auto &[group, from] : groups)
            if (group.size() == 1)
                Warning() << RedundancyText(group[0], from);
            else
                Warning() << Joined(IdsOf(group)) << (from.empty() ? " are redundant (consistent)" : " follow from " + Joined(from))
                          << "; remove them to keep the sketch minimal";
    }

    std::optional<std::string> ConstraintSketch::AddConstraint(cs::Constraint c, const std::optional<std::string> &id, std::optional<size_t> implied,
                                                               bool reportMakes, bool suspendable) {
        const std::string text = Text(c);
        const std::vector<double> drawn = s.Headings();
        const std::string name = book.NewId(sketch::KindName(c.kind), id);
        const cs::System::Mark before = s.Marked();
        size_t k;
        try {
            k = s.Add(std::move(c));
        } catch (const std::invalid_argument &e) {
            const std::string what = e.what();
            const size_t colon = what.find(": ");
            throw std::invalid_argument(text + ": " + (colon == std::string::npos ? what : what.substr(colon + 2)));
        }
        constraintIds.push_back(name);
        book.byId[name] = {false, k};
        CheckNothingShrinks(text);
        const std::vector<double> start = s.values;
        const SolveResult solved = s.Solve();
        if (!solved.solved) {
            const std::string hint = DomainHintOfSolve(start);
            const std::string what = text + ": no solution, it contradicts the constraints there are";
            if (!suspendable || !hint.empty())
                throw std::invalid_argument(what + hint);
            Suspend(k, start, what);
            return std::nullopt;
        }
        CheckBranch(drawn, name, text);
        const double amplifiedBefore = amplified;
        analysis = s.Analyze(false, solved.iterations == 0);
        amplified = analysis->amplification;
        const std::vector<char> now = s.Redundancies(*analysis);
        if (now[k]) {
            const std::vector<std::string> from = ListedIds(s.Dependencies(*analysis)[k]);
            if (implied && analysis->dof == *implied) {
                amplified = amplifiedBefore;
                return Joined(from);
            }
            if (cutRedundant)
                cutRedundant->push_back(name);
            else
                Warning() << RedundancyText(k, from);
        } else if (reportMakes && !DropImpliedMadeRedundant(now, before))
            ReportMadeRedundant(now, before);
        if (amplified > config::state->sensitivity_warning && amplifiedBefore <= config::state->sensitivity_warning)
            Warning() << name << " (" << text << "): nearly singular, a small change of the dimensions can move the geometry "
                      << NumberText(amplified) << " times as much";
        return std::nullopt;
    }

}
