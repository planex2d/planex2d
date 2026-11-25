// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "planex/boolean/boolean.hpp"
#include "planex/shape/measure.hpp"

namespace planex2d {

    // the dimensionally extended nine-intersection matrix (DE-9IM): rows the interior, boundary and exterior of a,
    // columns those of b; each entry the dimension of their intersection, -1 for none
    struct RelateMatrix {
        std::array<signed char, 9> dims{-1, -1, -1, -1, -1, -1, -1, -1, -1};

        void Raise(size_t row, size_t col, signed char dim) { dims[3 * row + col] = std::max(dims[3 * row + col], dim); }
        RelateMatrix Transposed() const {
            RelateMatrix t;
            for (size_t i = 0; i < 3; ++i)
                for (size_t j = 0; j < 3; ++j)
                    t.dims[3 * j + i] = dims[3 * i + j];
            return t;
        }
        std::string Text() const {
            std::string s;
            for (signed char d : dims)
                s += d < 0 ? 'F' : (char)('0' + d);
            return s;
        }
        // a pattern of nine characters: F none, T any, * anything, 0, 1, 2 that dimension
        bool Matches(const std::string &pattern) const {
            for (size_t k = 0; k < 9; ++k) {
                const char c = pattern[k];
                const signed char d = dims[k];
                if ((c == 'F' && d >= 0) || (c == 'T' && d < 0) || (c >= '0' && c <= '2' && d != c - '0'))
                    return false;
            }
            return true;
        }
    };

    inline void CheckRelatePattern(const std::string &pattern) {
        if (pattern.size() != 9 || pattern.find_first_not_of("FT*012") != std::string::npos)
            throw std::invalid_argument("Relate: a pattern is nine characters of F, T, *, 0, 1, 2, e.g. 'T*F**F***'; got '" +
                                        pattern + "'");
    }

    // lines: their ends of odd degree are the boundary
    struct RelateOperand {
        int dim = 2;
        Wire edges;
        Vertex point;

        bool Empty() const { return dim > 0 && edges.Empty(); }
    };

    template <class F>
    std::vector<Vertex> OddEnds(const Wire &w, F counted) {
        std::vector<Vertex> ends;
        for (size_t k = 0; k < w.Size(); ++k)
            if (counted(k) && !IsClosedEdge(w[k])) {
                ends.push_back(StartOf(w[k]));
                ends.push_back(EndOf(w[k]));
            }
        std::sort(ends.begin(), ends.end(), VertexLess);
        std::vector<Vertex> odd;
        for (size_t i = 0; i < ends.size();) {
            size_t j = i;
            while (j < ends.size() && ends[j] == ends[i])
                ++j;
            if ((j - i) % 2 == 1)
                odd.push_back(ends[i]);
            i = j;
        }
        return odd;
    }

    inline bool HasVertex(const std::vector<Vertex> &sorted, const Vertex &v) {
        return std::binary_search(sorted.begin(), sorted.end(), v, VertexLess);
    }

    std::vector<Vertex> VerticesOf(const Arrangement &r, unsigned char m);

    RelateMatrix RelateEmpty(const RelateOperand &other, bool otherIsA);

    RelateMatrix RelateRegions(const RelateOperand &a, const RelateOperand &b);

    RelateMatrix RelateRegionLines(const RelateOperand &a, const RelateOperand &b);

    RelateMatrix RelateLines(const RelateOperand &a, const RelateOperand &b);

    RelateMatrix RelatePoint(const RelateOperand &a, const Vertex &p);

    RelateMatrix Relate(const RelateOperand &a, const RelateOperand &b);

    inline bool Crosses(const RelateMatrix &m, int da, int db) {
        if (da < db)
            return m.Matches("T*T******");
        if (da > db)
            return m.Matches("T*****T**");
        return da == 1 && m.Matches("0********");
    }
    inline bool Overlaps(const RelateMatrix &m, int da, int db) {
        if (da != db)
            return false;
        return m.Matches(da == 1 ? "1*T***T**" : "T*T***T**");
    }
    inline bool Touches(const RelateMatrix &m) {
        return m.Matches("FT*******") || m.Matches("F**T*****") || m.Matches("F***T****");
    }
    inline bool Covers(const RelateMatrix &m) {
        return m.Matches("T*****FF*") || m.Matches("*T****FF*") || m.Matches("***T**FF*") || m.Matches("****T*FF*");
    }

    RelateOperand ShapeOperand(const Shape &s, const std::string &call);

}
