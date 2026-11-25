// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "planex/constraint_sketch/copyrecords.hpp"
#include "planex/constraint_sketch/dimensionfinder.hpp"
#include "planex/constraint_sketch/freemotions.hpp"
#include "planex/constraint_sketch/pending.hpp"
#include "planex/constraint_sketch/reports.hpp"
#include "planex/constraint_sketch/system.hpp"
#include "planex/core/objectid.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/edge.hpp"
#include "planex/offset/offsetpieces.hpp"
#include "planex/sketch/copies.hpp"
#include "planex/sketch/cornerparts.hpp"
#include "planex/sketch/guards.hpp"
#include "planex/sketch/offsetsides.hpp"
#include "planex/sketch/relationfinder.hpp"
#include "planex/sketch/sketch.hpp"

namespace planex2d {

    class ConstraintSketch : public BasicSketch<cs::System> {
    public:
        using BasicSketch::Mirror;

        struct Walk {
            double reached = 0.0;
            std::vector<std::string> branchChanges;
            std::optional<std::string> refused;
        };

        explicit ConstraintSketch(const AxisSystem &a = AxisSystem()) : BasicSketch(a) {}
        ConstraintSketch(const Sketch &quick, const std::optional<AutoConstraint> &autoConstraint) : BasicSketch(quick) {
            this->autoConstraint = autoConstraint;
            if (autoConstraint)
                AddRelations(AutoRelations(*autoConstraint, std::vector<char>(s.elements.size(), 1)));
        }

        // constraintsketch.cpp
        ConstraintSketch &Remove(const std::string &id) { return Remove(id, "Remove('" + id + "')"); }
        ConstraintSketch &Remove(const std::string &id, const std::string &call);
        // the last constraint goes when the element does, although it does not refer to it (the equal radii of a
        // symmetry line)
        void TieLastConstraintTo(const std::string &element) { companions[element].push_back(constraintIds.back()); }
        ConstraintSketch &Constrain(const std::vector<std::string> &refs, sketch::Kind kind, std::optional<double> value,
                                    const std::optional<std::string> &id, bool reference = false, bool ownTangents = false);
        void DropImpliedLike(const cs::Constraint &c);
        ConstraintSketch &ConstrainImplied(const std::vector<std::string> &refs, sketch::Kind kind);
        ConstraintSketch &ConstrainImplied(const cs::Constraint &c);
        ConstraintSketch &ConstrainHelped(const std::string &call, const std::string &id, const sketch::HelperTargets &t);
        void TieSymmetryLine(std::string a, std::string b, const std::string &axis);
        bool MakesOffsetTieRedundant(const cs::System::Mark &before) const;
        ConstraintSketch &RigidGroup(const std::vector<std::string> &ids, const std::optional<std::string> &id);

        // constraintsketchcalls.cpp
        ConstraintSketch &Mirror(const std::vector<std::string> &ids, const std::string &axis);
        ConstraintSketch &SetOrigin(const Vertex &p) override;
        ConstraintSketch &Cut(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse,
                              bool trim, bool tied = true) override;
        ConstraintSketch &Fillet(const std::vector<Vertex> &at, double r, bool curvature, const std::vector<std::string> &ids,
                                 const std::vector<Properties> &given) override;
        ConstraintSketch &Chamfer(const std::vector<Vertex> &at, const ChamferRule &rule, const std::vector<std::string> &ids,
                                  const std::vector<Properties> &given) override;
        ConstraintSketch &Extend(const std::string &id, const std::vector<std::string> &targets, std::optional<double> length) override;
        ConstraintSketch &Connect(const std::string &a, const std::string &b, sketch::Kind continuity, bool reverse, const Options &opt) override;
        void RequireConstraints(std::string_view) const override {}

