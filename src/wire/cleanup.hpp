// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "wire/wire.hpp"
#include "core/timer.hpp"

namespace planex2d {

    template <class A, class B>
    std::optional<Edge> Joined(const A &a, const B &b, const Vertex &v) {
        if constexpr (std::is_same_v<A, B> && !IsCircle<A>) {
            if (auto r = a.Joined(b, v))
                return Edge(std::move(*r));
        }
        return std::nullopt;
    }

    inline std::vector<size_t> Wire::SmallEdges(double size) const {
        std::vector<size_t> small;
        for (size_t i = 0; i < edges.size(); ++i)
            if (LengthOf(edges[i]) < size)
                small.push_back(i);
        return small;
    }

    inline void Wire::RemoveSmallGaps(double tol) {
        static Timer t("planex RemoveSmallGaps");
        Region reg(t);
        if (tol <= 0.0)
            return;
        std::vector<Vertex> pts = Endpoints();
        Grid grid = Grid::Of(pts, tol);
        for (size_t k = 0; k < pts.size(); ++k) {
            if (Matched(pts, grid, k))
                continue;
            std::vector<size_t> near;
            grid.ForNear(pts[k], [&](size_t j) {
                if (j / 2 != k / 2 && !IsClosedEdge(edges[j / 2]) && pts[j].IsClose(pts[k], tol))
                    near.push_back(j);
            });
            for (size_t j : near)
                Move(pts, grid, j, pts[k]);
        }
    }

    // the chains of lines that may merge (one partner at each joint, same properties, no marked vertex), each as its
    // vertices along it; a vertex goes while every vertex of the chain between its kept neighbours stays within tol of the
    // line through them, in passes until none goes, so the result stays within tol of what the call was given
    inline void Wire::MergeCollinearEdges(double tol) {
        static Timer t("planex MergeCollinearEdges");
        Region reg(t);
        if (tol <= 0.0)
            return;
        size_t n = edges.size();
        std::vector<Vertex> pts = Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        auto partner = [&](size_t k) -> std::optional<size_t> {
            std::optional<size_t> found;
            size_t count = 0;
            grid.ForCell(pts[k], [&](size_t j) {
                if (j / 2 != k / 2 && pts[j] == pts[k]) {
                    found = j;
                    ++count;
                }
            });
            return count == 1 ? found : std::nullopt;
        };
        auto joinable = [&](size_t k, size_t j) {
            const Line *a = std::get_if<Line>(&edges[k / 2]), *b = std::get_if<Line>(&edges[j / 2]);
            return a && b && !a->fromSpline && !b->fromSpline && Owner(k / 2) == Owner(j / 2) && SameProperties(a->props, b->props) &&
                   !HasPartition(a->props) && !Marked(EndpointProps(k)) && !Marked(EndpointProps(j));
        };
        std::vector<bool> visited(n, false), removed(n, false);
        std::vector<Line> merged;
        std::vector<uint32_t> mergedOwner;
        for (size_t i = 0; i < n; ++i) {
            const Line *first = std::get_if<Line>(&edges[i]);
            if (visited[i] || !first)
                continue;
            visited[i] = true;
            // the endpoints walked from each end of edge i: at a joint, the walked edge's endpoint, then the next edge's
            std::vector<size_t> side[2];
            bool closed = false;
            for (size_t dir = 0; dir < 2 && !closed; ++dir) {
                size_t k = 2 * i + dir;
                for (;;) {
                    std::optional<size_t> j = partner(k);
                    if (!j || !joinable(k, *j))
                        break;
                    if (*j / 2 == i) {
                        closed = true;
                        break;
                    }
                    if (visited[*j / 2])
                        break;
                    visited[*j / 2] = true;
                    side[dir].push_back(k);
                    side[dir].push_back(*j);
                    k = *j ^ 1;
                }
                side[dir].push_back(k);
            }
            // along the chain from its start: per vertex the endpoint before it (at[2v]) and after it (at[2v + 1]); edge e
            // of the chain runs from vertex e to vertex e + 1
            // (closed: the walk from edge i's start came round to its end, vertex 0 is there and comes again last)
            std::vector<size_t> at{closed ? 2 * i + 1 : side[0].back(), side[0].back()};
            for (size_t m = side[0].size() - 1; m-- > 0;)
                at.push_back(side[0][m]);
            if (closed) {
                at.push_back(2 * i + 1);
                at.push_back(side[0].back());
            } else {
                at.insert(at.end(), side[1].begin(), side[1].end());
                at.push_back(side[1].back());
            }
            const size_t count = at.size() / 2;
            if (count < 3)
                continue;
            std::vector<Vertex> v(count);
            for (size_t m = 0; m < count; ++m)
                v[m] = pts[at[2 * m]];
            // vertices of a closed chain repeat round it: the last one is the first
            const size_t vertices = closed ? count - 1 : count;
            std::vector<size_t> keep(vertices);
            for (size_t m = 0; m < vertices; ++m)
                keep[m] = m;
            const auto within = [&](size_t a, size_t c) {
                const Vertex &p = v[a], &q = v[c % vertices];
                const Vertex pq = q - p;
                const double l = Norm(pq);
                if (!(l > 0.0))
                    return false;
                for (size_t m = a + 1; m < c; ++m)
                    if (std::abs(Cross(pq, v[m % vertices] - p)) > tol * l)
                        return false;
                return true;
            };
            for (bool any = true; any && keep.size() > (closed ? 3u : 2u);) {
                any = false;
                for (size_t m = closed ? 0 : 1; m + (closed ? 0 : 1) < keep.size() && keep.size() > (closed ? 3u : 2u);) {
                    const size_t r = keep.size();
                    size_t a = keep[(m + r - 1) % r], b = keep[m], c = keep[(m + 1) % r];
                    if (b < a)
                        b += vertices;
                    if (c <= a)
                        c += vertices;
                    const Vertex &pb = v[b % vertices];
                    if (Dot(v[a] - pb, v[c % vertices] - pb) < 0.0 && within(a, c)) {
                        keep.erase(keep.begin() + (std::ptrdiff_t)m);
                        any = true;
                    } else
                        ++m;
                }
            }
            if (keep.size() == vertices)
                continue;
            if (closed)
                keep.push_back(keep.front() + vertices);
            for (size_t m = 0; m + 1 < keep.size(); ++m) {
                const size_t a = keep[m], c = keep[m + 1];
                if (c == a + 1)
                    continue;
                for (size_t e = a; e < c; ++e)
                    removed[at[2 * (e % vertices) + 1] / 2] = true;
                Line line(v[a % vertices], v[c % vertices]);
                line.props = first->props;
                line.ends = MakeEnds(EndpointProps(at[2 * (a % vertices) + 1]), EndpointProps(at[2 * (c % vertices)]));
                merged.push_back(line);
                mergedOwner.push_back(Owner(i));
            }
        }
        Erase(removed);
        for (size_t k = 0; k < merged.size(); ++k)
            Append(std::move(merged[k]), mergedOwner[k]);
    }

