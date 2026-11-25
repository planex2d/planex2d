// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include "planex/wire/wire.hpp"
#include "planex/core/disjointsets.hpp"

namespace planex2d {

    using HalfEdges = std::vector<std::pair<size_t, bool>>;

    inline Wire EdgesOf(const Wire &w, const HalfEdges &half) {
        Wire out(w.Axes());
        for (auto [i, forward] : half)
            out.Add(w[i]);
        return out;
    }

    std::vector<HalfEdges> ArrangementFaces(const Wire &w);

    std::vector<size_t> SideFaces(const std::vector<HalfEdges> &faces, size_t edges);

    std::vector<size_t> FaceDepths(const std::vector<HalfEdges> &faces, const std::vector<size_t> &faceOf, const std::vector<bool> &cut);

    std::vector<std::vector<size_t>> RegionFaces(const Wire &w, const std::vector<bool> &cut);

    Wire Boundary(const Wire &w, std::vector<size_t> *dropped = nullptr);

}
