// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/circle.hpp"

namespace planex2d {

    std::vector<Arc> Circle::Arcs(std::vector<Vertex> at, double tol) const {
        std::sort(at.begin(), at.end(), [&](const Vertex &p, const Vertex &q) { return Param(p) < Param(q); });
        at.erase(std::unique(at.begin(), at.end(), [&](const Vertex &p, const Vertex &q) { return p.IsClose(q, tol); }), at.end());
        if (at.size() > 1 && at.front().IsClose(at.back(), tol))
            at.pop_back();
        if (at.size() == 1)
            at.push_back(center * 2.0 - at.front());
        std::vector<Arc> arcs;
        for (size_t k = 0; k < at.size(); ++k) {
            arcs.emplace_back(at[k], at[(k + 1) % at.size()], center, ccw);
            arcs.back().props = props;
        }
        return arcs;
    }

}
