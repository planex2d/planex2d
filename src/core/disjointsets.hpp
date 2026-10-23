// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <numeric>
#include <vector>

namespace planex2d {

    class DisjointSets {
        private:
            std::vector<size_t> parent;

        public:
            explicit DisjointSets(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
            size_t Find(size_t v) {
                while (parent[v] != v)
                    v = parent[v] = parent[parent[v]];
                return v;
            }
            void Join(size_t a, size_t b) { parent[Find(a)] = Find(b); }
            bool IsRoot(size_t v) const { return parent[v] == v; }
    };

}
