// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/offsetsides.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

#include "planex/offset/offsetpath.hpp"
#include "planex/sketch/curveeval.hpp"
#include "planex/sketch/geometry.hpp"

namespace planex2d::sketch {

    // the cap round a keyhole's spine start becomes the hole of radius r: the sides start where their lines meet it,
    // so the cap's joints cross (no longer tangent)
    void OpenHole(OffsetSides &sides, double r, double ad) {
        const double reach = std::sqrt(r * r - ad * ad);
        for (auto &[fixed, loops] : sides)
            for (OffsetLoop &loop : loops) {
                const size_t m = loop.pieces.size();
                const size_t i = (size_t)(std::find_if(loop.pieces.begin(), loop.pieces.end(), [](const OffsetPiece &p) {
                                              return p.kind == OffsetPiece::Kind::Vertex && p.side == 0 && p.k == 0;
                                          }) - loop.pieces.begin());
                OffsetPiece &before = loop.pieces[(i + m - 1) % m], &cap = loop.pieces[i], &after = loop.pieces[(i + 1) % m];
                before.end = before.end - Normalized(before.end - before.start) * reach;
                after.start = after.start + Normalized(after.end - after.start) * reach;
                cap.start = before.end;
                cap.end = after.start;
                cap.r = r;
                loop.joints[i].tangent = false;
                loop.joints[(i + 1) % m].tangent = false;
            }
    }

    std::string ShapeOf(const OffsetSides &sides) {
        std::string out;
        for (const auto &[fixed, loops] : sides) {
            std::vector<std::string> keys;
            for (const OffsetLoop &loop : loops) {
                std::string key = loop.closed ? "o" : "-";
                for (size_t i = 0; i < loop.joints.size(); ++i) {
                    key += loop.joints[i].tangent ? " t" + std::to_string(loop.joints[i].vertex) : std::string(" x");
                    if (i < loop.pieces.size()) {
                        const OffsetPiece &p = loop.pieces[i];
                        key += " " + std::to_string((int)p.kind) + "." + std::to_string(p.k) + "." + std::to_string(p.side);
                    }
                }
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            out += fixed + "{";
            for (const std::string &k : keys)
                out += k + ";";
            out += "}";
        }
        return out;
    }

    OffsetChain OffsetChain::Of(const Geometry &g, const std::vector<size_t> &given, const std::vector<std::string> &ids, const std::string &call) {
        if (std::any_of(given.begin(), given.end(), [&](size_t e) { return g.elements[e].kind == ElementKind::Circle; })) {
            if (given.size() > 1)
                throw std::invalid_argument(call + ": a circle is a loop of its own, offset it alone");
            return {given, {1}, true};
        }
        if (std::any_of(given.begin(), given.end(), [&](size_t e) { return g.elements[e].IsClosedSpline(g); })) {
            if (given.size() > 1)
                throw std::invalid_argument(call + ": a closed spline is a loop of its own, offset it alone");
            return {given, {1}, true};
        }
        const auto rootsOf = [&](size_t e) {
            const std::array<size_t, 2> ends = g.elements[e].Ends(g);
            return std::array<size_t, 2>{g.coordsOf[ends[0]], g.coordsOf[ends[1]]};
        };
        std::map<size_t, size_t> degree;
        for (size_t e : given)
            for (size_t root : rootsOf(e))
                if (++degree[root] > 2)
                    throw std::invalid_argument(call + ": the elements branch at a point, an offset takes one chain");
        std::vector<char> used(given.size(), 0);
        used[0] = 1;
        const auto touching = [&](size_t root) -> std::optional<std::pair<size_t, bool>> {
            for (size_t i = 0; i < given.size(); ++i) {
                const std::array<size_t, 2> ends = rootsOf(given[i]);
                if (!used[i] && (ends[0] == root || ends[1] == root)) {
                    used[i] = 1;
                    return std::pair{given[i], ends[0] == root};
                }
            }
            return std::nullopt;
        };
        OffsetChain c{{given.front()}, {1}, false};
        size_t head = rootsOf(given.front())[0], tail = rootsOf(given.front())[1];
        while (const auto x = touching(tail)) {
            c.elements.push_back(x->first);
            c.forward.push_back(x->second);
            tail = rootsOf(x->first)[x->second ? 1 : 0];
        }
        while (const auto x = touching(head)) {
            c.elements.insert(c.elements.begin(), x->first);
            c.forward.insert(c.forward.begin(), !x->second);
            head = rootsOf(x->first)[x->second ? 1 : 0];
        }
        for (size_t i = 0; i < given.size(); ++i)
            if (!used[i])
                throw std::invalid_argument(call + ": the elements are not one chain (" + ids[given[i]] + " does not join on)");
        c.closed = head == tail;
        return c;
    }

    EdgeList OffsetChain::Edges(const Geometry &g) const {
        EdgeList edges;
        for (size_t k = 0; k < elements.size(); ++k) {
            const Element &e = g.elements[elements[k]];
            const bool f = forward[k];
            const size_t i = e.kind == ElementKind::Line ? 0 : 2;
            if (e.kind == ElementKind::Circle)
                continue;
            if (e.kind == ElementKind::Spline) {
                for (planex2d::Spline &half : e.DirectedSplineEdges(g, f))
                    edges.push_back(std::move(half));
                continue;
            }
            const Vertex a = g.Coords(e.p[f ? i : i + 1]), b = g.Coords(e.p[f ? i + 1 : i]);
            if (e.kind == ElementKind::Line)
                edges.push_back(planex2d::Line(a, b));
            else
                edges.push_back(planex2d::Arc(a, b, g.Coords(e.Center()), e.ccw == f));
        }
        return edges;
    }

    bool OffsetChain::IsClosedSpline(const Geometry &g) const { return elements.size() == 1 && g.elements[elements[0]].IsClosedSpline(g); }

    std::optional<std::string> OffsetChain::Cusp(const Geometry &g, const EdgeList &edges, double d, const std::vector<int> &towards,
                                             const std::vector<std::string> &ids) const {
        if (std::none_of(elements.begin(), elements.end(), [&](size_t e) { return g.elements[e].kind == ElementKind::Spline; }))
            return std::nullopt;
        const double turn = closed ? (ChainArea(edges) > 0.0 ? -1.0 : 1.0) : 1.0;
        for (int t : towards)
            for (int side : t == 0 ? std::vector<int>{1, -1} : std::vector<int>{t})
                for (size_t k = 0; k < elements.size(); ++k) {
                    const Element &e = g.elements[elements[k]];
                    if (e.kind != ElementKind::Spline)
                        continue;
                    const planex2d::Curve<double, Vertex> f = e.Curve(g);
                    const auto [t0, t1] = e.Range(g, f);
                    if (std::optional<std::string> cusp = sketch::Cusp(f, side * turn * (forward[k] ? 1.0 : -1.0), t0, t1, d, ids[elements[k]]))
                        return cusp;
                }
        return std::nullopt;
    }

}
