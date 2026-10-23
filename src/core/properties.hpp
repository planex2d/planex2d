// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/vertex.hpp"

namespace planex2d {

    // of an edge or a face; partition only on edges, quad_dominated only on faces
    struct ShapeProperties {
        std::optional<std::string> name;
        std::optional<double> maxh;
        std::optional<double> minh;
        std::optional<double> hpref;
        std::optional<int> layer;
        std::optional<std::array<double, 4>> col;
        std::optional<std::vector<double>> partition;  // increasing fractions of the edge length in (0, 1)
        std::optional<bool> quad_dominated;
        // the id within the object that holds the edge or face ("Line.3"), the object's id before it
        std::optional<std::string> id;
        bool operator==(const ShapeProperties &) const = default;
    };

    // shared between the shapes copied from one another, null = defaults; a change allocates a new object
    using Properties = std::shared_ptr<const ShapeProperties>;

    // the same apart from the id: what an edge is, not which one it is
    inline bool SameProperties(const Properties &a, const Properties &b) {
        if (a == b)
            return true;
        if (!a || !b) {
            const ShapeProperties &x = a ? *a : *b;
            ShapeProperties none;
            none.id = x.id;
            return x == none;
        }
        if (a->id == b->id)
            return *a == *b;
        ShapeProperties c = *b;
        c.id = a->id;
        return *a == c;
    }
    inline bool HasPartition(const Properties &p) { return p && p->partition; }
    inline std::string NameOf(const Properties &p) { return p && p->name ? *p->name : std::string(); }
    // properties that tell a vertex apart (its id alone does not): cleanups keep such a vertex
    inline bool Marked(const Properties &p) { return p && !SameProperties(p, nullptr); }

    template <class T>
    void KeepFirst(std::optional<T> &a, const std::optional<T> &b) {
        if (!a)
            a = b;
    }
    template <class T, class Better>
    void KeepBetter(std::optional<T> &a, const std::optional<T> &b, Better better) {
        if (b && (!a || better(*b, *a)))
            a = b;
    }

    // as netgen merges properties, plus minh: the first one's name, color, partition and quad_dominated win, maxh and
    // minh are the smaller, hpref and layer the larger ones
    inline Properties MergedProperties(const Properties &first, const Properties &second) {
        if (!first)
            return second;
        if (!second || first == second)
            return first;
        ShapeProperties p = *first;
        KeepFirst(p.name, second->name);
        KeepFirst(p.col, second->col);
        KeepFirst(p.partition, second->partition);
        KeepFirst(p.quad_dominated, second->quad_dominated);
        KeepFirst(p.id, second->id);
        KeepBetter(p.maxh, second->maxh, std::less<>());
        KeepBetter(p.minh, second->minh, std::less<>());
        KeepBetter(p.hpref, second->hpref, std::greater<>());
        KeepBetter(p.layer, second->layer, std::greater<>());
        return p == *first ? first : std::make_shared<const ShapeProperties>(p);
    }

    // a changed copy, null if nothing is left
    template <class F>
    Properties ChangedProperties(const Properties &p, F change) {
        ShapeProperties q = p ? *p : ShapeProperties{};
        change(q);
        if (q == ShapeProperties{})
            return nullptr;
        if (p && q == *p)
            return p;
        return std::make_shared<const ShapeProperties>(std::move(q));
    }
    inline Properties WithoutId(const Properties &p) {
        return ChangedProperties(p, [](ShapeProperties &q) { q.id.reset(); });
    }
    // the fields over sets replace those of base
    inline Properties Overridden(const Properties &base, const Properties &over) {
        if (!over)
            return base;
        return ChangedProperties(base, [&](ShapeProperties &q) {
            auto take = [](auto &field, const auto &value) {
                if (value)
                    field = value;
            };
            take(q.name, over->name);
            take(q.maxh, over->maxh);
            take(q.minh, over->minh);
            take(q.hpref, over->hpref);
            take(q.layer, over->layer);
            take(q.col, over->col);
            take(q.partition, over->partition);
            take(q.quad_dominated, over->quad_dominated);
        });
    }

    inline Properties ScaledSizes(const Properties &p, double s, bool scaleMaxh, bool scaleMinh) {
        return ChangedProperties(p, [&](ShapeProperties &q) {
            if (scaleMaxh && q.maxh)
                *q.maxh *= s;
            if (scaleMinh && q.minh)
                *q.minh *= s;
        });
    }
    inline Properties WithoutPartition(const Properties &p) {
        return HasPartition(p) ? ChangedProperties(p, [](ShapeProperties &q) { q.partition.reset(); }) : p;
    }

    // the properties of the vertices at an edge's start and end, null for none; a vertex has those of all edge ends
    // there, merged
    struct EndProperties {
        Properties start, end;
    };
    using Ends = std::shared_ptr<const EndProperties>;

    inline Ends MakeEnds(Properties start, Properties end) {
        if (!start && !end)
            return nullptr;
        return std::make_shared<const EndProperties>(EndProperties{std::move(start), std::move(end)});
    }
    inline Properties PropsAtEnd(const Ends &e, bool end) { return !e ? nullptr : end ? e->end : e->start; }
    inline Ends Swapped(const Ends &e) { return e ? MakeEnds(e->end, e->start) : nullptr; }
    // at the end of x that lies at v, at the other end
    template <class E>
    Properties PropsAt(const E &x, const Vertex &v) { return x.ends ? PropsAtEnd(x.ends, !(x.Start() == v)) : nullptr; }
    template <class E>
    Properties PropsAwayFrom(const E &x, const Vertex &v) { return x.ends ? PropsAtEnd(x.ends, x.Start() == v) : nullptr; }

    // the pieces of an edge in order: the first keeps the start's vertex properties, the last the end's, the points
    // between have none
    template <class E>
    void KeepOuterEnds(std::vector<E> &pieces, const Ends &ends) {
        if (!ends || pieces.empty())
            return;
        if (pieces.size() == 1) {
            pieces.front().ends = ends;
            return;
        }
        for (E &p : pieces)
            p.ends = nullptr;
        pieces.front().ends = MakeEnds(ends->start, nullptr);
        pieces.back().ends = MakeEnds(nullptr, ends->end);
    }

}
