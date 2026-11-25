// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/io/faceloops.hpp"

#include <algorithm>

namespace planex2d {

    std::vector<FaceLoop> FaceLoops(const Wire &c, const std::vector<size_t> &faceEdges) {
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
