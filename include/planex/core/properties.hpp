// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "planex/core/vertex.hpp"

namespace planex2d {

    // partition only on edges, quad_dominated only on faces
    struct ShapeProperties {
        std::optional<std::string> name;
        std::optional<double> maxh;
        std::optional<double> minh;
        std::optional<double> hpref;
        std::optional<int> layer;
        std::optional<std::array<double, 4>> col;
        std::optional<std::vector<double>> partition;  // increasing fractions of the edge length in (0, 1)
        std::optional<bool> quad_dominated;
        std::optional<std::string> id;
        bool operator==(const ShapeProperties &) const = default;
    };

    // null = defaults; shared between copies, a change allocates a new object
    using Properties = std::shared_ptr<const ShapeProperties>;

    bool SameProperties(const Properties &a, const Properties &b);
    inline bool HasPartition(const Properties &p) { return p && p->partition; }
    inline std::string NameOf(const Properties &p) { return p && p->name ? *p->name : std::string(); }
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

    Properties MergedProperties(const Properties &first, const Properties &second);

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
    Properties Overridden(const Properties &base, const Properties &over);

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
    template <class E>
    Properties PropsAt(const E &x, const Vertex &v) { return x.ends ? PropsAtEnd(x.ends, !(x.Start() == v)) : nullptr; }
    template <class E>
    Properties PropsAwayFrom(const E &x, const Vertex &v) { return x.ends ? PropsAtEnd(x.ends, x.Start() == v) : nullptr; }

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
