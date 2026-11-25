// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/system.hpp"

namespace planex2d::cs {

    // per constraint: it, or the equation that keeps its value (a driving dimension an expression uses), takes no
    // rank; a is the analysis of the last Build
    // the redundant constraints: by the analysis's rows, and a coincidence of two points that earlier ones merged
    // already (a merge takes no row, the analysis cannot see it)
    std::vector<char> System::RedundantFrom(size_t first) {
        Build();
        RankBasis basis;
        for (size_t c = 0; c < Count(); ++c)
            if (c < first || c >= constraints.size())
                TakesRank(c, basis);
        DisjointSets groups(ownCoordsOf.size());
        std::vector<char> out(constraints.size(), 0);
        for (size_t c = 0; c < constraints.size(); ++c) {
            if (IsMerge(constraints[c])) {
                const size_t p = groups.Find(constraints[c].refs[0].PointAt(*this)), q = groups.Find(constraints[c].refs[1].PointAt(*this));
                out[c] = c >= first && p == q;
                groups.Join(p, q);
            } else if (c >= first)
                out[c] = !TakesRank(c, basis);
        }
        return out;
    }

    bool System::TakesRank(size_t c, RankBasis &basis) const {
        size_t size = 0;
        for (const RowBlock &block : Blocks(c))
            size += block.rows * block.indices.size();
        std::vector<double> r(Rows(c), 0.0), local(size, 0.0);
        Evaluate(c, values, r.data(), local.data());
        bool took = false;
        size_t at = 0;
        for (const RowBlock &block : Blocks(c))
            for (size_t row = 0; row < block.rows; ++row) {
                std::vector<double> v(values.size(), 0.0);
                for (size_t u : block.indices) {
                    if (!fixed[u])
                        v[u] += local[at] * scale[u];
                    ++at;
                }
                took = basis.Joins(std::move(v)) || took;
            }
        return took;
    }

    std::vector<char> System::Redundancies(const ConstraintAnalysis &a) const {
        std::vector<char> out(a.redundant.begin(), a.redundant.begin() + (std::ptrdiff_t)constraints.size());
        for (const auto &[row, c] : valueRows)
            out[c] = out[c] || a.redundant[row];
        DisjointSets groups(ownCoordsOf.size());
        for (size_t c = 0; c < constraints.size(); ++c) {
            if (!IsMerge(constraints[c]))
                continue;
            const size_t p = groups.Find(constraints[c].refs[0].PointAt(*this)), q = groups.Find(constraints[c].refs[1].PointAt(*this));
            if (p == q)
                out[c] = 1;
            else
                groups.Join(p, q);
        }
        return out;
    }

    // per constraint Redundancies flags: the constraints it follows from, by the analysis's rows (a value equation
    // stands for the dimension it keeps), and for a coincidence of points earlier ones merged, the merges between them
    std::vector<std::vector<size_t>> System::Dependencies(const ConstraintAnalysis &a) const {
        const size_t n = constraints.size();
        std::vector<size_t> whose(a.partners.size(), sketch::none);
        for (size_t c = 0; c < n; ++c)
            whose[c] = c;
        for (const auto &[row, c] : valueRows)
            whose[row] = c;
        std::vector<std::vector<size_t>> out(n);
        const auto take = [&](size_t c, size_t row) {
            for (size_t j : a.partners[row])
                if (whose[j] != sketch::none && whose[j] != c && std::find(out[c].begin(), out[c].end(), whose[j]) == out[c].end())
                    out[c].push_back(whose[j]);
        };
        for (size_t c = 0; c < n; ++c)
            take(c, c);
        for (const auto &[row, c] : valueRows)
            take(c, row);
        // the merges so far: a forest of (point, point, constraint)
        std::vector<std::array<size_t, 3>> merges;
        DisjointSets groups(ownCoordsOf.size());
        for (size_t c = 0; c < n; ++c) {
            if (!IsMerge(constraints[c]))
                continue;
            const size_t p = constraints[c].refs[0].PointAt(*this), q = constraints[c].refs[1].PointAt(*this);
            if (groups.Find(p) == groups.Find(q))
                out[c] = MergesBetween(merges, p, q);
            else {
                groups.Join(p, q);
                merges.push_back({p, q, c});
            }
        }
        return out;
    }

    std::vector<size_t> System::MergesBetween(const std::vector<std::array<size_t, 3>> &merges, size_t p, size_t q) const {
        std::vector<size_t> via(ownCoordsOf.size(), sketch::none), reached{p};
        std::vector<char> seen(ownCoordsOf.size(), 0);
        seen[p] = 1;
        for (size_t k = 0; k < reached.size(); ++k)
            for (size_t m = 0; m < merges.size(); ++m)
                for (size_t side = 0; side < 2; ++side)
                    if (const size_t next = merges[m][1 - side]; merges[m][side] == reached[k] && !seen[next]) {
                        seen[next] = 1;
                        via[next] = m;
                        reached.push_back(next);
                    }
        std::vector<size_t> out;
        for (size_t x = q; x != p && via[x] != sketch::none; x = merges[via[x]][merges[via[x]][0] == x ? 1 : 0])
            out.push_back(merges[via[x]][2]);
        return out;
    }

    std::vector<size_t> System::MadeRedundant(const std::vector<char> &now, const Mark &mark) const {
        const size_t old = mark.constraints;
        if (std::none_of(now.begin(), now.begin() + (std::ptrdiff_t)old, [](char r) { return r != 0; }))
            return {};
        System earlier = *this;
        earlier.Truncate(mark);
        const std::vector<char> was = earlier.Redundancies(earlier.Analyze(false));
        std::vector<size_t> out;
        for (size_t j = 0; j < old; ++j)
            if (now[j] && !was[j])
                out.push_back(j);
        return out;
    }

}
