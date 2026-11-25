// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

namespace planex2d::cs {

    // orthonormal
    struct RankBasis {
        std::vector<std::vector<double>> vectors;

        bool Joins(std::vector<double> v);
    };

}