        // constraintsketchvalues.cpp
        ConstraintSketch &AddExpression(const std::string &text, const std::vector<cs::Node> &nodes, const std::vector<cs::ExpressionName> &names,
                                        const std::optional<std::string> &drives, const std::optional<std::string> &id);
        void CurveDimensions(sketch::CurveFormula &formula);
        std::vector<std::pair<std::string, std::string>> DrivenDimensions() const;
        std::optional<std::string> CurveUsing(size_t k) const;
        ConstraintSketch &RemoveExpression(const std::string &id);
        cs::ExpressionNames ExpressionNames() const;
        double Value(const std::string &id);
        std::vector<std::pair<std::string, double>> Dimensions();
        void SetValue(const std::string &id, double v);
        void SetValues(const std::vector<std::pair<std::string, double>> &values);
        ConstraintSketch &Update(const std::vector<std::pair<std::string, double>> &values = {}, size_t steps = 1);
        ConstraintSketch &Drag(const std::vector<std::string> &points, const std::vector<Vertex> &to);
        ConstraintSketch &UpdateValues(const std::vector<std::pair<std::string, double>> &values, size_t steps);
        ConstraintSketch &UpdateDimensions(size_t steps);
        static double Between(sketch::Kind kind, double from, double to, double t);
        Walk WalkTo(const std::vector<std::pair<size_t, double>> &change, const std::vector<double> &from, size_t steps);
        void Flush() override;
        ConstraintSketch &UseParameters(const std::vector<std::pair<std::string, double>> &values);
        ConstraintSketch &UpdateParameters(const std::vector<std::pair<std::string, double>> &values);

        // constraintsketchcopies.cpp
        ConstraintSketch &AddPattern(const std::vector<std::string> &ids, const std::string &ref, size_t count, double step, bool linear,
                                     bool keep, bool internal, const std::optional<std::string> &id, bool equal = false,
                                     const std::optional<sketch::PatternDir2> &second = std::nullopt, bool reverse = false);
        std::string CopiesOf(size_t e) const;
        std::vector<std::string> Expanded(const std::vector<std::string> &ids) const override;
        bool IsInOneCopy(const std::vector<sketch::Ref> &refs, const std::map<std::string, std::string> &copies) const;
        void FollowCopies(std::vector<double> &origin);
        std::optional<std::string> CountOf(const std::string &id) const;
        bool IsCount(const std::string &id) const { return CountOf(id).has_value(); }
        void ApplyCounts(size_t steps);
        void RemoveCopies(const std::string &id, const std::array<size_t, 2> &n);

        // constraintsketchoffsets.cpp
        std::vector<double> OffsetState(const std::string &name, const cs::OffsetInfo &info) const;
        std::optional<std::string> OffsetProblem(bool shape = true) const;
        void RetakeTurnedOffsets(const std::set<size_t> &mine);
        std::string ShapeChange(const cs::OffsetInfo &info, const std::set<std::tuple<int, size_t, int>> &now, const sketch::OffsetChain &source, double d) const;
        std::optional<std::string> OffsetDistanceProblem(size_t k, double value) const;
        std::optional<std::string> KeyholeProblem(size_t k, double value) const;

        // constraintsketchrelations.cpp
        std::vector<sketch::FoundRelation> AutoRelations(const AutoConstraint &o, std::vector<char> isNew, std::optional<double> tol = std::nullopt,
                                                         std::vector<sketch::FoundAxis> axes = {}, std::vector<sketch::FoundPitchCircle> pitches = {}) const;
        std::vector<sketch::FoundPitchCircle> PitchCircles(std::optional<double> tol);
        std::vector<sketch::FoundAxis> SymmetryAxes(std::optional<double> tol, bool newLines);
        void AddRelations(const std::vector<sketch::FoundRelation> &found, const char *call = "auto_constraint");
        void DropRedundantFrom(size_t first);
        void RelateAdded(size_t first);
        std::vector<cs::Suggestion> SuggestConstraints(const std::optional<AutoConstraint> &geometric, bool dimensional, std::optional<double> tol);
        std::vector<std::string> AutoConstrain(const std::optional<AutoConstraint> &geometric, bool dimensional, std::optional<double> tol);

        // constraintsketchreports.cpp
        void RequireFull(const std::string &call) override;
        std::vector<cs::ConstraintInfo> Infos();
        std::vector<cs::ConstraintEntry> Constraints() const;
        std::vector<std::pair<std::string, std::string>> Expressions() const;
        const ConstraintAnalysis &Analysis(bool complete = true);
        size_t Dof() { return Analysis(false).dof; }
        std::vector<cs::FreeMotion> FreeMotions();
        double Condition() { return Analysis().amplification; }
        cs::SketchJacobian Jacobian();
        std::string Mode() const { return strict ? "strict" : "normal"; }
        void SetMode(const std::string &mode);
        std::string Status();
        std::vector<std::pair<std::string, std::vector<std::string>>> Dependencies();
        std::vector<std::string> Redundant();
        std::vector<std::pair<std::string, std::string>> States();
        size_t ConstraintCount() const { return s.constraints.size(); }

