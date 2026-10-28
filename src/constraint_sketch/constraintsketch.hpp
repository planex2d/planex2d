// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "constraint_sketch/copyrecords.hpp"
#include "constraint_sketch/freemotions.hpp"
#include "constraint_sketch/pending.hpp"
#include "constraint_sketch/reports.hpp"
#include "constraint_sketch/system.hpp"
#include "core/objectid.hpp"
#include "core/vertex.hpp"
#include "curves/edge.hpp"
#include "offset/offsetpieces.hpp"
#include "sketch/copies.hpp"
#include "sketch/cornerparts.hpp"
#include "sketch/guards.hpp"
#include "sketch/offsetsides.hpp"
#include "sketch/relationfinder.hpp"
#include "sketch/sketch.hpp"

namespace planex2d {

    // the sketch with constraints: the drawing is the starting guess, the constraints move it as little as they need to
    class ConstraintSketch : public BasicSketch<cs::System> {
        friend class SketchWriter;
        friend class SketchReader;

    public:
        explicit ConstraintSketch(const AxisSystem &a = AxisSystem()) : BasicSketch(a) {}
        // the constraint sketch of a QuickSketch: its elements, no constraints; with autoConstraint the relations its
        // geometry shows
        ConstraintSketch(const Sketch &quick, const std::optional<AutoConstraint> &autoConstraint) : BasicSketch(quick) {
            this->autoConstraint = autoConstraint;
            if (autoConstraint)
                AddRelations(AutoRelations(*autoConstraint, std::vector<char>(s.elements.size(), 1)));
        }
        using BasicSketch::Mirror;

        // the elements mirrored in place (MirrorElements); the constraints among them keep holding (an angle between two
        // of them turns the other way, the sides and pairings taken anew), the sketch solves again; where the other
        // constraints do not allow the mirrored place it raises and nothing changes
        ConstraintSketch &Mirror(const std::vector<std::string> &ids, const std::string &axis) {
            Flush();
            const std::vector<size_t> elements = Originals(ids, "Mirror");
            const std::set<size_t> mine(elements.begin(), elements.end());
            const ConstraintSketch saved = *this;
            const std::vector<std::pair<size_t, Vertex>> targets = MirrorElements(elements, axis);
            std::vector<size_t> holding;
            for (size_t c = 0; c < s.constraints.size(); ++c) {
                cs::Constraint &k = s.constraints[c];
                if (k.refs.empty() || k.kind == sketch::Kind::Expression)
                    continue;
                const bool internal = std::all_of(k.refs.begin(), k.refs.end(), [&](const sketch::Ref &r) { return mine.contains(r.element); });
                // what may hold them where they are: a tie to the rest, or one of an element alone (Fix, Horizontal)
                if (k.refs.size() == 1 ? internal : std::any_of(k.refs.begin(), k.refs.end(), [&](const sketch::Ref &r) { return mine.contains(r.element); }) && !internal)
                    holding.push_back(c);
                if (!internal)
                    continue;
                if (k.kind == sketch::Kind::Angle && k.refs.size() == 2 && k.valueAt == sketch::none)
                    k.value = -k.value;
                s.Refreeze(c);
            }
            analysis.reset();
            bool held = s.Solve().solved && !OutOfDomain();
            for (const auto &[q, p] : targets)
                held = held && Coords(q).IsClose(p, Tol(std::max(NormInf(p), 1.0)));
            if (!held) {
                const std::string names = Joined(IdsOf(holding));
                *this = saved;
                throw std::invalid_argument("Mirror: " + (names.empty() ? std::string("the constraints") : names) +
                                            " hold the elements where they are; nothing changed. Mirrored(ids, axis) adds mirrored "
                                            "copies, then Remove the originals");
            }
            Sync();
            return *this;
        }

        // a constraint, or an element with the constraints on it; a line's or arc's ends go with it unless something
        // else ends there or a constraint holds them
        ConstraintSketch &Remove(const std::string &id) { return Remove(id, "Remove('" + id + "')"); }
        // call names it in the warnings about the expressions that go with what it removes
        ConstraintSketch &Remove(const std::string &id, const std::string &call) {
            // a dimension a curve uses raises when it would go (DropConstraint): nothing changes then
            sketch::Undoable(*this, [&] {
                const std::map<size_t, std::string> held = ValueHolders();
                RemoveOne(id);
                Prune(call, held);
            });
            return *this;
        }

        // the axes (H, V, the sketch's origin) to the point p of the plane; the drawing stays where it is, its
        // coordinates shift. Constraints to the axes follow them; without a solution then nothing changes
        ConstraintSketch &SetOrigin(const Vertex &p) override {
            Flush();
            const ConstraintSketch saved = *this;
            BasicSketch::SetOrigin(p);
            // what a Fix holds and a rigid group's frame are places in the plane: they stay, their coordinates shift
            for (cs::Constraint &c : s.constraints) {
                if (c.kind == sketch::Kind::RigidGroup && c.params.size() >= 2) {
                    s.values[c.params[0]] -= p.x;
                    s.values[c.params[1]] -= p.y;
                }
                if (c.kind != sketch::Kind::Fix || c.target.empty())
                    continue;
                const size_t points = c.heldPoints.empty() && c.held.empty() ? c.target.size() / 2 : c.heldPoints.size();
                for (size_t k = 0; k < points; ++k) {
                    c.target[2 * k] -= p.x;
                    c.target[2 * k + 1] -= p.y;
                }
                for (size_t k = 0; k < c.held.size(); ++k)
                    for (size_t e = 3; e < s.elements.size(); ++e)
                        if (s.elements[e].kind == sketch::ElementKind::Axis && s.elements[e].AxisDistance() == c.held[k])
                            c.target[2 * points + k] -= AxisShift(e, p);
            }
            // what references to lengths an expression uses measure in the new frame (coordinates); angles do not change
            for (const cs::Constraint &c : s.constraints)
                if (c.reference && c.valueAt != sketch::none && !s.isAngle[c.valueAt])
                    s.values[c.valueAt] = s.Measured(c);
            analysis.reset();
            if (!s.Solve().solved || OutOfDomain()) {
                *this = saved;
                throw std::invalid_argument("SetOrigin: the constraints to the axes have no solution there, nothing changed");
            }
            Sync();
            return *this;
        }

        // count instances of the elements (the original first): turned by step degrees about the point ref, or moved
        // step along the line ref (linear); keep ties every copied point to its original through a pattern element whose
        // step is a dimension (pattern.angle or pattern.spacing), arc and circle copies by equal radius too, and its
        // count (pattern.count) can change; equal: a circular pattern over the full turn, which a new count spreads anew
        ConstraintSketch &AddPattern(const std::vector<std::string> &ids, const std::string &ref, size_t count, double step, bool linear,
                                     bool keep, bool internal, const std::optional<std::string> &id, bool equal = false,
                                     const std::optional<sketch::PatternDir2> &second = std::nullopt) {
            if (!keep) {
                BasicSketch::AddPattern(ids, ref, count, step, linear, keep, internal, id, equal, second);
                return *this;
            }
            const std::string call = linear ? "LinearPattern" : "CircularPattern";
            CheckPattern(call, count, step, second);
            Atomic([&] {
                const sketch::Ref r = PatternRef(call, ref, linear);
                const std::vector<size_t> originals = Originals(ids, call);
                cs::PatternInfo info{{}, ref, linear, equal && !linear, 0, {}, {}, ids, internal};
                for (size_t e : originals)
                    info.originals.push_back(elementIds[e]);
                if (second) {
                    PatternRef(call, second->ref, true);
                    info.ref2 = second->ref;
                    info.count = count;
                    info.count2 = second->count;
                }
                AddElement(s.AddPattern(linear ? step : Radians(step), linear, second ? std::optional(second->step) : std::nullopt), call,
                           {id, {}, true});
                const size_t pattern = s.elements.size() - 1;
                AddConstraint({linear ? sketch::Kind::Distance : sketch::Kind::Angle, {{pattern}}, step}, elementIds.back() + (linear ? ".spacing" : ".angle"));
                if (second) {
                    cs::Constraint c{sketch::Kind::Distance, {{pattern}}, second->step};
                    c.variant2 = 2;
                    AddConstraint(c, elementIds.back() + ".spacing2");
                }
                for (const auto &[k, j] : PatternSteps(count, second ? second->count : 0))
                    PlaceTiedCopy(originals, r, linear, step, k, pattern, info, j);
                patterns[elementIds[pattern]] = std::move(info);
                groups.push_back({call, elementIds[pattern]});
            });
            return *this;
        }

        // as BasicSketch::Cut; not of a piece of an offset, and an expression that used what goes goes too
        ConstraintSketch &Cut(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse,
                              bool trim, bool tied = true) override {
            const std::string call = std::string(trim ? "Trim" : "Split") + "('" + id + "')";
            Flush();
            if (const std::optional<std::string> owner = OffsetOf(id))
                throw std::invalid_argument(call + ": part of the offset '" + *owner + "'; Remove('" + *owner + "')");
            if (!tied)
                if (const std::string copies = CopiesOf(RefOf(id).element); !copies.empty())
                    throw std::invalid_argument(call + ", auto_constraint=False: " + copies +
                                                " follow it as it is; Split it with auto_constraint=True, or Remove them first");
            sketch::Undoable(*this, [&] {
                const std::map<size_t, std::string> held = ValueHolders();
                CutPieces(id, cutterIds, nearPoint, reverse, trim, tied, call);
                Prune(call, held);
                if (const std::optional<std::string> wrong = OffsetProblem())
                    throw std::invalid_argument(call + ": " + *wrong);
            });
            analysis.reset();
            return *this;
        }

        // as BasicSketch.Fillet / Chamfer, and the new element is held: an arc by its tangency to the two elements at its
        // ends and the dimension Fillet.k.r, a line by its ends on the two elements and the dimensions Chamfer.k.d and
        // Chamfer.k.d2 or Chamfer.k.angle, measured from the sharp corner, and Chamfer.k.reverse. The constraints on the
        // ends of the elements cut follow as with Trim; where nothing can be solved it raises and changes nothing
        ConstraintSketch &Fillet(const std::vector<Vertex> &at, double r, bool curvature, const std::vector<std::string> &ids,
                                 const std::vector<Properties> &given) override {
            return Cornered("Fillet", [&] { BasicSketch::Fillet(at, r, curvature, ids, given); });
        }
        ConstraintSketch &Chamfer(const std::vector<Vertex> &at, const ChamferRule &rule, const std::vector<std::string> &ids,
                                  const std::vector<Properties> &given) override {
            return Cornered("Chamfer", [&] { BasicSketch::Chamfer(at, rule, ids, given); });
        }
        // as BasicSketch.Connect: the blend follows the elements it joins, through their unknowns
        ConstraintSketch &Connect(const std::string &a, const std::string &b, sketch::Kind continuity, bool reverse, const Options &opt) override {
            BasicSketch::Connect(a, b, continuity, reverse, opt);
            analysis.reset();
            return *this;
        }

        // the mirrors, patterns and offsets whose copies of element e follow it, by their ids
        std::string CopiesOf(size_t e) const {
            std::set<std::string> out;
            for (const auto &[o, info] : offsetInfos)
                if (info.HasSource(elementIds[e]))
                    out.insert(o);
            for (const auto &[pattern, info] : patterns)
                if (info.HasOriginal(elementIds[e]))
                    out.insert(pattern);
            for (const auto &[scope, m] : mirrors)
                if (m.tie && byId.contains(scope + "." + elementIds[e]))
                    out.insert(scope);
            return Joined({out.begin(), out.end()});
        }
        // the ids with every wire id replaced by its elements, an offset's id by its lines, arcs and circles
        std::vector<std::string> Expanded(const std::vector<std::string> &ids) const override {
            std::vector<std::string> out;
            for (const std::string &id : ids)
                if (const auto o = offsetInfos.find(id); o != offsetInfos.end()) {
                    for (const std::string &e : o->second.elements)
                        if (const auto at = byId.find(e); at != byId.end() && s.elements[at->second.index].kind != sketch::ElementKind::Point)
                            out.push_back(e);
                } else {
                    const std::vector<std::string> more = BasicSketch::Expanded({id});
                    out.insert(out.end(), more.begin(), more.end());
                }
            return out;
        }

        void RequireConstraints(std::string_view) const override {}

