// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/vertex.hpp"
#include "core/bbox.hpp"

namespace planex2d {

    class Grid {
        private:
            static constexpr int32_t none = -1;
            // a box over more cells goes to the big list, which every query reports
            static constexpr size_t maxCells = 10000;
            double x0, y0, h;
            size_t nx, ny;
            std::vector<int32_t> head;
            std::vector<int32_t> next;
            std::vector<uint32_t> item;
            std::vector<uint32_t> big;
            std::vector<uint32_t> stamp;
            uint32_t query = 0;

            size_t Count(double lo, double hi) const { return hi > lo ? (size_t)((hi - lo) / h) + 1 : 1; }
            size_t Index(double v, double o, size_t n) const {
                double t = (v - o) / h;
                return t <= 0.0 ? 0 : t >= (double)(n - 1) ? n - 1 : (size_t)t;
            }
            size_t X(double x) const { return Index(x, x0, nx); }
            size_t Y(double y) const { return Index(y, y0, ny); }
            size_t Cell(size_t x, size_t y) const { return x + nx * y; }
            size_t Cell(const Vertex &p) const { return Cell(X(p.x), Y(p.y)); }
            void Link(uint32_t id, size_t cell) {
                next.push_back(head[cell]);
                item.push_back(id);
                head[cell] = (int32_t)item.size() - 1;
            }
            void Reserve(size_t id) {
                if (id >= stamp.size())
                    stamp.resize(id + 1, 0);
            }
            bool FirstVisit(uint32_t id) {
                if (stamp[id] == query)
                    return false;
                stamp[id] = query;
                return true;
            }
            template <class F>
            void ForEntries(size_t cell, F &f) const {
                for (int32_t e = head[cell]; e != none; e = next[e])
                    f(item[e]);
            }

        public:
            Grid(const BBox &all, double cell)
                : x0(all.x0), y0(all.y0), h(cell), nx(Count(all.x0, all.x1)), ny(Count(all.y0, all.y1)) {
                head.assign(nx * ny, none);
            }
            // about n cells: the side of a square of area / n, the long side / n for a thin box
            static double CellSize(const BBox &all, size_t n, double minimum) {
                Vertex s = all.Size();
                double c = std::sqrt(s.x * s.y / (double)n);
                if ((s.x < s.y ? s.x : s.y) <= c)
                    c = NormInf(s) / (double)n;
                if (c < minimum)
                    c = minimum;
                return c > 0.0 ? c : 1.0;
            }
            static Grid Of(const std::vector<Vertex> &pts, double minimum) {
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

            // about one cell per box, none smaller than the median box; boxes not empty
            static Grid OfBoxes(const std::vector<BBox> &boxes) {
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

            void Insert(size_t id, const Vertex &p) {
                Reserve(id);
                Link((uint32_t)id, Cell(p));
            }
            void Insert(size_t id, const BBox &b) {
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
            void Erase(size_t id, const Vertex &p) {
                int32_t *link = &head[Cell(p)];
                while (*link != none && item[*link] != id)
                    link = &next[*link];
                if (*link != none)
                    *link = next[*link];
            }

            template <class F>
            void ForCell(const Vertex &p, F f) const {
                ForEntries(Cell(p), f);
            }
            template <class F>
            void ForNear(const Vertex &p, F f) const {
                size_t ix = X(p.x), iy = Y(p.y);
                for (size_t x = ix > 0 ? ix - 1 : 0; x <= ix + 1 && x < nx; ++x)
                    for (size_t y = iy > 0 ? iy - 1 : 0; y <= iy + 1 && y < ny; ++y)
                        ForEntries(Cell(x, y), f);
            }
            void InBox(const BBox &b, std::vector<uint32_t> &out) {
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
            // vertical: the ray from p towards +y, else towards +x
            template <bool vertical, class F>
            void ForRay(const Vertex &p, F f) {
                ++query;
                auto once = [&](uint32_t id) {
                    if (FirstVisit(id))
                        f(id);
                };
                for (uint32_t id : big)
                    once(id);
                size_t ix = X(p.x), iy = Y(p.y);
                if constexpr (vertical) {
                    for (size_t y = iy; y < ny; ++y)
                        ForEntries(Cell(ix, y), once);
                } else {
                    for (size_t x = ix; x < nx; ++x)
                        ForEntries(Cell(x, iy), once);
                }
            }
    };

    // endpoints within tol of an earlier one get its id: the number of ids
    inline size_t VertexIdsOf(const std::vector<Vertex> &pts, double tol, std::vector<size_t> &id) {
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
