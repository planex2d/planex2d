// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"

namespace planex2d::sketch {

    struct Solution {
        Vertex p;
        bool onEdge = true;
    };

    // comparisons up to a tolerance are no strict weak order, which std::sort needs: take the best of the rest, ties in
    // their order
    template <class Better>
    std::vector<size_t> BestFirst(size_t n, Better better) {
        std::vector<size_t> order(n);
        for (size_t k = 0; k < n; ++k)
            order[k] = k;
        for (size_t i = 0; i < n; ++i) {
            size_t best = i;
            for (size_t j = i + 1; j < n; ++j)
                if (better(order[j], order[best]))
                    best = j;
            std::rotate(order.begin() + (std::ptrdiff_t)i, order.begin() + (std::ptrdiff_t)best, order.begin() + (std::ptrdiff_t)best + 1);
        }
        return order;
    }

    struct Preference {
        Vertex ref, from, dir;
        double tol;

        double Turn(const Vertex &p) const {
            const Vertex v = p - from;
            return std::atan2(std::abs(Cross(dir, v)), Dot(dir, v));
        }
        bool IsBetter(const Solution &a, const Solution &b) const {
            if (a.onEdge != b.onEdge)
                return a.onEdge;
            const double da = Norm(a.p - ref), db = Norm(b.p - ref);
            if (std::abs(da - db) > tol)
                return da < db;
            // directions closer than 1e-9 rad count as equal, as in BuildLoops
            const double ta = Turn(a.p), tb = Turn(b.p);
            if (std::abs(ta - tb) > 1e-9)
                return ta < tb;
            return Cross(dir, a.p - from) > Cross(dir, b.p - from);
        }
        std::vector<size_t> Ranked(const std::vector<Solution> &s) const {
            return BestFirst(s.size(), [&](size_t a, size_t b) { return IsBetter(s[a], s[b]); });
        }
    };

    // reverse switches every free choice to its second solution; what it did decides the warning at the end
    class Choices {
    public:
        explicit Choices(bool reverse) : reverse(reverse) {}

        bool IsReverse() const { return reverse; }
        size_t Take(const std::vector<size_t> &order, bool free) {
            if (reverse && free) {
                if (order.size() > 1) {
                    switched = true;
                    return order[1];
                }
                onlyOne = true;
            }
            return order[0];
        }
        void MarkSwitched() { switched = true; }
        void MarkOnlyOne() { onlyOne = true; }
        void Warn(const std::string &call) const {
            if (reverse && !switched)
                Warning() << call << (onlyOne ? ": reverse found only one solution" : ": reverse has nothing to switch");
        }

    private:
        bool reverse;
        bool switched = false;
        bool onlyOne = false;
    };

}
