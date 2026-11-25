// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/boolean/cutloop.hpp"
#include "planex/boolean/pairrecord.hpp"
#include "planex/boolean/snap.hpp"
#include "planex/core/parallel.hpp"

namespace planex2d {

    enum class BooleanOp { Union, Intersection, Difference };

    // Shared: both operands have the piece, their regions on its one side (SharedSame) or on opposite sides (SharedOpposite)
    enum class PieceSide : unsigned char { Inside, Outside, SharedSame, SharedOpposite };

    // state mask: 1 of a, 2 of b, 3 of both; a piece of a set of lines has no inside
    struct Arrangement {
        Wire c;
        std::vector<CutState> state;
        std::vector<PieceSide> side;
    };

    Arrangement Arranged(const Wire &a, const Wire &b, const BBox &boxa, const BBox &boxb, double tol, bool sides);

    bool Keeps(BooleanOp op, bool froma, PieceSide side);

    Wire Boolean(const Wire &a, const Wire &b, BooleanOp op);

    // bit i of v moves to bit 2i
    inline uint64_t Spread(uint64_t v) {
        v = (v | (v << 8)) & 0x00FF00FF;
        v = (v | (v << 4)) & 0x0F0F0F0F;
        v = (v | (v << 2)) & 0x33333333;
        v = (v | (v << 1)) & 0x55555555;
        return v;
    }

    void SortSpatially(WireList &regions);

    Wire BalancedBoolean(WireList regions, BooleanOp op, bool multithreaded = false);

    std::vector<Vertex> Intersections(const Wire &a, const Wire &b);

    void Arrange(Wire &c, std::vector<CutState> &state);

}
