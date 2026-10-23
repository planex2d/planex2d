// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include "constraint_sketch/sparsecholesky.hpp"
#include "core/config.hpp"
#include "core/disjointsets.hpp"

// A system of constraints on unknowns, solved by damped Gauss-Newton (Levenberg-Marquardt) steps of minimal norm from the
// current values, so that what no constraint determines stays as it is. The System type is anything with
//   size_t Count() const                                      the number of constraints
//   size_t Rows(size_t c) const                               the residual rows of constraint c
//   const std::vector<size_t> &Indices(size_t c) const       the unknowns constraint c depends on
//   void Evaluate(size_t c, const std::vector<double> &x, double *r, double *J) const
//                                                             its residuals and their derivatives with respect to its
//                                                             unknowns (Rows(c) x Indices(c).size(), row major)
// Residuals are lengths, so that one tolerance relative to the size of the geometry fits all of them.

namespace planex2d {

    struct Unknowns {
        std::vector<double> value;
        // the change that counts as much as a unit change of a length: 1 for lengths, 1 / size for angles
        std::vector<double> scale;
        std::vector<char> fixed;
        explicit Unknowns(std::vector<double> v = {}) : value(std::move(v)), scale(value.size(), 1.0), fixed(value.size(), 0) {}
    };

    // the constraints that share free unknowns, directly or through others, with those unknowns
    struct ConstraintComponent {
        std::vector<size_t> constraints, unknowns;
    };

    template <class System>
    struct ConstraintComponents {
        std::vector<ConstraintComponent> parts;
        // constraints on fixed unknowns only: nothing can change them
        std::vector<size_t> constant;

        ConstraintComponents(const System &system, const Unknowns &x) {
            const size_t n = x.value.size();
            DisjointSets sets(n);
            for (size_t c = 0; c < system.Count(); ++c) {
                size_t first = n;
                for (size_t i : system.Indices(c)) {
                    if (x.fixed[i])
                        continue;
                    if (first == n)
                        first = sets.Find(i);
                    else
                        sets.Join(i, first);
                }
            }
            std::vector<size_t> part(n, n);
            std::vector<char> touched(n, 0);
            for (size_t c = 0; c < system.Count(); ++c) {
                size_t root = n;
                for (size_t i : system.Indices(c))
                    if (!x.fixed[i]) {
                        root = sets.Find(i);
                        touched[i] = 1;
                    }
                if (root == n) {
                    constant.push_back(c);
                    continue;
                }
                if (part[root] == n) {
                    part[root] = parts.size();
                    parts.emplace_back();
                }
                parts[part[root]].constraints.push_back(c);
            }
            for (size_t i = 0; i < n; ++i)
                if (touched[i])
                    parts[part[sets.Find(i)]].unknowns.push_back(i);
        }
    };

    // the residuals and their derivatives with respect to the scaled free unknowns of one component; J has every
    // derivative a constraint has, zero or not, so that its pattern stays the same
    template <class System>
    struct ComponentEquations {
        const System &system;
        const ConstraintComponent &part;
        std::vector<size_t> column;
        size_t m = 0;
        // J's pattern, all zero, and per row of a constraint and unknown of it the entry of J (-1: a fixed unknown)
        SparseMatrix pattern;
        std::vector<Eigen::Index> entry;

        ComponentEquations(const System &s, const ConstraintComponent &p, size_t nunknowns)
            : system(s), part(p), column(nunknowns, nunknowns) {
            for (size_t k = 0; k < part.unknowns.size(); ++k)
                column[part.unknowns[k]] = k;
            std::vector<Eigen::Triplet<double>> entries;
            for (size_t c : part.constraints) {
                for (size_t i = 0; i < system.Rows(c); ++i)
                    for (size_t u : system.Indices(c))
                        if (column[u] != column.size())
                            entries.emplace_back(m + i, column[u], 0.0);
                m += system.Rows(c);
            }
            pattern.resize(Eigen::Index(m), Eigen::Index(part.unknowns.size()));
            pattern.setFromTriplets(entries.begin(), entries.end());
            size_t row = 0;
            for (size_t c : part.constraints) {
                for (size_t i = 0; i < system.Rows(c); ++i)
                    for (size_t u : system.Indices(c)) {
                        if (column[u] == column.size()) {
                            entry.push_back(-1);
                            continue;
                        }
                        const int *begin = pattern.innerIndexPtr() + pattern.outerIndexPtr()[column[u]];
                        const int *end = pattern.innerIndexPtr() + pattern.outerIndexPtr()[column[u] + 1];
                        entry.push_back(pattern.outerIndexPtr()[column[u]] + (std::lower_bound(begin, end, (int)(row + i)) - begin));
                    }
                row += system.Rows(c);
            }
        }

