// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/sparseqr.hpp"

namespace planex2d {

    // 1 / the smallest kept singular value of J: the norm of its pseudo-inverse (I - N N^T) P [R11^-1; 0] Q1^T
    // (needs Kernels)
    double SparseQR::Amplification() const {
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

    // 1 / the smallest singular value of J (largest: its largest) from the sparse LDL^T of J J^T, without the dense bases,
    // if the rows of J are independent beyond doubt: the factor is positive and the condition number (squared in
    // J J^T) far below 1 / rank_tol, so that every rank decision would give the same. Else nothing (also for a column of
    // J in many rows, which would make J J^T dense)
    std::optional<double> RowAmplification(const SparseMatrix &J, double largest) {
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
