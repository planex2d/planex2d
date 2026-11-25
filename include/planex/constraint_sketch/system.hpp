// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "planex/constraint_sketch/analysis.hpp"
#include "planex/constraint_sketch/constraint.hpp"
#include "planex/constraint_sketch/dual.hpp"
#include "planex/constraint_sketch/eval.hpp"
#include "planex/constraint_sketch/expression.hpp"
#include "planex/constraint_sketch/rankbasis.hpp"
#include "planex/constraint_sketch/shapedsystem.hpp"
#include "planex/constraint_sketch/solver.hpp"
#include "planex/core/disjointsets.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/sketch/curveeval.hpp"
#include "planex/sketch/geometry.hpp"

namespace planex2d::cs {

    using sketch::Element, sketch::ElementKind, sketch::Kind, sketch::KindName, sketch::OffsetCurveData, sketch::OffsetTie, sketch::Part,
        sketch::Ref, sketch::SplineData, sketch::SplinePiece;

    class System : public sketch::Geometry {
    public:
        std::vector<Constraint> constraints;
        // per value, for the solver: the change that counts as much as a unit change of a length, and whether it is
        // fixed (held, or a point merged into another); set by Build
        std::vector<double> scale;
        std::vector<char> fixed;

        // undo everything added after the mark
        // the values too: a solve moves those there were
        struct Mark {
            Geometry::Mark geometry;
            size_t constraints;
            std::vector<double> values;
        };

        System() = default;
        explicit System(const sketch::Geometry &g) : sketch::Geometry(g) {}

        // system.cpp
        size_t Add(Constraint c);
        void Refreeze(size_t c);
        void RemoveConstraint(size_t c);
        double CurvatureAt(const Ref &r, size_t point);
        void RemoveElement(size_t element);
        bool IsMerge(const Constraint &c) const;
        Mark Marked() const { return {Geometry::Marked(), constraints.size(), values}; }
        void Truncate(const Mark &m);
        bool Sideless(const Constraint &c) const;
        // an Angle of one arc: its sweep, in degrees, negative for a clockwise arc
        bool IsSweep(const Constraint &c) const { return c.kind == Kind::Angle && c.refs.size() == 1 && c.refs[0].IsArc(*this); }
        const char *SweepOutside(const Constraint &c, double v) const;
        double Measured(const Constraint &c) const;
        std::optional<size_t> DriverOf(size_t c) const;
        std::string DomainError(const Constraint &c) const;
        void Link(size_t c, double v);
        void Unlink(size_t c);
        double Linked(const Constraint &c) const;
        bool Fits(const Constraint &c) const;
        double DimensionValue(size_t k) const;
        std::vector<double> LengthValues() const;
        bool IsFixed(const sketch::Ref &point, sketch::Kind kind) const;
        void Refix(size_t c);
        bool IsReferenced(size_t element) const;
        std::string PlaceKeyOf(sketch::Kind kind, const std::vector<sketch::Ref> &refs) const;
        std::set<std::string> ConstraintKeys() const;
        std::optional<size_t> CurveUsing(size_t k) const;

        // systembuild.cpp
        void Build();
        void RefitCurves();
        size_t Count() const { return constraints.size() + structural.size(); }
        const std::vector<size_t> &StructuralOwners() const { return owner; }
        const std::vector<std::pair<size_t, size_t>> &ValueEquations() const { return valueRows; }
        size_t Rows(size_t c) const { return rows[c]; }
        const std::vector<size_t> &Indices(size_t c) const { return indices[c]; }
        const std::vector<RowBlock> &Blocks(size_t c) const { return blocks[c]; }
        double RowScale(size_t c) const;
        SolveResult Solve(bool predict = false, const std::vector<double> *origin = nullptr);
        SolveResult SolveDragged(const std::vector<size_t> &dragged, bool held);
        ConstraintAnalysis Analyze(bool complete = true, bool built = false);
        std::pair<Unknowns, Eigen::MatrixXd> Motions();

        // systemrows.cpp
        void Evaluate(size_t c, const std::vector<double> &v, double *r, double *J) const;

        // systemrank.cpp
        std::vector<char> RedundantFrom(size_t first);
        std::vector<char> Redundancies(const ConstraintAnalysis &a) const;
        std::vector<std::vector<size_t>> Dependencies(const ConstraintAnalysis &a) const;
        std::vector<size_t> MadeRedundant(const std::vector<char> &now, const Mark &mark) const;

