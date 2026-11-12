// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "boolean/cutloop.hpp"
#include "boolean/arrangement.hpp"
#include "boolean/pairrecord.hpp"

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
            e.template Trim<true>(s, t);
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

    // two edges between the same two vertices, running opposite ways within tol of each other halfway: a slit of no
    // width, which bounds nothing (the snap with the whole wire's tol merged the ends of edges a local cut kept apart)
    inline std::vector<bool> SlitEdges(const Wire &w, double tol) {
        const std::vector<EdgeSpan> spans = SortedSpans(w, [](size_t) { return true; });
        std::vector<bool> slit(w.Size(), false);
        for (size_t a = 0; a < spans.size();) {
            const size_t b = SameSpansEnd(spans, a);
            for (size_t x = a; x < b; ++x)
                for (size_t y = x + 1; y < b; ++y) {
                    const uint32_t i = spans[x].k, j = spans[y].k;
                    if (!slit[i] && !slit[j] && StartOf(w[i]) == EndOf(w[j]) && MidPointOf(w[i]).IsClose(MidPointOf(w[j]), tol))
                        slit[i] = slit[j] = true;
                }
            a = b;
        }
        return slit;
    }

    // a ray through two endpoints one ulp apart would count both edges
    inline void Snap(Wire &w) {
        static Timer t("planex Snap");
        Region reg(t);
        const double tol = w.Tol();
        SnapEndpoints(w, tol, [](size_t) { return true; });
        w.Erase(EpsEdges(w, tol, [](size_t) { return true; }));
        const std::vector<bool> slit = SlitEdges(w, tol);
        if (std::find(slit.begin(), slit.end(), true) == slit.end())
            return;
        // the edges a slit ended between may run on along one carrier there
        w.Erase(slit);
        w.MergeOnCarriers();
    }

    // overlapping stretches removed, crossings cut, snapped, only what separates inside from outside kept: the edges of
    // the region the wire bounds. fromOpen: per edge, it comes from an open wire, which cancels nothing (empty: all
    // closed)
    inline std::string NoRegionError(const char *call, const Wire &c, const std::vector<std::pair<size_t, bool>> &ends) {
        std::vector<Vertex> at;
        for (const auto &[k, end] : ends)
            at.push_back(end ? EndOf(c[k]) : StartOf(c[k]));
        std::ostringstream text;
        text << call << ": the wires bound no region, a wire is open (" << ends.size() << " open ends";
        if (at.size() >= 2 && at.size() <= 2000) {
            double d = Norm(at[1] - at[0]);
            for (size_t i = 0; i < at.size(); ++i)
                for (size_t j = i + 1; j < at.size(); ++j)
                    d = std::min(d, Norm(at[j] - at[i]));
            text << ", the closest " << std::setprecision(3) << d << " apart";
        }
        text << "); close it with Close() or px.ClosedOpenWires(wires)";
        return text.str();
    }

    inline Wire InitialClean(const Wire &w, const std::vector<bool> &fromOpen = {}, const char *call = nullptr) {
        static Timer t("planex InitialClean");
        Region reg(t);
        Wire c = w;
        std::vector<CutState> state(c.Size());
        for (size_t k = 0; k < fromOpen.size(); ++k)
            state[k].open = fromOpen[k];
        CutCircles(c, state);
        Grid grid = c.EdgeGrid();
        CutRecord rec;
        CutAll(c, grid, state, true, &rec);
        ApplyIdentified(c, state, VertexMerges(rec.identified), [](size_t) { return true; });
        std::vector<bool> removed(c.Size());
        for (size_t k = 0; k < c.Size(); ++k)
            removed[k] = state[k].excluded || state[k].drop;
        c.Erase(removed);
        Snap(c);
        const std::vector<std::pair<size_t, bool>> ends = c.OpenEnds();
        const size_t open = ends.size();
        std::vector<size_t> dropped;
        Wire kept = Boundary(c, &dropped);
        const bool given = std::any_of(fromOpen.begin(), fromOpen.end(), [](bool b) { return b; });
        if (open && call && given && kept.Empty())
            throw std::invalid_argument(NoRegionError(call, c, ends));
        if (open)
            Warning() << "open wire with " << open << " open ends, inside and outside are not defined; consider "
                      << "px.ClosedOpenWires(wires)";
        if (!open && !dropped.empty()) {
            const Vertex m = MidPointOf(c[dropped.front()]);
            Warning() << dropped.size() << (dropped.size() == 1 ? " edge separates" : " edges separate")
                      << " no inside from outside and " << (dropped.size() == 1 ? "is" : "are") << " dropped, e.g. at (" << m.x
                      << ", " << m.y << "): a chain ending on other edges (a T-junction) or running across the region; "
                      << "consider px.ClosedOpenWires(wires)";
        }
        c = std::move(kept);
        c.MergeOnCarriers();
        return c;
    }

}