    // joins start from the edges use selects; the joined edge takes the place of the first
    template <class F>
    void Wire::MergeOnCarriers(F use) {
        static Timer t("planex MergeOnCarriers");
        Region reg(t);
        const size_t n = edges.size();
        std::vector<Vertex> pts = Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        auto partner = [&](size_t k) -> std::optional<size_t> {
            std::optional<size_t> found;
            size_t count = 0;
            grid.ForCell(pts[k], [&](size_t j) {
                if (j / 2 != k / 2 && pts[j] == pts[k]) {
                    found = j;
                    ++count;
                }
            });
            return count == 1 ? found : std::nullopt;
        };
        std::vector<bool> removed(n, false);
        for (size_t i = 0; i < n; ++i) {
            if (removed[i] || !use(i) || HasPartition(PropsOf(edges[i])))
                continue;
            Edge current = edges[i];
            bool any = false;
            for (size_t k : {2 * i + 1, 2 * i}) {
                for (;;) {
                    std::optional<size_t> j = partner(k);
                    if (!j || *j / 2 == i || removed[*j / 2] || Owner(*j / 2) != Owner(i) ||
                        !SameProperties(PropsOf(current), PropsOf(edges[*j / 2])) || HasPropsAt(current, pts[k]) ||
                        Marked(EndpointProps(*j)))
                        break;
                    std::optional<Edge> e = std::visit([&](const auto &a, const auto &b) { return Joined(a, b, pts[k]); },
                                                       current, edges[*j / 2]);
                    if (!e)
                        break;
                    current = std::move(*e);
                    removed[*j / 2] = true;
                    any = true;
                    k = *j ^ 1;
                }
            }
            if (any)
                edges[i] = std::move(current);
        }
        Erase(removed);
    }

    inline void Wire::MergeOnCarriers() {
        MergeOnCarriers([](size_t) { return true; });
    }