    private:
        // system.cpp
        void MovePointsApart(const Constraint &c);
        void GiveTangents(Constraint &c);
        bool LacksTangent(size_t element, size_t k) const;
        void GiveTangent(Constraint &c, size_t element, size_t k, const Vertex &t);
        Vertex DirectionAt(const Ref &r, size_t point) const;
        void GiveCurvature(Constraint &c, size_t element, size_t k, double curvature);
        void GiveParams(Constraint &c);
        void Remerge();
        void GiveFrame(Constraint &c);
        void CheckMeasurable(const Constraint &c) const { Measured(c); }
        void HeldOf(const Element &e, Kind kind, std::vector<size_t> &points, std::vector<size_t> &others) const;
        void CheckRef(const Ref &r, Kind k) const;

        // systembuild.cpp
        void AddArcEquations();
        void AddCurveEquations();
        void AddValueEquations();
        double LeverOf(const Constraint &c) const;
        double Weight(const Constraint &c) const;
        Eval PlainEval() const { return Eval{*this, sharedCurves, values, noLocals, builtSize}; }
        Unknowns SolverUnknowns() const;
        const Constraint &At(size_t c) const { return c < constraints.size() ? constraints[c] : structural[c - constraints.size()]; }
        std::vector<RowBlock> BlocksOf(const Constraint &c) const;
        // the unknowns a whole constraint is evaluated with outside the solver: none for a pointwise one, which is
        // evaluated there block by block and here only for its values
        std::vector<size_t> LocalsOf(const Constraint &c) const { return IsPointwise(c) ? std::vector<size_t>{} : IndicesOf(c); }
        std::vector<size_t> IndicesOf(const Constraint &c) const;
        std::vector<size_t> UnknownsReadBy(const Constraint &c) const;

        // systemrows.cpp
        bool IsPointwise(const Constraint &c) const;
        std::optional<size_t> Joint(const Ref &a, const Ref &b) const;
        std::vector<size_t> JoinPoints(const Element &e) const;
        bool HasEnd(const Ref &r, size_t point) const;
        std::optional<size_t> JointOf(const Constraint &c) const;
        double Sense(const Ref &a, const Ref &b, size_t joint, const Eval &e) const;
        size_t Residuals(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const;
        std::optional<size_t> RowsOf(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const;
        std::optional<size_t> CoincidentRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> HorizontalVerticalRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> ParallelPerpendicularRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> TangentRows(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const;
        std::optional<size_t> CurvatureRows(const Constraint &c, const Eval &e, std::vector<Dual> &r, std::optional<size_t> joint) const;
        std::optional<size_t> CurvatureRadiusRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> EqualRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> SymmetricRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> SymmetricCarriersRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> ConcentricRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> FixRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> DistanceRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> RadiusRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> AngleRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> RigidGroupRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const { return AllHeldRows(c, e, r); }
        size_t HeldRows(const Constraint &c, const Eval &e, size_t k, Dual *r) const;
        size_t AllHeldRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> CopyRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<DualVector> CopiedPoint(const Constraint &c, const Eval &e, const DualVector &p) const;
        std::optional<size_t> OnCircleRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> OnCurveRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> OffsetRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> ExpressionRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;
        std::optional<size_t> ValueRows(const Constraint &c, const Eval &e, std::vector<Dual> &r) const;

        // systemfreeze.cpp
        void Freeze(Constraint &c, const Eval &e) const;
        void FreezeSmallest(Constraint &c, const Eval &e, int variants) const;
        void FreezeSide(Constraint &c, const Eval &e) const;
        void FreezeAngle(Constraint &c, const Eval &e) const;
        void FreezeSymmetric(Constraint &c, const Eval &e) const;
        void FreezeFix(Constraint &c, const Eval &e) const;

        // systemrank.cpp
        bool TakesRank(size_t c, RankBasis &basis) const;
        std::vector<size_t> MergesBetween(const std::vector<std::array<size_t, 3>> &merges, size_t p, size_t q) const;

        std::vector<Constraint> structural;
        std::vector<size_t> owner;
        std::vector<std::pair<size_t, size_t>> valueRows;
        std::vector<std::vector<size_t>> indices;
        std::vector<std::vector<RowBlock>> blocks;
        std::vector<size_t> rows;
        std::vector<double> levers;
        std::vector<std::optional<size_t>> joints;
        double builtSize = 1.0;
        AnalysisCache analysisCache;
        mutable SharedCurves sharedCurves;
        static inline const std::vector<size_t> noLocals{};
    };

}
