// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    ConstraintSketch &ConstraintSketch::AddPattern(const std::vector<std::string> &ids, const std::string &ref, size_t count, double step, bool linear,
                                                   bool keep, bool internal, const std::optional<std::string> &id, bool equal,
                                                   const std::optional<sketch::PatternDir2> &second, bool reverse) {
        if (!keep) {
            BasicSketch::AddPattern(ids, ref, count, step, linear, keep, internal, id, equal, second, reverse);
            return *this;
        }
        const std::string call = linear ? "LinearPattern" : "CircularPattern";
        CheckPattern(call, count, step, second);
        Atomic([&] {
            const sketch::Ref r = PatternRef(call, ref, linear);
            const std::vector<size_t> originals = Originals(ids, call);
            cs::PatternInfo info{{}, ref, linear, equal && !linear, 0, {}, {}, ids, internal};
            for (size_t e : originals)
                info.originals.push_back(book.elementIds[e]);
            info.reverse = reverse;
            if (second) {
                PatternRef(call, second->ref, true);
                info.ref2 = second->ref;
                info.count = count;
                info.count2 = second->count;
                info.reverse2 = second->reverse;
            }
            AddElement(s.AddPattern(linear ? step : Radians(step), linear, second ? std::optional(second->step) : std::nullopt), call,
                       {id, {}, true});
            const size_t pattern = s.elements.size() - 1;
            AddConstraint({linear ? sketch::Kind::Distance : sketch::Kind::Angle, {{pattern}}, step}, book.elementIds.back() + (linear ? ".spacing" : ".angle"));
            if (second) {
                cs::Constraint c{sketch::Kind::Distance, {{pattern}}, second->step};
                c.variant2 = 2;
                AddConstraint(c, book.elementIds.back() + ".spacing2");
            }
            for (const auto &[k, j] : PatternSteps(count, second ? second->count : 0))
                PlaceTiedCopy(originals, r, linear, step, k, pattern, info, j);
            patterns[book.elementIds[pattern]] = std::move(info);
            book.groups.push_back({call, book.elementIds[pattern]});
        });
        return *this;
    }

    std::string ConstraintSketch::CopiesOf(size_t e) const {
        std::set<std::string> out;
        for (const auto &[o, info] : offsetInfos)
            if (info.HasSource(book.elementIds[e]))
                out.insert(o);
        for (const auto &[pattern, info] : patterns)
            if (info.HasOriginal(book.elementIds[e]))
                out.insert(pattern);
        for (const auto &[scope, m] : mirrors)
            if (m.tie && book.byId.contains(scope + "." + book.elementIds[e]))
                out.insert(scope);
        return Joined({out.begin(), out.end()});
    }

    std::vector<std::string> ConstraintSketch::Expanded(const std::vector<std::string> &ids) const {
        std::vector<std::string> out;
        for (const std::string &id : ids)
            if (const auto o = offsetInfos.find(id); o != offsetInfos.end()) {
                for (const std::string &e : o->second.elements)
                    if (const auto at = book.byId.find(e); at != book.byId.end() && s.elements[at->second.index].kind != sketch::ElementKind::Point)
                        out.push_back(e);
            } else {
                const std::vector<std::string> more = BasicSketch::Expanded({id});
                out.insert(out.end(), more.begin(), more.end());
            }
        return out;
    }

    bool ConstraintSketch::IsInOneCopy(const std::vector<sketch::Ref> &refs, const std::map<std::string, std::string> &copies) const {
        std::optional<std::string> copy;
        for (const sketch::Ref &r : refs) {
            const auto it = copies.find(book.elementIds[r.element]);
            if (it == copies.end() || (copy && *copy != it->second))
                return false;
            copy = it->second;
        }
        return copy.has_value();
    }

    void ConstraintSketch::FollowCopies(std::vector<double> &origin) {
        std::vector<char> follows(s.values.size(), 0);
        std::set<std::string> ties;
        std::set<size_t> mine;
        for (const auto &[id, p] : patterns) {
            const auto at = book.byId.find(id);
            if (at == book.byId.end() || !at->second.isElement)
                continue;
            mine.insert(at->second.index);
            for (size_t u : s.elements[at->second.index].Values(s))
                follows[u] = 1;
            for (const auto &[elements, copyTies] : p.copies) {
                for (const std::string &e : elements)
                    if (const std::optional<size_t> c = book.ElementAt(e))
                        for (size_t u : s.elements[*c].Values(s))
                            follows[u] = 1;
                ties.insert(copyTies.begin(), copyTies.end());
            }
        }
        if (mine.empty())
            return;
        // the others are left out by suspending them: removed, they would shift the indices a curve's formula reads its
        // dimensions by
        cs::System alone = s;
        for (size_t c = 0; c < s.constraints.size(); ++c) {
            const std::vector<sketch::Ref> &refs = s.constraints[c].refs;
            const bool dimension = !refs.empty() && std::all_of(refs.begin(), refs.end(), [&](const sketch::Ref &r) { return mine.contains(r.element); });
            if (!dimension && !ties.contains(constraintIds[c]))
                alone.constraints[c].suspended = true;
        }
        for (size_t u = 0; u < follows.size(); ++u)
            if (!follows[u])
                alone.held[u] = 1;
        if (!alone.Solve().solved)
            return;
        for (size_t u = 0; u < follows.size(); ++u)
            if (follows[u])
                s.values[u] = origin[u] = alone.values[u];
    }

    std::optional<std::string> ConstraintSketch::CountOf(const std::string &id) const {
        for (const std::string_view suffix : {std::string_view(".count"), std::string_view(".count2")}) {
            if (id.size() <= suffix.size() || !id.ends_with(suffix))
                continue;
            const std::string pattern = id.substr(0, id.size() - suffix.size());
            if (const auto p = patterns.find(pattern); p != patterns.end() && (suffix == ".count" || p->second.IsGrid()))
                return pattern;
        }
        return std::nullopt;
    }

    std::vector<std::pair<std::string, std::array<size_t, 2>>> ConstraintSketch::WantedCounts(const std::vector<std::pair<std::string, size_t>> &changes) const {
        std::vector<std::pair<std::string, std::array<size_t, 2>>> wanted;
        for (const auto &[count, n] : changes) {
            const std::string id = count.substr(0, count.rfind('.'));
            if (!patterns.contains(id))
                throw std::invalid_argument(count + ": the pattern is gone");
            auto at = std::find_if(wanted.begin(), wanted.end(), [&](const auto &w) { return w.first == id; });
            if (at == wanted.end())
                at = wanted.insert(wanted.end(), {id, {patterns.at(id).CountNow(false), patterns.at(id).CountNow(true)}});
            at->second[count.ends_with("2") ? 1 : 0] = n;
        }
        return wanted;
    }

    void ConstraintSketch::AddCopies(const std::string &id, const std::array<size_t, 2> &n) {
        cs::PatternInfo &p = patterns.at(id);
        std::vector<size_t> originals;
        for (const std::string &o : p.originals)
            if (const std::optional<size_t> at = book.ElementAt(o))
                originals.push_back(*at);
        if (originals.empty())
            throw std::invalid_argument(p.CountText(id, n) + ": the elements it copies are gone");
        const size_t pattern = book.byId.at(id).index;
        const double value = s.values[s.elements[pattern].Step()];
        const double step = p.linear ? value : Degrees(value);
        const sketch::Ref r = RefOf(p.ref);
        for (const auto &[k, j] : PatternSteps(n[0], p.IsGrid() ? n[1] : 0))
            if (std::find(p.steps.begin(), p.steps.end(), std::pair{k, j}) == p.steps.end())
                PlaceTiedCopy(originals, r, p.linear, step, k, pattern, p, j);
    }

    void ConstraintSketch::ApplyCounts(size_t steps) {
        const std::vector<std::pair<std::string, size_t>> changes = std::exchange(pending.counts, {});
        const std::vector<std::pair<std::string, std::array<size_t, 2>>> wanted = WantedCounts(changes);
        std::vector<std::pair<std::string, std::array<size_t, 2>>> grow;
        for (const auto &[id, n] : wanted) {
            cs::PatternInfo &p = patterns.at(id);
            const size_t now = p.CountNow(false), now2 = p.CountNow(true);
            if (p.IsGrid() && n[0] * n[1] < 2)
                throw std::invalid_argument(p.CountText(id, n) + ": a pattern keeps the original and one copy at least");
            if (n[0] < now || (p.IsGrid() && n[1] < now2))
                RemoveCopies(id, n);
            if (n[0] > now || (p.IsGrid() && n[1] > now2))
                grow.push_back({id, n});
            if (p.IsGrid()) {
                p.count = n[0];
                p.count2 = n[1];
            }
            if (const std::string angle = id + ".angle"; p.equal && n[0] != now && book.byId.contains(angle) && !s.DriverOf(DimensionAt(angle))) {
                pending.EraseValue(angle);
                pending.values.push_back({angle, 360.0 / (double)n[0]});
            }
        }
        UpdateDimensions(steps);
        for (const auto &[id, n] : grow)
            AddCopies(id, n);
    }

    void ConstraintSketch::RemoveCopies(const std::string &id, const std::array<size_t, 2> &n) {
        cs::PatternInfo &p = patterns.at(id);
        const std::string text = p.CountText(id, n);
        std::vector<std::string> dropped;
        for (size_t ci = p.copies.size(); ci-- > 0;) {
            const auto [k, j] = p.steps[ci];
            if (k < n[0] && (!p.IsGrid() || j < n[1]))
                continue;
            const auto [elements, ties] = std::move(p.copies[ci]);
            p.copies.erase(p.copies.begin() + (std::ptrdiff_t)ci);
            p.steps.erase(p.steps.begin() + (std::ptrdiff_t)ci);
            if (ci < p.before.size()) {
                if (!p.IsGrid()) {
                    std::erase_if(book.counters, [&](const auto &c) { return c.first.starts_with(id + "."); });
                    book.counters.insert(p.before[ci].first.begin(), p.before[ci].first.end());
                    p.ties = p.before[ci].second;
                }
                p.before.erase(p.before.begin() + (std::ptrdiff_t)ci);
            }
            std::set<size_t> mine;
            for (const std::string &e : elements)
                if (const std::optional<size_t> at = book.ElementAt(e))
                    mine.insert(*at);
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (std::find(ties.begin(), ties.end(), constraintIds[c]) == ties.end() &&
                    std::any_of(s.constraints[c].refs.begin(), s.constraints[c].refs.end(), [&](const sketch::Ref &r) { return mine.contains(r.element); }))
                    dropped.push_back(constraintIds[c]);
            for (auto e = elements.rbegin(); e != elements.rend(); ++e)
                if (const std::optional<size_t> at = book.ElementAt(*e)) {
                    try {
                        Remove(*e, text);
                    } catch (const std::invalid_argument &x) {
                        throw std::invalid_argument(text + ": " + x.what());
                    }
                }
        }
        if (!dropped.empty())
            Warning() << text << ": removed " << Joined(dropped) << " with the copies they held";
    }

    void ConstraintSketch::MirrorTied(const sketch::Copies &made, const sketch::Ref &axis) {
        const std::string copy = book.elementIds[made.elements.empty() ? made.points.front().second : made.elements.front().second];
        for (const auto &[o, c] : made.points)
            copyOfCall[book.elementIds[c]] = copy;
        for (const auto &[o, c] : made.elements)
            copyOfCall[book.elementIds[c]] = copy;
        for (const auto &[o, c] : made.points)
            if (!made.placed.contains(o))
                AddTie({sketch::Kind::Symmetric, {{o}, {c}, axis}});
        // an arc copy is held by its ends and its radius, a circle copy by its centre and its radius
        for (const auto &[o, c] : made.elements) {
            const sketch::ElementKind k = s.elements[o].kind;
            if (k == sketch::ElementKind::Circle)
                AddTie({sketch::Kind::Symmetric, {{o, sketch::Part::Center}, {c, sketch::Part::Center}, axis}});
            if (s.elements[o].IsRound())
                AddTie({sketch::Kind::Equal, {{o}, {c}}});
        }
        const auto none = [](size_t) { return std::optional<std::string>{}; };
        for (const auto &[o, c] : made.splines)
            TieTangents(o, c, {axis}, 0, none);
        for (const sketch::Copies::Position &x : made.positions)
            TiePosition(x, std::nullopt);
    }

    void ConstraintSketch::PlaceTiedCopy(const std::vector<size_t> &originals, const sketch::Ref &r, bool linear, double step, size_t k, size_t pattern,
                                         cs::PatternInfo &info, size_t j) {
        sketch::Scoped scope{book.idScope, std::exchange(book.idScope, book.elementIds[pattern])};
        std::map<std::string, size_t> mine;
        for (const auto &[key, n] : book.counters)
            if (key.starts_with(book.idScope + "."))
                mine.emplace(key, n);
        info.before.push_back({std::move(mine), info.ties});
        const size_t elements = book.elementIds.size(), constraints = constraintIds.size();
        const std::optional<sketch::Ref> r2 = info.IsGrid() ? std::optional(RefOf(info.ref2)) : std::nullopt;
        const double step2 = info.IsGrid() ? s.values[s.elements[pattern].Step2()] : 0.0;
        const std::string prefix = StepPrefix(book.elementIds[pattern], k, j, info.IsGrid());
        const int along = info.reverse ? -(int)k : (int)k, along2 = info.reverse2 ? -(int)j : (int)j;
        const sketch::Copies made = PlaceCopy(originals, r, linear, info.reverse ? -step : step, k, prefix, r2, info.reverse2 ? -step2 : step2, j);
        for (size_t e = elements; e < book.elementIds.size(); ++e)
            copyOfCall[book.elementIds[e]] = prefix;
        book.CopyWires(info.given, made, prefix);
        const auto tie = [&] { return book.elementIds[pattern] + ".copy." + std::to_string(++info.ties); };
        const auto copyOf = [&](const sketch::Ref &a, const sketch::Ref &b) {
            cs::Constraint c{sketch::Kind::Copy, {a, b, {pattern}, r}};
            if (r2)
                c.refs.push_back(*r2);
            c.variant = along;
            c.variant2 = along2;
            AddConstraint(c, tie());
        };
        for (const auto &[a, b] : made.points)
            if (!made.placed.contains(a))
                copyOf({a}, {b});
        for (const auto &[a, b] : made.elements) {
            const sketch::ElementKind kind = s.elements[a].kind;
            if (kind == sketch::ElementKind::Circle)
                copyOf({a, sketch::Part::Center}, {b, sketch::Part::Center});
            if (s.elements[a].IsRound())
                AddTie({sketch::Kind::Equal, {{a}, {b}}});
        }
        const auto id = [&](size_t) { return std::optional<std::string>{tie()}; };
        for (const auto &[a, b] : made.splines)
            TieTangents(a, b, r2 ? std::vector<sketch::Ref>{{pattern}, r, *r2} : std::vector<sketch::Ref>{{pattern}, r}, along, id, along2);
        for (const sketch::Copies::Position &x : made.positions)
            TiePosition(x, id(0));
        info.copies.push_back({{book.elementIds.begin() + (std::ptrdiff_t)elements, book.elementIds.end()},
                               {constraintIds.begin() + (std::ptrdiff_t)constraints, constraintIds.end()}});
        info.steps.push_back({k, j});
    }

    void ConstraintSketch::MirrorMade(const std::string &scope, const std::vector<std::string> &given, const std::string &axis, bool tie, bool internal) {
        mirrors[scope] = {given, axis, tie, internal};
        mirroredInCall.push_back({scope, axis});
    }

    void ConstraintSketch::ConstraintsCopied(const std::string &prefix, bool flip) {
        const size_t n = s.constraints.size();
        for (size_t c = 0; c < n; ++c)
            if (std::optional<cs::Constraint> d = CopiedConstraint(s.constraints[c], prefix, flip); d && !book.byId.contains(prefix + "." + constraintIds[c]))
                AddConstraint(std::move(*d), prefix + "." + constraintIds[c]);
    }

    std::optional<cs::Constraint> ConstraintSketch::CopiedConstraint(const cs::Constraint &c, const std::string &prefix, bool flip) const {
        if (c.tie || c.refs.empty() || c.kind == sketch::Kind::Copy || c.kind == sketch::Kind::Expression || c.kind == sketch::Kind::Offset ||
            c.automatic)
            return std::nullopt;
        if (sketch::IsFix(c.kind) || c.kind == sketch::Kind::RigidGroup ||
            ((c.kind == sketch::Kind::Horizontal || c.kind == sketch::Kind::Vertical) && c.refs.size() == 1))
            return std::nullopt;
        cs::Constraint d = c;
        d.valueAt = sketch::none;
        for (sketch::Ref &r : d.refs) {
            const auto it = book.byId.find(prefix + "." + book.elementIds[r.element]);
            if (it == book.byId.end() || !it->second.isElement)
                return std::nullopt;
            r.element = it->second.index;
        }
        if (flip && d.kind == sketch::Kind::Angle && d.refs.size() == 2)
            d.value = -d.value;
        return d;
    }

    std::string ConstraintSketch::Given(const std::vector<std::string> &ids) {
        if (ids.size() == 1)
            return "\"" + ids[0] + "\"";
        std::string out;
        for (const std::string &id : ids)
            out += (out.empty() ? "[\"" : ", \"") + id + "\"";
        return out + "]";
    }

    std::vector<cs::ConstraintInfo> ConstraintSketch::CopyEntries() const {
        std::vector<cs::ConstraintInfo> out;
        const auto derived = [&](const std::string &prefix, bool flip) {
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (const std::optional<cs::Constraint> d = CopiedConstraint(s.constraints[c], prefix, flip)) {
                    cs::ConstraintInfo i{prefix + "." + constraintIds[c], sketch::KindName(d->kind), Text(*d) + ", derived=True", {}, std::nullopt, false,
                           d->reference, true};
                    for (const sketch::Ref &r : d->refs)
                        i.refs.push_back(RefName(r));
                    if (sketch::HasValue(d->kind))
                        i.value = flip && d->kind == sketch::Kind::Angle && d->refs.size() == 2 ? -s.DimensionValue(c) : s.DimensionValue(c);
                    out.push_back(std::move(i));
                }
        };
        for (const auto &[scope, m] : mirrors) {
            if (!book.wires.contains(scope) || !m.tie)
                continue;
            const auto axis = book.byId.find(m.axis);
            out.push_back({scope, "Symmetric",
                           Given(m.given) + ", \"" + scope + "\", " + (axis != book.byId.end() ? RefText({axis->second.index}) : "\"" + m.axis + "\"") +
                               ", px.Symmetric",
                           {}, std::nullopt, false, false, false, true});
            if (m.internal)
                derived(scope, true);
        }
        for (const auto &[pattern, p] : patterns)
            for (const auto &[k, j] : p.steps) {
                const std::string prefix = StepPrefix(pattern, k, j, p.IsGrid());
                const std::string step = p.IsGrid() ? "steps " + std::to_string(k) + ", " + std::to_string(j) : "step " + std::to_string(k);
                out.push_back({prefix, p.linear ? "LinearPattern" : "CircularPattern", Given(p.given) + ", " + step + " of \"" + pattern + "\"", {},
                               std::nullopt, false, false, false, true});
                if (p.internal)
                    derived(prefix, false);
            }
        return out;
    }

    std::optional<std::string> ConstraintSketch::CopyHint(const std::string &id) const {
        for (const cs::ConstraintInfo &i : CopyEntries()) {
            if (i.id != id)
                continue;
            std::string pattern = id.substr(0, id.rfind('.'));
            if (!patterns.contains(pattern) && pattern.find('.') != std::string::npos)
                pattern = pattern.substr(0, pattern.rfind('.'));
            if (i.tie && patterns.contains(pattern)) {
                const cs::PatternInfo &p = patterns.at(pattern);
                return id + " is a copy of " + pattern + ", it follows the pattern: change " + pattern + (p.linear ? ".spacing" : ".angle") +
                       (p.IsGrid() ? ", " + pattern + ".spacing2, " + pattern + ".count or " + pattern + ".count2" : " or " + pattern + ".count");
            }
            if (i.tie)
                return id + " holds the copies symmetric to their originals; Remove('" + id + "') removes the copies";
            return id + " is derived from " + id.substr(id.rfind('.') + 1) + " of the originals: change that";
        }
        return std::nullopt;
    }

    void ConstraintSketch::TiePosition(const sketch::Copies::Position &x, const std::optional<std::string> &id) {
        cs::Constraint c{sketch::Kind::Copy, {{x.originalElement}, {x.copyElement}}};
        c.held = {x.original, x.copy};
        AddConstraint(c, id);
    }

}
