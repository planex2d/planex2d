// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "planex/boolean/cutloop.hpp"
#include "planex/boolean/arrangement.hpp"
#include "planex/boolean/pairrecord.hpp"

namespace planex2d {

    // arcs and splines come first: lines move onto them
    template <class F>
    void SnapEndpoints(Wire &w, double tol, F use) {
        if (tol <= 0.0)
            return;
        BBox all;
        for (const Edge &e : w)
            all.Grow(BoxOf(e));
        Grid grid(all, Grid::CellSize(all, 2 * w.Size(), tol));
        std::vector<Vertex> pts;
        std::vector<size_t> owner;
        auto merge = [&](const Vertex &p, size_t id) {
            size_t found = pts.size();
            grid.ForNear(p, [&](uint32_t k) {
                if (found == pts.size() && owner[k] != id && Norm(pts[k] - p) <= tol)
                    found = k;
            });
            if (found < pts.size())
                return pts[found];
            grid.Insert(pts.size(), p);
            pts.push_back(p);
            owner.push_back(id);
            return p;
        };
        auto move = [&](auto &e, size_t k) {
            if (!use(k))
                return;
            Vertex s = merge(e.Start(), k);
            Vertex t = merge(e.End(), k);
            Wire::MoveEnds(e, s, t, tol);
        };
        for (size_t k = 0; k < w.Size(); ++k)
            if (Arc *a = std::get_if<Arc>(&w[k]))
                move(*a, k);
        for (size_t k = 0; k < w.Size(); ++k)
            if (Spline *s = std::get_if<Spline>(&w[k]))
                move(*s, k);
        for (size_t k = 0; k < w.Size(); ++k)
            if (Line *l = std::get_if<Line>(&w[k]))
                move(*l, k);
    }

    // the chord also for arcs: near zero length the sweep can flip to ~2pi. Shorter than its own tolerance, or no
    // longer than tol where snapped with it: the snap merged its ends' neighbours, one of them may already sit on the
    // other end, which would leave it dangling
    template <class F>
    std::vector<bool> EpsEdges(const Wire &w, double tol, F snapped) {
        std::vector<bool> eps(w.Size());
        for (size_t k = 0; k < w.Size(); ++k)
            eps[k] = !IsClosedEdge(w[k]) && std::visit([&](const auto &e) {
                if constexpr (IsCircle<decltype(e)>)
                    return false;
                double l = Norm(e.End() - e.Start());
                const bool own = l < Tol(e.LengthScale());
                if (!own && !(snapped(k) && l <= tol))
                    return false;
                // an edge of no length at all is nothing to tell, one within the snap tolerance is the snap's
                if (own && l > 0.0)
                    Warning() << "removed edge of length " << l << " (rounding)";
                return true;
            }, w[k]);
        return eps;
    }
    inline std::vector<bool> EpsEdges(const Wire &w) {
        return EpsEdges(w, 0.0, [](size_t) { return false; });
    }

    inline void RemoveEpsEdges(Wire &w) { w.Erase(EpsEdges(w)); }

    std::vector<bool> SlitEdges(const Wire &w, double tol);

    void Snap(Wire &w);

    std::string NoRegionError(const char *call, const Wire &c, const std::vector<std::pair<size_t, bool>> &ends);

    Wire InitialClean(const Wire &w, const std::vector<bool> &fromOpen = {}, const char *call = nullptr);

}
