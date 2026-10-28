// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "wire/wire.hpp"

namespace planex2d {

    // a loop of a face of an export: the outer one counterclockwise, a hole clockwise, as edge indices into the export's
    // wire with whether the loop runs along the edge
    struct FaceLoop {
        bool outer;
        std::vector<std::pair<size_t, bool>> edges;
    };

    // the loops of the face made of the edges faceEdges of c (a face of Shape::Export's plan)
    inline std::vector<FaceLoop> FaceLoops(const Wire &c, const std::vector<size_t> &faceEdges) {
        Wire w(c.Axes());
        for (size_t i : faceEdges)
            w.Add(c[i]);
        std::vector<FaceLoop> out;
        for (const NestedLoop &n : w.Nesting()) {
            std::vector<std::pair<size_t, bool>> loop = n.loop.edges;
            if ((w.LoopArea(loop) > 0.0) != (n.depth == 0)) {
                std::reverse(loop.begin(), loop.end());
                for (auto &[k, forward] : loop)
                    forward = !forward;
            }
            for (auto &[k, forward] : loop)
                k = faceEdges[k];
            out.push_back({n.depth == 0, std::move(loop)});
        }
        return out;
    }

}
