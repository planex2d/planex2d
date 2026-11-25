// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <Eigen/SparseCholesky>

namespace planex2d {

    using SparseMatrix = Eigen::SparseMatrix<double>;

    // an unknown in more rows than one constraint can hold unknowns (an offset's distance, a pattern's step) would make
    // J J^T dense: its column comes back as a rank-one update instead
    constexpr Eigen::Index denseRows = 128;

    // the lower triangle of J J^T (rows) or J^T J of J's pattern, from J's values by a scatter found once; for J J^T a
    // column of J in more than denseRows rows stays out, as a column of V
    class NormalMatrix {
    public:
        SparseMatrix A;
        Eigen::MatrixXd V;

        NormalMatrix(const SparseMatrix &J, bool rows);

        void Assemble(const SparseMatrix &J) {
            std::fill(A.valuePtr(), A.valuePtr() + A.nonZeros(), 0.0);
            for (const auto &[a, p, q] : terms)
                A.valuePtr()[a] += J.valuePtr()[p] * J.valuePtr()[q];
            V.setZero();
            for (const auto &[i, k, p] : dense)
                V(i, k) = J.valuePtr()[p];
        }

    private:
        // A's entry += J's entry times J's entry
        std::vector<std::array<Eigen::Index, 3>> terms;
        // V(row, column) = J's entry
        std::vector<std::array<Eigen::Index, 3>> dense;
    };

    // A + shift I + V V^T for a symmetric positive semidefinite A and a few dense columns V: the sparse LDL^T of
    // A + shift I, then V column by column as product-form rank-one updates (Gill, Golub, Murray, Saunders 1974, method
    // C1; Goldfarb, Scheinberg 2004), so that V does not fill the factor
    class SparseCholesky {
    public:
        // the ordering and the symbolic factorization, for every A of this pattern
        void Analyze(const SparseMatrix &A) { ldlt.analyzePattern(A); }

        bool Factor(const SparseMatrix &A, double shift, const Eigen::MatrixXd &V);

        void Solve(Eigen::VectorXd &x) const;

    private:
        Eigen::SimplicialLDLT<SparseMatrix> ldlt;
        Eigen::VectorXd d;
        // update u is the unit lower triangular I + (w[u] beta[u]^T below the diagonal)
        std::vector<Eigen::VectorXd> w, beta;

        void Forward(size_t u, Eigen::VectorXd &x) const {
            double s = 0.0;
            for (Eigen::Index i = 0; i < x.size(); ++i) {
                x[i] -= w[u][i] * s;
                s += beta[u][i] * x[i];
            }
        }
        void Backward(size_t u, Eigen::VectorXd &x) const {
            double s = 0.0;
            for (Eigen::Index i = x.size(); i-- > 0;) {
                x[i] -= beta[u][i] * s;
                s += w[u][i] * x[i];
            }
        }
    };

    // the damped normal equations of J in the smaller of their two forms, J J^T for fewer rows than columns, else J^T J:
    // the same solution by (J^T J + l I)^-1 J^T = J^T (J J^T + l I)^-1
    class NormalEquations {
    public:
        explicit NormalEquations(const SparseMatrix &J) : wide(J.rows() < J.cols()), normal(J, wide) {}

        void Assemble(const SparseMatrix &J) { normal.Assemble(J); }
        void Analyze() { cholesky.Analyze(normal.A); }
        double MaxDiagonal() const;
        bool Factor(double shift) { return cholesky.Factor(normal.A, shift, normal.V); }
        Eigen::VectorXd Solved(const SparseMatrix &J, const Eigen::VectorXd &v) const;
        Eigen::VectorXd Solved(const SparseMatrix &J, const Eigen::VectorXd &v, const Eigen::VectorXd &JTv) const;

    private:
        bool wide;
        NormalMatrix normal;
        SparseCholesky cholesky;
    };

}
