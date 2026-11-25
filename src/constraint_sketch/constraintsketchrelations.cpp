// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    // never with the origin or the axes; tol: an absolute length, none for rounding
    std::vector<sketch::FoundRelation> ConstraintSketch::AutoRelations(const AutoConstraint &o, std::vector<char> isNew, std::optional<double> tol,
                                                                       std::vector<sketch::FoundAxis> axes,
                                                                       std::vector<sketch::FoundPitchCircle> pitches) const {
        std::vector<char> skip(s.elements.size(), 0);
        for (const char *id : {"origin", "H", "V"})
            if (const std::optional<size_t> at = book.ElementAt(id))
                skip[*at] = 1;
        for (const sketch::FoundAxis &axis : axes)
            if (axis.isNew)
                for (size_t k = 0; k < s.elements.size(); ++k)
                    skip[k] = skip[k] || k == axis.line || s.IsEndPointOf(k, axis.line);
        for (size_t k = 0; k < s.elements.size(); ++k)
            if (isNew[k] && s.elements[k].IsHeld(s))
                isNew[k] = 0;
        std::vector<sketch::FoundRelation> found = sketch::RelationFinder(s, o, std::move(isNew), std::move(skip), tol, std::move(axes), std::move(pitches)).Find();
        std::erase_if(found, [&](const sketch::FoundRelation &r) { return !s.Fits({r.kind, r.refs}); });
        return found;
    }

    std::vector<sketch::FoundPitchCircle> ConstraintSketch::PitchCircles(std::optional<double> tol) {
        std::vector<char> skip(s.elements.size(), 0);
        for (size_t k = 0; k < s.elements.size(); ++k)
            skip[k] = !IsDrawn(k);
        const auto skipped = [&](const std::string &id) {
            if (const std::optional<size_t> at = book.ElementAt(id))
                skip[*at] = 1;
        };
        for (const auto &[name, info] : patterns) {
            for (const std::string &id : info.originals)
                skipped(id);
            for (const auto &[elements, others] : info.copies) {
                for (const std::string &id : elements)
                    skipped(id);
                for (const std::string &id : others)
                    skipped(id);
            }
        }
        std::vector<sketch::FoundPitchCircle> out = sketch::PitchCircleFinder(s, skip, tol).Circles();
        for (sketch::FoundPitchCircle &pitch : out) {
            pitch.circle = AddElement(s.AddCircle(s.NewPoint(pitch.centre), pitch.r), "Circle", {std::nullopt, {}, true});
            for (size_t hole : pitch.holes) {
                const size_t a = AddElement(s.AddPoint(s.NewPoint(pitch.centre)), "Point", {std::nullopt, {}, true});
                const size_t b = AddElement(s.AddPoint(s.NewPoint(s.Coords(s.elements[hole].Center()))), "Point", {std::nullopt, {}, true});
                pitch.spokes.push_back(AddElement(s.AddLine(s.elements[a].Point(), s.elements[b].Point()), "Line", {std::nullopt, {}, true}));
            }
        }
        return out;
    }

    std::vector<sketch::FoundAxis> ConstraintSketch::SymmetryAxes(std::optional<double> tol, bool newLines) {
        std::vector<char> skip(s.elements.size(), 0);
        for (size_t k = 0; k < s.elements.size(); ++k)
            skip[k] = !IsDrawn(k);
        std::vector<sketch::FoundAxis> out;
        for (sketch::FoundAxis &axis : sketch::SymmetryFinder(s, skip, tol).Axes()) {
            if (axis.line == sketch::none && !newLines)
                continue;
            if (axis.line == sketch::none) {
                const size_t a = AddElement(s.AddPoint(s.NewPoint(axis.At(axis.from))), "Point", {std::nullopt, {}, true});
                const size_t b = AddElement(s.AddPoint(s.NewPoint(axis.At(axis.to))), "Point", {std::nullopt, {}, true});
                axis.line = AddElement(s.AddLine(s.elements[a].Point(), s.elements[b].Point()), "Line", {std::nullopt, {}, true});
                axis.isNew = true;
            }
            out.push_back(axis);
        }
        return out;
    }

    void ConstraintSketch::AddRelations(const std::vector<sketch::FoundRelation> &found, const char *call) {
        if (found.empty())
            return;
        Atomic([&] {
            const std::set<std::string> have = s.ConstraintKeys();
            const size_t first = s.constraints.size();
            for (const sketch::FoundRelation &r : found) {
                if (have.contains(s.PlaceKeyOf(r.kind, r.refs)))
                    continue;
                impliedIds.insert(AddUnsolved({r.kind, r.refs}));
            }
            analysis.reset();
            if (!s.Solve().solved)
                throw std::invalid_argument(std::string(call) + ": the relations found have no solution");
            DropRedundantFrom(first);
            DropRedundantImplied();
        });
    }

    void ConstraintSketch::DropRedundantFrom(size_t first) {
        const std::vector<char> redundant = s.RedundantFrom(first);
        std::vector<std::string> drop;
        for (size_t c = first; c < constraintIds.size(); ++c)
            if (redundant[c] && impliedIds.contains(constraintIds[c]))
                drop.push_back(constraintIds[c]);
        for (size_t k = drop.size(); k-- > 0;) {
            RemoveImplied(drop[k]);
        }
    }

    void ConstraintSketch::RelateAdded(size_t first) {
        if (autoConstraint) {
            std::vector<char> isNew(s.elements.size(), 0);
            std::fill(isNew.begin() + (std::ptrdiff_t)std::min(first, isNew.size()), isNew.end(), 1);
            AddRelations(AutoRelations(*autoConstraint, std::move(isNew)));
        }
        callElements = s.elements.size();
        removedInCall = 0;
        touched.clear();
    }

    std::vector<cs::Suggestion> ConstraintSketch::SuggestConstraints(const std::optional<AutoConstraint> &geometric, bool dimensional, std::optional<double> tol) {
        Flush();
        ConstraintSketch scratch = *this;
        std::vector<cs::Suggestion> out;
        if (geometric)
            for (const cs::Constraint &c : scratch.AddedRelations(*geometric, tol, "SuggestConstraints", false))
                out.push_back(scratch.Suggested(c, ""));
        if (dimensional)
            for (const auto &[c, removes] : scratch.RemovingDimensions(tol))
                out.push_back(scratch.Suggested(c, removes));
        return out;
    }

    std::vector<std::string> ConstraintSketch::AutoConstrain(const std::optional<AutoConstraint> &geometric, bool dimensional, std::optional<double> tol) {
        Flush();
        const std::set<std::string> had(constraintIds.begin(), constraintIds.end());
        const std::vector<Vertex> was = s.PointPlaces();
        const size_t elementsBefore = s.elements.size();
        Atomic([&] {
            if (geometric)
                AddedRelations(*geometric, tol, "AutoConstrain", true);
            if (dimensional)
                AddDimensions(RemovingDimensions(tol));
        });
        const std::vector<Vertex> now = s.PointPlaces();
        double moved = 0.0;
        for (size_t p = 0; p < was.size(); ++p)
            moved = std::max(moved, NormInf(now[p] - was[p]));
        if (moved > Tol(s.Size()))
            Warning() << "AutoConstrain: the relations moved the geometry by up to " << NumberText(moved);
        std::vector<std::string> out;
        for (size_t k = elementsBefore; k < s.elements.size(); ++k)
            if (s.elements[k].kind == sketch::ElementKind::Line || s.elements[k].kind == sketch::ElementKind::Circle)
                out.push_back(book.elementIds[k]);
        for (const std::string &id : constraintIds)
            if (!had.contains(id))
                out.push_back(id);
        return out;
    }

    void ConstraintSketch::RelationsOfCall() {
        std::vector<std::string> joints;
        joints.swap(touched);
        const std::map<std::string, std::string> copies = std::exchange(copyOfCall, {});
        if (!autoConstraint)
            return;
        std::vector<char> isNew(s.elements.size(), 0);
        bool any = false;
        for (size_t k = callElements - std::min(callElements, removedInCall); k < s.elements.size(); ++k) {
            isNew[k] = 1;
            any = true;
        }
        for (const std::string &id : joints)
            if (const std::optional<size_t> at = book.ElementAt(id)) {
                isNew[*at] = 1;
                any = true;
            }
        if (!any)
            return;
        const std::set<std::string> have = s.ConstraintKeys();
        for (const sketch::FoundRelation &r : AutoRelations(*autoConstraint, std::move(isNew)))
            if (!have.contains(s.PlaceKeyOf(r.kind, r.refs)) && !IsInOneCopy(r.refs, copies))
                ConstrainImplied({r.kind, r.refs});
    }

    std::vector<cs::Constraint> ConstraintSketch::AddedRelations(const AutoConstraint &o, std::optional<double> tol, const char *call, bool newAxes) {
        const std::set<std::string> had(constraintIds.begin(), constraintIds.end());
        std::vector<sketch::FoundAxis> axes = o.symmetric ? SymmetryAxes(tol, newAxes) : std::vector<sketch::FoundAxis>{};
        std::vector<sketch::FoundPitchCircle> pitches = o.pitch_circle && newAxes ? PitchCircles(tol) : std::vector<sketch::FoundPitchCircle>{};
        const std::vector<std::pair<std::string, std::vector<std::string>>> helpers = HelpersAdded(axes, pitches);
        AddRelations(AutoRelations(o, std::vector<char>(s.elements.size(), 1), tol, std::move(axes), std::move(pitches)), call);
        DropUnrelating(helpers);
        std::vector<cs::Constraint> out;
        for (size_t c = 0; c < constraintIds.size(); ++c)
            if (!had.contains(constraintIds[c]))
                out.push_back(s.constraints[c]);
        return out;
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> ConstraintSketch::HelpersAdded(const std::vector<sketch::FoundAxis> &axes,
                                                                                                 const std::vector<sketch::FoundPitchCircle> &pitches) const {
        std::vector<std::pair<std::string, std::vector<std::string>>> out;
        const auto withEnds = [&](size_t line, std::vector<std::string> &ids) {
            ids.push_back(book.elementIds[line]);
            for (size_t k = 0; k < s.elements.size(); ++k)
                if (s.IsEndPointOf(k, line))
                    ids.push_back(book.elementIds[k]);
        };
        for (const sketch::FoundAxis &axis : axes)
            if (axis.isNew) {
                out.push_back({book.elementIds[axis.line], {}});
                withEnds(axis.line, out.back().second);
            }
        for (const sketch::FoundPitchCircle &pitch : pitches) {
            out.push_back({book.elementIds[pitch.circle], {}});
            for (size_t spoke : pitch.spokes)
                withEnds(spoke, out.back().second);
            out.back().second.push_back(book.elementIds[pitch.circle]);
        }
        return out;
    }

    void ConstraintSketch::DropUnrelating(const std::vector<std::pair<std::string, std::vector<std::string>>> &helpers) {
        for (const auto &[helper, parts] : helpers) {
            const auto at = book.byId.find(helper);
            if (at == book.byId.end())
                continue;
            const size_t h = at->second.index;
            std::set<size_t> own, others;
            for (const std::string &id : parts)
                if (const auto p = book.byId.find(id); p != book.byId.end())
                    own.insert(p->second.index);
            for (const cs::Constraint &c : s.constraints)
                if (c.RefersTo(h))
                    for (const sketch::Ref &r : c.refs)
                        if (!own.contains(r.element))
                            others.insert(r.element);
            if (others.size() <= 1)
                for (const std::string &id : parts)
                    if (book.byId.contains(id))
                        RemoveOne(id);
        }
    }

    std::vector<std::pair<cs::Constraint, std::string>> ConstraintSketch::RemovingDimensions(std::optional<double> tol) {
        const std::vector<cs::Constraint> candidates = DimensionCandidates(tol);
        std::vector<std::pair<cs::Constraint, std::string>> out;
        for (const auto &[k, removes] : cs::FreeMotionFinder(s, UnknownLabels(), book.elementIds).Removing(candidates))
            out.push_back({candidates[k], removes});
        return out;
    }

    // in the order they are tried: a datum on the origin held there, the drafter's dimensions (DimensionFinder), then
    // every free point's h and v from the origin, the datum's first (Distance from V and H; a point on an axis, within
    // tol: Coincident with it); values as the geometry has them
    std::vector<cs::Constraint> ConstraintSketch::DimensionCandidates(std::optional<double> tol) const {
        std::vector<char> drawn(s.elements.size(), 0);
        for (size_t k = 0; k < s.elements.size(); ++k)
            drawn[k] = IsDrawn(k);
        const cs::DimensionFinder finder(s, std::move(drawn));
        std::vector<cs::Constraint> out;
        const std::vector<std::pair<std::string, std::string>> label = UnknownLabels();
        std::vector<char> seen(s.values.size(), 0);
        const std::optional<size_t> datum = finder.DatumUnknown();
        if (const std::optional<size_t> line = finder.OriginLine())
            out.push_back({sketch::Kind::Coincident, {sketch::Ref{2}, sketch::Ref{*line}}});
        if (datum && finder.IsAtOrigin()) {
            seen[*datum] = 1;
            Positions(RefOf(label[*datum].first), *datum, tol, out);
        }
        const std::vector<cs::Constraint> drafted = finder.Candidates();
        out.insert(out.end(), drafted.begin(), drafted.end());
        if (datum && !seen[*datum]) {
            seen[*datum] = 1;
            Positions(RefOf(label[*datum].first), *datum, tol, out);
        }
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e) {
            const sketch::ElementKind kind = s.elements[e].kind;
            if (kind != sketch::ElementKind::Point && kind != sketch::ElementKind::Line && kind != sketch::ElementKind::Arc &&
                kind != sketch::ElementKind::Circle && kind != sketch::ElementKind::Spline)
                continue;
            const std::vector<size_t> u = s.elements[e].Values(s);
            const sketch::Element::Names names = s.elements[e].ValueNames(s);
            for (size_t k = 0; k < u.size(); ++k) {
                // a curve's frame is no point to dimension: its ends are
                if (names[k].second != "x" || names[k].first == ".origin" || s.held[u[k]] || seen[u[k]])
                    continue;
                seen[u[k]] = 1;
                Positions(RefOf(label[u[k]].first), u[k], tol, out);
            }
        }
        return out;
    }

    void ConstraintSketch::Positions(const sketch::Ref &p, size_t unknown, std::optional<double> tol, std::vector<cs::Constraint> &out) const {
        const double rounding = std::max(Tol(s.Size()), tol.value_or(0.0));
        // V (element 1) measures h, H (element 0) v
        for (const size_t axis : {1, 0}) {
            const double at = s.values[unknown + 1 - axis];
            cs::Constraint c{std::abs(at) > rounding ? sketch::Kind::Distance : sketch::Kind::Coincident, {p, sketch::Ref{axis}}};
            c.value = std::abs(at);
            out.push_back(c);
        }
    }

    void ConstraintSketch::AddDimensions(const std::vector<std::pair<cs::Constraint, std::string>> &found) {
        if (found.empty())
            return;
        Atomic([&] {
            for (const auto &[c, removes] : found)
                AddUnsolved(c);
            analysis.reset();
            s.Solve();
        });
    }

    cs::Suggestion ConstraintSketch::Suggested(const cs::Constraint &c, const std::string &removes) const {
        cs::Suggestion out{c.kind, {}, std::nullopt, Text(c), removes};
        for (const sketch::Ref &r : c.refs)
            out.refs.push_back(RefName(r));
        if (sketch::HasValue(c.kind))
            out.value = c.value;
        return out;
    }

}
