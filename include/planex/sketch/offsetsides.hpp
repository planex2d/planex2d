// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/offset/offsetpieces.hpp"

namespace planex2d::sketch {

    class Geometry;

    // forward: the element runs along the chain
    struct OffsetChain {
        std::vector<size_t> elements;
        std::vector<char> forward;
        bool closed = false;

        static OffsetChain Of(const Geometry &g, const std::vector<size_t> &given, const std::vector<std::string> &ids, const std::string &call);
        EdgeList Edges(const Geometry &g) const;
        bool IsClosedSpline(const Geometry &g) const;
        std::optional<std::string> Cusp(const Geometry &g, const EdgeList &edges, double d, const std::vector<int> &towards,
                                        const std::vector<std::string> &ids) const;
    };

    using OffsetSides = std::vector<std::pair<std::string, std::vector<OffsetLoop>>>;

    void OpenHole(OffsetSides &sides, double r, double ad);
    std::string ShapeOf(const OffsetSides &sides);
    inline std::set<std::tuple<int, size_t, int>> SidePieces(const OffsetSides &sides) {
        std::set<std::tuple<int, size_t, int>> out;
        for (const auto &[fixed, loops] : sides)
            for (const OffsetLoop &loop : loops)
                for (const OffsetPiece &p : loop.pieces)
                    out.insert({(int)p.kind, p.k, p.side});
        return out;
    }

}
