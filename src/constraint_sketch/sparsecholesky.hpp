// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
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

        NormalMatrix(const SparseMatrix &J, bool rows) {
            std::vector<std::vector<std::pair<Eigen::Index, Eigen::Index>>> groups((size_t)(rows ? J.cols() : J.rows()));
            Eigen::Index count = 0;
            for (Eigen::Index j = 0; j < J.cols(); ++j) {
                const bool many = rows && J.col(j).nonZeros() > denseRows;
                for (Eigen::Index p = J.outerIndexPtr()[j]; p < J.outerIndexPtr()[j + 1]; ++p) {
                    const Eigen::Index i = J.innerIndexPtr()[p];
                    if (many)
                        dense.push_back({i, count, p});
                    else if (rows)
                        groups[(size_t)j].push_back({i, p});
                    else
                        groups[(size_t)i].push_back({j, p});
                }
                count += many;
            }
            std::vector<Eigen::Triplet<double>> entries;
            for (const auto &group : groups)
                for (const auto &[a, p] : group)
                    for (const auto &[b, q] : group)
                        if (a >= b)
                            entries.emplace_back(a, b, 0.0);
            const Eigen::Index size = rows ? J.rows() : J.cols();
            A.resize(size, size);
            A.setFromTriplets(entries.begin(), entries.end());
            for (const auto &group : groups)
                for (const auto &[a, p] : group)
                    for (const auto &[b, q] : group)
                        if (a >= b) {
                            const int *begin = A.innerIndexPtr() + A.outerIndexPtr()[b], *end = A.innerIndexPtr() + A.outerIndexPtr()[b + 1];
                            terms.push_back({A.outerIndexPtr()[b] + (std::lower_bound(begin, end, a) - begin), p, q});
                        }
            V.resize(size, count);
        }

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

        // false if A + shift I + V V^T is not numerically positive definite
        bool Factor(const SparseMatrix &A, double shift, const Eigen::MatrixXd &V) {
            ldlt.setShift(shift);
            ldlt.factorize(A);
            if (ldlt.info() != Eigen::Success || !(ldlt.vectorD().array() > 0.0).all())
                return false;
            d = ldlt.vectorD();
            w.clear();
            beta.clear();
            for (Eigen::Index k = 0; k < V.cols(); ++k) {
                Eigen::VectorXd x = ldlt.permutationP() * V.col(k);
                ldlt.matrixL().solveInPlace(x);
                for (size_t u = 0; u < w.size(); ++u)
                    Forward(u, x);
                Eigen::VectorXd b(x.size());
                double sigma = 1.0;
                for (Eigen::Index i = 0; i < x.size(); ++i) {
                    const double updated = d[i] + sigma * x[i] * x[i];
                    b[i] = sigma * x[i] / updated;
                    sigma *= d[i] / updated;
                    d[i] = updated;
                }
                w.push_back(std::move(x));
                beta.push_back(std::move(b));
            }
            return true;
        }

        void Solve(Eigen::VectorXd &x) const {
            x = ldlt.permutationP() * x;
            ldlt.matrixL().solveInPlace(x);
            for (size_t u = 0; u < w.size(); ++u)
                Forward(u, x);
            x.array() /= d.array();
            for (size_t u = w.size(); u-- > 0;)
                Backward(u, x);
            ldlt.matrixU().solveInPlace(x);
            x = ldlt.permutationPinv() * x;
        }

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

}
