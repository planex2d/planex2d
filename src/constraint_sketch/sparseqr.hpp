// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <Eigen/Core>
#include <Eigen/QR>
#include <Eigen/SparseCore>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseQR>

#include "constraint_sketch/sparsecholesky.hpp"

namespace planex2d {

    // the largest eigenvalue of a symmetric positive semidefinite operator on vectors of the given size, by power
    // iteration from a start no symmetry of the geometry is orthogonal to, to about 1e-6
    template <class Operator>
    inline double LargestEigenvalue(Eigen::Index size, const Operator &apply) {
        Eigen::VectorXd v = Eigen::VectorXd::LinSpaced(size, 1.0, 2.0).normalized();
        double largest = 0.0;
        for (int k = 0; k < 100; ++k) {
            const Eigen::VectorXd w = apply(v);
            const double next = v.dot(w);
            if (!(next > largest * (1.0 + 1e-6)))
                return std::max(largest, next);
            largest = next;
            v = w.normalized();
        }
        return largest;
    }

    inline double LargestSingularValue(const SparseMatrix &J) {
        return std::sqrt(LargestEigenvalue(J.cols(), [&](const Eigen::VectorXd &v) { return Eigen::VectorXd(J.transpose() * (J * v)); }));
    }

    // J P = Q [R11 R12; 0 0]: Eigen's SparseQR (COLAMD ordering) with Heath's dead columns, a column whose part outside
    // the span of the kept columns before it is below tol takes no rank
    class SparseQR {
    public:
        Eigen::Index rank = 0;
        // after Kernels: orthonormal bases of the motions J allows (J N = 0) and of the dependencies among its rows (Y^T J = 0)
        Eigen::MatrixXd motions, dependencies;

        SparseQR(const SparseMatrix &J, double tol) : m(J.rows()), n(J.cols()) {
            // SparseQR keeps a column whose norm reaches the threshold: a zero column must not
            qr.setPivotThreshold(std::max(tol, std::numeric_limits<double>::min()));
            qr.compute(J);
            rank = qr.rank();
            const Eigen::SparseMatrix<double, Eigen::RowMajor> R = qr.matrixR();
            R11 = R.topLeftCorner(rank, rank);
            R12 = R.topRightCorner(rank, n - rank);
        }

        // the dense bases, as many columns as J has motions and dependencies
        void Kernels() {
            Eigen::MatrixXd basic(n, n - rank);
            basic.topRows(rank) = -R11.triangularView<Eigen::Upper>().solve(Eigen::MatrixXd(R12));
            basic.bottomRows(n - rank).setIdentity();
            motions = qr.colsPermutation() * Eigen::MatrixXd(Eigen::HouseholderQR<Eigen::MatrixXd>(basic).householderQ() *
                                                             Eigen::MatrixXd::Identity(n, n - rank));
            Eigen::MatrixXd unit = Eigen::MatrixXd::Zero(m, m - rank);
            unit.bottomRows(m - rank).setIdentity();
            dependencies = qr.matrixQ() * unit;
        }

        // 1 / the smallest kept singular value of J: the norm of its pseudo-inverse (I - N N^T) P [R11^-1; 0] Q1^T
        // (needs Kernels)
        double Amplification() const {
            const auto inverse = [&](const Eigen::VectorXd &c) {
                Eigen::VectorXd x = Eigen::VectorXd::Zero(motions.rows());
                x.head(rank) = R11.triangularView<Eigen::Upper>().solve(c);
                x = qr.colsPermutation() * x;
                return Eigen::VectorXd(x - motions * (motions.transpose() * x));
            };
            const auto adjoint = [&](const Eigen::VectorXd &x) {
                const Eigen::VectorXd y = qr.colsPermutation().transpose() * Eigen::VectorXd(x - motions * (motions.transpose() * x));
                return Eigen::VectorXd(R11.transpose().triangularView<Eigen::Lower>().solve(y.head(rank)));
            };
            return rank == 0 ? 0.0 : std::sqrt(LargestEigenvalue(rank, [&](const Eigen::VectorXd &c) { return adjoint(inverse(c)); }));
        }

    private:
        Eigen::Index m, n;
        Eigen::SparseQR<SparseMatrix, Eigen::COLAMDOrdering<int>> qr;
        Eigen::SparseMatrix<double, Eigen::RowMajor> R11, R12;
    };

    // 1 / the smallest singular value of J (largest: its largest) from the sparse LDL^T of J J^T, without the dense bases,
    // if the rows of J are independent beyond doubt: the factor is positive and the condition number (squared in
    // J J^T) far below 1 / rank_tol, so that every rank decision would give the same. Else nothing (also for a column of
    // J in many rows, which would make J J^T dense)
    inline std::optional<double> RowAmplification(const SparseMatrix &J, double largest) {
        constexpr double beyondDoubt = 1e4;
        for (Eigen::Index j = 0; j < J.cols(); ++j)
            if (J.col(j).nonZeros() > denseRows)
                return std::nullopt;
        const Eigen::SimplicialLDLT<SparseMatrix> ldlt(SparseMatrix(J * J.transpose()));
        if (ldlt.info() != Eigen::Success || !(ldlt.vectorD().array() > 0.0).all())
            return std::nullopt;
        const double amplification = std::sqrt(LargestEigenvalue(J.rows(), [&](const Eigen::VectorXd &v) { return Eigen::VectorXd(ldlt.solve(v)); }));
        return std::isfinite(amplification) && amplification * largest <= beyondDoubt ? std::optional<double>(amplification) : std::nullopt;
    }

}
