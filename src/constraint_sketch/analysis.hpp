// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Core>
#include <Eigen/SVD>

#include "constraint_sketch/solver.hpp"
#include "constraint_sketch/sparseqr.hpp"
#include "core/config.hpp"

// What the constraints of a System (see solver.hpp) leave free: the rank of their Jacobian, the degrees of freedom, the
// unknowns they determine and the constraints that depend on the others.

namespace planex2d {

    struct ConstraintAnalysis {
        size_t rank = 0;
        // the free unknowns minus the independent equations: how many ways the geometry can still move
        size_t dof = 0;
        // per unknown: no motion the constraints allow changes it (fixed ones count as determined)
        std::vector<char> determined;
        // per constraint: it depends on the others (for a solved system: it is redundant, the system over-constrained)
        std::vector<char> redundant;
        // the most a small change of the dimensions can move the geometry, relative to it: 1 / the smallest kept
        // singular value (lengths and angles times the size alike); large when nearly singular
        double amplification = 0.0;
        // false: only the rank, dof, redundant and amplification are there, the last as RowAmplification finds it, not
        // as the dense bases do; determined has the fixed unknowns only
        bool complete = true;
    };

    // an orthonormal basis (columns) of the v with A v = 0, singular values up to tol counting as zero
    inline Eigen::MatrixXd Kernel(const Eigen::MatrixXd &A, double tol) {
        const Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullV);
        Eigen::Index rank = 0;
        while (rank < svd.singularValues().size() && svd.singularValues()[rank] > tol)
            ++rank;
        return svd.matrixV().rightCols(A.cols() - rank);
    }

    // what the analysis of one component comes to, per unknown and constraint of it
    struct PartAnalysis {
        size_t rank = 0;
        double amplification = 0.0;
        std::vector<char> determined, redundant;
        // false: determined was not worked out (the amplification is RowAmplification's)
        bool complete = true;
        // false: it depends on more than the Jacobian (the second derivatives at a double root)
        bool local = true;
    };

    // PartAnalysis by a component's unknowns, Jacobian and size as they were: a component that no call touched comes
    // out the same. Of the last two passes only
    class AnalysisCache {
    public:
        const PartAnalysis *Find(const std::string &key) {
            if (const auto it = now.find(key); it != now.end())
                return &it->second;
            const auto it = seen.find(key);
            return it == seen.end() ? nullptr : &now.emplace(*it).first->second;
        }
        void Store(std::string key, PartAnalysis analysis) { now.emplace(std::move(key), std::move(analysis)); }
        void PassEnds() {
            seen = std::move(now);
            now.clear();
        }

    private:
        std::unordered_map<std::string, PartAnalysis> seen, now;
    };

    // the bytes of what a component's analysis follows from, with J's pattern and values as they are now
    template <class System>
    inline std::string PartKey(const ComponentEquations<System> &eq, const SparseMatrix &J, double size, bool complete) {
        std::string key;
        const auto add = [&](const auto *data, size_t count) { key.append((const char *)data, count * sizeof(*data)); };
        add(&size, 1);
        add(&complete, 1);
        add(eq.part.unknowns.data(), eq.part.unknowns.size());
        for (size_t c : eq.part.constraints) {
            const size_t rows = eq.system.Rows(c);
            add(&rows, 1);
        }
        add(J.outerIndexPtr(), (size_t)J.outerSize() + 1);
        add(J.innerIndexPtr(), (size_t)J.nonZeros());
        add(J.valuePtr(), (size_t)J.nonZeros());
        return key;
    }

    // at a double root (a fold: a semicircle on its two ends and its radius) a null motion of J is no motion, the
    // residuals grow with its square: the second derivatives along the null motions, seen by each left null vector. A
    // motion that one of them curves away from is blocked; a dependency with no curvature is one for good, a redundancy.
    // The motions and dependencies are replaced if some are blocked; the rank is raised by the number blocked
    template <class System>
    inline size_t BlockedMotions(const ComponentEquations<System> &eq, const Unknowns &x, SparseQR &qr, double size) {
        const ConstraintComponent &part = eq.part;
        const size_t n = part.unknowns.size();
        // null vectors are accurate to about eps / rank_tol (the smallest gap to a kept singular value), a real motion
        // moves its unknowns by far more than this
        const double zero = std::sqrt(config::rank_tol);
        const Eigen::MatrixXd &N = qr.motions, &Y = qr.dependencies;
        const Eigen::Index d = N.cols(), q = Y.cols();
        const double h = 1e-6 * size, tol = zero / size;
        Eigen::MatrixXd S(q * d, d), B(d * d, q);
        Eigen::VectorXd r;
        Unknowns moved = x;
        SparseMatrix plus = eq.pattern, minus = eq.pattern;
        for (Eigen::Index k = 0; k < d; ++k) {
            for (const double t : {h, -h}) {
                for (size_t j = 0; j < n; ++j) {
                    const size_t u = part.unknowns[j];
                    moved.value[u] = x.value[u] + t * x.scale[u] * N(Eigen::Index(j), k);
                }
                eq.Evaluate(moved, r, t > 0.0 ? plus : minus);
            }
            const Eigen::MatrixXd M = (SparseMatrix(plus - minus).transpose() * Y).transpose() * N / (2.0 * h);
            for (Eigen::Index a = 0; a < q; ++a)
                for (Eigen::Index l = 0; l < d; ++l) {
                    S(a * d + k, l) = M(a, l);
                    B(k * d + l, a) = M(a, l);
                }
        }
        const Eigen::MatrixXd K = Kernel(S, tol), C = Kernel(B, tol);
        if (K.cols() >= d && C.cols() >= q)
            return 0;
        qr.motions = N * K;
        qr.dependencies = Y * C;
        return (size_t)(d - K.cols());
    }

    // the analysis of one component, J its Jacobian at x; complete false: not the dense bases when its rows are
    // independent beyond doubt
    template <class System>
    inline PartAnalysis AnalyzePart(const ComponentEquations<System> &eq, const Unknowns &x, const SparseMatrix &J, double size, bool complete) {
        const size_t m = eq.m, n = eq.part.unknowns.size();
        const double zero = std::sqrt(config::rank_tol);
        PartAnalysis out;
        out.determined.assign(n, 0);
        out.redundant.assign(eq.part.constraints.size(), 0);
        // the rank as the singular values above rank_tol times the largest would give it
        const double largest = LargestSingularValue(J);
        if (!complete)
            if (const std::optional<double> amplification = RowAmplification(J, largest)) {
                out.rank = m;
                out.amplification = *amplification;
                out.complete = false;
                return out;
            }
        SparseQR qr(J, config::rank_tol * largest);
        qr.Kernels();
        size_t rank = (size_t)qr.rank;
        if (rank > 0)
            out.amplification = qr.Amplification();
        if (rank < n && rank < m) {
            rank += BlockedMotions(eq, x, qr, size);
            out.local = false;
        }
        out.rank = rank;
        for (size_t j = 0; j < n; ++j)
            out.determined[j] = qr.motions.row(Eigen::Index(j)).norm() <= zero;
        // a constraint is redundant if it takes no rank: without its rows the rank stays, so the dependencies
        // restricted to its rows have full rank
        size_t row = 0;
        for (size_t c = 0; c < eq.part.constraints.size(); ++c) {
            const size_t rows = eq.system.Rows(eq.part.constraints[c]);
            size_t spanned = 0;
            if (rows > 0 && qr.dependencies.cols() >= Eigen::Index(rows))
                for (double v : Eigen::JacobiSVD<Eigen::MatrixXd>(qr.dependencies.middleRows(Eigen::Index(row), Eigen::Index(rows))).singularValues())
                    spanned += v > zero;
            out.redundant[c] = rows > 0 && spanned == rows;
            row += rows;
        }
        return out;
    }

    // size: the size of the geometry, the scale of the second derivatives at a double root; with complete false a
    // component of independent rows skips the dense bases of its motions (all that determined and Y need); cache: the
    // components of the passes before that come out the same are not analysed again
    template <class System>
    inline ConstraintAnalysis AnalyzeConstraints(const System &system, const Unknowns &x, double size = 1.0, bool complete = true,
                                                 AnalysisCache *cache = nullptr) {
        const ConstraintComponents<System> components(system, x);
        ConstraintAnalysis analysis;
        analysis.determined.assign(x.value.size(), 0);
        analysis.redundant.assign(system.Count(), 0);
        for (size_t i = 0; i < x.value.size(); ++i)
            analysis.determined[i] = x.fixed[i];
        for (size_t c : components.constant)
            analysis.redundant[c] = system.Rows(c) > 0;
        size_t free = 0;
        for (char f : x.fixed)
            free += !f;
        for (const ConstraintComponent &part : components.parts) {
            const ComponentEquations<System> eq(system, part, x.value.size());
            if (eq.m == 0)
                continue;
            Eigen::VectorXd r;
            SparseMatrix J = eq.pattern;
            eq.Evaluate(x, r, J);
            const std::string key = cache ? PartKey(eq, J, size, complete) : std::string();
            const PartAnalysis *found = cache ? cache->Find(key) : nullptr;
            const PartAnalysis p = found ? *found : AnalyzePart(eq, x, J, size, complete);
            if (cache && !found && p.local)
                cache->Store(key, p);
            analysis.rank += p.rank;
            analysis.amplification = std::max(analysis.amplification, p.amplification);
            analysis.complete = analysis.complete && p.complete;
            for (size_t c = 0; c < part.constraints.size(); ++c)
                analysis.redundant[part.constraints[c]] = p.redundant[c];
            if (p.complete)
                for (size_t j = 0; j < part.unknowns.size(); ++j)
                    analysis.determined[part.unknowns[j]] = p.determined[j];
        }
        if (cache)
            cache->PassEnds();
        analysis.dof = free - analysis.rank;
        return analysis;
    }


}