        // strict mode: call raises while there are degrees of freedom, naming what can still move (lines, arcs, ...
        // and points no element runs through)
        void RequireFull(const std::string &call) override {
            for (const auto &[id, with] : conflicts)
                Warning() << call << ": " << id << " (" << Text(s.constraints[byId.at(id).index]) << ") is suspended, it contradicts the other constraints; "
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
                const sketch::Element &e = s.elements[byId.at(id).index];
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

        // the last constraint goes when the element does, although it does not refer to it (the equal radii of a
        // symmetry line)
        void TieLastConstraintTo(const std::string &element) { companions[element].push_back(constraintIds.back()); }
        // refs: ids, "id.start" / ".end" / ".center" / ".mid"
        ConstraintSketch &Constrain(const std::vector<std::string> &refs, sketch::Kind kind, std::optional<double> value,
                                    const std::optional<std::string> &id, bool reference = false) {
            if (value)
                IsFinite(sketch::KindName(kind), {*value});
            cs::Constraint c{kind};
            for (const std::string &r : refs)
                c.refs.push_back(RefOf(r));
            c.reference = reference;
            if (reference && (!HasValue(kind) || value))
                throw std::invalid_argument(Text(c) + (value ? ": a reference measures, it takes no value" : ": only dimensions can be references"));
            if (!reference && HasValue(kind) != value.has_value())
                throw std::invalid_argument(Text(c) + (value ? ": takes no value" : ": needs a value"));
            c.value = value.value_or(0.0);
            if (kind == sketch::Kind::Fix || kind == sketch::Kind::RigidGroup)
                for (size_t k = 0; k < refs.size(); ++k)
                    if (c.refs[k].part == sketch::Part::Whole && s.elements[c.refs[k].element].kind == sketch::ElementKind::OffsetCurve)
                        throw std::invalid_argument(Text(c) + ": '" + refs[k] + "' is an offset curve, it follows its source; " +
                                                    (kind == sketch::Kind::Fix ? "fix" : "group") + " its source and the offset's distance instead");
            Atomic([&] {
                RetrySuspended();
                AddConstraint(c, id, std::nullopt, true, true);
            });
            return *this;
        }

        // an equation between dimensions (nodes over the names: Value k the k-th name) as one more equation of the
        // system, solved with the rest; measures get automatic reference dimensions (Reference.N). drives: its left side
        // if that is a dimension alone, which it then drives, unless it is a reference
        ConstraintSketch &AddExpression(const std::string &text, const std::vector<cs::Node> &nodes, const std::vector<cs::ExpressionName> &names,
                                        const std::optional<std::string> &drives, const std::optional<std::string> &id) {
            const std::string call = "Expression '" + text + "'";
            Flush();
            const std::vector<double> lengths = LengthValues();
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
        // the dimensions a curve's formula names (formula.ids): their constraints and values; each must drive
        void CurveDimensions(sketch::CurveFormula &formula) {
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
        // the dimensions a curve's formula cannot use, each with why: references and the driven ones
        std::vector<std::pair<std::string, std::string>> DrivenDimensions() const {
            std::vector<std::pair<std::string, std::string>> out;
            for (size_t k = 0; k < s.constraints.size(); ++k) {
                if (!HasValue(s.constraints[k].kind))
                    continue;
                if (s.constraints[k].reference)
                    out.push_back({constraintIds[k], "a reference dimension, it measures the solved geometry"});
                else if (const std::optional<size_t> e = s.DriverOf(k))
                    out.push_back({constraintIds[k], "driven by " + constraintIds[*e] + " (" + s.constraints[*e].text + ")"});
            }
            return out;
        }
        // the curve whose formula uses the dimension k, if any
        std::optional<std::string> CurveUsing(size_t k) const {
            for (size_t e = 0; e < s.elements.size(); ++e)
                if (const sketch::Element &el = s.elements[e]; el.IsFitted(s))
                    if (const std::optional<sketch::CurveFormula> &f = s.splines[el.SplineIndex()].fitted->formula;
                        f && std::find(f->dimensions.begin(), f->dimensions.end(), k) != f->dimensions.end())
                        return elementIds[e];
            return std::nullopt;
        }
        // the expression id goes; a dimension it drove keeps its value as its own
        ConstraintSketch &RemoveExpression(const std::string &id) {
            const auto it = byId.find(id);
            if (it == byId.end() || it->second.isElement || s.constraints[it->second.index].kind != sketch::Kind::Expression)
                throw std::invalid_argument("RemoveExpression('" + id + "'): no such expression");
            return Remove(id, "RemoveExpression('" + id + "')");
        }
        cs::ExpressionNames ExpressionNames() const {
            cs::ExpressionNames out;
            for (size_t c = 0; c < s.constraints.size(); ++c) {
                const sketch::Kind k = s.constraints[c].kind;
                if (HasValue(k))
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

        // the relations of o between the elements isNew marks (not projected ones) and the others, never with the origin
        // or the axes; tol: an absolute length, none for rounding
        std::vector<sketch::FoundRelation> AutoRelations(const AutoConstraint &o, std::vector<char> isNew, std::optional<double> tol = std::nullopt) const {
            std::vector<char> skip(s.elements.size(), 0);
            for (const char *id : {"origin", "H", "V"})
                if (const auto at = byId.find(id); at != byId.end() && at->second.isElement)
                    skip[at->second.index] = 1;
            for (size_t k = 0; k < s.elements.size(); ++k)
                if (isNew[k] && s.elements[k].IsHeld(s))
                    isNew[k] = 0;
            return sketch::RelationFinder(s, o, std::move(isNew), std::move(skip), tol).Find();
        }
        // relations found in bulk (copied or added geometry): added without a solve each, one solve, then the ones that
        // follow from the others left out quietly
        void AddRelations(const std::vector<sketch::FoundRelation> &found, const char *call = "auto_constraint") {
            if (found.empty())
                return;
            Atomic([&] {
                const std::set<std::string> have = ConstraintKeys();
                for (const sketch::FoundRelation &r : found) {
                    if (have.contains(KeyOf(r.kind, r.refs)))
                        continue;
                    const size_t k = s.Add({r.kind, r.refs});
                    const std::string name = NewId(sketch::KindName(r.kind), std::nullopt);
                    constraintIds.push_back(name);
                    byId[name] = {false, k};
                    impliedIds.insert(name);
                }
                analysis.reset();
                if (!s.Solve().solved)
                    throw std::invalid_argument(std::string(call) + ": the relations found have no solution");
                DropRedundantImplied();
            });
        }
        // the elements from first on came in at once (Add): their relations in bulk, none found again when the call ends
        void RelateAdded(size_t first) {
            if (autoConstraint) {
                std::vector<char> isNew(s.elements.size(), 0);
                std::fill(isNew.begin() + (std::ptrdiff_t)std::min(first, isNew.size()), isNew.end(), 1);
                AddRelations(AutoRelations(*autoConstraint, std::move(isNew)));
            }
            callElements = s.elements.size();
            removedInCall = 0;
            touched.clear();
        }
        // a constraint's kind and references in any order, to find the same relation again
        static std::string KeyOf(sketch::Kind kind, std::vector<sketch::Ref> refs) {
            std::sort(refs.begin(), refs.end(), [](const sketch::Ref &a, const sketch::Ref &b) {
                return std::tie(a.element, a.part, a.index) < std::tie(b.element, b.part, b.index);
            });
            std::string key = std::to_string((int)kind);
            for (const sketch::Ref &r : refs)
                key += "," + std::to_string(r.element) + ":" + std::to_string((int)r.part) + ":" + std::to_string(r.index);
            return key;
        }
        std::set<std::string> ConstraintKeys() const {
            std::set<std::string> out;
            for (const cs::Constraint &c : s.constraints)
                out.insert(KeyOf(c.kind, c.refs));
            return out;
        }

        // a constraint a drawing call implies (its helpers, auto_constraint): left out quietly when it follows from the
        // others already (it takes no degree of freedom; the drawing is what the user drew); the implied ones it makes
        // redundant are removed
        ConstraintSketch &ConstrainImplied(const std::vector<std::string> &refs, sketch::Kind kind) {
            cs::Constraint c{kind};
            for (const std::string &r : refs)
                c.refs.push_back(RefOf(r));
            return ConstrainImplied(std::move(c));
        }
        ConstraintSketch &ConstrainImplied(const cs::Constraint &c) {
            const size_t dof = Analysis(false).dof;
            AtomicIf([&] {
                if (AddConstraint(c, std::nullopt, dof))
                    return false;
                const std::string added = constraintIds.back();
                DropRedundantImplied();
                impliedIds.insert(added);
                return true;
            });
            return *this;
        }

        double Value(const std::string &id) {
            if (const std::optional<std::string> chamfer = ReverseOf(id)) {
                for (const auto &[i, reverse] : pending.flips)
                    if (i == *chamfer)
                        return reverse ? 1.0 : 0.0;
                return chamfers.at(*chamfer).reverse ? 1.0 : 0.0;
            }
            if (const std::optional<std::string> pattern = CountOf(id)) {
                for (const auto &[i, n] : pending.counts)
                    if (i == id)
                        return (double)n;
                return (double)CountNow(*pattern, id.ends_with("2"));
            }
            const size_t k = DimensionAt(id);
            if (s.constraints[k].reference || s.DriverOf(k)) {
                Flush();
                return DimensionValue(DimensionAt(id));
            }
            for (const auto &[i, v] : pending.values)
                if (i == id)
                    return v;
            return s.constraints[k].value;
        }
        // every dimension (references measured): {id: value}
        std::vector<std::pair<std::string, double>> Dimensions() {
            Flush();
            std::vector<std::pair<std::string, double>> out;
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (HasValue(s.constraints[c].kind))
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
        void SetValue(const std::string &id, double v) {
            IsFinite(id.c_str(), {v});
            if (const std::optional<std::string> chamfer = ReverseOf(id)) {
                if (v != 0.0 && v != 1.0)
                    throw std::invalid_argument(id + " = " + NumberText(v) + ": reverse is True (1) or False (0)");
                std::erase_if(pending.flips, [&](const auto &x) { return x.first == *chamfer; });
                pending.flips.push_back({*chamfer, v == 1.0});
                analysis.reset();
                return;
            }
            if (CountOf(id)) {
                if (!(v >= 1.0) || v != std::floor(v))
                    throw std::invalid_argument(id + " = " + NumberText(v) + ": a count is a whole number of instances, the original included");
                std::erase_if(pending.counts, [&](const auto &x) { return x.first == id; });
                pending.counts.push_back({id, (size_t)v});
                analysis.reset();
                return;
            }
            const size_t k = DimensionAt(id);
            if (s.constraints[k].kind == sketch::Kind::Parameter)
                throw std::invalid_argument(id + " = " + NumberText(v) + ": " + id + " is a global parameter (px.Parameters); its Update sets it in "
                                            "every sketch that uses it");
            if (const std::optional<size_t> e = s.DriverOf(k))
                throw std::invalid_argument(id + " = " + NumberText(v) + ": " + id + " is driven by " + constraintIds[*e] + " (" + s.constraints[*e].text +
                                            "); remove the expression to set it");
            if (s.constraints[k].reference)
                throw std::invalid_argument(id + " = " + NumberText(v) + ": a reference dimension measures, it is not set");
            if (const std::optional<std::string> wrong = OffsetDistanceProblem(k, v))
                throw std::invalid_argument(id + " = " + NumberText(v) + ": " + *wrong);
            if (const std::optional<std::string> wrong = KeyholeProblem(k, v))
                throw std::invalid_argument(id + " = " + NumberText(v) + ": " + *wrong);
            if (s.Sideless(s.constraints[k]) && !(v > 0.0))
                throw std::invalid_argument(id + " = " + NumberText(v) + ": a length or radius must be positive");
            if (const char *outside = s.SweepOutside(s.constraints[k], v))
                throw std::invalid_argument(id + " = " + NumberText(v) + ": " + outside);
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
        // the values set, or none of them if one is refused
        void SetValues(const std::vector<std::pair<std::string, double>> &values) {
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
        ConstraintSketch &Update(const std::vector<std::pair<std::string, double>> &values = {}, size_t steps = 1) {
            UpdateValues(values, steps);
            RetrySuspended();
            return *this;
        }
        ConstraintSketch &UpdateValues(const std::vector<std::pair<std::string, double>> &values, size_t steps) {
            SetValues(values);
            if (!pending.flips.empty())
                Reversed();
            if (pending.counts.empty())
                return UpdateDimensions(steps);
            // copies come and go: on any failure everything is as before
            ConstraintSketch before = *this;
            try {
                ApplyCounts(steps);
            } catch (...) {
                *this = std::move(before);
                pending.Clear();
                analysis.reset();
                throw;
            }
            analysis.reset();
            RewriteExpressions();
            return *this;
        }
        ConstraintSketch &UpdateDimensions(size_t steps) {
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
            const std::vector<double> values0 = s.values, lengths = LengthValues();
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
            throw std::invalid_argument("Update(" + text + "): no solution, nothing changed" + hint);
        }
        // how far a walk to new values got (1: all the way), the branch changes it warns about, the one it refused
        struct Walk {
            double reached = 0.0;
            std::vector<std::string> branchChanges;
            std::optional<std::string> refused;
        };
        // the dimensions change[k].first walked from from[k] to change[k].second, in steps of at least 1 / steps
        Walk WalkTo(const std::vector<std::pair<size_t, double>> &change, const std::vector<double> &from, size_t steps) {
            Walk walk;
            // the smallest step tried: there a solution is taken as it comes, a branch change warned about (or refused)
            constexpr double finest = 1.0 / 4096.0;
            double &t = walk.reached;
            double h = 1.0 / (double)(std::max<size_t>(steps, 1));
            const std::vector<double> origin = s.values;
            while (t < 1.0) {
                const double next = std::min(1.0, t + h);
                for (size_t k = 0; k < change.size(); ++k)
                    s.constraints[change[k].first].value = next == 1.0 ? change[k].second : from[k] + (change[k].second - from[k]) * next;
                const std::vector<double> before = s.values, headed = Headings();
                const double size = s.Size();
                const SolveResult r = s.Solve(true, &origin);
                double moved = 0.0;
                for (size_t u = 0; u < before.size(); ++u)
                    moved = std::max(moved, std::abs(s.values[u] - before[u]) / s.scale[u]);
                const auto [turned, element] = Turned(headed, Headings());
                const bool steady = r.predicted <= 0.25 * size && moved <= 2.0 * r.predicted + Tol(size) && turned <= smoothTurn;
                if (r.solved && (steady || h <= finest)) {
                    if (turned > branchTurn) {
                        const std::string what = elementIds[element] + " turned round (" + std::to_string(std::lround(Degrees(turned))) +
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
        void Flush() override {
            if (!pending.IsEmpty())
                Update();
        }

        // every constraint with the ids of what it relates ("Line.1", "Line.1.end", "H") and whether it is redundant
        std::vector<cs::ConstraintInfo> Infos() {
            const std::vector<char> redundant = s.Redundancies(Analysis(false));
            const std::set<size_t> over = OverConstraints();
            std::vector<cs::ConstraintInfo> out;
            for (size_t c = 0; c < s.constraints.size(); ++c) {
                const cs::Constraint &k = s.constraints[c];
                if (!IsListed(k))
                    continue;
                cs::ConstraintInfo i{constraintIds[c], sketch::KindName(k.kind), Text(k), {}, std::nullopt, bool(redundant[c]), k.reference};
                i.over = over.contains(c);
                for (const sketch::Ref &r : k.refs)
                    i.refs.push_back(RefName(r));
                if (HasValue(k.kind))
                    i.value = DimensionValue(c);
                out.push_back(std::move(i));
            }
            for (cs::ConstraintInfo &i : CopyEntries())
                if (i.derived && !i.tie)
                    out.push_back(std::move(i));
            return out;
        }
        static bool IsListed(const cs::Constraint &c) {
            return c.kind != sketch::Kind::Copy && c.kind != sketch::Kind::Expression && c.kind != sketch::Kind::Offset &&
                   c.kind != sketch::Kind::Parameter && !c.tie;
        }
        std::vector<cs::ConstraintEntry> Constraints() const {
            std::vector<cs::ConstraintEntry> out;
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (IsListed(s.constraints[c]))
                    out.push_back({sketch::KindName(s.constraints[c].kind), constraintIds[c], Text(s.constraints[c])});
            for (const cs::ConstraintInfo &i : CopyEntries())
                out.push_back({i.kind, i.id, i.text});
            return out;
        }
        // the expressions: (id, the text given)
        std::vector<std::pair<std::string, std::string>> Expressions() const {
            std::vector<std::pair<std::string, std::string>> out;
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (s.constraints[c].kind == sketch::Kind::Expression)
                    out.push_back({constraintIds[c], s.constraints[c].text});
            return out;
        }
        // complete false: what AddConstraint needs (ConstraintAnalysis::complete), enough for the dof and the redundant ones
        const ConstraintAnalysis &Analysis(bool complete = true) {
            Flush();
            if (!analysis || (complete && !analysis->complete)) {
                analysis = s.Analyze(complete);
                amplified = analysis->amplification;
            }
            return *analysis;
        }
        size_t Dof() { return Analysis(false).dof; }
        std::vector<cs::FreeMotion> FreeMotions() {
            Flush();
            return cs::FreeMotionFinder(s, UnknownLabels(), elementIds).Find();
        }
        // the global parameters (px.Parameters) as values its expressions and curve formulas can name: a name new to the
        // sketch joins it, one it has takes the value; a name the sketch has for anything else raises
        ConstraintSketch &UseParameters(const std::vector<std::pair<std::string, double>> &values) {
            Flush();
            for (const auto &[name, v] : values) {
                IsFinite(name.c_str(), {v});
                if (const auto it = byId.find(name); it != byId.end() && (it->second.isElement || s.constraints[it->second.index].kind != sketch::Kind::Parameter))
                    throw std::invalid_argument("Use: the sketch has " + std::string(it->second.isElement ? "an element" : "a constraint") + " '" + name +
                                                "'; a parameter's name must be free in the sketches that use it");
            }
            Atomic([&] {
                for (const auto &[name, v] : values)
                    if (!byId.contains(name)) {
                        cs::Constraint c{sketch::Kind::Parameter};
                        c.value = v;
                        AddConstraint(c, name);
                    }
            });
            return UpdateParameters(values);
        }
        // the global parameters' values, the sketch solved with them (everything as before if it has no solution)
        ConstraintSketch &UpdateParameters(const std::vector<std::pair<std::string, double>> &values) {
            Flush();
            for (const auto &[name, v] : values) {
                IsFinite(name.c_str(), {v});
                const auto it = byId.find(name);
                if (it == byId.end() || it->second.isElement || s.constraints[it->second.index].kind != sketch::Kind::Parameter)
                    throw std::invalid_argument("'" + name + "' is no global parameter of this sketch (sk.Use)");
                if (s.constraints[it->second.index].value != v)
                    pending.values.push_back({name, v});
            }
            UpdateDimensions(1);
            RetrySuspended();
            return *this;
        }
        // the relations geometric finds within tol (none: to rounding) that do not follow from the constraints, then
        // (dimensional) the dimensions that each remove one of the free motions left, valued as the geometry is
        std::vector<cs::Suggestion> SuggestConstraints(const std::optional<AutoConstraint> &geometric, bool dimensional, std::optional<double> tol) {
            Flush();
            ConstraintSketch scratch = *this;
            std::vector<cs::Suggestion> out;
            if (geometric)
                for (const cs::Constraint &c : scratch.AddedRelations(*geometric, tol, "SuggestConstraints"))
                    out.push_back(scratch.Suggested(c, ""));
            if (dimensional)
                for (const auto &[c, removes] : scratch.RemovingDimensions())
                    out.push_back(scratch.Suggested(c, removes));
            return out;
        }
        // adds what SuggestConstraints suggests; the ids of what it added
        std::vector<std::string> AutoConstrain(const std::optional<AutoConstraint> &geometric, bool dimensional, std::optional<double> tol) {
            Flush();
            const std::set<std::string> had(constraintIds.begin(), constraintIds.end());
            const std::vector<Vertex> was = PointPlaces();
            Atomic([&] {
                if (geometric)
                    AddedRelations(*geometric, tol, "AutoConstrain");
                if (dimensional)
                    AddDimensions(RemovingDimensions());
            });
            const std::vector<Vertex> now = PointPlaces();
            double moved = 0.0;
            for (size_t p = 0; p < was.size(); ++p)
                moved = std::max(moved, NormInf(now[p] - was[p]));
            if (moved > Tol(s.Size()))
                Warning() << "AutoConstrain: the relations moved the geometry by up to " << NumberText(moved);
            std::vector<std::string> out;
            for (const std::string &id : constraintIds)
                if (!had.contains(id))
                    out.push_back(id);
            return out;
        }
        // the elements keep their shape (as they are now) together, moving and turning as one
        ConstraintSketch &RigidGroup(const std::vector<std::string> &ids, const std::optional<std::string> &id) {
            if (ids.empty())
                throw std::invalid_argument("RigidGroup: no elements");
            return Constrain(Expanded(ids), sketch::Kind::RigidGroup, std::nullopt, id);
        }
        // the most a small change of the dimensions can move the geometry, relative to it (large: nearly singular)
        double Condition() { return Analysis().amplification; }
        cs::SketchJacobian Jacobian() {
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
            const std::vector<std::pair<size_t, size_t>> &held = s.ValueRows();
            out.indptr.push_back(0);
            std::vector<double> r, local;
            std::vector<std::pair<size_t, double>> entries;
            for (size_t c = 0; c < s.Count(); ++c) {
                const auto value = std::find_if(held.begin(), held.end(), [&](const std::pair<size_t, size_t> &v) { return v.first == c; });
                const std::string &id = c < s.constraints.size() ? constraintIds[c]
                                        : value != held.end()   ? constraintIds[value->second]
                                                                : elementIds[s.StructuralOwners()[c - s.constraints.size()]];
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
        std::string Mode() const { return strict ? "strict" : "normal"; }
        void SetMode(const std::string &mode) {
            if (mode != "normal" && mode != "strict")
                throw std::invalid_argument("mode='" + mode + "': 'normal' or 'strict'");
            strict = mode == "strict";
            errorOnBranchChange = errorOnBranchChange || strict;
        }
        void SetAutoConstraint(const std::optional<AutoConstraint> &on) {
            autoConstraint = on;
        }
        static constexpr const char *NotSmooth = "style 'clamped' with given tangents is not smooth for the solver (OCC scales "
                                                 "the tangents by L1 norms of Lagrange derivatives); style 'averaged' is";
        // 'over' while a constraint is suspended (it contradicts the others); else from the degrees of freedom alone: a solved sketch's redundant constraints are consistent, listed by Redundant()
        std::string Status() {
            const bool free = Analysis(false).dof > 0;
            return !conflicts.empty() ? "over" : free ? "under" : "full";
        }
        // the constraints that follow from the others (consistently, the sketch is solved)
        std::vector<std::string> Redundant() {
            const std::vector<char> redundant = s.Redundancies(Analysis(false));
            std::vector<std::string> out;
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (redundant[c] && s.constraints[c].kind != sketch::Kind::Copy && !s.constraints[c].tie)
                    out.push_back(constraintIds[c]);
            return out;
        }
        // per element: "full" (nothing of it can move), "under" or "over" (it is in a suspended constraint or in its set)
        std::vector<std::pair<std::string, std::string>> States() {
            std::vector<std::pair<std::string, std::string>> out;
            const ConstraintAnalysis &a = Analysis();
            const std::set<size_t> over = OverElements();
            for (size_t e = 0; e < s.elements.size(); ++e) {
                bool full = true;
                for (size_t u : s.elements[e].Values(s))
                    full = full && a.determined[u];
                out.push_back({elementIds[e], over.contains(e) ? "over" : full ? "full" : "under"});
            }
            return out;
        }
        size_t ConstraintCount() const { return s.constraints.size(); }

        static bool HasValue(sketch::Kind k) {
            return k == sketch::Kind::Distance || k == sketch::Kind::Radius || k == sketch::Kind::Diameter || k == sketch::Kind::Angle ||
                   k == sketch::Kind::Parameter;
        }

        // the pattern whose count id names ("LinearPattern.1.count"), if any
        std::optional<std::string> CountOf(const std::string &id) const {
            for (const std::string_view suffix : {std::string_view(".count"), std::string_view(".count2")}) {
                if (id.size() <= suffix.size() || !id.ends_with(suffix))
                    continue;
                const std::string pattern = id.substr(0, id.size() - suffix.size());
                if (const auto p = patterns.find(pattern); p != patterns.end() && (suffix == ".count" || p->second.IsGrid()))
                    return pattern;
            }
            return std::nullopt;
        }
        bool IsCount(const std::string &id) const { return CountOf(id).has_value(); }
        // the new counts: fewer copies go (the last ones, with the constraints on them; a pattern of two directions loses
        // the copies with a step beyond either count), a pattern over the full turn takes its new angle, then more copies
        // come at the steps there are
        void ApplyCounts(size_t steps) {
            const std::vector<std::pair<std::string, size_t>> changes = std::exchange(pending.counts, {});
            // per pattern its counts after the change (along dir, along dir2), in the order first changed
            std::vector<std::pair<std::string, std::array<size_t, 2>>> wanted;
            for (const auto &[count, n] : changes) {
                const std::string id = count.substr(0, count.rfind('.'));
                if (!patterns.contains(id))
                    throw std::invalid_argument(count + ": the pattern is gone");
                auto at = std::find_if(wanted.begin(), wanted.end(), [&](const auto &w) { return w.first == id; });
                if (at == wanted.end())
                    at = wanted.insert(wanted.end(), {id, {CountNow(id, false), CountNow(id, true)}});
                at->second[count.ends_with("2") ? 1 : 0] = n;
            }
            std::vector<std::pair<std::string, std::array<size_t, 2>>> grow;
            for (const auto &[id, n] : wanted) {
                cs::PatternInfo &p = patterns.at(id);
                const size_t now = CountNow(id, false), now2 = CountNow(id, true);
                if (p.IsGrid() && n[0] * n[1] < 2)
                    throw std::invalid_argument(CountText(id, n) + ": a pattern keeps the original and one copy at least");
                if (n[0] < now || (p.IsGrid() && n[1] < now2))
                    RemoveCopies(id, n);
                if (n[0] > now || (p.IsGrid() && n[1] > now2))
                    grow.push_back({id, n});
                if (p.IsGrid()) {
                    p.count = n[0];
                    p.count2 = n[1];
                }
                if (const std::string angle = id + ".angle"; p.equal && n[0] != now && byId.contains(angle) && !s.DriverOf(DimensionAt(angle))) {
                    std::erase_if(pending.values, [&](const auto &x) { return x.first == angle; });
                    pending.values.push_back({angle, 360.0 / (double)n[0]});
                }
            }
            UpdateDimensions(steps);
            for (const auto &[id, n] : grow) {
                cs::PatternInfo &p = patterns.at(id);
                std::vector<size_t> originals;
                for (const std::string &o : p.originals)
                    if (const auto at = byId.find(o); at != byId.end() && at->second.isElement)
                        originals.push_back(at->second.index);
                if (originals.empty())
                    throw std::invalid_argument(CountText(id, n) + ": the elements it copies are gone");
                const size_t pattern = byId.at(id).index;
                const double value = s.values[s.elements[pattern].Step()];
                const double step = p.linear ? value : Degrees(value);
                const sketch::Ref r = RefOf(p.ref);
                for (const auto &[k, j] : PatternSteps(n[0], p.IsGrid() ? n[1] : 0))
                    if (std::find(p.steps.begin(), p.steps.end(), std::pair{k, j}) == p.steps.end())
                        PlaceTiedCopy(originals, r, p.linear, step, k, pattern, p, j);
            }
        }
        // a pattern's number of instances along dir (second: along dir2, 1 for a pattern of one direction)
        size_t CountNow(const std::string &id, bool second) const {
            const cs::PatternInfo &p = patterns.at(id);
            if (!p.IsGrid())
                return second ? 1 : p.copies.size() + 1;
            return second ? p.count2 : p.count;
        }
        std::string CountText(const std::string &id, const std::array<size_t, 2> &n) const {
            return id + ".count = " + std::to_string(n[0]) + (patterns.at(id).IsGrid() ? ", " + id + ".count2 = " + std::to_string(n[1]) : "");
        }
        // the copies beyond the counts n go, from the last made on (one direction: the counters as before them, so that
        // they come back with the same ids)
        void RemoveCopies(const std::string &id, const std::array<size_t, 2> &n) {
            cs::PatternInfo &p = patterns.at(id);
            const std::string text = CountText(id, n);
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
                        std::erase_if(counters, [&](const auto &c) { return c.first.starts_with(id + "."); });
                        counters.insert(p.before[ci].first.begin(), p.before[ci].first.end());
                        p.ties = p.before[ci].second;
                    }
                    p.before.erase(p.before.begin() + (std::ptrdiff_t)ci);
                }
                std::set<size_t> mine;
                for (const std::string &e : elements)
                    if (const auto at = byId.find(e); at != byId.end() && at->second.isElement)
                        mine.insert(at->second.index);
                for (size_t c = 0; c < s.constraints.size(); ++c)
                    if (std::find(ties.begin(), ties.end(), constraintIds[c]) == ties.end() &&
                        std::any_of(s.constraints[c].refs.begin(), s.constraints[c].refs.end(), [&](const sketch::Ref &r) { return mine.contains(r.element); }))
                        dropped.push_back(constraintIds[c]);
                for (auto e = elements.rbegin(); e != elements.rend(); ++e)
                    if (const auto at = byId.find(*e); at != byId.end() && at->second.isElement) {
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

        // its distance and the unknowns of its source, as the check compares them; empty if a source element is gone
        std::vector<double> OffsetState(const std::string &name, const cs::OffsetInfo &info) const {
            const auto at = byId.find(name);
            if (at == byId.end())
                return {};
            std::vector<double> out{s.values[s.elements[at->second.index].Distance()]};
            for (const std::string &id : info.sources) {
                const auto e = byId.find(id);
                if (e == byId.end())
                    return {};
                for (size_t u : s.elements[e->second.index].Values(s))
                    out.push_back(s.values[u]);
            }
            return out;
        }
        // rule 2 of 0088: an offset keeps the shape it was made with. The first offset whose exact offset now (its source
        // and distance as they are) has another shape: what changes; offsets unchanged since their last check are skipped
        std::optional<std::string> OffsetProblem() const {
            for (const auto &[name, info] : offsetInfos) {
                const std::vector<double> state = OffsetState(name, info);
                if (state.empty() || state == info.checked)
                    continue;
                const std::string call = "Offset('" + name + "')", again = "; make the offset again (Remove('" + name + "'), then Offset)";
                const double d = state.front();
                if (!(d > 0.0))
                    return call + ": its distance d = " + NumberText(d) + " is no longer positive" + again;
                sketch::OffsetChain source{{}, info.forward, info.closed};
                for (const std::string &id : info.sources)
                    source.elements.push_back(byId.at(id).index);
                const size_t n = source.elements.size();
                // where chain element k starts (end: ends) along the chain
                const auto pointAt = [&](size_t k, bool end) {
                    const std::array<size_t, 2> ends = s.elements[source.elements[k]].Ends(s);
                    return s.coordsOf[ends[end == bool(source.forward[k]) ? 1 : 0]];
                };
                if (s.elements[source.elements.front()].kind != sketch::ElementKind::Circle)
                    for (size_t k = 0; k + (source.closed ? 0 : 1) < n; ++k)
                        if (pointAt(k, true) != pointAt((k + 1) % n, false))
                            return call + ": its source is no longer one chain (" + elementIds[source.elements[k]] + " and " +
                                   elementIds[source.elements[(k + 1) % n]] + " came apart)" + again;
                for (const auto &[curve, id] : info.curves) {
                    std::optional<std::string> wrong = CurveCusp(curve, id, d);
                    if (!wrong)
                        wrong = CurveCrossing(curve, id);
                    if (wrong)
                        return call + ": " + *wrong + again;
                }
                try {
                    const auto sides = SidesOf(source, SourceEdges(source), d, info.towards, info.forms, call);
                    if (sketch::ShapeOf(sides) == info.shape) {
                        info.checked = state;
                        continue;
                    }
                    return call + ": " + ShapeChange(info, sketch::SidePieces(sides), source, d) + again;
                } catch (const std::invalid_argument &x) {
                    const std::string what = x.what();
                    return (what.starts_with(call + ": ") ? what : call + ": " + what) + again;
                }
            }
            return std::nullopt;
        }
        // what changes between the pieces an offset was made with and those it would have now
        std::string ShapeChange(const cs::OffsetInfo &info, const std::set<std::tuple<int, size_t, int>> &now, const sketch::OffsetChain &source, double d) const {
            const size_t n = source.elements.size();
            const auto id = [&](size_t k) { return elementIds[source.elements[k % n]]; };
            const auto radius = [&](size_t k) { return s.values[s.elements[source.elements[k]].Radius()]; };
            for (const auto &[kind, k, side] : info.pieces)
                if (!now.contains({kind, k, side})) {
                    if (kind == (int)OffsetPiece::Kind::Vertex)
                        return "the corner where " + id(k) + " starts would vanish";
                    if (kind == (int)OffsetPiece::Kind::Arc && radius(k) <= d)
                        return "the offset of " + id(k) + " would vanish at d = " + NumberText(d) + (d > radius(k) ? " > r = " : " = r = ") + NumberText(radius(k));
                    return "the offset of " + id(k) + " would vanish";
                }
            for (const auto &[kind, k, side] : now)
                if (!info.pieces.contains({kind, k, side})) {
                    if (kind == (int)OffsetPiece::Kind::Vertex)
                        return "a corner would appear where " + id(k) + " starts";
                    if (kind == (int)OffsetPiece::Kind::Arc && radius(k) > d)
                        return "the offset of " + id(k) + " would come back (r = " + NumberText(radius(k)) + " > d = " + NumberText(d) + ")";
                    return "the offset of " + id(k) + " would come back";
                }
            return "its pieces would meet otherwise";
        }
        // setting the distance of an offset (dimension k) to v: not to 0 or less (its side is fixed), and not so far that one of
        // its arcs would vanish (the solve could not get there)
        std::optional<std::string> OffsetDistanceProblem(size_t k, double value) const {
            const cs::Constraint &c = s.constraints[k];
            if ((c.kind != sketch::Kind::Distance && c.kind != sketch::Kind::Diameter) || c.refs.size() != 1 || !c.refs[0].IsOffset(s))
                return std::nullopt;
            const double v = c.kind == sketch::Kind::Diameter ? value / 2.0 : value;
            const std::string &name = elementIds[c.refs[0].element];
            const auto info = offsetInfos.find(name);
            const bool slot = c.kind == sketch::Kind::Diameter;
            const std::string again = "; make the offset again (Remove('" + name + "'), then Offset)";
            if (!(v > 0.0))
                return slot ? "the width of a slot is positive" : "the distance of an offset is positive, its side is fixed" + again + " for the other side";
            if (info == offsetInfos.end())
                return std::nullopt;
            for (const auto &[arc, sign] : info->second.rounds)
                if (const auto at = byId.find(arc); at != byId.end() && sign < 0) {
                    const double r = s.values[s.elements[at->second.index].Radius()];
                    if (r - v <= Tol(std::max(r, v)) && slot)
                        return "the slot's width must be below 2 r = " + NumberText(2.0 * r) + " of its spine arc (" + arc + ")";
                    if (r - v <= Tol(std::max(r, v)))
                        return "Offset('" + name + "'): the offset of " + arc + " would vanish at d = " + NumberText(v) + (v > r ? " > r = " : " = r = ") +
                               NumberText(r) + again;
                }
            for (const auto &[curve, id] : info->second.curves)
                if (const std::optional<std::string> cusp = CurveCusp(curve, id, v))
                    return "Offset('" + name + "'): " + *cusp + again;
            return std::nullopt;
        }
        // setting a keyhole's width (the Diameter of its offset) or hole radius (the Radius of its hole arc, id.r) to value
        // (a width as the width 2 d): r above w / 2 and the spine longer than where the sides meet the hole
        std::optional<std::string> KeyholeProblem(size_t k, double value) const {
            const cs::Constraint &c = s.constraints[k];
            const bool width = c.kind == sketch::Kind::Diameter && c.refs.size() == 1 && c.refs[0].IsOffset(s);
            const bool radius = c.kind == sketch::Kind::Radius && constraintIds[k].ends_with(".r");
            if (!width && !radius)
                return std::nullopt;
            const std::string name = width ? elementIds[c.refs[0].element] : constraintIds[k].substr(0, constraintIds[k].size() - 2);
            if (!byId.contains(name + ".hole"))
                return std::nullopt;
            const double w = width ? value : 2.0 * s.values[s.elements[byId.at(name).index].Distance()];
            const double r = width ? s.values[s.elements[byId.at(name + ".hole").index].Radius()] : value;
            return HoleProblem(w, r, s.elements[byId.at(name + ".spine").index].Length(s));
        }
        // the offset id (an element or a tie) is part of
        std::optional<std::string> OffsetOf(const std::string &id) const {
            for (const auto &[o, info] : offsetInfos)
                if (info.Owns(id))
                    return o;
            return std::nullopt;
        }

        // the relations auto constraints find, none when off
        std::optional<AutoConstraint> autoConstraint;
        // the amplification of the last analysis: a new constraint warns when it takes it over config::sensitivity_warning
        double amplified = 0.0;
        // a solve that turns a line round or an arc to its other side left the branch the sketch was drawn in: an error
        // (and nothing changes) instead of a warning
        bool errorOnBranchChange = false;
        // the exports raise while the sketch can still move
        bool strict = false;

    protected:
        // the dimensions the names of an expression name (automatic references for measures), each with its value as an
        // unknown
        std::vector<size_t> LinkedDimensions(const std::vector<cs::ExpressionName> &names) {
            std::vector<size_t> dimensions;
            for (const cs::ExpressionName &n : names)
                dimensions.push_back(n.measure.empty() ? DimensionAt(n.id) : MeasureReference(n.id, n.measure));
            for (size_t k : dimensions)
                if (s.constraints[k].valueAt == sketch::none)
                    s.Link(k, DimensionValue(k));
            analysis.reset();
            return dimensions;
        }
        // the expression's constraint over the value unknowns of dimensions (one per name)
        cs::Constraint ExpressionOf(const std::string &text, const std::vector<cs::Node> &nodes, const std::vector<cs::ExpressionName> &names,
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
                    c.written.push_back({names[n].id + "." + names[n].measure, names[n].measure, d.valueAt, MeasuredRef(d).element});
                }
            return c;
        }
        // per unknown (reference, coordinate): the first element (in the order drawn) other than a point element that has
        // it, a lone point's point element, else its constraint, else ("", its index)
        std::vector<std::pair<std::string, std::string>> UnknownLabels() const {
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
                        give(u[k], elementIds[e] + parts[k].first, parts[k].second);
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

        template <class F>
        ConstraintSketch &Cornered(const char *call, F cut) {
            Flush();
            sketch::Undoable(*this, [&] {
                const std::map<size_t, std::string> held = ValueHolders();
                cut();
                Prune(call, held);
                if (const std::optional<std::string> wrong = OffsetProblem())
                    throw std::invalid_argument(std::string(call) + ": " + *wrong);
            });
            analysis.reset();
            return *this;
        }
        void CheckNotOffset(const sketch::CornerParts &c, const char *call) const {
            for (size_t arm : c.arms)
                if (const std::optional<std::string> owner = OffsetOf(elementIds[arm]))
                    throw std::invalid_argument(std::string(call) + ": '" + elementIds[arm] + "' is part of the offset '" + *owner + "'; Remove('" + *owner + "')");
        }
        // the arc is tangent to both elements at its ends (they share the end points) and has the radius r
        void CornerMade(const FilletRule &rule, const sketch::CornerParts &c) override {
            CheckNotOffset(c, "Fillet");
            FixCorners();
            AddConstraint({sketch::Kind::Tangent, {{c.arms[0]}, {c.made}}}, std::nullopt);
            AddConstraint({sketch::Kind::Tangent, {{c.made}, {c.arms[1]}}}, std::nullopt);
            AddConstraint({sketch::Kind::Radius, {{c.made}}, rule.r}, elementIds[c.made] + ".r");
        }

        // the fillet's arc, tangent to both elements with the radius r, stays as a hidden helper (id.arc, its centre point
        // hidden too) that sets where the blend starts and ends; the blend between its ends is the fillet, with its id,
        // properties and the dimension id.r
        void CornerMade(const CurvatureFilletRule &rule, const sketch::CornerParts &c) override {
            CheckNotOffset(c, "Fillet");
            CheckBlendArms({c.arms[0], c.arms[1]}, "Fillet");
            FixCorners();
            const std::string id = elementIds[c.made];
            const sketch::Element arc = s.elements[c.made];
            const size_t centre = pointElementOf.at(arc.Center());
            Rename(c.made, id + ".arc");
            roles[c.made] = sketch::Role::Helper;
            roles[centre] = sketch::Role::Helper;
            AddElement(s.AddBlend(arc.Start(), arc.End(), {{c.arms[0], c.arms[1]}, c.made, true}), "Fillet", {id, props[c.made], false});
            AddConstraint({sketch::Kind::Tangent, {{c.arms[0]}, {c.made}}}, std::nullopt);
            AddConstraint({sketch::Kind::Tangent, {{c.made}, {c.arms[1]}}}, std::nullopt);
            AddConstraint({sketch::Kind::Radius, {{c.made}}, rule.r}, id + ".r");
        }

        // the chamfer whose reverse the id names ("Chamfer.1.reverse"), if any
        std::optional<std::string> ReverseOf(const std::string &id) const {
            constexpr std::string_view suffix = ".reverse";
            if (id.size() <= suffix.size() || !id.ends_with(suffix))
                return std::nullopt;
            const std::string chamfer = id.substr(0, id.size() - suffix.size());
            return chamfers.contains(chamfer) ? std::optional<std::string>{chamfer} : std::nullopt;
        }
        // d on the first arm, then d2 on the second or the angle to the first arm, as the dimensions id.d and id.d2 / id.angle;
        // an angle is the turn from the first arm, towards the corner, to the chamfer, so the line pair is given in the order
        // that makes it positive
        void ChamferDimensions(const std::string &id, const cs::ChamferInfo &info, double d, double second) {
            const size_t f = info.reverse ? 1 : 0, o = 1 - f;
            const sketch::Ref corner = RefOf(info.corner[f]), cut = RefOf(info.cut[f]), other = RefOf(info.cut[o]);
            AddConstraint({sketch::Kind::Distance, {corner, cut}, d}, id + ".d");
            if (!info.angled) {
                AddConstraint({sketch::Kind::Distance, {RefOf(info.corner[o]), other}, second}, id + ".d2");
                return;
            }
            const size_t line = RefOf(info.arm[f]).element, chamfer = byId.at(id).index;
            if (s.elements[line].kind != sketch::ElementKind::Line)
                throw std::invalid_argument(id + ".angle: the first element, '" + info.arm[f] + "', is not a line; the angle is to a line (give d2 instead)");
            const Vertex p = corner.PointValue(s), q = cut.PointValue(s);
            const bool left = Cross(p - q, other.PointValue(s) - q) > 0.0;
            AddConstraint({sketch::Kind::Angle, left ? std::vector<sketch::Ref>{{line}, {chamfer}} : std::vector<sketch::Ref>{{chamfer}, {line}}, second},
                          id + ".angle");
        }
        void CornerMade(const ChamferRule &rule, const sketch::CornerParts &c) override {
            CheckNotOffset(c, "Chamfer");
            FixCorners();
            const std::string id = elementIds[c.made];
            const size_t low = rule.reverse ? 1 : 0, high = 1 - low;
            const auto text = [&](size_t k) { return std::array<std::string, 3>{elementIds[c.arms[k]], RefName(c.at[k].cut), RefName(c.at[k].corner)}; };
            const std::array<std::string, 3> armOne = text(0), armTwo = text(1);
            cs::ChamferInfo info;
            info.arm = {low == 0 ? armOne[0] : armTwo[0], high == 0 ? armOne[0] : armTwo[0]};
            info.cut = {low == 0 ? armOne[1] : armTwo[1], high == 0 ? armOne[1] : armTwo[1]};
            info.corner = {low == 0 ? armOne[2] : armTwo[2], high == 0 ? armOne[2] : armTwo[2]};
            info.reverse = rule.reverse;
            info.angled = rule.angle.has_value();
            ChamferDimensions(id, info, rule.d, rule.angle ? *rule.angle : rule.d2.value_or(rule.d));
            chamfers[id] = info;
            companions[id].push_back(id + ".d");
            companions[id].push_back(id + (info.angled ? ".angle" : ".d2"));
        }
        // the reverses set: the dimensions d and d2 / angle of the chamfer move to the other arms, with the values they
        // had, and the sketch solves again
        void ApplyFlips() {
            const std::vector<std::pair<std::string, bool>> changes = std::exchange(pending.flips, {});
            for (const auto &[id, reverse] : changes) {
                cs::ChamferInfo &info = chamfers.at(id);
                if (info.reverse == reverse)
                    continue;
                const std::string second = id + (info.angled ? ".angle" : ".d2");
                for (const std::string &dimension : {id + ".d", second}) {
                    const size_t k = DimensionAt(dimension);
                    if (s.DriverOf(k) || s.constraints[k].valueAt != sketch::none)
                        throw std::invalid_argument(id + ".reverse: " + dimension + " is in an expression; remove the expression first");
                }
                const double d = s.constraints[DimensionAt(id + ".d")].value, other = s.constraints[DimensionAt(second)].value;
                for (const std::string &dimension : {id + ".d", second}) {
                    DropConstraint(DimensionAt(dimension));
                    Reindex();
                }
                info.reverse = reverse;
                ChamferDimensions(id, info, d, other);
            }
        }
        void Reversed() {
            ConstraintSketch before = *this;
            try {
                ApplyFlips();
            } catch (...) {
                *this = std::move(before);
                pending.Clear();
                analysis.reset();
                throw;
            }
            analysis.reset();
        }

        void Relate(sketch::Kind kind, std::vector<sketch::Ref> refs, std::vector<size_t> params = {}) override {
            cs::Constraint c{kind, std::move(refs)};
            c.params = std::move(params);
            AddConstraint(std::move(c), std::nullopt);
        }
        void RelateGiven(sketch::Kind kind, std::vector<sketch::Ref> refs) override {
            AddConstraint({kind, std::move(refs)}, std::nullopt, std::nullopt, true);
        }
        void Dimension(sketch::Kind kind, std::vector<sketch::Ref> refs, double value, const std::string &id, bool inOffset) override {
            if (inOffset)
                AddUnsolved({kind, std::move(refs), value}, id);
            else
                AddConstraint({kind, std::move(refs), value}, id);
        }
        void CallStarts() override {
            Flush();
            analysis.reset();
            if (atomic == 0) {
                callElements = s.elements.size();
                removedInCall = 0;
                touched.clear();
            }
        }
        void CallEnds() override {
            RelationsOfCall();
            RewriteExpressions();
            if (!offsetInfos.empty())
                if (const std::optional<std::string> wrong = OffsetProblem())
                    throw std::invalid_argument(*wrong);
        }
        void CallUndone() override {
            constraintIds.resize(s.constraints.size());
            analysis.reset();
            touched.clear();
        }
        void Chained(size_t a, size_t b) override {
            if (autoConstraint) {
                touched.push_back(elementIds[a]);
                touched.push_back(elementIds[b]);
            }
        }
        // the relations auto constraints find between what the call drew (and the elements its chain joints join) and
        // the rest, each left out quietly where it follows from the others
        void RelationsOfCall() {
            std::vector<std::string> joints;
            joints.swap(touched);
            if (!autoConstraint)
                return;
            std::vector<char> isNew(s.elements.size(), 0);
            bool any = false;
            for (size_t k = callElements - std::min(callElements, removedInCall); k < s.elements.size(); ++k) {
                isNew[k] = 1;
                any = true;
            }
            for (const std::string &id : joints)
                if (const auto at = byId.find(id); at != byId.end() && at->second.isElement) {
                    isNew[at->second.index] = 1;
                    any = true;
                }
            if (!any)
                return;
            const std::set<std::string> have = ConstraintKeys();
            for (const sketch::FoundRelation &r : AutoRelations(*autoConstraint, std::move(isNew)))
                if (!have.contains(KeyOf(r.kind, r.refs)))
                    ConstrainImplied({r.kind, r.refs});
        }
        void SplineAdded(size_t e) override {
            const sketch::SplineData &d = s.splines[s.elements[e].SplineIndex()];
            if (d.style == SplineStyle::Clamped && !d.tangentAt.empty())
                Warning() << elementIds[e] << ": " << NotSmooth;
        }
        void Reindex() override {
            BasicSketch::Reindex();
            for (size_t c = 0; c < constraintIds.size(); ++c)
                byId[constraintIds[c]] = {false, c};
        }
        bool IsReferenced(size_t element) const override {
            for (const cs::Constraint &c : s.constraints)
                for (const sketch::Ref &r : c.refs)
                    if (r.element == element)
                        return true;
            return false;
        }

        // a constraint; an offset whole (its elements with the ties on them, then its element with its distance); an
        // element with the constraints on it. A source of an offset stays: it can become construction, or the offset
        // goes first
        void RemoveOne(const std::string &id) override {
            const auto source = [&](const std::string &part) {
                for (const auto &[o, info] : offsetInfos)
                    if (o != id && info.HasSource(part))
                        throw std::invalid_argument("Remove('" + id + "'): the offset '" + o + "' is made from it; keep it as construction with sk('" +
                                                    id + "').construction = True, or Remove('" + o + "') first");
            };
            if (const auto whole = offsetInfos.find(id); whole != offsetInfos.end()) {
                const std::vector<std::string> parts = whole->second.elements;
                for (const std::string &part : parts)
                    source(part);
                offsetInfos.erase(whole);
                for (auto e = parts.rbegin(); e != parts.rend(); ++e)
                    if (byId.contains(*e))
                        RemoveOne(*e);
                if (IsSlot(id))
                    RemoveOne(id + ".spine");
            }
            const auto owned = [&](const std::string &part) {
                if (const std::optional<std::string> owner = OffsetOf(part))
                    throw std::invalid_argument("Remove('" + part + "'): part of the offset '" + *owner + "'; Remove('" + *owner + "')");
            };
            if (const auto w = wires.find(id); w != wires.end())
                for (const std::string &e : w->second) {
                    owned(e);
                    source(e);
                }
            else {
                owned(id);
                source(id);
                std::erase_if(pending.values, [&](const auto &p) { return p.first == id; });
                const auto it = byId.find(id);
                if (it == byId.end()) {
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
            if (const auto it = byId.find(id); it != byId.end() && it->second.isElement && IsBlend(it->second.index))
                if (const size_t h = s.splines[s.elements[it->second.index].SplineIndex()].blend->helper; h != sketch::none)
                    helper = elementIds[h];
            BasicSketch::RemoveOne(id);
            if (helper)
                RemoveOne(*helper);
            analysis.reset();
            RetrySuspended();
        }
        // what goes with element k: its pattern, the constraints tied to it, the constraints on it
        void ElementGoes(size_t k, const std::string &id) override {
            ++removedInCall;
            patterns.erase(id);
            chamfers.erase(id);
            std::erase_if(pending.flips, [&](const auto &x) { return x.first == id; });
            std::erase_if(pending.counts, [&](const auto &x) { return x.first == id + ".count" || x.first == id + ".count2"; });
            if (const auto tied = companions.find(id); tied != companions.end()) {
                const std::vector<std::string> others = tied->second;
                companions.erase(tied);
                for (const std::string &c : others)
                    if (const auto at = byId.find(c); at != byId.end() && !at->second.isElement)
                        RemoveOne(c);
            }
            for (size_t c = s.constraints.size(); c-- > 0;)
                for (const sketch::Ref &r : s.constraints[c].refs)
                    if (r.element == k) {
                        std::erase_if(pending.values, [&](const auto &p) { return p.first == constraintIds[c]; });
                        DropConstraint(c);
                        break;
                    }
        }

        // a fillet or chamfer keeps the constraints of the line it cuts, on its far end and the sharp corner (kept: the far end
        // as a reference to the line, then the corner on the piece cut off): its length is the distance between them, its Fix
        // becomes the Fix of its far end (the corner is fixed in CornerMade); true if constraint k is such
        bool KeptAtCorner(size_t c, const std::array<sketch::Ref, 2> &kept) {
            cs::Constraint &k = s.constraints[c];
            if (k.kind == sketch::Kind::Distance && k.refs.size() == 1) {
                k.refs = {kept[0], kept[1]};
                s.Refreeze(c);
                return true;
            }
            if (k.kind != sketch::Kind::Fix)
                return false;
            k.refs = {kept[0]};
            k.heldPoints.clear();
            k.held.clear();
            k.target.clear();
            s.Refreeze(c);
            fixedCorners.push_back({constraintIds[c], kept[1]});
            return true;
        }
        bool IsFixed(const sketch::Ref &point) const {
            return std::any_of(s.constraints.begin(), s.constraints.end(), [&](const cs::Constraint &k) {
                return k.kind == sketch::Kind::Fix && k.refs.size() == 1 && k.refs[0].IsPoint(s) && k.refs[0].PointAt(s) == point.PointAt(s);
            });
        }
        // the Fix of the corners of the elements cut, which a fillet or chamfer keeps fixed
        void FixCorners() {
            const auto corners = std::exchange(fixedCorners, {});
            for (const auto &[fix, corner] : corners)
                if (!IsFixed(corner))
                    AddConstraint({sketch::Kind::Fix, {corner}}, fix + ".corner");
        }

        // constraints on an end of the cut element a that went to another piece follow it there; of a line or arc those
        // that held its old ends go, with a warning
        void PiecesCut(size_t a, const sketch::Element &old, const std::vector<size_t> &pieces, const std::string &id, const std::string &call) override {
            const bool circle = old.kind == sketch::ElementKind::Circle, line = old.kind == sketch::ElementKind::Line;
            const std::array<size_t, 2> oldEnds = old.Ends(s);
            const auto endAt = [&](size_t point) -> std::optional<sketch::Ref> {
                for (size_t e : pieces) {
                    const std::array<size_t, 2> ends = s.elements[e].Ends(s);
                    if (ends[0] == point || ends[1] == point)
                        return sketch::Ref{e, ends[0] == point ? sketch::Part::Start : sketch::Part::End};
                }
                return std::nullopt;
            };
            std::optional<std::array<sketch::Ref, 2>> kept;
            if (cornering && line) {
                const std::optional<sketch::Ref> one = endAt(oldEnds[0]), two = endAt(oldEnds[1]);
                kept = one->element == a ? std::array<sketch::Ref, 2>{*one, *two} : std::array<sketch::Ref, 2>{*two, *one};
            }
            std::vector<size_t> drop;
            for (size_t c = 0; c < s.constraints.size(); ++c) {
                cs::Constraint &k = s.constraints[c];
                bool whole = false;
                for (sketch::Ref &r : k.refs) {
                    if (r.element != a)
                        continue;
                    if (!circle && (r.part == sketch::Part::Start || r.part == sketch::Part::End))
                        if (const std::optional<sketch::Ref> moved = endAt(oldEnds[r.part == sketch::Part::End]))
                            r = *moved;
                    whole = whole || r.part == sketch::Part::Whole;
                }
                const bool ends = k.kind == sketch::Kind::Fix || (line && (k.kind == sketch::Kind::Equal || (k.kind == sketch::Kind::Distance && k.refs.size() == 1))) ||
                                  s.IsSweep(k) ||
                                  (k.kind == sketch::Kind::Symmetric && k.refs.size() == 3 && !k.refs[0].IsPoint(s) && !k.refs[0].IsRound(s));
                if (whole && ends && !circle && old.kind != sketch::ElementKind::Spline) {
                    if (kept && KeptAtCorner(c, *kept))
                        continue;
                    drop.push_back(c);
                }
            }
            if (drop.empty())
                return;
            const std::string names = Joined(IdsOf(drop));
            for (size_t c = drop.size(); c-- > 0;)
                DropConstraint(drop[c]);
            Reindex();
            Warning() << call << ": removed " << names << ", they held the old ends of '" << id << "'";
        }

        void AddTie(cs::Constraint c) {
            c.tie = true;
            AddConstraint(std::move(c), std::nullopt);
        }
        // with symmetry every pair of points (and circle centres) is symmetric about the axis and every arc and circle
        // copy of equal radius, so the copies follow the originals
        void MirrorTied(const sketch::Copies &made, const sketch::Ref &axis) override {
            for (const auto &[o, c] : made.points)
                if (!made.placed.contains(o))
                    AddTie({sketch::Kind::Symmetric, {{o}, {c}, axis}});
            // an arc copy is held by its ends and its radius, a circle copy by its centre and its radius
            for (const auto &[o, c] : made.elements) {
                const sketch::ElementKind k = s.elements[o].kind;
                if (k == sketch::ElementKind::Circle)
                    AddTie({sketch::Kind::Symmetric, {{o, sketch::Part::Center}, {c, sketch::Part::Center}, axis}});
                if (k == sketch::ElementKind::Arc || k == sketch::ElementKind::Circle)
                    AddTie({sketch::Kind::Equal, {{o}, {c}}});
            }
            const auto none = [](size_t) { return std::optional<std::string>{}; };
            for (const auto &[o, c] : made.splines)
                TieTangents(o, c, {axis}, 0, none);
            for (const sketch::Copies::Position &x : made.positions)
                TiePosition(x, std::nullopt);
        }

        // the k-th copy of a kept pattern (the pattern element), tied through it; its ids go under the pattern's, what it
        // made goes to info
        // (in a pattern of two directions also j steps along its second, the copy's ids under both steps)
        void PlaceTiedCopy(const std::vector<size_t> &originals, const sketch::Ref &r, bool linear, double step, size_t k, size_t pattern,
                           cs::PatternInfo &info, size_t j = 0) {
            sketch::Scoped scope{idScope, std::exchange(idScope, elementIds[pattern])};
            std::map<std::string, size_t> mine;
            for (const auto &[key, n] : counters)
                if (key.starts_with(idScope + "."))
                    mine.emplace(key, n);
            info.before.push_back({std::move(mine), info.ties});
            const size_t elements = elementIds.size(), constraints = constraintIds.size();
            const std::optional<sketch::Ref> r2 = info.IsGrid() ? std::optional(RefOf(info.ref2)) : std::nullopt;
            const double step2 = info.IsGrid() ? s.values[s.elements[pattern].Step2()] : 0.0;
            const std::string prefix = StepPrefix(elementIds[pattern], k, j, info.IsGrid());
            const sketch::Copies made = PlaceCopy(originals, r, linear, step, k, prefix, r2, step2, j);
            CopyWires(info.given, made, prefix);
            const auto tie = [&] { return elementIds[pattern] + ".copy." + std::to_string(++info.ties); };
            const auto copyOf = [&](const sketch::Ref &a, const sketch::Ref &b) {
                cs::Constraint c{sketch::Kind::Copy, {a, b, {pattern}, r}};
                if (r2)
                    c.refs.push_back(*r2);
                c.variant = (int)k;
                c.variant2 = (int)j;
                AddConstraint(c, tie());
            };
            for (const auto &[a, b] : made.points)
                if (!made.placed.contains(a))
                    copyOf({a}, {b});
            for (const auto &[a, b] : made.elements) {
                const sketch::ElementKind kind = s.elements[a].kind;
                if (kind == sketch::ElementKind::Circle)
                    copyOf({a, sketch::Part::Center}, {b, sketch::Part::Center});
                if (kind == sketch::ElementKind::Arc || kind == sketch::ElementKind::Circle)
                    AddTie({sketch::Kind::Equal, {{a}, {b}}});
            }
            const auto id = [&](size_t) { return std::optional<std::string>{tie()}; };
            for (const auto &[a, b] : made.splines)
                TieTangents(a, b, r2 ? std::vector<sketch::Ref>{{pattern}, r, *r2} : std::vector<sketch::Ref>{{pattern}, r}, (int)k, id, (int)j);
            for (const sketch::Copies::Position &x : made.positions)
                TiePosition(x, id(0));
            info.copies.push_back({{elementIds.begin() + (std::ptrdiff_t)elements, elementIds.end()},
                                   {constraintIds.begin() + (std::ptrdiff_t)constraints, constraintIds.end()}});
            info.steps.push_back({k, j});
        }

        // a constraint of a call that solves once all of them are in
        void AddUnsolved(cs::Constraint c, const std::string &id) {
            CheckId(id);
            const size_t k = s.Add(std::move(c));
            constraintIds.push_back(id);
            byId[id] = {false, k};
        }
        // an offset follows its source: its element holds the distance (the dimension id.d), its points are tied
        bool TiesOffsets() const override { return true; }
        size_t OffsetElement(double ad, const Options &opt, bool slot) override {
            offsetDof = s.Analyze().dof;
            AddElement(s.AddOffset(ad), "Offset", {opt.id, {}, true});
            offsetFirstTie = s.constraints.size();
            if (slot)
                AddUnsolved({sketch::Kind::Diameter, {{s.elements.size() - 1}}, 2.0 * ad}, elementIds.back() + ".w");
            else
                AddUnsolved({sketch::Kind::Distance, {{s.elements.size() - 1}}, ad}, elementIds.back() + ".d");
            return s.elements.size() - 1;
        }
        void TieOffset(sketch::OffsetTie how, std::vector<sketch::Ref> refs, double sign, const std::string &id) override {
            cs::Constraint c{sketch::Kind::Offset, std::move(refs)};
            c.variant = (int)how;
            c.sign = sign;
            AddUnsolved(std::move(c), id);
        }
        // the ties solved once all are in: they take no degree of freedom, and the offset curves have no cusp and do not
        // cross themselves; then what the offset was made as, for the checks after later changes
        void OffsetMade(const std::string &name, const std::vector<std::string> &made, const std::vector<std::string> &tieIds, const sketch::OffsetChain &source,
                        const EdgeList &edges, double ad, const std::vector<int> &towards, const OffsetForms &forms,
                        const sketch::OffsetSides &sides,
                        const std::vector<std::pair<std::string, std::string>> &curveIds, const std::string &call) override {
            if (!s.Solve().solved) {
                if (const std::optional<std::string> cusp = ChainCusp(source, edges, ad, towards))
                    throw std::invalid_argument(call + ": " + *cusp);
                throw std::logic_error(call + ": internal error, the offset's ties have no solution");
            }
            analysis = s.Analyze();
            amplified = analysis->amplification;
            const std::vector<char> redundant = s.Redundancies(*analysis);
            if (analysis->dof != offsetDof || std::any_of(redundant.begin() + (std::ptrdiff_t)offsetFirstTie, redundant.end(), [](char x) { return x != 0; }))
                throw std::logic_error(call + ": internal error, the offset's ties are not independent");
            for (const auto &[curve, id] : curveIds) {
                std::optional<std::string> wrong = CurveCusp(curve, id, ad);
                if (!wrong)
                    wrong = CurveCrossing(curve, id);
                if (wrong)
                    throw std::invalid_argument(call + ": " + *wrong);
            }
            Sync();
            const auto element = [&](size_t k) { return source.elements[k]; };
            cs::OffsetInfo info{{}, made, tieIds, source.forward, source.closed, towards, sketch::ShapeOf(sides), sketch::SidePieces(sides), {}, {}, curveIds, forms};
            for (size_t e : source.elements)
                info.sources.push_back(elementIds[e]);
            for (const auto &[fixed, loops] : sides)
                for (const OffsetLoop &loop : loops)
                    for (const OffsetPiece &p : loop.pieces)
                        if (p.kind == OffsetPiece::Kind::Arc)
                            info.rounds.push_back({elementIds[element(p.k)], p.r > s.values[s.elements[element(p.k)].Radius()] ? 1 : -1});
            if (s.elements[element(0)].kind == sketch::ElementKind::Circle)
                for (int t : towards)
                    info.rounds.push_back({elementIds[element(0)], t});
            info.checked = OffsetState(name, info);
            offsetInfos[name] = std::move(info);
        }

        // a length in an expression does not scale with the sketch: the expressions hold again after a solve
        void ScaleInside(double f, bool scaleMaxh, bool scaleMinh) override {
            CheckScale(f);
            Flush();
            const std::optional<ConstraintSketch> saved = Expressions().empty() ? std::nullopt : std::optional<ConstraintSketch>(*this);
            BasicSketch::ScaleInside(f, scaleMaxh, scaleMinh);
            for (cs::Constraint &c : s.constraints) {
                if (HasValue(c.kind) && c.kind != sketch::Kind::Angle)
                    c.value *= f;
                const size_t points = 2 * c.heldPoints.size();
                const bool held = (c.kind == sketch::Kind::Fix || c.kind == sketch::Kind::RigidGroup) && (!c.held.empty() || points);
                for (size_t k = 0; k < c.target.size(); ++k)
                    if (!held || k < points || !s.isAngle[c.held[k - points]])
                        c.target[k] *= f;
            }
            analysis.reset();
            if (saved) {
                const std::string hint = DomainHint({});
                const bool solved = s.Solve().solved;
                const std::optional<std::string> wrong = solved ? OutOfDomain() : std::nullopt;
                if (!solved || wrong) {
                    *this = *saved;
                    throw std::invalid_argument("Scale(" + NumberText(f) + "): the expressions have no solution at the new size (a length in an "
                                                "expression does not scale), nothing changed" + (wrong ? "; " + *wrong : hint));
                }
                Sync();
            }
        }
        void MirrorMade(const std::string &scope, const std::vector<std::string> &given, const std::string &axis, bool tie, bool internal) override {
            mirrors[scope] = {given, axis, tie, internal};
        }
        // the constraints among the originals (all they relate copied under prefix) as constraints of the copies, under
        // prefix: copies that are not tied keep the shape (flip: a mirror, an angle turns the other way)
        void ConstraintsCopied(const std::string &prefix, bool flip) override {
            const size_t n = s.constraints.size();
            for (size_t c = 0; c < n; ++c)
                // one the copy made itself (a cut point on its copied spline) is there
                if (std::optional<cs::Constraint> d = CopiedConstraint(s.constraints[c], prefix, flip); d && !byId.contains(prefix + "." + constraintIds[c]))
                    AddConstraint(std::move(*d), prefix + "." + constraintIds[c]);
        }
        // c as it relates the copies under prefix, none unless it relates only copied elements
        std::optional<cs::Constraint> CopiedConstraint(const cs::Constraint &c, const std::string &prefix, bool flip) const {
            if (c.tie || c.refs.empty() || c.kind == sketch::Kind::Copy || c.kind == sketch::Kind::Expression || c.kind == sketch::Kind::Offset ||
                c.automatic)
                return std::nullopt;
            // what holds an element to the sketch's frame, which is not copied
            if (c.kind == sketch::Kind::Fix || c.kind == sketch::Kind::RigidGroup ||
                ((c.kind == sketch::Kind::Horizontal || c.kind == sketch::Kind::Vertical) && c.refs.size() == 1))
                return std::nullopt;
            cs::Constraint d = c;
            d.valueAt = sketch::none;
            for (sketch::Ref &r : d.refs) {
                const auto it = byId.find(prefix + "." + elementIds[r.element]);
                if (it == byId.end() || !it->second.isElement)
                    return std::nullopt;
                r.element = it->second.index;
            }
            if (flip && d.kind == sketch::Kind::Angle && d.refs.size() == 2)
                d.value = -d.value;
            return d;
        }
        static std::string Given(const std::vector<std::string> &ids) {
            if (ids.size() == 1)
                return "\"" + ids[0] + "\"";
            std::string out;
            for (const std::string &id : ids)
                out += (out.empty() ? "[\"" : ", \"") + id + "\"";
            return out + "]";
        }
        // what the copies of mirrors and patterns show: each copy's one tie (tie), printed like the call that made it, and
        // the constraints among the originals on the copies (derived), from the originals as they are now
        std::vector<cs::ConstraintInfo> CopyEntries() const {
            std::vector<cs::ConstraintInfo> out;
            const auto derived = [&](const std::string &prefix, bool flip) {
                for (size_t c = 0; c < s.constraints.size(); ++c)
                    if (const std::optional<cs::Constraint> d = CopiedConstraint(s.constraints[c], prefix, flip)) {
                        cs::ConstraintInfo i{prefix + "." + constraintIds[c], sketch::KindName(d->kind), Text(*d) + ", derived=True", {}, std::nullopt, false,
                               d->reference, true};
                        for (const sketch::Ref &r : d->refs)
                            i.refs.push_back(RefName(r));
                        if (HasValue(d->kind))
                            i.value = flip && d->kind == sketch::Kind::Angle && d->refs.size() == 2 ? -DimensionValue(c) : DimensionValue(c);
                        out.push_back(std::move(i));
                    }
            };
            for (const auto &[scope, m] : mirrors) {
                if (!wires.contains(scope) || !m.tie)
                    continue;
                const auto axis = byId.find(m.axis);
                out.push_back({scope, "Symmetric",
                               Given(m.given) + ", \"" + scope + "\", " + (axis != byId.end() ? RefText({axis->second.index}) : "\"" + m.axis + "\"") +
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
        // what to change instead of a copy's tie or a derived constraint, none for another id
        std::optional<std::string> CopyHint(const std::string &id) const {
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
        // what a solve must not turn round, per element: a line's or axis's direction, an arc's sweep (radians); NaN for
        // the rest and for lines too short to have a direction
        std::vector<double> Headings() const {
            const auto at = [&](size_t p) { return Vertex{s.values[s.coordsOf[p]], s.values[s.coordsOf[p] + 1]}; };
            const double tiny = config::state->branch_check_min_length * s.Size();
            std::vector<double> h(s.elements.size(), std::numeric_limits<double>::quiet_NaN());
            for (size_t e = 3; e < s.elements.size(); ++e) {
                const sketch::Element &el = s.elements[e];
                if (el.kind == sketch::ElementKind::Line) {
                    if (const Vertex d = at(el.End()) - at(el.Start()); Norm(d) > tiny)
                        h[e] = std::atan2(d.y, d.x);
                } else if (el.kind == sketch::ElementKind::Axis)
                    h[e] = s.values[el.AxisAngle()];
                else if (el.kind == sketch::ElementKind::Arc) {
                    const Vertex c = at(el.Center()), a = at(el.Start()) - c, b = at(el.End()) - c;
                    const double w = std::atan2(Cross(a, b), Dot(a, b)) * (el.ccw ? 1.0 : -1.0);
                    h[e] = w <= 0.0 ? w + 2.0 * std::numbers::pi : w;
                }
            }
            return h;
        }
        // the largest turn between two Headings() (elements made in between left out) and the element that turned
        std::pair<double, size_t> Turned(const std::vector<double> &before, const std::vector<double> &after) const {
            double most = 0.0;
            size_t which = 0;
            for (size_t e = 0; e < before.size(); ++e) {
                const double d = after[e] - before[e];
                if (std::isnan(d))
                    continue;
                const double turn = s.elements[e].kind == sketch::ElementKind::Arc ? std::abs(d) : std::abs(std::atan2(std::sin(d), std::cos(d)));
                if (turn > most) {
                    most = turn;
                    which = e;
                }
            }
            return {most, which};
        }
        // the copy's tangent directions follow the original's: mirrored at the axis in how, or turned by the
        // pattern how[0] step times (how: the pattern and its centre or direction)
        // (a fitted spline's frame: its turn as a tangent's, its place as a point's; step2: the steps along a grid's second
        // direction, how[2] its line)
        template <class Id>
        void TieTangents(size_t original, size_t copy, const std::vector<sketch::Ref> &how, int step, Id id, int step2 = 0) {
            const sketch::SplineData &a = s.splines[s.elements[original].SplineIndex()], &b = s.splines[s.elements[copy].SplineIndex()];
            std::vector<size_t> from = a.tangent, to = b.tangent;
            if (a.fitted) {
                from.push_back(a.fitted->angle);
                to.push_back(b.fitted->angle);
            }
            for (size_t t = 0; t < from.size(); ++t) {
                cs::Constraint c{sketch::Kind::Copy, {{original}, {copy}}};
                c.refs.insert(c.refs.end(), how.begin(), how.begin() + (std::ptrdiff_t)std::min<size_t>(how.size(), 2));
                c.held = {from[t], to[t]};
                c.variant = step;
                AddConstraint(c, id(t));
            }
            if (a.fitted) {
                cs::Constraint c{sketch::Kind::Copy, {{original}, {copy}}};
                c.refs.insert(c.refs.end(), how.begin(), how.end());
                c.heldPoints = {a.fitted->origin, b.fitted->origin};
                c.variant = step;
                c.variant2 = step2;
                AddConstraint(c, id(from.size()));
            }
        }
        // a copied piece ends where the original does along its spline: mirrors and patterns keep chord lengths
        void TiePosition(const sketch::Copies::Position &x, const std::optional<std::string> &id) {
            cs::Constraint c{sketch::Kind::Copy, {{x.originalElement}, {x.copyElement}}};
            c.held = {x.original, x.copy};
            AddConstraint(c, id);
        }
        size_t DimensionAt(const std::string &id) const {
            const auto it = byId.find(id);
            if (it == byId.end() || it->second.isElement) {
                if (const std::optional<std::string> hint = CopyHint(id))
                    throw std::invalid_argument("no dimension '" + id + "': " + *hint);
                throw std::invalid_argument("no dimension '" + id + "'");
            }
            const cs::Constraint &c = s.constraints[it->second.index];
            if (!HasValue(c.kind))
                throw std::invalid_argument("'" + id + "' (px." + sketch::KindName(c.kind) + ") has no value");
            return it->second.index;
        }

        // as the arguments of the Constrain call that makes it
        std::string Text(const cs::Constraint &c) const {
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
            else if (HasValue(c.kind))
                t += ", " + NumberText(driven ? s.Linked(c) : c.value);
            return t;
        }

        // a dimension's value now: what a reference measures, what drives a driven one, else its own (angles in degrees)
        double DimensionValue(size_t k) const {
            const cs::Constraint &c = s.constraints[k];
            if (c.reference)
                return s.Measured(c);
            if (s.DriverOf(k))
                return s.Linked(c);
            return c.value;
        }
        // the automatic reference dimension (Reference.N) measuring the measure of ref, made if there is none: a
        // line's length (Distance) and direction (Angle from H, the direction, not its carrier's), a radius (Radius), an
        // arc's sweep (Angle), a point's h and v (Distance from V and H, signed as coordinates)
        size_t MeasureReference(const std::string &ref, const std::string &measure) {
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
            AddConstraint(c, NewId("Reference", std::nullopt));
            s.constraints.back().automatic = true;
            s.constraints.back().sign = sign;
            return s.constraints.size() - 1;
        }
        // the dimensions expressions use: their value unknown with their id
        std::map<size_t, std::string> ValueHolders() const {
            std::map<size_t, std::string> out;
            for (size_t c = 0; c < s.constraints.size(); ++c)
                if (s.constraints[c].valueAt != sketch::none)
                    out[s.constraints[c].valueAt] = constraintIds[c];
            return out;
        }
        // after removals (by call; held: ValueHolders() before them): an expression a dimension of which went goes
        // too, with a warning; a dimension no expression uses any more keeps its value as its own, an automatic
        // reference goes
        void Prune(const std::string &call, const std::map<size_t, std::string> &held) {
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
        // the relations o finds between all elements within tol, added in bulk; the ones kept
        std::vector<cs::Constraint> AddedRelations(const AutoConstraint &o, std::optional<double> tol, const char *call) {
            const std::set<std::string> had(constraintIds.begin(), constraintIds.end());
            AddRelations(AutoRelations(o, std::vector<char>(s.elements.size(), 1), tol), call);
            std::vector<cs::Constraint> out;
            for (size_t c = 0; c < constraintIds.size(); ++c)
                if (!had.contains(constraintIds[c]))
                    out.push_back(s.constraints[c]);
            return out;
        }
        // the candidates that each remove one of the free motions, with the drag of the motion each removes
        std::vector<std::pair<cs::Constraint, std::string>> RemovingDimensions() {
            const std::vector<cs::Constraint> candidates = DimensionCandidates();
            std::vector<std::pair<cs::Constraint, std::string>> out;
            for (const auto &[k, removes] : cs::FreeMotionFinder(s, UnknownLabels(), elementIds).Removing(candidates))
                out.push_back({candidates[k], removes});
            return out;
        }
        // in the order they are tried, sizes first: every line's length, arc's radius and full circle's diameter, the
        // angle between two lines that meet; then every free point's h and v from the origin (Distance from V and H; a
        // point on an axis: Coincident with it); values as the geometry has them
        std::vector<cs::Constraint> DimensionCandidates() const {
            const double rounding = Tol(s.Size());
            std::vector<cs::Constraint> out;
            std::map<size_t, std::vector<size_t>> linesAt;
            for (size_t e = 3; e < s.elements.size(); ++e) {
                const sketch::Element &element = s.elements[e];
                const sketch::ElementKind kind = element.kind;
                if (element.IsHeld(s) || (kind != sketch::ElementKind::Line && kind != sketch::ElementKind::Arc && kind != sketch::ElementKind::Circle))
                    continue;
                cs::Constraint c{kind == sketch::ElementKind::Line  ? sketch::Kind::Distance
                                 : kind == sketch::ElementKind::Arc ? sketch::Kind::Radius
                                                                    : sketch::Kind::Diameter,
                                 {sketch::Ref{e}}};
                c.value = std::abs(s.Measured(c));
                if (c.value > rounding)
                    out.push_back(c);
                if (kind == sketch::ElementKind::Line)
                    for (size_t p : {element.Start(), element.End()})
                        linesAt[s.coordsOf[p]].push_back(e);
            }
            for (const auto &[point, lines] : linesAt)
                for (size_t i = 0; i < lines.size(); ++i)
                    for (size_t j = i + 1; j < lines.size(); ++j) {
                        cs::Constraint c{sketch::Kind::Angle, {sketch::Ref{lines[i]}, sketch::Ref{lines[j]}}};
                        c.value = std::abs(s.Measured(c));
                        // parallel lines: an angle of 0 or 180 degrees is no dimension
                        if (std::abs(std::sin(c.value * std::numbers::pi / 180.0)) > 1e-9)
                            out.push_back(c);
                    }
            const std::vector<std::pair<std::string, std::string>> label = UnknownLabels();
            std::vector<char> seen(s.values.size(), 0);
            for (size_t e = 3; e < s.elements.size(); ++e) {
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
                    const sketch::Ref p = RefOf(label[u[k]].first);
                    // V (element 1) measures h, H (element 0) v
                    for (const size_t axis : {1, 0}) {
                        const double at = s.values[u[k] + 1 - axis];
                        cs::Constraint c{std::abs(at) > rounding ? sketch::Kind::Distance : sketch::Kind::Coincident, {p, sketch::Ref{axis}}};
                        c.value = std::abs(at);
                        out.push_back(c);
                    }
                }
            }
            return out;
        }
        // dimensions that hold as the geometry is: added in bulk, one solve
        void AddDimensions(const std::vector<std::pair<cs::Constraint, std::string>> &found) {
            if (found.empty())
                return;
            Atomic([&] {
                for (const auto &[c, removes] : found) {
                    const size_t k = s.Add(c);
                    const std::string name = NewId(sketch::KindName(c.kind), std::nullopt);
                    constraintIds.push_back(name);
                    byId[name] = {false, k};
                }
                analysis.reset();
                s.Solve();
            });
        }
        cs::Suggestion Suggested(const cs::Constraint &c, const std::string &removes) const {
            cs::Suggestion out{c.kind, {}, std::nullopt, Text(c), removes};
            for (const sketch::Ref &r : c.refs)
                out.refs.push_back(RefName(r));
            if (HasValue(c.kind))
                out.value = c.value;
            return out;
        }
        std::vector<Vertex> PointPlaces() const {
            std::vector<Vertex> out;
            for (size_t p = 0; p < s.coordsOf.size(); ++p)
                out.push_back(s.Coords(p));
            return out;
        }

        // the implied constraints that are redundant now go, the last first: of two that are redundant only with each
        // other one stays
        void DropRedundantImplied() {
            for (;;) {
                const std::vector<char> redundant = s.Redundancies(Analysis());
                std::optional<std::string> drop;
                for (size_t c = constraintIds.size(); c-- > 0 && !drop;)
                    if (redundant[c] && impliedIds.contains(constraintIds[c]))
                        drop = constraintIds[c];
                if (!drop)
                    return;
                Remove(*drop);
                impliedIds.erase(*drop);
            }
        }
        // the constraint k goes; the dimension an expression drove keeps its value as its own; one a curve uses raises
        void DropConstraint(size_t k) {
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
        // the element a reference dimension measures: the point of a coordinate, the line of a direction
        static sketch::Ref MeasuredRef(const cs::Constraint &d) { return d.kind == sketch::Kind::Angle && d.refs.size() == 2 ? d.refs[1] : d.refs[0]; }
        // text with each whole name from replaced by to
        static std::string Renamed(const std::string &text, const std::string &from, const std::string &to) {
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
        // after Split, Trim or a count change the expressions' text names what its measures belong to now; a warning if
        // that is another element than before, quiet if only its id changed
        void RewriteExpressions() {
            for (size_t k = 0; k < s.constraints.size(); ++k)
                for (cs::Written &w : s.constraints[k].written) {
                    const auto d = std::find_if(s.constraints.begin(), s.constraints.end(),
                                                [&](const cs::Constraint &o) { return o.automatic && o.valueAt == w.unknown; });
                    if (d == s.constraints.end())
                        continue;
                    const sketch::Ref r = MeasuredRef(*d);
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
        // per constraint: the value of a driving or driven length or radius now, 0 for any other
        std::vector<double> LengthValues() const {
            std::vector<double> out(s.constraints.size(), 0.0);
            for (size_t k = 0; k < out.size(); ++k)
                if (s.Sideless(s.constraints[k]) && !s.constraints[k].reference)
                    out[k] = DimensionValue(k);
            return out;
        }
        // the length or radius the new expression c drives (not yet added) at the values now: its sign must hold; uses
        // names the other dimensions and measures it uses
        std::optional<std::string> SignChangeOf(const cs::Constraint &c, const std::vector<double> &before, const std::vector<cs::ExpressionName> &names,
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
                            NumberText(DimensionValue(dimensions[n]));
            return constraintIds[k] + " = " + NumberText(target) + ", a length must not change sign" + (uses.empty() ? "" : " (" + uses + ")");
        }
        // a length or radius an expression drives that is 0, or has the other sign than in before (LengthValues()), at
        // the values v of the unknowns; uses: what the expression uses, "w = 4, ..."
        std::optional<std::string> SignChange(const std::vector<double> &before, const std::vector<double> &v, const std::string &uses = "") const {
            for (size_t k = 0; k < before.size(); ++k)
                if (before[k] != 0.0 && s.DriverOf(k))
                    if (const double now = v[s.constraints[k].valueAt]; before[k] * now <= 0.0)
                        return constraintIds[k] + " = " + NumberText(now) + ", a length must not change sign" + (uses.empty() ? "" : " (" + uses + ")");
            return std::nullopt;
        }
        // an expression out of its domain, or on its edge, at the values now: "Expression.N (text): what"
        std::optional<std::string> OutOfDomain() const {
            for (size_t k = 0; k < s.constraints.size(); ++k)
                if (s.constraints[k].kind == sketch::Kind::Expression)
                    if (const std::string error = s.DomainError(s.constraints[k]); !error.empty())
                        return constraintIds[k] + " (" + s.constraints[k].text + "): " + error;
            return OffsetProblem();
        }
        // the values of the unknowns at the new values (change: dimension, value), the driven dimensions following their
        // expressions; an expression out of its domain there sets error to "; Expression.N (text): what there"
        std::vector<double> Following(const std::vector<std::pair<size_t, double>> &change, std::string &error) const {
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
        // why a solve for the new values may have failed: an expression out of its domain there, or nothing
        std::string DomainHint(const std::vector<std::pair<size_t, double>> &change) const {
            std::string error;
            Following(change, error);
            return error;
        }
        // why a solve with the new constraint failed: the expressions left out, the rest solves, and an expression is out
        // of its domain at the references that solution measures
        std::string DomainHintWithoutExpressions(const std::vector<double> &start) const {
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
        std::string DomainHintOfSolve(const std::vector<double> &start) const {
            const std::string hint = DomainHint({});
            return hint.empty() ? DomainHintWithoutExpressions(start) : hint;
        }

        // the listed constraints that, left out, let the sketch solve again with constraint k (best effort: one at a time)
        std::vector<std::string> ConflictSet(size_t k, const std::vector<double> &start) const {
            std::vector<std::string> out;
            for (size_t j = 0; j < s.constraints.size(); ++j) {
                if (j == k || !IsListed(s.constraints[j]) || s.constraints[j].reference)
                    continue;
                cs::System rest = s;
                rest.values = start;
                rest.RemoveConstraint(j);
                if (rest.Solve().solved)
                    out.push_back(constraintIds[j]);
            }
            return out;
        }
        std::vector<std::string> IdsOf(const std::vector<size_t> &constraints) const {
            std::vector<std::string> out;
            for (size_t c : constraints)
                out.push_back(constraintIds[c]);
            return out;
        }
        // constraint k, which contradicts the others, stays but is not solved; the geometry stays at start
        void Suspend(size_t k, const std::vector<double> &start, const std::string &what) {
            std::vector<std::string> with = ConflictSet(k, start);
            s.constraints[k].suspended = true;
            s.values = start;
            analysis.reset();
            Warning() << what << (with.empty() ? "" : " (" + Joined(with) + ")") << "; " << constraintIds[k]
                      << " is kept but not solved, the sketch is over-constrained";
            conflicts[constraintIds[k]] = std::move(with);
        }
        static std::string WithText(const std::string &with) { return with.empty() ? "" : " with " + with; }
        // the ids of the other redundant constraints of the flags now
        std::string RedundantWith(size_t k, const std::vector<char> &now) const {
            std::vector<size_t> with;
            for (size_t j = 0; j < s.constraints.size(); ++j)
                if (j != k && now[j])
                    with.push_back(j);
            return Joined(IdsOf(with));
        }
        // the suspended constraint k solves again: it becomes active; else everything stays
        void TryActivate(size_t k) {
            const std::vector<double> start = s.values;
            s.constraints[k].suspended = false;
            analysis.reset();
            if (s.Solve().solved) {
                conflicts.erase(constraintIds[k]);
                analysis = s.Analyze(false);
                if (const std::vector<char> now = s.Redundancies(*analysis); now[k])
                    Warning() << constraintIds[k] << " (" << Text(s.constraints[k]) << ") is redundant (consistent)" << WithText(RedundantWith(k, now));
                return;
            }
            s.constraints[k].suspended = true;
            s.values = start;
        }
        void RetrySuspended() {
            for (size_t k = 0; k < s.constraints.size(); ++k)
                if (s.constraints[k].suspended)
                    TryActivate(k);
        }
        // the constraints that are suspended or in the set of one
        std::set<size_t> OverConstraints() const {
            std::set<size_t> out;
            for (const auto &[id, with] : conflicts) {
                out.insert(byId.at(id).index);
                for (const std::string &w : with)
                    if (const auto at = byId.find(w); at != byId.end() && !at->second.isElement)
                        out.insert(at->second.index);
            }
            return out;
        }
        std::set<size_t> OverElements() const {
            std::set<size_t> out;
            for (size_t c : OverConstraints())
                for (const sketch::Ref &r : s.constraints[c].refs)
                    out.insert(r.element);
            return out;
        }
        // the constraints before the last one that are redundant now (flags) and were not at the mark, before it was
        // added; the analysis at the mark is only made if something is redundant now
        std::vector<size_t> MadeRedundant(const std::vector<char> &now, const cs::System::Mark &mark) const {
            const size_t old = mark.constraints;
            if (std::none_of(now.begin(), now.begin() + (std::ptrdiff_t)old, [](char r) { return r != 0; }))
                return {};
            cs::System earlier = s;
            earlier.Truncate(mark);
            const std::vector<char> was = earlier.Redundancies(earlier.Analyze(false));
            std::vector<size_t> out;
            for (size_t j = 0; j < old; ++j)
                if (now[j] && !was[j])
                    out.push_back(j);
            return out;
        }
        void CheckNothingShrinks(const std::string &text) const {
            for (size_t e = 0; e < s.elements.size(); ++e) {
                const sketch::Element &el = s.elements[e];
                const bool line = el.kind == sketch::ElementKind::Line, arc = el.kind == sketch::ElementKind::Arc;
                if ((line || arc) && s.coordsOf[el.Start()] == s.coordsOf[el.End()])
                    throw std::invalid_argument(text + ": " + elementIds[e] + " would shrink to a point");
            }
        }
        // the solve turned an element round from drawn (Headings()): a warning, an error with errorOnBranchChange
        void CheckBranch(const std::vector<double> &drawn, const std::string &name, const std::string &text) const {
            if (const auto [turned, element] = Turned(drawn, Headings()); turned > branchTurn) {
                const std::string what = elementIds[element] + " turned round (" + std::to_string(std::lround(Degrees(turned))) +
                                         " degrees): the solution is not the branch drawn";
                if (errorOnBranchChange)
                    throw std::invalid_argument(text + ": " + what + " (error_on_branch_change)");
                Warning() << name << " (" << text << "): " << what;
            }
        }
        // the constraints the new one, name, makes redundant (now: the flags, before: the mark before it was added)
        void ReportMadeRedundant(const std::vector<char> &now, const cs::System::Mark &before, const std::string &name) const {
            const std::vector<size_t> made = MadeRedundant(now, before);
            if (!made.empty())
                Warning() << name << " makes " << Joined(IdsOf(made)) << " redundant (consistent); remove " << (made.size() > 1 ? "them" : "it")
                          << " to keep the sketch minimal";
        }
        // suspendable: a contradicting constraint is kept, not solved (Suspend) instead of raising
        // implied (the degrees of freedom before): a redundant c that takes none of them is left out, it returns what c
        // follows from instead of a warning; reportMakes: a call of the user's says which constraints it makes redundant
        std::optional<std::string> AddConstraint(cs::Constraint c, const std::optional<std::string> &id, std::optional<size_t> implied = std::nullopt,
                                                 bool reportMakes = false, bool suspendable = false) {
            const std::string text = Text(c);
            const std::vector<double> drawn = Headings();
            const std::string name = NewId(sketch::KindName(c.kind), id);
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
            byId[name] = {false, k};
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
                const std::string with = RedundantWith(k, now);
                if (implied && analysis->dof == *implied) {
                    amplified = amplifiedBefore;
                    return with;
                }
                Warning() << name << " (" << text << ") is redundant (consistent)" << WithText(with);
            } else if (reportMakes)
                ReportMadeRedundant(now, before, name);
            if (amplified > config::state->sensitivity_warning && amplifiedBefore <= config::state->sensitivity_warning)
                Warning() << name << " (" << text << "): nearly singular, a small change of the dimensions can move the geometry "
                          << NumberText(amplified) << " times as much";
            return std::nullopt;
        }

        // a step of a solve that turns a line or arc by more than this is halved; by more than branchTurn the element
        // turned round, to the other branch
        static constexpr double smoothTurn = std::numbers::pi / 6.0, branchTurn = std::numbers::pi / 2.0;

        std::vector<std::string> constraintIds;
        // the constraints the drawing helpers implied (auto_constraint), which go when another makes them redundant
        std::set<std::string> impliedIds;
        // the elements of the outermost call (from callElements, less the ones it removed) and those its chain joints
        // join (ids): related to the others when it ends (auto_constraint)
        size_t callElements = 0, removedInCall = 0;
        std::vector<std::string> touched;
        // per element id the constraints that go with it although they do not refer to it
        std::map<std::string, std::vector<std::string>> companions;
        // the kept patterns, the chamfers and the offsets made with the constraints on, by id; the mirror calls by scope
        std::map<std::string, cs::PatternInfo> patterns;
        std::map<std::string, cs::ChamferInfo> chamfers;
        std::map<std::string, cs::OffsetInfo> offsetInfos;
        std::map<std::string, cs::MirrorInfo> mirrors;
        // the sharp corners whose elements had a Fix: the corner is fixed too, under the id of the Fix and .corner
        std::vector<std::pair<std::string, sketch::Ref>> fixedCorners;
        // the degrees of freedom before the offset being made and its first tie
        size_t offsetDof = 0, offsetFirstTie = 0;
        cs::Pending pending;
        // the last analysis, reset by every change
        std::optional<ConstraintAnalysis> analysis;
        // the suspended constraints (kept, not solved) and the constraints each contradicts
        std::map<std::string, std::vector<std::string>> conflicts;
    };

}
