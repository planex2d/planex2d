// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    void ConstraintSketch::RequireFull(const std::string &call) {
        for (const auto &[id, with] : conflicts)
            Warning() << call << ": " << id << " (" << Text(s.constraints[book.byId.at(id).index]) << ") is suspended, it contradicts the other constraints; "
                      << "the geometry is the last solved one";
        if (!strict)
            return;
        const size_t dof = Dof();
        if (dof == 0)
            return;
        std::set<size_t> through;
        for (const sketch::Element &e : s.elements)
            if (e.kind != sketch::ElementKind::Point)
                for (size_t p : e.PointsAndCentre(s))
                    through.insert(p);
        std::vector<std::string> free;
        for (const auto &[id, state] : States()) {
            const sketch::Element &e = s.elements[book.byId.at(id).index];
            if (state == "under" && !(e.kind == sketch::ElementKind::Point && through.contains(e.Point())))
                free.push_back(id);
        }
        // a long list says no more than its start
        constexpr size_t shown = 8;
        std::string list;
        for (size_t k = 0; k < std::min(free.size(), shown); ++k)
            list += (k ? ", " : "") + free[k];
        if (free.size() > shown)
            list += ", ... (" + std::to_string(free.size()) + " in all)";
        throw std::invalid_argument(call + ": mode='strict' and the sketch is not fully constrained (" + std::to_string(dof) +
                                    (dof == 1 ? " degree" : " degrees") + " of freedom; free: " + list + ")");
    }

    std::vector<cs::ConstraintInfo> ConstraintSketch::Infos() {
        const std::vector<char> redundant = s.Redundancies(Analysis(false));
        const std::set<size_t> over = OverConstraints();
        std::vector<cs::ConstraintInfo> out;
        for (size_t c = 0; c < s.constraints.size(); ++c) {
            const cs::Constraint &k = s.constraints[c];
            if (!k.IsListed())
                continue;
            cs::ConstraintInfo i{constraintIds[c], sketch::KindName(k.kind), Text(k), {}, std::nullopt, bool(redundant[c]), k.reference};
            i.over = over.contains(c);
            for (const sketch::Ref &r : k.refs)
                i.refs.push_back(RefName(r));
            if (sketch::HasValue(k.kind))
                i.value = s.DimensionValue(c);
            out.push_back(std::move(i));
        }
        for (cs::ConstraintInfo &i : CopyEntries())
            if (i.derived && !i.tie)
                out.push_back(std::move(i));
        return out;
    }

    std::vector<cs::ConstraintEntry> ConstraintSketch::Constraints() const {
        std::vector<cs::ConstraintEntry> out;
        for (size_t c = 0; c < s.constraints.size(); ++c)
            if (s.constraints[c].IsListed())
                out.push_back({sketch::KindName(s.constraints[c].kind), constraintIds[c], Text(s.constraints[c])});
        for (const cs::ConstraintInfo &i : CopyEntries())
            out.push_back({i.kind, i.id, i.text});
        return out;
    }

    std::vector<std::pair<std::string, std::string>> ConstraintSketch::Expressions() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (size_t c = 0; c < s.constraints.size(); ++c)
            if (s.constraints[c].kind == sketch::Kind::Expression)
                out.push_back({constraintIds[c], s.constraints[c].text});
        return out;
    }

    // complete false: what AddConstraint needs (ConstraintAnalysis::complete), enough for the dof and the redundant ones
    const ConstraintAnalysis &ConstraintSketch::Analysis(bool complete) {
        Flush();
        if (!analysis || (complete && !analysis->complete)) {
            analysis = s.Analyze(complete);
            amplified = analysis->amplification;
        }
        return *analysis;
    }

    std::vector<cs::FreeMotion> ConstraintSketch::FreeMotions() {
        Flush();
        return cs::FreeMotionFinder(s, UnknownLabels(), book.elementIds).Find();
    }

    cs::SketchJacobian ConstraintSketch::Jacobian() {
        Flush();
        s.Build();
        cs::SketchJacobian out;
        const size_t unknowns = s.values.size();
        std::vector<size_t> column(unknowns, sketch::none);
        for (size_t u = 0; u < unknowns; ++u)
            if (!s.fixed[u]) {
                column[u] = out.scale.size();
                out.scale.push_back(s.isAngle[u] ? Degrees(s.scale[u]) : s.scale[u]);
            }
        const std::vector<std::pair<std::string, std::string>> label = UnknownLabels();
        for (size_t u = 0; u < unknowns; ++u)
            if (column[u] != sketch::none)
                out.columns.push_back(label[u]);
        const std::vector<std::pair<size_t, size_t>> &held = s.ValueEquations();
        out.indptr.push_back(0);
        std::vector<double> r, local;
        std::vector<std::pair<size_t, double>> entries;
        for (size_t c = 0; c < s.Count(); ++c) {
            const auto value = std::find_if(held.begin(), held.end(), [&](const std::pair<size_t, size_t> &v) { return v.first == c; });
            const std::string &id = c < s.constraints.size() ? constraintIds[c]
                                    : value != held.end()   ? constraintIds[value->second]
                                                            : book.elementIds[s.StructuralOwners()[c - s.constraints.size()]];
            size_t size = 0;
            for (const RowBlock &block : s.Blocks(c))
                size += block.rows * block.indices.size();
            r.assign(s.Rows(c), 0.0);
            local.assign(size, 0.0);
            s.Evaluate(c, s.values, r.data(), local.data());
            size_t i = 0, at = 0;
            for (const RowBlock &block : s.Blocks(c))
                for (size_t row = 0; row < block.rows; ++row, ++i) {
                    out.rows.push_back({id, value != held.end() ? std::nullopt : std::optional<size_t>(i)});
                    out.row_scale.push_back(s.RowScale(c));
                    entries.clear();
                    for (size_t u : block.indices) {
                        if (column[u] != sketch::none)
                            entries.push_back({column[u], local[at] * s.scale[u]});
                        ++at;
                    }
                    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
                    for (const auto &[k, v] : entries)
                        if (out.indices.size() > out.indptr.back() && out.indices.back() == k)
                            out.data.back() += v;
                        else {
                            out.indices.push_back(k);
                            out.data.push_back(v);
                        }
                    out.indptr.push_back(out.indices.size());
                }
        }
        return out;
    }

    void ConstraintSketch::SetMode(const std::string &mode) {
        if (mode != "normal" && mode != "strict")
            throw std::invalid_argument("mode='" + mode + "': 'normal' or 'strict'");
        strict = mode == "strict";
        errorOnBranchChange = errorOnBranchChange || strict;
    }

    // 'over' while a constraint is suspended (it contradicts the others); else from the degrees of freedom alone: a solved sketch's redundant constraints are consistent, listed by Redundant()
    std::string ConstraintSketch::Status() {
        const bool free = Analysis(false).dof > 0;
        return !conflicts.empty() ? "over" : free ? "under" : "full";
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> ConstraintSketch::Dependencies() {
        const ConstraintAnalysis &a = Analysis(false);
        const std::vector<char> redundant = s.Redundancies(a);
        const std::vector<std::vector<size_t>> dependencies = s.Dependencies(a);
        std::vector<std::pair<std::string, std::vector<std::string>>> out;
        for (size_t c = 0; c < s.constraints.size(); ++c)
            if (redundant[c] && !s.constraints[c].IsCopyTie())
                out.push_back({constraintIds[c], ListedIds(dependencies[c])});
        return out;
    }

    std::vector<std::string> ConstraintSketch::Redundant() {
        const std::vector<char> redundant = s.Redundancies(Analysis(false));
        std::vector<std::string> out;
        for (size_t c = 0; c < s.constraints.size(); ++c)
            if (redundant[c] && !s.constraints[c].IsCopyTie())
                out.push_back(constraintIds[c]);
        return out;
    }

    std::vector<std::pair<std::string, std::string>> ConstraintSketch::States() {
        std::vector<std::pair<std::string, std::string>> out;
        const ConstraintAnalysis &a = Analysis();
        const std::set<size_t> over = OverElements();
        for (size_t e = 0; e < s.elements.size(); ++e) {
            bool full = true;
            for (size_t u : s.elements[e].Values(s))
                full = full && a.determined[u];
            out.push_back({book.elementIds[e], over.contains(e) ? "over" : full ? "full" : "under"});
        }
        return out;
    }

    std::vector<std::pair<std::string, std::string>> ConstraintSketch::UnknownLabels() const {
        const size_t unknowns = s.values.size();
        std::vector<std::pair<std::string, std::string>> label(unknowns);
        std::vector<char> labelled(unknowns, 0);
        const auto give = [&](size_t u, const std::string &id, const std::string &coordinate) {
            if (!labelled[u]) {
                label[u] = {id, coordinate};
                labelled[u] = 1;
            }
        };
        for (const bool points : {false, true})
            for (size_t e = 0; e < s.elements.size(); ++e) {
                if ((s.elements[e].kind == sketch::ElementKind::Point) != points)
                    continue;
                const std::vector<size_t> u = s.elements[e].Values(s);
                const std::vector<std::pair<std::string, std::string>> parts = s.elements[e].ValueNames(s);
                for (size_t k = 0; k < u.size(); ++k)
                    give(u[k], book.elementIds[e] + parts[k].first, parts[k].second);
            }
        for (size_t c = 0; c < s.constraints.size(); ++c) {
            for (size_t k = 0; k < s.constraints[c].params.size(); ++k)
                give(s.constraints[c].params[k], constraintIds[c], "param " + std::to_string(k));
            if (s.constraints[c].valueAt != sketch::none)
                give(s.constraints[c].valueAt, constraintIds[c], "value");
        }
        for (size_t u = 0; u < unknowns; ++u)
            if (!labelled[u])
                label[u] = {"", std::to_string(u)};
        return label;
    }

}
