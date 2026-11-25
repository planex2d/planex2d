// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/fillet.hpp"

namespace planex2d {

    // 17 digits: rounded, the point may read as the vertex it missed
    std::string OffVertex(const Vertex &p, const std::vector<Vertex> &vertices, const std::string &select) {
        const std::string text = "the corner at (" + NumberText(p.x, 17) + ", " + NumberText(p.y, 17) + ") is on no vertex";
        if (vertices.empty())
            return text;
        Vertex nearest = vertices.front();
        for (const Vertex &v : vertices)
            if (Dot(v - p, v - p) < Dot(nearest - p, nearest - p))
                nearest = v;
        return text + " (the nearest, " + PointText(nearest) + ", is " + LengthText(Norm(nearest - p)) + " away" +
               (select.empty() ? "" : "; " + select) + ")";
    }

    void ReportCorners(const std::string &call, size_t done, const std::vector<std::string> &failed) {
        if (done == 0) {
            std::string all;
            for (const std::string &why : failed)
                all += (all.empty() ? "" : "; ") + why;
            throw std::invalid_argument(call + ": no corner can be cut: " + all +
                                        "; give corners that are the end of exactly two edges, with a size that fits");
        }
        for (const std::string &why : failed)
            Warning() << call << ": " << why << ", left as it is";
    }

}