        // J has the pattern
        void Evaluate(const Unknowns &x, Eigen::VectorXd &r, SparseMatrix &J) const {
            r.setZero(Eigen::Index(m));
            std::fill(J.valuePtr(), J.valuePtr() + J.nonZeros(), 0.0);
            std::vector<double> local;
            size_t row = 0, e = 0;
            for (size_t c : part.constraints) {
                const std::vector<size_t> &idx = system.Indices(c);
                const size_t rows = system.Rows(c);
                local.assign(rows * idx.size(), 0.0);
                system.Evaluate(c, x.value, r.data() + row, local.data());
                for (size_t k = 0; k < local.size(); ++k, ++e)
                    if (entry[e] >= 0)
                        J.valuePtr()[entry[e]] += local[k] * x.scale[idx[k % idx.size()]];
                row += rows;
            }
        }
    };

    // NaN (an expression out of its domain) stays: nothing that compares with it counts as solved
    template <class Vector>
    inline double MaxAbs(const Vector &v) {
        double m = 0.0;
        for (double x : v)
            m = std::isnan(x) || std::abs(x) > m ? std::abs(x) : m;
        return m;
    }

    struct SolveResult {
        bool solved = true;
        size_t iterations = 0;
        double residual = 0.0;
        // asked for: the largest change of an unknown (scaled to a length) the linear prediction makes, the undamped
        // smallest step from where the solve started
        double predicted = 0.0;
    };