    inline bool IsFromSpline(const Edge &e) {
        return std::visit([](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline> || IsCircle<decltype(x)>)
                return false;
            else
                return x.fromSpline;
        }, e);
    }

    // per small edge, the length of the run of small edges it lies in (small edges meeting at their ends)
    inline std::vector<double> SmallRunLengths(const EdgeList &edges, const std::vector<Vertex> &pts, Grid &grid,
                                               const std::vector<bool> &small) {
        const size_t n = edges.size();
        std::vector<size_t> run(n);
        for (size_t i = 0; i < n; ++i)
            run[i] = i;
        auto root = [&](size_t i) {
            while (run[i] != i)
                i = run[i] = run[run[i]];
            return i;
        };
        for (size_t j = 0; j < 2 * n; ++j) {
            if (!small[j / 2])
                continue;
            grid.ForCell(pts[j], [&](size_t k) {
                if (small[k / 2] && pts[k] == pts[j])
                    run[root(j / 2)] = root(k / 2);
            });
        }
        std::vector<double> total(n, 0.0);
        for (size_t i = 0; i < n; ++i)
            if (small[i])
                total[root(i)] += LengthOf(edges[i]);
        std::vector<double> length(n, 0.0);
        for (size_t i = 0; i < n; ++i)
            if (small[i])
                length[i] = total[root(i)];
        return length;
    }

    inline void Wire::RemoveSmallEdges(double tol, bool splinePieces) {
        static Timer t("planex RemoveSmallEdges");
        Region reg(t);
        size_t n = edges.size();
        std::vector<bool> small(n), removed(n, false);
        bool any = false;
        for (size_t i = 0; i < n; ++i) {
            small[i] = std::visit([&](const auto &x) {
                if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline> || IsCircle<decltype(x)>)
                    return false;
                else
                    return (splinePieces || !x.fromSpline) && !HasPartition(x.props) && x.Length() <= tol;
            }, edges[i]);
            any = any || small[i];
        }
        if (!any)
            return;
        std::vector<Vertex> pts = Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        // each small edge goes to its midpoint, one after the other, so a run of them shrinks to one point: a spline's
        // pieces go only where their whole run is no longer than tol
        if (splinePieces) {
            const std::vector<double> run = SmallRunLengths(edges, pts, grid, small);
            for (size_t i = 0; i < n; ++i)
                if (small[i] && run[i] > tol && IsFromSpline(edges[i]))
                    small[i] = false;
        }
        auto moveAll = [&](const Vertex &from, const Vertex &to) {
            std::vector<size_t> at;
            grid.ForCell(from, [&](size_t j) {
                if (!removed[j / 2] && pts[j] == from)
                    at.push_back(j);
            });
            for (size_t j : at)
                Move(pts, grid, j, to);
        };
        for (size_t i = 0; i < n; ++i) {
            if (!small[i])
                continue;
            removed[i] = true;
            Vertex a = pts[2 * i], b = pts[2 * i + 1];
            if (a == b) {
                Warning() << "chain of small edges collapsed at (" << a.x << ", " << a.y << ")";
                continue;
            }
            Vertex m = (a + b) * 0.5;
            moveAll(a, m);
            moveAll(b, m);
        }
        for (size_t i = 0; i < n; ++i)
            if (!removed[i] && !IsClosedEdge(edges[i]) && pts[2 * i] == pts[2 * i + 1]) {
                Warning() << "edge collapsed to a point at (" << pts[2 * i].x << ", " << pts[2 * i].y << ")";
                removed[i] = true;
            }
        Erase(removed);
    }

    inline void Wire::RemoveFreeEdges() {
        static Timer t("planex RemoveFreeEdges");
        Region reg(t);
        std::vector<Vertex> pts = Endpoints();
        Grid grid = Grid::Of(pts, 0.0);
        std::vector<bool> removed(edges.size());
        size_t n = 0;
        for (size_t i = 0; i < edges.size(); ++i) {
            removed[i] = Owner(i) < faceOwners && !Matched(pts, grid, 2 * i) && !Matched(pts, grid, 2 * i + 1);
            n += removed[i];
        }
        if (n == 0)
            return;
        Warning() << n << " free edge" << (n > 1 ? "s" : "") << " removed";
        Erase(removed);
    }

    inline Wire Wire::Cleaned(const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup) const {
        Wire w = *this;
        w.RemoveSmallGaps(cleanup.close_gaps_smaller);
        // edges below the merge tolerance go first: lines merged across them can change which loops there are
        w.RemoveSmallEdges(std::min(cleanup.remove_edges_smaller, cleanup.merge_collinear_closer));
        w.MergeCollinearEdges(cleanup.merge_collinear_closer);
        if (arcs.enabled)
            w.FitArcs(arcs);
        if (fit.enabled)
            w.FitSplines(fit);
        // what is left goes to OCC as it is, OCC takes no edge shorter than its precision
        w.RemoveSmallEdges(cleanup.remove_edges_smaller, true);
        if (cleanup.remove_free_edges)
            w.RemoveFreeEdges();
        return w;
    }

    inline Wire &Wire::Simplify(const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup) {
        return *this = Cleaned(arcs, fit, cleanup);
    }

}
