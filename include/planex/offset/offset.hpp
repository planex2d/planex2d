// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <span>

#include "planex/boolean/boolean.hpp"
#include "planex/core/grid.hpp"
#include "planex/offset/offsetpath.hpp"

namespace planex2d {

    template <class E>
    Wire EdgeOffset(const E &e, double d) {
        Wire w;
        w.Add(e);
        return w.Offset(d);
    }

    void BandPieces(const EdgeList &chain, bool closed, double d, OffsetEnd end, OffsetCorner corner, const AxisSystem &axes, double tol,
                    WireList &pieces);

    Wire LoopOffset(const EdgeList &chain, double d, OffsetCorner corners, const AxisSystem &axes, double tol);

    EdgeList InRunOrder(const EdgeList &pieces, double tol);

    EdgeList OpenSideOffset(const EdgeList &chain, double s, OffsetCorner corners, const AxisSystem &axes, double tol);

    Wire OffsetCurves(std::span<const Wire> wires, double d, std::optional<bool> bothSides, OffsetEnd ends, OffsetCorner corners);

}
