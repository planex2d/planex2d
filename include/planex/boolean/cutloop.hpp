// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/core/bbox.hpp"
#include "planex/curves/edge.hpp"
#include "planex/core/grid.hpp"
#include "planex/wire/crossing.hpp"
#include "planex/wire/wire.hpp"
#include "planex/boolean/cut.hpp"
#include "planex/core/timer.hpp"

namespace planex2d {

    struct CutState {
        static constexpr uint32_t noPart = std::numeric_limits<uint32_t>::max();

        unsigned char mask = 0;  // 1 = A, 2 = B
        bool needsCutting = true;
        bool excluded = false;
        bool drop = false;
        bool open = false;
        uint32_t part = noPart;

        bool IsToCut() const { return !excluded && needsCutting; }
        // edges of one clean part (a Glue input) meet only at their shared ends, the cut leaves such pairs alone
        bool IsSamePart(const CutState &o) const { return part != noPart && part == o.part; }
        // o is cut against this one; a stretch both operands share still cuts an edge of one of them, where the other's
        // vertex at its end lies on that edge
        bool CanCut(const CutState &o) const {
            const bool apart = (mask & o.mask) == 0 || ((mask | o.mask) == 3 && mask != o.mask);
            return o.IsToCut() && apart && !IsSamePart(o);
        }
    };

    void CutCircles(Wire &w, std::vector<CutState> &state);

    // parity of the ray from m per operand: bit 1 odd for A, bit 2 for B; only edges with a parent in wanted count. m on
    // the piece on (a shared piece's midpoint): its crossing at m does not count, every other edge's does however near
    // m it passes
    template <bool vertical>
    unsigned char CrossingParity(Grid &grid, const Wire &c, const std::vector<CutState> &state, const Vertex &m,
                                 unsigned char wanted, size_t on = std::numeric_limits<size_t>::max()) {
        static Timer t("planex Crossing", false);
        Region reg(t);
        unsigned char odd = 0;
        grid.ForRay<vertical>(m, [&](uint32_t k) {
            const CutState &s = state[k];
            if (s.excluded || !(s.mask & wanted))
                return;
            if (k == on ? RayParity<vertical, true>(c[k], m) : RayParity<vertical, false>(c[k], m))
                odd ^= s.mask;
        });
        return odd;
    }

    void RemapPartition(Edge &piece, const Edge &parent);

    class CutLoop {
        private:
            Wire &w;
            Grid &grid;
            std::vector<CutState> &state;
            const bool drop;

            CutState &Append(Edge &e, unsigned char mask, size_t parent, bool open);
            void Settle(size_t i, size_t j, CutResult r, CutPieces &pieces, const std::optional<Edge> &pa,
                        const std::optional<Edge> &pb);

        public:
            CutLoop(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop) : w(w), grid(grid), state(state), drop(drop) {}
            void CutAll(CutRecord &rec);
            void ShareCongruent(double tol);
    };

    void CutAll(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, CutRecord *record = nullptr);
    void ShareCongruent(Wire &w, Grid &grid, std::vector<CutState> &state, bool drop, double tol);

}
