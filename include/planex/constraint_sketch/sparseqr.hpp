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

#include "planex/constraint_sketch/sparsecholesky.hpp"

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

        double Amplification() const;

    private:
        Eigen::Index m, n;
        Eigen::SparseQR<SparseMatrix, Eigen::COLAMDOrdering<int>> qr;
        Eigen::SparseMatrix<double, Eigen::RowMajor> R11, R12;
    };

    std::optional<double> RowAmplification(const SparseMatrix &J, double largest);

}
