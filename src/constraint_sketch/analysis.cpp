// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/analysis.hpp"

namespace planex2d {

    Eigen::MatrixXd RowsOf(const SparseMatrix &J, const std::vector<size_t> &first, const std::vector<size_t> &constraints) {
        std::vector<Eigen::Index> at((size_t)J.rows(), -1);
        Eigen::Index k = 0;
        for (size_t c : constraints)
            for (size_t row = first[c]; row < first[c + 1]; ++row)
                at[row] = k++;
        Eigen::MatrixXd out = Eigen::MatrixXd::Zero(k, J.cols());
        for (Eigen::Index o = 0; o < J.outerSize(); ++o)
            for (SparseMatrix::InnerIterator it(J, o); it; ++it)
                if (at[(size_t)it.row()] >= 0)
                    out(at[(size_t)it.row()], it.col()) = it.value();
        return out;
    }

    static auto RowsOfConstraint(const Eigen::MatrixXd &M, const std::vector<size_t> &first, size_t c) {
        return M.middleRows(Eigen::Index(first[c]), Eigen::Index(first[c + 1] - first[c]));
    }

    // a constraint is redundant if it takes no rank: without its rows the rank stays, so the dependencies restricted to
    // its rows have full rank
    std::vector<char> RedundantConstraints(const Eigen::MatrixXd &dependencies, const std::vector<size_t> &first, double zero) {
        std::vector<char> redundant(first.size() - 1, 0);
        for (size_t c = 0; c + 1 < first.size(); ++c) {
            const size_t rows = first[c + 1] - first[c];
            size_t spanned = 0;
            if (rows > 0 && dependencies.cols() >= Eigen::Index(rows)) {
                // a range-for over singularValues() of a temporary SVD reads it after it is destroyed (until C++23)
                const Eigen::JacobiSVD<Eigen::MatrixXd> svd(RowsOfConstraint(dependencies, first, c));
                for (double v : svd.singularValues())
                    spanned += v > zero;
            }
            redundant[c] = rows > 0 && spanned == rows;
        }
        return redundant;
    }

    // what a redundant constraint follows from: the others with weight in its rows' projections onto the dependencies, the
    // smallest combinations of rows through it (not always the fewest constraints)
    std::vector<size_t> PartnersOf(size_t c, const SparseMatrix &J, const Eigen::MatrixXd &dependencies, const std::vector<size_t> &first, double zero,
                                   double tol) {
        const size_t count = first.size() - 1;
        const Eigen::MatrixXd through = dependencies * RowsOfConstraint(dependencies, first, c).transpose();
        const double most = through.cwiseAbs().maxCoeff();
        std::vector<size_t> from;
        for (size_t j = 0; j < count; ++j)
            if (j != c && first[j + 1] > first[j] && RowsOfConstraint(through, first, j).cwiseAbs().maxCoeff() > zero * most)
                from.push_back(j);
        // a partner goes where c's rows still depend on the others' (from the last one back)
        for (size_t k = from.size(); k-- > 0;) {
            std::vector<size_t> fewer = from;
            fewer.erase(fewer.begin() + (std::ptrdiff_t)k);
            std::vector<size_t> with = fewer;
            with.push_back(c);
            if (RankOf(RowsOf(J, first, with), tol) == RankOf(RowsOf(J, first, fewer), tol))
                from = std::move(fewer);
        }
        return from;
    }

}