    // Levenberg's damping (lambda I, not Marquardt's diagonal) keeps every step free of components along which the
    // residuals do not change, so each step is the smallest change that reduces them
    template <class System>
    inline SolveResult SolveComponent(const ComponentEquations<System> &eq, Unknowns &x, double size, bool predict = false,
                                      const std::vector<double> *origin = nullptr) {
        const double target = config::solve_tol * size;
        const size_t m = eq.m, n = eq.part.unknowns.size();
        // the smaller of J J^T (m < n) and J^T J: the same step by (J^T J + l I)^-1 J^T = J^T (J J^T + l I)^-1
        const bool wide = m < n;
        SolveResult result;
        Eigen::VectorXd r, rNew, g, z, delta;
        SparseMatrix J = eq.pattern, JNew = eq.pattern;
        eq.Evaluate(x, r, J);
        result.residual = MaxAbs(r);
        result.solved = result.residual <= target;
        if (result.solved && result.residual <= std::numeric_limits<double>::epsilon() * size)
            return result;
        NormalMatrix normal(J, wide);
        SparseCholesky cholesky;
        double F = 0.5 * r.squaredNorm();
        double lambda = -1.0, nu = 2.0, maxDiagonal = 0.0;
        double polished = std::numeric_limits<double>::infinity();
        // the rounding of the geometry's coordinates: the kernel compares within Tol of the coordinates where they are,
        // which can be below Tol(size)
        const double floor = std::numeric_limits<double>::epsilon() * size;
        Unknowns trial = x;
        // once solved, Newton steps toward the solution closest to the start (origin, if given) take back what the steps
        // moved along directions no constraint determines: minimal steps keep those values only to first order
        std::vector<double> start(n);
        for (size_t j = 0; j < n; ++j)
            start[j] = (origin ? *origin : x.value)[eq.part.unknowns[j]];
        Eigen::VectorXd e = Eigen::VectorXd::Zero(Eigen::Index(n));
        bool pull = true, pulling = false;
        double pulled = std::numeric_limits<double>::infinity();
        // the solved values the pull started from: kept if it does not end solved
        std::vector<double> solvedAt(n);
        const auto endPull = [&] {
            pulling = false;
            if (MaxAbs(r) <= target)
                return;
            for (size_t j = 0; j < n; ++j)
                x.value[eq.part.unknowns[j]] = solvedAt[j];
            eq.Evaluate(x, r, J);
        };
        while (true) {
            result.residual = MaxAbs(r);
            result.solved = result.residual <= target;
            if (result.solved && pull) {
                pull = false;
                pulling = true;
                for (size_t j = 0; j < n; ++j)
                    solvedAt[j] = x.value[eq.part.unknowns[j]];
            }
            // then nearly undamped steps take the residuals down to rounding while they drop: a point constrained onto
            // a curve then lies on it within the Tol the rest of planex compares with
            if (result.solved && !pulling && (result.residual <= floor || result.residual >= polished))
                return result;
            if (result.solved && !pulling)
                polished = result.residual;
            if (result.solved || pulling)
                lambda = std::min(lambda, 1e-12 * maxDiagonal);
            if (result.iterations == config::solve_max_iterations) {
                if (pulling) {
                    endPull();
                    result.residual = MaxAbs(r);
                    result.solved = result.residual <= target;
                }
                return result;
            }
            ++result.iterations;
            g = J.transpose() * r;
            normal.Assemble(J);
            if (lambda < 0.0) {
                for (size_t a = 0; a < (wide ? m : n); ++a)
                    maxDiagonal = std::max(maxDiagonal, normal.A.coeff(a, a) + normal.V.row(a).squaredNorm());
                if (maxDiagonal == 0.0)
                    return result;
                cholesky.Analyze(normal.A);
                // the Gauss-Newton step, damped only as much as it takes to be solvable
                for (double tiny = 1e-12; predict && tiny <= 1e-3; tiny *= 1e3) {
                    if (!cholesky.Factor(normal.A, tiny * maxDiagonal, normal.V))
                        continue;
                    z = wide ? r : g;
                    cholesky.Solve(z);
                    result.predicted = wide ? (J.transpose() * z).cwiseAbs().maxCoeff() : z.cwiseAbs().maxCoeff();
                    break;
                }
                // a small start: the values are a good guess, the drawing; solved already: nearly undamped
                lambda = (result.solved ? 1e-12 : 1e-3) * maxDiagonal;
            }
            if (!cholesky.Factor(normal.A, lambda, normal.V)) {
                if (pulling) {
                    endPull();
                    continue;
                }
                lambda *= nu;
                nu *= 2.0;
                continue;
            }
            // the step d with J d = -r and the least change from the start: d = -J^+ (r - J e) - e, e the change so far
            if (pulling)
                for (size_t j = 0; j < n; ++j)
                    e[j] = (x.value[eq.part.unknowns[j]] - start[j]) / x.scale[eq.part.unknowns[j]];
            z = pulling ? Eigen::VectorXd(r - J * e) : r;
            if (!wide)
                z = pulling ? Eigen::VectorXd(J.transpose() * z) : g;
            cholesky.Solve(z);
            if (wide)
                delta = -(J.transpose() * z);
            else
                delta = -z;
            if (pulling)
                delta -= e;
            double step = 0.0, predicted = 0.0;
            for (size_t j = 0; j < n; ++j) {
                const size_t u = eq.part.unknowns[j];
                trial.value[u] = x.value[u] + x.scale[u] * delta[j];
                step = std::max(step, std::abs(x.scale[u] * delta[j]));
                predicted += 0.5 * delta[j] * (lambda * delta[j] - g[j]);
            }
            // Newton's steps toward the closest solution shrink until they reach rounding
            if (pulling) {
                if (step > floor && step < pulled) {
                    for (size_t u : eq.part.unknowns)
                        x.value[u] = trial.value[u];
                    eq.Evaluate(x, r, J);
                    pulled = step;
                } else
                    endPull();
                F = 0.5 * r.squaredNorm();
                continue;
            }
            if (step <= (result.solved ? floor : Tol(size)))
                return result;
            eq.Evaluate(trial, rNew, JNew);
            const double FNew = 0.5 * rNew.squaredNorm();
            const double rho = predicted > 0.0 ? (F - FNew) / predicted : -1.0;
            if (rho > 0.0) {
                for (size_t u : eq.part.unknowns)
                    x.value[u] = trial.value[u];
                std::swap(r, rNew);
                std::swap(J, JNew);
                F = FNew;
                const double t = 2.0 * rho - 1.0;
                lambda *= std::max(1.0 / 3.0, 1.0 - t * t * t);
                nu = 2.0;
            } else {
                if (result.solved)
                    return result;
                lambda *= nu;
                nu *= 2.0;
            }
        }
    }

    // moves the free unknowns until every residual is below config::solve_tol * size; size is the size of the geometry.
    // Unsolved: the unknowns are left where the residuals were smallest, the caller restores them if it needs to.
    // origin: the values to stay closest to, if not x's (a walk in steps stays closest to where it began)
    template <class System>
    inline SolveResult SolveConstraints(const System &system, Unknowns &x, double size, bool predict = false,
                                        const std::vector<double> *origin = nullptr) {
        const ConstraintComponents<System> components(system, x);
        SolveResult result;
        std::vector<double> r;
        for (size_t c : components.constant) {
            r.assign(system.Rows(c), 0.0);
            std::vector<double> J(system.Rows(c) * system.Indices(c).size());
            system.Evaluate(c, x.value, r.data(), J.data());
            result.residual = std::max(result.residual, MaxAbs(r));
            if (!(MaxAbs(r) <= config::solve_tol * size))
                result.solved = false;
        }
        for (const ConstraintComponent &part : components.parts) {
            const SolveResult p = SolveComponent(ComponentEquations<System>(system, part, x.value.size()), x, size, predict, origin);
            result.solved = result.solved && p.solved;
            result.iterations = std::max(result.iterations, p.iterations);
            result.residual = std::max(result.residual, p.residual);
            result.predicted = std::max(result.predicted, p.predicted);
        }
        return result;
    }

}
