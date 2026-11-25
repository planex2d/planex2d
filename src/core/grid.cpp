// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/grid.hpp"

#include <algorithm>
#include <cmath>

namespace planex2d {

    double Grid::CellSize(const BBox &all, size_t n, double minimum) {
        Vertex s = all.Size();
        double c = std::sqrt(s.x * s.y / (double)n);
        // thin: the short side at most the square's side, written short * n <= long, since short * long / n
        // underflows to 0 for a subnormal short side
        if ((s.x < s.y ? s.x : s.y) * (double)n <= NormInf(s))
            c = NormInf(s) / (double)n;
        if (c < minimum)
            c = minimum;
        return c > 0.0 ? c : 1.0;
    }

    Grid Grid::Of(const std::vector<Vertex> &pts, double minimum) {
        BBox all;
        for (const Vertex &p : pts)
            all.Grow(p);
        if (pts.empty())
            all.Grow(Vertex{});
        Grid grid(all, CellSize(all, pts.size(), minimum));
        for (size_t k = 0; k < pts.size(); ++k)
            grid.Insert(k, pts[k]);
        return grid;
    }

    Grid Grid::OfBoxes(const std::vector<BBox> &boxes) {
        BBox all;
        std::vector<double> extent;
        for (const BBox &b : boxes) {
            all.Grow(b);
            extent.push_back(NormInf(b.Size()));
        }
        const size_t n = boxes.size();
        std::nth_element(extent.begin(), extent.begin() + (long)(n / 2), extent.end());
        const double cell = std::max(extent[n / 2], std::sqrt((all.x1 - all.x0) * (all.y1 - all.y0) / (double)n));
        Grid grid(all, cell > 0.0 ? cell : 1.0);
        for (size_t k = 0; k < n; ++k)
            grid.Insert(k, boxes[k]);
        return grid;
    }

    void Grid::Insert(size_t id, const BBox &b) {
        Reserve(id);
        size_t ax = X(b.x0), bx = X(b.x1), ay = Y(b.y0), by = Y(b.y1);
        if ((bx - ax + 1) * (by - ay + 1) > maxCells) {
            big.push_back((uint32_t)id);
            return;
        }
        for (size_t x = ax; x <= bx; ++x)
            for (size_t y = ay; y <= by; ++y)
                Link((uint32_t)id, Cell(x, y));
    }

    void Grid::InBox(const BBox &b, std::vector<uint32_t> &out) {
        ++query;
        auto add = [&](uint32_t id) {
            if (FirstVisit(id))
                out.push_back(id);
        };
        // an id inserted again with a smaller box is in the big list and in cells
        for (uint32_t id : big)
            add(id);
        size_t ax = X(b.x0), bx = X(b.x1), ay = Y(b.y0), by = Y(b.y1);
        for (size_t x = ax; x <= bx; ++x)
            for (size_t y = ay; y <= by; ++y)
                ForEntries(Cell(x, y), add);
    }

    size_t VertexIdsOf(const std::vector<Vertex> &pts, double tol, std::vector<size_t> &id) {
        const size_t none = (size_t)-1;
        const Grid grid = Grid::Of(pts, tol);
        id.assign(pts.size(), none);
        size_t n = 0;
        for (size_t k = 0; k < pts.size(); ++k) {
            grid.ForNear(pts[k], [&](size_t j) {
                if (id[k] == none && id[j] != none && pts[j].IsClose(pts[k], tol))
                    id[k] = id[j];
            });
            if (id[k] == none)
                id[k] = n++;
        }
        return n;
    }

}
