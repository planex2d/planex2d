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

#include "planex/constraint_sketch/equations.hpp"
#include "planex/constraint_sketch/sparsecholesky.hpp"
#include "planex/core/config.hpp"

// A system of constraints (equations.hpp) is solved by damped Gauss-Newton (Levenberg-Marquardt) steps of minimal norm
// from the current values, so that what no constraint determines stays as it is.

namespace planex2d {

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
        // asked for: the largest change of an unknown of the geometry (scaled to a length; the unknowns that follow, if
        // any do: not a ShapedSystem's slacks) the linear prediction makes, the undamped smallest step from where the
        // solve started
        double predicted = 0.0;
    };

    // Levenberg's damping (lambda I, not Marquardt's diagonal) keeps every step free of components along which the
    // residuals do not change, so each step is the smallest change that reduces them
    template <class System>
    class ComponentSolver {
    public:
        // residuals, derivatives: of x as it is
        ComponentSolver(const ComponentEquations<System> &eq, Unknowns &x, double size, const std::vector<double> *origin,
                        Eigen::VectorXd residuals, SparseMatrix derivatives)
            : eq(eq), x(x), size(size), target(config::state->solve_tol * size),
              floor(std::numeric_limits<double>::epsilon() * size), n(eq.part.unknowns.size()), r(std::move(residuals)),
              J(std::move(derivatives)), JNew(eq.pattern), normal(eq.pattern), F(0.5 * r.squaredNorm()), trial(x), start(n),
              e(Eigen::VectorXd::Zero(Eigen::Index(n))), roundStart(n), solvedAt(n) {
            for (size_t j = 0; j < n; ++j)
                start[j] = (origin ? *origin : x.value)[eq.part.unknowns[j]];
        }

        SolveResult Solve(bool predict) {
            while (true) {
                UpdateResult();
                if (result.solved && pull)
                    StartPull();
                // then nearly undamped steps take the residuals down to rounding while they drop: a point constrained
                // onto a curve then lies on it within the Tol the rest of planex compares with
                if (result.solved && !pulling && (result.residual <= floor || result.residual >= polished))
                    return result;
                if (result.solved && !pulling)
                    polished = result.residual;
                if (result.solved || pulling)
                    lambda = std::min(lambda, 1e-12 * maxDiagonal);
                if (result.iterations == config::state->solve_max_iterations) {
                    if (pulling) {
                        EndPull();
                        UpdateResult();
                    }
                    return result;
                }
                ++result.iterations;
                g = J.transpose() * r;
                normal.Assemble(J);
                if (lambda < 0.0) {
                    maxDiagonal = normal.MaxDiagonal();
                    if (maxDiagonal == 0.0)
                        return result;
                    StartDamping(predict);
                }
                if (!normal.Factor(lambda)) {
                    if (pulling) {
                        EndPull();
                        continue;
                    }
                    lambda *= nu;
                    nu *= 2.0;
                    continue;
                }
                if (pulling)
                    PullStep();
                else if (!LevenbergStep())
                    return result;
            }
        }

    private:
        static constexpr size_t rounds = 50;
        const ComponentEquations<System> &eq;
        Unknowns &x;
        const double size, target;
        // the rounding of the geometry's coordinates: the kernel compares within Tol of the coordinates where they are,
        // which can be below Tol(size)
        const double floor;
        const size_t n;
        SolveResult result;
        Eigen::VectorXd r, rNew, g, delta;
        SparseMatrix J, JNew;
        NormalEquations normal;
        double F;
        double lambda = -1.0, nu = 2.0, maxDiagonal = 0.0;
        double polished = std::numeric_limits<double>::infinity();
        Unknowns trial;
        // once solved, Newton steps toward the solution closest to the start (origin, if given) take back what the steps
        // moved along directions no constraint determines: minimal steps keep those values only to first order
        std::vector<double> start;
        Eigen::VectorXd e;
        bool pull = true, pulling = false;
        double pulled = std::numeric_limits<double>::infinity();
        // the pull rounds: the unknowns that follow start the next round where this one left them; a round that does not
        // end solved (its target far, the steps toward it short of rounding) goes back to where it began
        size_t round = 0;
        std::vector<double> roundStart;
        // the solved values the pull started from: kept if it does not end solved
        std::vector<double> solvedAt;

        void UpdateResult() {
            result.residual = MaxAbs(r);
            result.solved = result.residual <= target;
        }

        void StartDamping(bool predict) {
            normal.Analyze();
            if (predict)
                PredictMove();
            // a small start: the values are a good guess, the drawing; solved already: nearly undamped
            lambda = (result.solved ? 1e-12 : 1e-3) * maxDiagonal;
        }

        // the Gauss-Newton step, damped only as much as it takes to be solvable
        void PredictMove() {
            for (double tiny = 1e-12; tiny <= 1e-3; tiny *= 1e3) {
                if (!normal.Factor(tiny * maxDiagonal))
                    continue;
                const Eigen::VectorXd gaussNewton = normal.Solved(J, r, g);
                for (size_t j = 0; j < n; ++j)
                    if (x.follow.empty() || x.follow[eq.part.unknowns[j]])
                        result.predicted = std::max(result.predicted, std::abs(gaussNewton[j]));
                break;
            }
        }

        double Try() {
            double step = 0.0;
            for (size_t j = 0; j < n; ++j) {
                const size_t u = eq.part.unknowns[j];
                trial.value[u] = x.value[u] + x.scale[u] * delta[j];
                step = std::max(step, std::abs(x.scale[u] * delta[j]));
            }
            return step;
        }

        // false: no step from here brings the residuals down, the solve ends
        bool LevenbergStep() {
            delta = -normal.Solved(J, r, g);
            const double step = Try();
            const bool short_ = step <= (result.solved ? floor : Tol(size));
            if (short_ && result.solved)
                return false;
            eq.Evaluate(trial, rNew, JNew);
            // a short step ends the solve only if it does not bring the residuals down: a small sketch (Tol's floor above
            // its size) or a system of two scales (ShapedSystem) still converges in steps that short
            if (short_ && !(MaxAbs(rNew) < MaxAbs(r)))
                return false;
            double predicted = 0.0;
            for (size_t j = 0; j < n; ++j)
                predicted += 0.5 * delta[j] * (lambda * delta[j] - g[j]);
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
                    return false;
                lambda *= nu;
                nu *= 2.0;
            }
            return true;
        }

        // Newton's steps toward the closest solution shrink until they reach rounding
        void PullStep() {
            // the step d with J d = -r and the least change from the start: d = -J^+ (r - J e) - e, e the change so far
            for (size_t j = 0; j < n; ++j)
                e[j] = (x.value[eq.part.unknowns[j]] - start[j]) / x.scale[eq.part.unknowns[j]];
            delta = -normal.Solved(J, r - J * e);
            delta -= e;
            // the damping leaves lambda (J J^T + lambda)^-1 (r - J e) of the constraints' change undone, far above the
            // target when the change so far (e) is large; the same solve of what is left takes it to its square
            delta -= normal.Solved(J, r + J * delta);
            const double step = Try();
            if (step > floor && step < pulled) {
                for (size_t u : eq.part.unknowns)
                    x.value[u] = trial.value[u];
                eq.Evaluate(x, r, J);
                pulled = step;
            } else if (!NextRound())
                EndPull();
            F = 0.5 * r.squaredNorm();
        }

        void StartPull() {
            pull = false;
            pulling = true;
            for (size_t j = 0; j < n; ++j)
                solvedAt[j] = roundStart[j] = x.value[eq.part.unknowns[j]];
        }

        void EndPull() {
            pulling = false;
            if (MaxAbs(r) <= target)
                return;
            for (size_t j = 0; j < n; ++j)
                x.value[eq.part.unknowns[j]] = solvedAt[j];
            eq.Evaluate(x, r, J);
        }

        bool NextRound() {
            if (MaxAbs(r) > target) {
                for (size_t j = 0; j < n; ++j)
                    x.value[eq.part.unknowns[j]] = roundStart[j];
                eq.Evaluate(x, r, J);
            }
            if (x.follow.empty() || ++round == rounds)
                return false;
            double moved = 0.0;
            for (size_t j = 0; j < n; ++j)
                if (x.follow[eq.part.unknowns[j]])
                    moved = std::max(moved, std::abs(x.value[eq.part.unknowns[j]] - start[j]));
            if (moved <= floor)
                return false;
            for (size_t j = 0; j < n; ++j) {
                if (x.follow[eq.part.unknowns[j]])
                    start[j] = x.value[eq.part.unknowns[j]];
                roundStart[j] = x.value[eq.part.unknowns[j]];
            }
            pulled = std::numeric_limits<double>::infinity();
            return true;
        }
    };

    template <class System>
    inline SolveResult SolveComponent(const ComponentEquations<System> &eq, Unknowns &x, double size, bool predict = false,
                                      const std::vector<double> *origin = nullptr) {
        Eigen::VectorXd r;
        SparseMatrix J = eq.pattern;
        eq.Evaluate(x, r, J);
        SolveResult result;
        result.residual = MaxAbs(r);
        result.solved = result.residual <= config::state->solve_tol * size;
        if (result.solved && result.residual <= std::numeric_limits<double>::epsilon() * size)
            return result;
        return ComponentSolver<System>(eq, x, size, origin, std::move(r), std::move(J)).Solve(predict);
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
            if (!(MaxAbs(r) <= config::state->solve_tol * size))
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
