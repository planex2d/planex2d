// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    // the constraints among the elements turn with them; returns those that hold the elements where they are
    std::vector<size_t> ConstraintSketch::MirrorConstraintsAmong(const std::set<size_t> &mine) {
        std::vector<size_t> holding;
        for (size_t c = 0; c < s.constraints.size(); ++c) {
            cs::Constraint &k = s.constraints[c];
            if (k.refs.empty() || k.kind == sketch::Kind::Expression)
                continue;
            const auto in = [&](const sketch::Ref &r) { return mine.contains(r.element); };
            // an offset's element carries only its distance: the ties of an offset mirrored whole are among the elements
            const auto along = [&](const sketch::Ref &r) { return in(r) || s.elements[r.element].kind == sketch::ElementKind::Offset; };
            const bool internal = std::all_of(k.refs.begin(), k.refs.end(), along) && std::any_of(k.refs.begin(), k.refs.end(), in);
            if (k.refs.size() == 1 ? internal : std::any_of(k.refs.begin(), k.refs.end(), in) && !internal)
                holding.push_back(c);
            if (!internal)
                continue;
            if (k.kind == sketch::Kind::Angle && k.refs.size() == 2 && k.valueAt == sketch::none)
                k.value = -k.value;
            if (k.kind == sketch::Kind::Offset)
                k.sign = -k.sign;
            s.Refreeze(c);
        }
        return holding;
    }

    ConstraintSketch &ConstraintSketch::Mirror(const std::vector<std::string> &ids, const std::string &axis) {
        Flush();
        const std::vector<size_t> elements = Originals(WithSlotSpines(ids), "Mirror");
        const ConstraintSketch saved = *this;
        const std::vector<std::pair<size_t, Vertex>> targets = MirrorElements(elements, axis);
        std::set<size_t> mine(elements.begin(), elements.end()), moved;
        for (const auto &[q, p] : targets)
            moved.insert(s.coordsOf[q]);
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (s.elements[e].kind == sketch::ElementKind::Point && moved.contains(s.coordsOf[s.elements[e].Point()]))
                mine.insert(e);
        const std::vector<size_t> holding = MirrorConstraintsAmong(mine);
        analysis.reset();
        const bool solved = s.Solve().solved;
        if (solved)
            RetakeTurnedOffsets(mine);
        bool held = solved && !OutOfDomain();
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

    ConstraintSketch &ConstraintSketch::SetOrigin(const Vertex &p) {
        Flush();
        const ConstraintSketch saved = *this;
        BasicSketch::SetOrigin(p);
        // what a Fix holds and a rigid group's frame are places in the plane: they stay, their coordinates shift
        for (cs::Constraint &c : s.constraints) {
            if (c.kind == sketch::Kind::RigidGroup && c.params.size() >= 2) {
                s.values[c.params[0]] -= p.x;
                s.values[c.params[1]] -= p.y;
            }
            if (!sketch::IsFix(c.kind) || c.target.empty())
                continue;
            const size_t points = c.heldPoints.empty() && c.held.empty() ? c.target.size() / 2 : c.heldPoints.size();
            for (size_t k = 0; k < points; ++k) {
                c.target[2 * k] -= p.x;
                c.target[2 * k + 1] -= p.y;
            }
            for (size_t k = 0; k < c.held.size(); ++k)
                for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
                    if (s.elements[e].kind == sketch::ElementKind::Axis && s.elements[e].AxisDistance() == c.held[k])
                        c.target[2 * points + k] -= s.elements[e].AxisShift(s, p);
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

    ConstraintSketch &ConstraintSketch::Cut(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse,
                                            bool trim, bool tied) {
        const std::string call = std::string(trim ? "Trim" : "Split") + "('" + id + "')";
        Flush();
        if (!tied)
            if (const std::string copies = CopiesOf(RefOf(id).element); !copies.empty())
                throw std::invalid_argument(call + ", auto_constraint=False: " + copies +
                                            " follow it as it is; Split it with auto_constraint=True, or Remove them first");
        sketch::Undoable(*this, [&] {
            const std::map<size_t, std::string> held = ValueHolders();
            const size_t cut = RefOf(id).element, elementsBefore = s.elements.size();
            const std::set<std::string> earlier(constraintIds.begin(), constraintIds.end());
            cutRedundant.emplace();
            CutPieces(id, cutterIds, nearPoint, reverse, trim, tied, call);
            Prune(call, held);
            if (tied)
                DropTakenOver(cut, elementsBefore, earlier);
            ReportStillRedundant(*std::exchange(cutRedundant, std::nullopt));
            if (const std::optional<std::string> wrong = OffsetProblem())
                throw std::invalid_argument(call + ": " + *wrong);
        });
        analysis.reset();
        return *this;
    }

    ConstraintSketch &ConstraintSketch::Fillet(const std::vector<Vertex> &at, double r, bool curvature, const std::vector<std::string> &ids,
                                               const std::vector<Properties> &given) {
        return Cornered("Fillet", [&] { BasicSketch::Fillet(at, r, curvature, ids, given); });
    }

    ConstraintSketch &ConstraintSketch::Chamfer(const std::vector<Vertex> &at, const ChamferRule &rule, const std::vector<std::string> &ids,
                                                const std::vector<Properties> &given) {
        return Cornered("Chamfer", [&] { BasicSketch::Chamfer(at, rule, ids, given); });
    }

    ConstraintSketch &ConstraintSketch::Extend(const std::string &id, const std::vector<std::string> &targets, std::optional<double> length) {
        const std::string call = "Extend('" + id + "')";
        Flush();
        sketch::Undoable(*this, [&] {
            Atomic([&] {
                const auto [point, at] = ExtendedEnd(id, targets, length, call);
                const size_t e = RefOf(id).element;
                s.MovePoint(point, at);
                analysis.reset();
                if (!(s.Solve().solved && Coords(point).IsClose(at, Tol(std::max(NormInf(at), 1.0))))) {
                    std::vector<size_t> holding;
                    for (size_t c = 0; c < s.constraints.size(); ++c)
                        if (s.constraints[c].RefersTo(e))
                            holding.push_back(c);
                    throw std::invalid_argument(call + ": " + (holding.empty() ? std::string("the constraints") : Joined(IdsOf(holding))) +
                                                (holding.size() == 1 ? " holds" : " hold") + " its end where it is; nothing changed");
                }
                if (autoConstraint)
                    touched.push_back(book.elementIds[e]);
                Sync();
            });
        });
        analysis.reset();
        return *this;
    }

    ConstraintSketch &ConstraintSketch::Connect(const std::string &a, const std::string &b, sketch::Kind continuity, bool reverse, const Options &opt) {
        BasicSketch::Connect(a, b, continuity, reverse, opt);
        analysis.reset();
        return *this;
    }

    void ConstraintSketch::CornerMade(const FilletRule &rule, const sketch::CornerParts &c) {
        FixCorners();
        AddConstraint({sketch::Kind::Tangent, {{c.arms[0]}, {c.made}}}, std::nullopt);
        AddConstraint({sketch::Kind::Tangent, {{c.made}, {c.arms[1]}}}, std::nullopt);
        AddConstraint({sketch::Kind::Radius, {{c.made}}, rule.r}, book.elementIds[c.made] + ".r");
    }

    void ConstraintSketch::CornerMade(const CurvatureFilletRule &rule, const sketch::CornerParts &c) {
        FixCorners();
        const std::string id = book.elementIds[c.made];
        const sketch::Element arc = s.elements[c.made];
        const size_t centre = pointElementOf.at(arc.Center());
        book.Rename(c.made, id + ".arc");
        roles[c.made] = sketch::Role::Helper;
        roles[centre] = sketch::Role::Helper;
        AddElement(s.AddBlend(arc.Start(), arc.End(), {{c.arms[0], c.arms[1]}, c.made, true}), "Fillet", {id, props[c.made], false});
        AddConstraint({sketch::Kind::Tangent, {{c.arms[0]}, {c.made}}}, std::nullopt);
        AddConstraint({sketch::Kind::Tangent, {{c.made}, {c.arms[1]}}}, std::nullopt);
        AddConstraint({sketch::Kind::Radius, {{c.made}}, rule.r}, id + ".r");
    }

    void ConstraintSketch::CornerMade(const ChamferRule &rule, const sketch::CornerParts &c) {
        FixCorners();
        const std::string id = book.elementIds[c.made];
        const size_t low = rule.reverse ? 1 : 0, high = 1 - low;
        const auto text = [&](size_t k) { return std::array<std::string, 3>{book.elementIds[c.arms[k]], RefName(c.at[k].cut), RefName(c.at[k].corner)}; };
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

    std::optional<std::string> ConstraintSketch::ReverseOf(const std::string &id) const {
        constexpr std::string_view suffix = ".reverse";
        if (id.size() <= suffix.size() || !id.ends_with(suffix))
            return std::nullopt;
        const std::string chamfer = id.substr(0, id.size() - suffix.size());
        return chamfers.contains(chamfer) ? std::optional<std::string>{chamfer} : std::nullopt;
    }

    void ConstraintSketch::ChamferDimensions(const std::string &id, const cs::ChamferInfo &info, double d, double second) {
        const size_t f = info.reverse ? 1 : 0, o = 1 - f;
        const sketch::Ref corner = RefOf(info.corner[f]), cut = RefOf(info.cut[f]), other = RefOf(info.cut[o]);
        AddConstraint({sketch::Kind::Distance, {corner, cut}, d}, id + ".d");
        if (!info.angled) {
            AddConstraint({sketch::Kind::Distance, {RefOf(info.corner[o]), other}, second}, id + ".d2");
            return;
        }
        const size_t line = RefOf(info.arm[f]).element, chamfer = book.byId.at(id).index;
        if (s.elements[line].kind != sketch::ElementKind::Line)
            throw std::invalid_argument(id + ".angle: the first element, '" + info.arm[f] + "', is not a line; the angle is to a line (give d2 instead)");
        const Vertex p = corner.PointValue(s), q = cut.PointValue(s);
        const bool left = Cross(p - q, other.PointValue(s) - q) > 0.0;
        AddConstraint({sketch::Kind::Angle, left ? std::vector<sketch::Ref>{{line}, {chamfer}} : std::vector<sketch::Ref>{{chamfer}, {line}}, second},
                      id + ".angle");
    }

    void ConstraintSketch::ApplyFlips() {
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

    void ConstraintSketch::Reversed() {
        UndoableUpdate([&] { ApplyFlips(); });
        analysis.reset();
    }

    void ConstraintSketch::CallStarts() {
        Flush();
        analysis.reset();
        if (atomic == 0) {
            callElements = s.elements.size();
            removedInCall = 0;
            touched.clear();
            mirroredInCall.clear();
            copyOfCall.clear();
            cutRedundant.reset();
        }
    }

    void ConstraintSketch::CallEnds() {
        RelationsOfCall();
        // after the relations, which join a point on the axis with its copy
        for (const auto &[scope, axis] : std::exchange(mirroredInCall, {}))
            DropOnAxisTakenOver(scope, RefOf(axis));
        RewriteExpressions();
        if (!offsetInfos.empty())
            if (const std::optional<std::string> wrong = OffsetProblem())
                throw std::invalid_argument(*wrong);
    }

    void ConstraintSketch::CallUndone() {
        constraintIds.resize(s.constraints.size());
        analysis.reset();
        touched.clear();
    }

    void ConstraintSketch::Chained(size_t a, size_t b) {
        if (autoConstraint) {
            touched.push_back(book.elementIds[a]);
            touched.push_back(book.elementIds[b]);
        }
    }

    void ConstraintSketch::SplineAdded(size_t e) {
        const sketch::SplineData &d = s.splines[s.elements[e].SplineIndex()];
        if (d.style == SplineStyle::Clamped && !d.tangentAt.empty())
            Warning() << book.elementIds[e] << ": " << NotSmooth;
    }

    bool ConstraintSketch::KeptAtCorner(size_t c, const std::array<sketch::Ref, 2> &kept) {
        cs::Constraint &k = s.constraints[c];
        if (k.kind == sketch::Kind::Distance && k.refs.size() == 1) {
            k.refs = {kept[0], kept[1]};
            s.Refreeze(c);
            return true;
        }
        if (!sketch::IsFix(k.kind))
            return false;
        k.refs = {kept[0]};
        s.Refix(c);
        fixedCorners.push_back({constraintIds[c], kept[1], k.kind});
        return true;
    }

    void ConstraintSketch::FixCorners() {
        const auto corners = std::exchange(fixedCorners, {});
        for (const auto &[fix, corner, kind] : corners)
            if (!s.IsFixed(corner, kind))
                AddConstraint({kind, {corner}}, fix + ".corner");
    }

    void ConstraintSketch::PiecesCut(size_t a, const sketch::Element &old, const std::vector<size_t> &pieces, const std::string &id, const std::string &call) {
        const bool circle = old.kind == sketch::ElementKind::Circle, line = old.kind == sketch::ElementKind::Line;
        const bool curve = old.kind == sketch::ElementKind::Spline || old.kind == sketch::ElementKind::OffsetCurve;
        const std::array<size_t, 2> oldEnds = old.Ends(s);
        if (old.kind == sketch::ElementKind::OffsetCurve)
            OffsetCurveCut(a, pieces);
        std::optional<std::array<sketch::Ref, 2>> kept;
        if (cornering && line) {
            const std::optional<sketch::Ref> one = s.PieceEndAt(pieces, oldEnds[0]), two = s.PieceEndAt(pieces, oldEnds[1]);
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
                    if (const std::optional<sketch::Ref> moved = s.PieceEndAt(pieces, oldEnds[r.part == sketch::Part::End]))
                        r = *moved;
                whole = whole || r.part == sketch::Part::Whole;
            }
            const bool ends = sketch::IsFix(k.kind) || (line && (k.kind == sketch::Kind::Equal || (k.kind == sketch::Kind::Distance && k.refs.size() == 1))) ||
                              s.IsSweep(k) ||
                              (k.kind == sketch::Kind::Symmetric && k.refs.size() == 3 && !k.refs[0].IsPoint(s) && !k.refs[0].IsRound(s));
            if (whole && ends && !curve) {
                if (kept && KeptAtCorner(c, *kept))
                    continue;
                if (sketch::IsFix(k.kind)) {
                    s.Refix(c);
                    continue;
                }
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

    // a length in an expression does not scale with the sketch: the expressions hold again after a solve
    void ConstraintSketch::ScaleInside(double f, bool scaleMaxh, bool scaleMinh) {
        CheckScale(f);
        Flush();
        const std::optional<ConstraintSketch> saved = Expressions().empty() ? std::nullopt : std::optional<ConstraintSketch>(*this);
        BasicSketch::ScaleInside(f, scaleMaxh, scaleMinh);
        for (cs::Constraint &c : s.constraints) {
            if (sketch::HasValue(c.kind) && c.kind != sketch::Kind::Angle)
                c.value *= f;
            const size_t points = 2 * c.heldPoints.size();
            const bool held = (sketch::IsFix(c.kind) || c.kind == sketch::Kind::RigidGroup) && (!c.held.empty() || points);
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

    // the implied constraints that are redundant now go, the last first: of two that are redundant only with each
    // other one stays
    // a cut ties its cut points to the cutters' ends: a coincidence of a point there on the element cut or one of
    // its pieces (the user's, an earlier cut's) repeats the tie and goes
    void ConstraintSketch::DropTakenOver(size_t cut, size_t elementsBefore, const std::set<std::string> &earlier) {
        std::vector<size_t> pieces{cut};
        for (size_t e = elementsBefore; e < s.elements.size(); ++e)
            if (!sketch::Ref{e}.IsPoint(s))
                pieces.push_back(e);
        const auto piece = [&](const sketch::Ref &r) {
            return r.part == sketch::Part::Whole && std::find(pieces.begin(), pieces.end(), r.element) != pieces.end();
        };
        const auto atAnEnd = [&](const sketch::Ref &r) {
            const auto at = s.coordsOf[r.PointAt(s)];
            for (size_t e : pieces)
                if (s.elements[e].kind != sketch::ElementKind::Circle)
                    for (size_t end : s.elements[e].Ends(s))
                        if (s.coordsOf[end] == at)
                            return true;
            return false;
        };
        DropRedundantCoincidences([&](const std::string &id, const sketch::Ref &point, const sketch::Ref &on) {
            return earlier.contains(id) && piece(on) && atAnEnd(point);
        });
    }

    // a mirror joins a point on its axis with its copy: a coincidence of that point on the axis repeats the
    // join and goes
    void ConstraintSketch::DropOnAxisTakenOver(const std::string &scope, const sketch::Ref &axis) {
        std::vector<size_t> copies;
        for (size_t e = 0; e < s.elements.size(); ++e)
            if (s.elements[e].kind == sketch::ElementKind::Point && book.elementIds[e].starts_with(scope + "."))
                copies.push_back(s.elements[e].Point());
        DropRedundantCoincidences([&](const std::string &, const sketch::Ref &point, const sketch::Ref &on) {
            const Vertex at = Coords(point.PointAt(s));
            return on.element == axis.element && on.part == sketch::Part::Whole &&
                   std::any_of(copies.begin(), copies.end(), [&](size_t q) { return Coords(q).IsClose(at, Tol(s.Size())); });
        });
    }

}
