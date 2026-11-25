// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include "planex/constraint_sketch/sparsecholesky.hpp"
#include "planex/core/disjointsets.hpp"

// A system of constraints on unknowns, as the solver and the analysis see it. The System type is anything with
//   size_t Count() const                                      the number of constraints
//   size_t Rows(size_t c) const                               the residual rows of constraint c
//   const std::vector<size_t> &Indices(size_t c) const       the unknowns constraint c depends on
//   const std::vector<RowBlock> &Blocks(size_t c) const      its rows in blocks, each with the unknowns it depends on
//   void Evaluate(size_t c, const std::vector<double> &x, double *r, double *J) const
//                                                             its residuals and their derivatives with respect to the
//                                                             unknowns of their block, block by block (rows x
//                                                             indices.size(), row major)
// Residuals are lengths, so that one tolerance relative to the size of the geometry fits all of them.

namespace planex2d {

    // rows of a constraint that depend on the same unknowns: most constraints are one block, a Fix and a rigid group one
    // per held point and value, so that J has only the derivatives they have
    struct RowBlock {
        size_t rows = 0;
        std::vector<size_t> indices;
    };

    struct Unknowns {
        std::vector<double> value;
        // the change that counts as much as a unit change of a length: 1 for lengths, 1 / size for angles
        std::vector<double> scale;
        std::vector<char> fixed;
        // the unknowns whose target in a later pull round is where the round before left them (empty: none), so that
        // only the others go on being pulled to theirs
        std::vector<char> follow;
        explicit Unknowns(std::vector<double> v = {}) : value(std::move(v)), scale(value.size(), 1.0), fixed(value.size(), 0) {}
    };

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
        // J's pattern, all zero, and per row of a block and unknown of it the entry of J (-1: a fixed unknown) and the
        // unknown
        SparseMatrix pattern;
        std::vector<Eigen::Index> entry;
        std::vector<size_t> unknown;

        ComponentEquations(const System &s, const ConstraintComponent &p, size_t nunknowns)
            : system(s), part(p), column(nunknowns, nunknowns) {
            for (size_t k = 0; k < part.unknowns.size(); ++k)
                column[part.unknowns[k]] = k;
            std::vector<Eigen::Triplet<double>> entries;
            for (size_t c : part.constraints)
                for (const RowBlock &b : system.Blocks(c)) {
                    for (size_t i = 0; i < b.rows; ++i)
                        for (size_t u : b.indices)
                            if (column[u] != column.size())
                                entries.emplace_back(m + i, column[u], 0.0);
                    m += b.rows;
                }
            pattern.resize(Eigen::Index(m), Eigen::Index(part.unknowns.size()));
            pattern.setFromTriplets(entries.begin(), entries.end());
            size_t row = 0;
            for (size_t c : part.constraints)
                for (const RowBlock &b : system.Blocks(c)) {
                    for (size_t i = 0; i < b.rows; ++i)
                        for (size_t u : b.indices) {
                            unknown.push_back(u);
                            if (column[u] == column.size()) {
                                entry.push_back(-1);
                                continue;
                            }
                            const int *begin = pattern.innerIndexPtr() + pattern.outerIndexPtr()[column[u]];
                            const int *end = pattern.innerIndexPtr() + pattern.outerIndexPtr()[column[u] + 1];
                            entry.push_back(pattern.outerIndexPtr()[column[u]] + (std::lower_bound(begin, end, (int)(row + i)) - begin));
                        }
                    row += b.rows;
                }
        }

        void Evaluate(const Unknowns &x, Eigen::VectorXd &r, SparseMatrix &J) const {
            r.setZero(Eigen::Index(m));
            std::fill(J.valuePtr(), J.valuePtr() + J.nonZeros(), 0.0);
            std::vector<double> local;
            size_t row = 0, e = 0;
            for (size_t c : part.constraints) {
                size_t size = 0;
                for (const RowBlock &b : system.Blocks(c))
                    size += b.rows * b.indices.size();
                local.assign(size, 0.0);
                system.Evaluate(c, x.value, r.data() + row, local.data());
                for (size_t k = 0; k < size; ++k, ++e)
                    if (entry[e] >= 0)
                        J.valuePtr()[entry[e]] += local[k] * x.scale[unknown[e]];
                row += system.Rows(c);
            }
        }
    };

}
