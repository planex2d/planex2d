// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/propagation.hpp"

#include <utility>

namespace planex2d {

    // across an end where exactly one other chain goes on with that continuity, never where two or more do (a branch);
    // the seeds first, then the chains in the order they are reached
    std::vector<size_t> Propagated(const std::vector<EdgeList> &chains, const std::vector<size_t> &seeds,
                                   Continuity c) {
        EdgeList all;
        for (const EdgeList &chain : chains)
            all.insert(all.end(), chain.begin(), chain.end());
        const double tol = all.JoinTol(std::nullopt);
        std::vector<std::optional<std::array<ChainEnd, 2>>> ends;
        std::vector<Vertex> points;
        std::vector<std::pair<size_t, size_t>> owners;
        for (size_t k = 0; k < chains.size(); ++k) {
            ends.push_back(ChainEndsOf(chains[k], tol));
            if (ends.back())
                for (size_t side = 0; side < 2; ++side) {
                    points.push_back((*ends.back())[side].at);
                    owners.push_back({k, side});
                }
        }
        const Grid grid = Grid::Of(points, tol);
        std::vector<bool> taken(chains.size(), false);
        std::vector<size_t> out;
        for (size_t s : seeds)
            if (!taken[s]) {
                taken[s] = true;
                out.push_back(s);
            }
        for (size_t i = 0; i < out.size(); ++i) {
            const size_t k = out[i];
            if (!ends[k])
                continue;
            for (const ChainEnd &e : *ends[k]) {
                size_t next = chains.size(), count = 0;
                grid.ForNear(e.at, [&](uint32_t p) {
                    const auto [j, side] = owners[p];
                    const ChainEnd &o = (*ends[j])[side];
                    if (j != k && o.at.IsClose(e.at, tol) && e.IsContinuedBy(o, c)) {
                        ++count;
                        next = j;
                    }
                });
                if (count == 1 && !taken[next]) {
                    taken[next] = true;
                    out.push_back(next);
                }
            }
        }
        return out;
    }

}
