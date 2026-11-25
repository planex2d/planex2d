// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/sparsecholesky.hpp"

#include <utility>

namespace planex2d {

    NormalMatrix::NormalMatrix(const SparseMatrix &J, bool rows) {
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

    // false if A + shift I + V V^T is not numerically positive definite
    bool SparseCholesky::Factor(const SparseMatrix &A, double shift, const Eigen::MatrixXd &V) {
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

    void SparseCholesky::Solve(Eigen::VectorXd &x) const {
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

    double NormalEquations::MaxDiagonal() const {
        double largest = 0.0;
        for (Eigen::Index a = 0; a < normal.A.rows(); ++a)
            largest = std::max(largest, normal.A.coeff(a, a) + normal.V.row(a).squaredNorm());
        return largest;
    }

    // (J^T J + shift I)^-1 J^T v for the shift factored last; JTv: J^T v, where the caller has it
    Eigen::VectorXd NormalEquations::Solved(const SparseMatrix &J, const Eigen::VectorXd &v, const Eigen::VectorXd &JTv) const {
        Eigen::VectorXd z = wide ? v : JTv;
        cholesky.Solve(z);
        if (wide)
            return J.transpose() * z;
        return z;
    }

    Eigen::VectorXd NormalEquations::Solved(const SparseMatrix &J, const Eigen::VectorXd &v) const {
        return wide ? Solved(J, v, v) : Solved(J, v, J.transpose() * v);
    }

}
