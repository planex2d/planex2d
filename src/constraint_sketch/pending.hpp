// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace planex2d::cs {

    // what is set and not applied yet, applied by Update: dimension values, pattern counts, chamfer reverses
    struct Pending {
        std::vector<std::pair<std::string, double>> values;
        std::vector<std::pair<std::string, size_t>> counts;
        std::vector<std::pair<std::string, bool>> flips;

        bool IsEmpty() const { return values.empty() && counts.empty() && flips.empty(); }
        void Clear() {
            values.clear();
            counts.clear();
            flips.clear();
        }
    };

}