        static constexpr const char *NotSmooth = "style 'clamped' with given tangents is not smooth for the solver (OCC scales "
                                                 "the tangents by L1 norms of Lagrange derivatives); style 'averaged' is";
        std::optional<AutoConstraint> autoConstraint;
        // a solve that turns a line round or an arc to its other side left the branch the sketch was drawn in: an error
        // (and nothing changes) instead of a warning
        bool errorOnBranchChange = false;

    protected:
        double amplified = 0.0;
        bool strict = false;

        void WarnLandedOnEnd(const std::string &call, const std::string &id, const std::string &t, const std::string &end, const char *role);

        // constraintsketch.cpp
        void Relate(sketch::Kind kind, std::vector<sketch::Ref> refs, std::vector<size_t> params = {}) override;
        void RelateGiven(sketch::Kind kind, std::vector<sketch::Ref> refs) override;
        void Dimension(sketch::Kind kind, std::vector<sketch::Ref> refs, double value, const std::string &id, bool inOffset) override;
        void Reindex() override;
        bool IsReferenced(size_t element) const override { return s.IsReferenced(element); }
        void RemoveOne(const std::string &id) override;
        void ElementGoes(size_t k, const std::string &id) override;
        void AddTie(cs::Constraint c);
        void AddUnsolved(cs::Constraint c, const std::string &id);
        const std::string &AddUnsolved(cs::Constraint c);
        void RemoveImplied(const std::string &id);
        std::string Text(const cs::Constraint &c) const;
        void ReportStillRedundant(const std::vector<std::string> &ids);
        template <class Matches> void DropRedundantCoincidences(const Matches &matches) {
            for (;;) {
                const std::vector<char> redundant = s.Redundancies(Analysis(false));
                std::optional<std::string> drop;
                for (size_t k = constraintIds.size(); k-- > 0 && !drop;) {
                    const cs::Constraint &c = s.constraints[k];
                    if (redundant[k] && c.kind == sketch::Kind::Coincident && c.refs.size() == 2)
                        for (size_t i = 0; i < 2 && !drop; ++i)
                            if (c.refs[i].IsPoint(s) && matches(constraintIds[k], c.refs[i], c.refs[1 - i]))
                                drop = constraintIds[k];
                }
                if (!drop)
                    return;
                RemoveImplied(*drop);
            }
        }
        void DropRedundantImplied();
        void DropConstraint(size_t k);
        std::vector<std::string> ConflictSet(size_t k, const std::vector<double> &start) const;
        std::string ContradictionText(const std::vector<std::pair<size_t, double>> &change) const;
        std::vector<std::string> IdsOf(const std::vector<size_t> &constraints) const;
        void Suspend(size_t k, const std::vector<double> &start, const std::string &what);
        std::vector<std::string> ListedIds(const std::vector<size_t> &constraints) const;
        std::string RedundancyText(size_t k, const std::vector<std::string> &from) const;
        void TryActivate(size_t k);
        void RetrySuspended();
        std::set<size_t> OverConstraints() const;
        std::set<size_t> OverElements() const;
        void CheckNothingShrinks(const std::string &text) const;
        void CheckBranch(const std::vector<double> &drawn, const std::string &name, const std::string &text) const;
        bool DropImpliedMadeRedundant(const std::vector<char> &now, const cs::System::Mark &before);
        void ReportMadeRedundant(const std::vector<char> &now, const cs::System::Mark &before) const;
        std::optional<std::string> AddConstraint(cs::Constraint c, const std::optional<std::string> &id, std::optional<size_t> implied = std::nullopt,
                                                 bool reportMakes = false, bool suspendable = false);

        // constraintsketchcalls.cpp
        std::vector<size_t> MirrorConstraintsAmong(const std::set<size_t> &mine);
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
        void CornerMade(const FilletRule &rule, const sketch::CornerParts &c) override;
        void CornerMade(const CurvatureFilletRule &rule, const sketch::CornerParts &c) override;
        std::optional<std::string> ReverseOf(const std::string &id) const;
        void ChamferDimensions(const std::string &id, const cs::ChamferInfo &info, double d, double second);
        void CornerMade(const ChamferRule &rule, const sketch::CornerParts &c) override;
        void ApplyFlips();
        void Reversed();
        void CallStarts() override;
        void CallEnds() override;
        void CallUndone() override;
        void Chained(size_t a, size_t b) override;
        void SplineAdded(size_t e) override;
        bool KeptAtCorner(size_t c, const std::array<sketch::Ref, 2> &kept);
        void FixCorners();
        void PiecesCut(size_t a, const sketch::Element &old, const std::vector<size_t> &pieces, const std::string &id, const std::string &call) override;
        void ScaleInside(double f, bool scaleMaxh, bool scaleMinh) override;
        void DropTakenOver(size_t cut, size_t elementsBefore, const std::set<std::string> &earlier);
        void DropOnAxisTakenOver(const std::string &scope, const sketch::Ref &axis);

