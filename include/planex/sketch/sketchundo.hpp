// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    template <class F>
    BasicSketch<Store> &BasicSketch<Store>::Atomic(F f) {
        AtomicIf([&] {
            f();
            return true;
        });
        return *this;
    }

    template <class Store>
    template <class F>
    bool BasicSketch<Store>::AtomicIf(F f) {
        CallStarts();
        const Undo u{s.Marked(), book.counters, book.elementIds.size(), pen.WithoutChain(),
                     pen.chain.size(), ended.size(), book.wireLog.size(), parked, book.groups.size()};
        ++atomic;
        bool keep;
        try {
            keep = f();
            if (keep && atomic == 1)
                CallEnds();
        } catch (...) {
            --atomic;
            Restore(u);
            throw;
        }
        if (!keep) {
            --atomic;
            Restore(u);
            return false;
        }
        if (--atomic == 0)
            ended.clear();
        return true;
    }

    template <class Store>
    void BasicSketch<Store>::Restore(const Undo &u) {
        s.Truncate(u.mark);
        book.counters = u.counters;
        book.elementIds.resize(u.elements);
        props.resize(u.elements);
        roles.resize(u.elements);
        CallUndone();
        std::vector<size_t> chain = std::move(pen.chain);
        pen = u.pen;
        parked = u.parked;
        book.UndoWires(u.wireLog);
        book.groups.resize(u.groups);
        if (ended.size() > u.ended) {
            chain = std::move(ended[u.ended]);
            ended.resize(u.ended);
        }
        pen.chain = std::move(chain);
        pen.chain.resize(u.chain);
        if (const auto it = book.wires.find(pen.wire); it != book.wires.end())
            it->second.resize(u.chain);
        Reindex();
    }

    template <class Store>
    template <class F>
    BasicSketch<Store> &BasicSketch<Store>::Drawing(const Options &opt, F f) {
        if (normalPen || !opt.construction)
            return Atomic([&] {
                if (!normalPen)
                    parked.reset();
                f();
            });
        return Atomic([&] {
            if (!parked)
                parked = sketch::PenState{pen.pos, pen.dir, pen.pos, pen.point, pen.point, std::nullopt, {}, {}};
            normalPen = Swapped(std::move(*parked));
            struct Back {
                BasicSketch &owner;
                bool done = false;
                void Restore() {
                    done = true;
                    owner.parked = owner.Swapped(std::move(*owner.normalPen));
                    owner.normalPen.reset();
                }
                ~Back() {
                    if (!done)
                        Restore();
                }
            } back{*this};
            f();
            back.Restore();
            Sync();
        });
    }

}
