// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/sketch/geometry.hpp"

namespace planex2d::sketch {

    struct Pen {
        Vertex pos, dir;
        double tol;
    };

    struct PenState {
        Vertex pos{}, dir{1.0, 0.0}, start{};
        std::optional<size_t> point, first, last;
        std::vector<size_t> chain;
        std::string wire;

        void MoveTo(const Vertex &p) { pos = start = p; }
        void HeadAlong(const Vertex &t) { dir = t * (1.0 / Norm(t)); }
        void Lift() { point = first = last = std::nullopt; }
        void Translate(const Vertex &d) {
            pos = pos + d;
            start = start + d;
        }
        void Scale(double f) {
            pos = pos * f;
            start = start * f;
        }
        void Sync(const Geometry &g) {
            if (point)
                pos = g.Coords(g.elements[*point].Point());
            if (first)
                start = g.Coords(g.elements[*first].Point());
        }
        void Renumber(const std::vector<size_t> &moved) {
            for (std::optional<size_t> *e : {&point, &first, &last})
                if (*e)
                    *e = moved[**e];
            for (size_t &e : chain)
                e = moved[e];
        }
        PenState WithoutChain() const { return {pos, dir, start, point, first, last, {}, wire}; }
    };

}