        // constraintsketchvalues.cpp
        std::optional<std::string> SettingProblem(size_t k, double v, const std::string &id);
        std::vector<size_t> LinkedDimensions(const std::vector<cs::ExpressionName> &names);
        cs::Constraint ExpressionOf(const std::string &text, const std::vector<cs::Node> &nodes, const std::vector<cs::ExpressionName> &names,
                                    const std::vector<size_t> &dimensions, const std::optional<std::string> &drives, const std::string &call) const;
        // what waited goes with the change that failed
        template <class F>
        void UndoableUpdate(F f) {
            ConstraintSketch before = *this;
            try {
                f();
            } catch (...) {
                *this = std::move(before);
                pending.Clear();
                analysis.reset();
                throw;
            }
        }
        size_t DimensionAt(const std::string &id) const;
        size_t MeasureReference(const std::string &ref, const std::string &measure);
        std::map<size_t, std::string> ValueHolders() const;
        void Prune(const std::string &call, const std::map<size_t, std::string> &held);
        static std::string Renamed(const std::string &text, const std::string &from, const std::string &to);
        void RewriteExpressions();
        std::optional<std::string> SignChangeOf(const cs::Constraint &c, const std::vector<double> &before, const std::vector<cs::ExpressionName> &names,
                                                const std::vector<size_t> &dimensions) const;
        std::optional<std::string> SignChange(const std::vector<double> &before, const std::vector<double> &v, const std::string &uses = "") const;
        std::optional<std::string> OutOfDomain(bool shape = true) const;
        std::vector<double> Following(const std::vector<std::pair<size_t, double>> &change, std::string &error) const;
        std::string DomainHint(const std::vector<std::pair<size_t, double>> &change) const;
        std::string DomainHintWithoutExpressions(const std::vector<double> &start) const;
        std::string DomainHintOfSolve(const std::vector<double> &start) const;

        // constraintsketchcopies.cpp
        std::vector<std::pair<std::string, std::array<size_t, 2>>> WantedCounts(const std::vector<std::pair<std::string, size_t>> &changes) const;
        void AddCopies(const std::string &id, const std::array<size_t, 2> &n);
        void MirrorTied(const sketch::Copies &made, const sketch::Ref &axis) override;
        void PlaceTiedCopy(const std::vector<size_t> &originals, const sketch::Ref &r, bool linear, double step, size_t k, size_t pattern,
                           cs::PatternInfo &info, size_t j = 0);
        void MirrorMade(const std::string &scope, const std::vector<std::string> &given, const std::string &axis, bool tie, bool internal) override;
        void ConstraintsCopied(const std::string &prefix, bool flip) override;
        std::optional<cs::Constraint> CopiedConstraint(const cs::Constraint &c, const std::string &prefix, bool flip) const;
        static std::string Given(const std::vector<std::string> &ids);
        std::vector<cs::ConstraintInfo> CopyEntries() const;
        std::optional<std::string> CopyHint(const std::string &id) const;
        template <class Id>
        void TieTangents(size_t original, size_t copy, const std::vector<sketch::Ref> &how, int step, Id id, int step2 = 0) {
            const sketch::SplineData &a = s.splines[s.elements[original].SplineIndex()], &b = s.splines[s.elements[copy].SplineIndex()];
            const auto tie = [&](size_t refs) {
                cs::Constraint c{sketch::Kind::Copy, {{original}, {copy}}};
                c.refs.insert(c.refs.end(), how.begin(), how.begin() + (std::ptrdiff_t)std::min(how.size(), refs));
                c.variant = step;
                return c;
            };
            std::vector<size_t> from = a.tangent, to = b.tangent;
            if (a.fitted) {
                from.push_back(a.fitted->angle);
                to.push_back(b.fitted->angle);
            }
            for (size_t t = 0; t < from.size(); ++t) {
                cs::Constraint c = tie(2);
                c.held = {from[t], to[t]};
                AddConstraint(c, id(t));
            }
            for (size_t k = 0; k < a.curvature.size(); ++k)
                if (a.curvature[k] != sketch::none && b.CurvatureAt(k) != sketch::none) {
                    cs::Constraint c = tie(2);
                    c.held = {a.curvature[k], b.curvature[k]};
                    AddConstraint(c, id(from.size() + 1 + k));
                }
            if (a.fitted) {
                cs::Constraint c = tie(how.size());
                c.heldPoints = {a.fitted->origin, b.fitted->origin};
                c.variant2 = step2;
                AddConstraint(c, id(from.size()));
            }
        }
        void TiePosition(const sketch::Copies::Position &x, const std::optional<std::string> &id);

