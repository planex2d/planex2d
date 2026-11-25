// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/properties.hpp"

#include <functional>

namespace planex2d {

    // apart from the id
    bool SameProperties(const Properties &a, const Properties &b) {
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

    // as netgen merges properties, plus minh
    Properties MergedProperties(const Properties &first, const Properties &second) {
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

    Properties Overridden(const Properties &base, const Properties &over) {
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

}
