// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "boolean/boolean.hpp"
#include "shape/measure.hpp"

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

    // a region (the loops of faces), lines (a wire's edges, their ends of odd degree the boundary) or a point
    struct RelateOperand {
        int dim = 2;
        Wire edges;
        Vertex point;

        bool Empty() const { return dim > 0 && edges.Empty(); }
    };

    // the vertices where an odd number of the pieces end: the boundary of lines
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

    // the vertices of the pieces of mask m (or shared)
    inline std::vector<Vertex> VerticesOf(const Arrangement &r, unsigned char m) {
        std::vector<Vertex> out;
        for (size_t k = 0; k < r.c.Size(); ++k)
            if (!r.state[k].excluded && (r.state[k].mask & m) != 0) {
                out.push_back(StartOf(r.c[k]));
                out.push_back(EndOf(r.c[k]));
            }
        std::sort(out.begin(), out.end(), VertexLess);
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // a or b empty: the other's interior and boundary lie in its exterior
    inline RelateMatrix RelateEmpty(const RelateOperand &other, bool otherIsA) {
        RelateMatrix m;
        m.Raise(2, 2, 2);
        if (!other.Empty()) {
            m.Raise(2, 0, (signed char)other.dim);
            if (other.dim == 2)
                m.Raise(2, 1, 1);
            else if (other.dim == 1 && !OddEnds(other.edges, [](size_t) { return true; }).empty())
                m.Raise(2, 1, 0);
        }
        return otherIsA ? m.Transposed() : m;
    }

    inline RelateMatrix RelateRegions(const RelateOperand &a, const RelateOperand &b) {
        RelateMatrix m;
        const BBox boxa = a.edges.Box(), boxb = b.edges.Box();
        const Arrangement r = Arranged(a.edges, b.edges, boxa, boxb, Tol(std::max(boxa.NormInf(), boxb.NormInf())), true);
        bool shared = false;
        for (size_t k = 0; k < r.c.Size(); ++k) {
            if (r.state[k].excluded)
                continue;
            const PieceSide side = r.side[k];
            switch (r.state[k].mask) {
                case 3:
                    shared = true;
                    m.Raise(1, 1, 1);
                    if (side == PieceSide::SharedSame) {
                        m.Raise(0, 0, 2);
                    } else {
                        m.Raise(0, 2, 2);
                        m.Raise(2, 0, 2);
                    }
                    break;
                case 1:
                    if (side == PieceSide::Inside) {
                        m.Raise(0, 0, 2);
                        m.Raise(1, 0, 1);
                    } else {
                        m.Raise(0, 2, 2);
                        m.Raise(1, 2, 1);
                    }
                    break;
                default:
                    if (side == PieceSide::Inside) {
                        m.Raise(0, 0, 2);
                        m.Raise(0, 1, 1);
                    } else {
                        m.Raise(2, 0, 2);
                        m.Raise(2, 1, 1);
                    }
            }
        }
        if (!shared) {
            const std::vector<Vertex> va = VerticesOf(r, 1);
            for (const Vertex &v : VerticesOf(r, 2))
                if (HasVertex(va, v)) {
                    m.Raise(1, 1, 0);
                    break;
                }
        }
        return m;
    }

    // a a region, b lines
    inline RelateMatrix RelateRegionLines(const RelateOperand &a, const RelateOperand &b) {
        RelateMatrix m;
        m.Raise(0, 2, 2);
        const BBox boxa = a.edges.Box(), boxb = b.edges.Box();
        // the lines first: the sides of their pieces against the region
        const Arrangement r = Arranged(b.edges, a.edges, boxb, boxa, Tol(std::max(boxa.NormInf(), boxb.NormInf())), true);
        for (size_t k = 0; k < r.c.Size(); ++k) {
            if (r.state[k].excluded)
                continue;
            switch (r.state[k].mask) {
                case 3: m.Raise(1, 0, 1); break;
                case 1: m.Raise(r.side[k] == PieceSide::Inside ? 0 : 2, 0, 1); break;
                default: m.Raise(1, 2, 1);
            }
        }
        const std::vector<Vertex> ends = OddEnds(r.c, [&](size_t k) { return !r.state[k].excluded && (r.state[k].mask & 1) != 0; });
        const std::vector<Vertex> onRegion = VerticesOf(r, 2);
        for (const Vertex &e : ends) {
            if (HasVertex(onRegion, e)) {
                m.Raise(1, 1, 0);
                continue;
            }
            for (size_t k = 0; k < r.c.Size(); ++k)
                if (!r.state[k].excluded && r.state[k].mask == 1 && (StartOf(r.c[k]) == e || EndOf(r.c[k]) == e)) {
                    m.Raise(r.side[k] == PieceSide::Inside ? 0 : 2, 1, 0);
                    break;
                }
        }
        for (const Vertex &v : VerticesOf(r, 1))
            if (HasVertex(onRegion, v) && !HasVertex(ends, v))
                m.Raise(1, 0, 0);
        return m;
    }

    inline RelateMatrix RelateLines(const RelateOperand &a, const RelateOperand &b) {
        RelateMatrix m;
        const BBox boxa = a.edges.Box(), boxb = b.edges.Box();
        const Arrangement r = Arranged(a.edges, b.edges, boxa, boxb, Tol(std::max(boxa.NormInf(), boxb.NormInf())), false);
        for (size_t k = 0; k < r.c.Size(); ++k)
            if (!r.state[k].excluded)
                switch (r.state[k].mask) {
                    case 3: m.Raise(0, 0, 1); break;
                    case 1: m.Raise(0, 2, 1); break;
                    default: m.Raise(2, 0, 1);
                }
        auto ends = [&](unsigned char mask) {
            return OddEnds(r.c, [&](size_t k) { return !r.state[k].excluded && (r.state[k].mask & mask) != 0; });
        };
        const std::vector<Vertex> endsA = ends(1), endsB = ends(2);
        const std::vector<Vertex> va = VerticesOf(r, 1), vb = VerticesOf(r, 2);
        for (const Vertex &v : va)
            if (HasVertex(vb, v))
                m.Raise(HasVertex(endsA, v) ? 1 : 0, HasVertex(endsB, v) ? 1 : 0, 0);
        for (const Vertex &e : endsA)
            if (!HasVertex(vb, e))
                m.Raise(1, 2, 0);
        for (const Vertex &e : endsB)
            if (!HasVertex(va, e))
                m.Raise(2, 1, 0);
        return m;
    }

    // a a region or lines, p a point: inside, on the boundary or outside as Locate decides
    inline RelateMatrix RelatePoint(const RelateOperand &a, const Vertex &p) {
        RelateMatrix m;
        m.Raise(0, 2, (signed char)a.dim);
        const std::vector<Vertex> ends = a.dim == 1 ? OddEnds(a.edges, [](size_t) { return true; }) : std::vector<Vertex>{};
        if (a.dim == 2)
            m.Raise(1, 2, 1);
        const Planar s = a.dim == 2 ? Planar{a.edges, Wire(a.edges.Axes())} : Planar{Wire(a.edges.Axes()), a.edges};
        const double tol = std::max(s.Tolerance(), Tol(NormInf(p)));
        size_t atEnd = ends.size();
        for (size_t i = 0; i < ends.size(); ++i)
            if (ends[i].IsClose(p, tol))
                atEnd = i;
        if (ends.size() > (atEnd < ends.size() ? 1u : 0u))
            m.Raise(1, 2, 0);
        const Where w = Locate(s, p);
        if (a.dim == 2)
            m.Raise(w == Where::Inside ? 0 : w == Where::Boundary ? 1 : 2, 0, 0);
        else
            m.Raise(atEnd < ends.size() ? 1 : w == Where::Boundary ? 0 : 2, 0, 0);
        return m;
    }

    inline RelateMatrix Relate(const RelateOperand &a, const RelateOperand &b) {
        if (a.Empty())
            return RelateEmpty(b, false);
        if (b.Empty())
            return RelateEmpty(a, true);
        if (a.dim < b.dim)
            return Relate(b, a).Transposed();
        RelateMatrix m;
        m.Raise(2, 2, 2);
        RelateMatrix r;
        if (b.dim == 0 && a.dim == 0) {
            const double tol = Tol(std::max(NormInf(a.point), NormInf(b.point)));
            if (a.point.IsClose(b.point, tol)) {
                r.Raise(0, 0, 0);
            } else {
                r.Raise(0, 2, 0);
                r.Raise(2, 0, 0);
            }
        } else if (b.dim == 0) {
            r = RelatePoint(a, b.point);
        } else if (a.dim == 2 && b.dim == 2) {
            r = RelateRegions(a, b);
        } else if (a.dim == 2) {
            r = RelateRegionLines(a, b);
        } else {
            r = RelateLines(a, b);
        }
        for (size_t k = 0; k < 9; ++k)
            m.dims[k] = std::max(m.dims[k], r.dims[k]);
        return m;
    }

    // the named relations on the matrix, by the dimensions of the operands
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

}
