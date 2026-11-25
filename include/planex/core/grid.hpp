// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/core/bbox.hpp"

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
            static double CellSize(const BBox &all, size_t n, double minimum);
            static Grid Of(const std::vector<Vertex> &pts, double minimum);

            static Grid OfBoxes(const std::vector<BBox> &boxes);

            void Insert(size_t id, const Vertex &p) {
                Reserve(id);
                Link((uint32_t)id, Cell(p));
            }
            void Insert(size_t id, const BBox &b);
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
            void InBox(const BBox &b, std::vector<uint32_t> &out);
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

    size_t VertexIdsOf(const std::vector<Vertex> &pts, double tol, std::vector<size_t> &id);

}
