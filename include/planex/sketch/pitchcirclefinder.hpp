// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/sketch/geometry.hpp"

namespace planex2d::sketch {

    struct FoundPitchCircle {
        size_t middle;
        Vertex centre;
        double r;
        std::vector<size_t> holes;
        size_t circle = none;
        std::vector<size_t> spokes;
    };

    class PitchCircleFinder {
    public:
        PitchCircleFinder(const Geometry &g, const std::vector<char> &skip, std::optional<double> tol);

        std::vector<FoundPitchCircle> Circles() const;

    private:
        struct Round {
            size_t e;
            Vertex c;
            double r;
            bool circle;
        };

        bool IsAlike(size_t i, size_t j, const Vertex &c) const {
            return std::abs(rounds[i].r - rounds[j].r) <= nearby && std::abs(Norm(rounds[i].c - c) - Norm(rounds[j].c - c)) <= nearby;
        }
        bool IsEquallySpaced(std::vector<size_t> &group, const Vertex &c) const;

        std::vector<Round> rounds;
        double nearby = 0.0;
    };

}