        // constraintsketchoffsets.cpp
        [[noreturn]] void OffsetUnsolved(const std::string &call, const sketch::OffsetChain &source, const EdgeList &edges, double ad,
                                         const std::vector<int> &towards) const;
        cs::OffsetInfo OffsetRecord(const std::vector<std::string> &made, const std::vector<std::string> &tieIds, const sketch::OffsetChain &source,
                                    const std::vector<int> &towards, const OffsetForms &forms, const sketch::OffsetSides &sides,
                                    const std::vector<std::pair<std::string, std::string>> &curveIds) const;
        void CheckNoOffsetFrom(const std::string &id, const std::string &part) const;
        void CheckHoldsNoOffset(const std::string &part) const;
        std::optional<std::string> OffsetWithoutPieces(const std::string &id) const;
        void OffsetCurveCut(size_t a, const std::vector<size_t> &pieces);
        bool TiesOffsets() const override { return true; }
        size_t OffsetElement(double ad, const Options &opt, bool slot) override;
        void TieOffset(sketch::OffsetTie how, std::vector<sketch::Ref> refs, double sign, const std::string &id) override;
        void OffsetMade(const std::string &name, const std::vector<std::string> &made, const std::vector<std::string> &tieIds, const sketch::OffsetChain &source,
                        const EdgeList &edges, double ad, const std::vector<int> &towards, const OffsetForms &forms,
                        const sketch::OffsetSides &sides,
                        const std::vector<std::pair<std::string, std::string>> &curveIds, const std::string &call) override;

        // constraintsketchrelations.cpp
        void RelationsOfCall();
        std::vector<cs::Constraint> AddedRelations(const AutoConstraint &o, std::optional<double> tol, const char *call, bool newAxes);
        std::vector<std::pair<std::string, std::vector<std::string>>> HelpersAdded(const std::vector<sketch::FoundAxis> &axes,
                                                                                   const std::vector<sketch::FoundPitchCircle> &pitches) const;
        void DropUnrelating(const std::vector<std::pair<std::string, std::vector<std::string>>> &helpers);
        std::vector<std::pair<cs::Constraint, std::string>> RemovingDimensions(std::optional<double> tol = std::nullopt);
        std::vector<cs::Constraint> DimensionCandidates(std::optional<double> tol = std::nullopt) const;
        void Positions(const sketch::Ref &p, size_t unknown, std::optional<double> tol, std::vector<cs::Constraint> &out) const;
        void AddDimensions(const std::vector<std::pair<cs::Constraint, std::string>> &found);
        cs::Suggestion Suggested(const cs::Constraint &c, const std::string &removes) const;

        // constraintsketchreports.cpp
        std::vector<std::pair<std::string, std::string>> UnknownLabels() const;

        // a step of a solve that turns a line or arc by more than this is halved; by more than branchTurn the element
        // turned round, to the other branch
        static constexpr double smoothTurn = std::numbers::pi / 6.0, branchTurn = std::numbers::pi / 2.0;
        std::vector<std::string> constraintIds;
        std::set<std::string> impliedIds;
        size_t callElements = 0, removedInCall = 0;
        std::vector<std::string> touched;
        std::map<std::string, std::vector<std::string>> companions;
        std::map<std::string, cs::PatternInfo> patterns;
        std::map<std::string, cs::ChamferInfo> chamfers;
        std::map<std::string, cs::OffsetInfo> offsetInfos;
        std::map<std::string, cs::MirrorInfo> mirrors;
        std::vector<std::pair<std::string, std::string>> mirroredInCall;
        std::map<std::string, std::string> copyOfCall;
        std::optional<std::vector<std::string>> cutRedundant;
        std::vector<std::tuple<std::string, sketch::Ref, sketch::Kind>> fixedCorners;
        size_t offsetDof = 0, offsetFirstTie = 0;
        cs::Pending pending;
        std::optional<ConstraintAnalysis> analysis;
        std::map<std::string, std::vector<std::string>> conflicts;
    };

}
