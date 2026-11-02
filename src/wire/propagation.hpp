// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "core/grid.hpp"
#include "curves/corner.hpp"
#include "curves/edgelist.hpp"

namespace planex2d {

    // how a selection propagates across the ends of what it holds: where they meet, meet tangentially, or meet with
    // equal curvature too
    enum class Continuity { Point, Tangent, Curvature };

    // an end of a chain of edges: its point, the unit direction leaving it into the chain, and the curvature there
    // (signed along that direction, a left turn positive)
    struct ChainEnd {
        Vertex at, leaving;
        double curvature;

        // the chain at o, the end of another chain at the same point, goes on as this one: the same curve passed
        // through leaves the point the opposite way with the opposite curvature; directions within 1e-9 rad as loop
        // building takes them, curvatures within 1e-9 of the larger
        bool IsContinuedBy(const ChainEnd &o, Continuity c) const {
            if (c == Continuity::Point)
                return true;
            if (Dot(leaving, o.leaving) >= 0.0 || std::abs(Cross(leaving, o.leaving)) > 1e-9)
                return false;
            return c == Continuity::Tangent ||
                   std::abs(curvature + o.curvature) <= 1e-9 * std::max(std::abs(curvature), std::abs(o.curvature));
        }
    };

    // the ends of a chain of edges in order, on its exact curves; none for a closed chain (a circle, a closed spline,
    // a loop)
    inline std::optional<std::array<ChainEnd, 2>> EndsOf(const EdgeList &chain, double tol) {
        if (chain.empty() || chain.HasCircle())
            return std::nullopt;
        const ExactCurve first(chain.front()), last(chain.back());
        const ChainEnd start{first.At(first.First()), first.Direction(first.First()), first.Curvature(first.First())};
        const ChainEnd end{last.At(last.Last()), last.Direction(last.Last()) * -1.0, -last.Curvature(last.Last())};
        if (start.at.IsClose(end.at, tol))
            return std::nullopt;
        return std::array{start, end};
    }

    // the chains reached from the seeds across their ends with the continuity c, as a selection propagates: across an
    // end where exactly one other chain goes on with that continuity, never where two or more do (a branch); the
    // seeds first, then the chains in the order they are reached
    inline std::vector<size_t> Propagated(const std::vector<EdgeList> &chains, const std::vector<size_t> &seeds,
                                          Continuity c) {
        EdgeList all;
        for (const EdgeList &chain : chains)
            all.insert(all.end(), chain.begin(), chain.end());
        const double tol = all.JoinTol(std::nullopt);
        std::vector<std::optional<std::array<ChainEnd, 2>>> ends;
        std::vector<Vertex> points;
        std::vector<std::pair<size_t, size_t>> owners;
        for (size_t k = 0; k < chains.size(); ++k) {
            ends.push_back(EndsOf(chains[k], tol));
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
