// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "wire/wire.hpp"
#include "core/disjointsets.hpp"
#include "curves/curvefit.hpp"
#include "core/timer.hpp"

namespace planex2d {

    inline Properties Wire::PropsAtVertex(const Chains &c, size_t i, size_t v) const {
        const Ends &x = EndsOf(edges[i]);
        if (!x)
            return nullptr;
        return c.Start(i) == v && x->start ? x->start : c.End(i) == v ? x->end : nullptr;
    }

    inline Ends Wire::ChainEnds(const Chains &c, const Chain &ch, size_t b, size_t e) const {
        return MakeEnds(PropsAtVertex(c, ch.edges[b], ch.vids[b]), PropsAtVertex(c, ch.edges[e - 1], ch.vids[e]));
    }

    // maxLength none: an edge is short enough when shorter than 1/20 of the length of the loop or open wire it lies in
    // (its connected edges), so a tessellation is fitted at any scale and a polygon of few sides stays
    inline Wire::Chains Wire::FindChains(std::optional<double> maxLength, double maxAngle, double ignore, bool fromSplineIsSmall, bool stopAtVertexProps) const {
        Chains c;
        size_t n = edges.size();
        size_t nv = VertexIds(c.id);
        c.inc.assign(nv, {});
        c.isSmall.assign(n, false);
        c.isTiny.assign(n, false);
        std::vector<bool> &isSmall = c.isSmall;
        std::vector<bool> used(n, false);
        DisjointSets connected(nv);
        for (size_t i = 0; i < n; ++i)
            connected.Join(c.Start(i), c.End(i));
        std::vector<double> runLength(nv, 0.0);
        for (size_t i = 0; i < n; ++i)
            runLength[connected.Find(c.Start(i))] += LengthOf(edges[i]);
        for (size_t i = 0; i < n; ++i) {
            c.inc[c.Start(i)].push_back(i);
            c.inc[c.End(i)].push_back(i);
            const double limit = maxLength ? *maxLength : runLength[connected.Find(c.Start(i))] / 20.0;
            std::visit([&](const auto &x) {
                if constexpr (!std::is_same_v<std::decay_t<decltype(x)>, Spline> && !IsCircle<decltype(x)>) {
                    isSmall[i] = ((fromSplineIsSmall && x.fromSpline) || x.Length() < limit) && !HasPartition(x.props);
                    c.isTiny[i] = x.Length() < ignore;
                }
            }, edges[i]);
        }
        auto leaving = [&](size_t i, size_t v) { return Leaving(edges[i], v == c.Start(i)).dir; };
        auto other = [&](size_t i, size_t v) { return v == c.Start(i) ? c.End(i) : c.Start(i); };
        const double mincos = std::cos(Radians(maxAngle));
        auto next = [&](size_t i, size_t v, size_t ref, size_t refv) -> std::optional<size_t> {
            if (c.inc[v].size() != 2)
                return std::nullopt;
            size_t o = c.inc[v][0] == i ? c.inc[v][1] : c.inc[v][0];
            if (!isSmall[o] || used[o] || Owner(o) != Owner(i) ||
                !SameProperties(PropsOf(edges[o]), PropsOf(edges[i])) ||
                (stopAtVertexProps && (Marked(PropsAtVertex(c, i, v)) || Marked(PropsAtVertex(c, o, v)))))
                return std::nullopt;
            if (!c.isTiny[o] && ref != none && Dot(leaving(ref, refv) * -1.0, leaving(o, v)) <= mincos)
                return std::nullopt;
            return o;
        };
        for (size_t i = 0; i < n; ++i) {
            if (!isSmall[i] || used[i])
                continue;
            used[i] = true;
            std::deque<size_t> chain{i};
            std::deque<size_t> vids{c.Start(i), c.End(i)};
            auto walk = [&](bool forward) {
                size_t v = forward ? c.End(i) : c.Start(i);
                size_t e = i, ref = c.isTiny[i] ? none : i, refv = v;
                for (;;) {
                    std::optional<size_t> o = next(e, v, ref, refv);
                    if (!o)
                        break;
                    used[*o] = true;
                    v = other(*o, v);
                    if (forward) {
                        chain.push_back(*o);
                        vids.push_back(v);
                    } else {
                        chain.push_front(*o);
                        vids.push_front(v);
                    }
                    e = *o;
                    if (!c.isTiny[*o]) {
                        ref = *o;
                        refv = v;
                    }
                }
            };
            walk(true);
            walk(false);
            c.chains.push_back({std::vector<size_t>(chain.begin(), chain.end()),
                                std::vector<size_t>(vids.begin(), vids.end())});
        }
        return c;
    }

    inline std::vector<PrimitiveEdge> Wire::Oriented(const Chain &ch, size_t b, size_t e, const Chains &c) const {
        std::vector<PrimitiveEdge> out;
        for (size_t k = b; k < e; ++k)
            out.push_back(std::visit([&](const auto &x) -> PrimitiveEdge {
                if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                    throw std::logic_error("spline in a chain");
                else if constexpr (IsCircle<decltype(x)>)
                    throw std::logic_error("circle in a chain");
                else
                    return c.Start(ch.edges[k]) == ch.vids[k] ? x : x.Reversed();
            }, edges[ch.edges[k]]));
        return out;
    }

    inline void Wire::FitArcs(const ArcFit &fit) {
        static Timer t("planex FitArcs");
        Region reg(t);
        const Chains c = FindChains(fit.max_edge_length, fit.max_edge_angle, fit.ignore_edges_smaller, false, true);
        std::vector<bool> removed(edges.size(), false);
        EdgeList fitted;
        std::vector<uint32_t> fittedOwner;
        struct Piece {
            Edge edge;
            size_t b, e;
        };
        for (const Chain &ch : c.chains) {
            const size_t N = ch.edges.size();
            auto segment = [&](size_t b, size_t e, std::vector<Piece> &out) {
                for (size_t k = b; e - k >= 3;) {
                    std::optional<Edge> best;
                    size_t end = k;
                    for (size_t j = k + 3; j <= e; ++j) {
                        const std::vector<PrimitiveEdge> chain = Oriented(ch, k, j, c);
                        std::optional<Edge> a = FitArc(chain, fit.tol, fit.ignore_edges_smaller);
                        // a chain whose chord is within tol is too short to fit yet, a longer one may fit
                        if (!a && !best && Norm(chain.back().End() - chain.front().Start()) <= fit.tol)
                            continue;
                        if (!a)
                            break;
                        best = std::move(a);
                        end = j;
                    }
                    if (!best) {
                        ++k;
                        continue;
                    }
                    out.push_back({std::move(*best), k, end});
                    k = end;
                }
            };
            auto coverage = [](const std::vector<Piece> &pieces) {
                size_t n = 0;
                for (const Piece &p : pieces)
                    n += p.e - p.b;
                return n;
            };
            std::vector<Piece> pieces;
            if (ch.vids.front() != ch.vids.back()) {
                segment(0, N, pieces);
            } else {
                std::vector<double> cum{0.0};
                for (size_t k = 0; k < N; ++k)
                    cum.push_back(cum.back() + LengthOf(edges[ch.edges[k]]));
                auto nearest = [&](double s) {
                    size_t k = (size_t)(std::lower_bound(cum.begin(), cum.end(), s) - cum.begin());
                    if (k == cum.size() || (k > 0 && s - cum[k - 1] <= cum[k] - s))
                        --k;
                    return std::clamp<size_t>(k, 1, N - 1);
                };
                auto parts = [&](size_t n, std::vector<Piece> &out) {
                    for (size_t i = 1, b = 0; i <= n; ++i) {
                        size_t e = i == n ? N : nearest(cum.back() * (double)i / (double)n);
                        segment(b, e, out);
                        b = e;
                    }
                };
                // a ring rarely splits into two arcs of at most 180 degrees at a vertex: thirds if halves leave edges
                parts(2, pieces);
                if (coverage(pieces) < N) {
                    std::vector<Piece> thirds;
                    parts(3, thirds);
                    if (coverage(thirds) > coverage(pieces))
                        pieces.swap(thirds);
                }
            }
            for (Piece &p : pieces) {
                SetEnds(p.edge, ChainEnds(c, ch, p.b, p.e));
                fitted.push_back(std::move(p.edge));
                fittedOwner.push_back(Owner(ch.edges[p.b]));
                for (size_t m = p.b; m < p.e; ++m)
                    removed[ch.edges[m]] = true;
            }
        }
        Erase(removed);
        for (size_t k = 0; k < fitted.size(); ++k)
            Append(std::move(fitted[k]), fittedOwner[k]);
    }

    inline void Wire::FitSplines(const SplineFit &fit) {
        static Timer t("planex FitSplines");
        Region reg(t);
        if (!config::state->spline_fitter)
            throw std::runtime_error("fit_splines: fitting splines needs planex's OCC integration (netgen and OCC), which is not "
                                     "loaded; pip install planex2d[occ] (planex2d-occ, with netgen)");
        // a spline over vertices with properties is split at its points nearest them, keeping the curve smooth; the
        // vertices move onto it (by at most tol)
        const Chains c = FindChains(fit.max_edge_length, fit.max_edge_angle, fit.ignore_edges_smaller, true, false);
        const std::vector<bool> &isSmall = c.isSmall, &isTiny = c.isTiny;
        const std::vector<std::vector<size_t>> &inc = c.inc;
        auto leaving = [&](size_t i, size_t v) { return Leaving(edges[i], v == c.Start(i)).dir; };
        const double mincos = std::cos(Radians(fit.max_edge_angle));
        auto coarse = [&](const std::vector<PrimitiveEdge> &pieces, size_t n) {
            return std::ranges::any_of(pieces, [&](const PrimitiveEdge &x) {
                const Arc *a = std::get_if<Arc>(&x);
                return a && a->Radius() * (1.0 - std::cos(a->Sweep() / (2.0 * (double)n))) > fit.tol;
            });
        };
        std::vector<bool> removed(edges.size(), false);
        std::vector<Spline> fitted;
        std::vector<uint32_t> fittedOwner;
        for (const Chain &ch : c.chains) {
            const std::vector<size_t> &chain = ch.edges, &vids = ch.vids;
            const size_t N = chain.size();
            auto direction = [&](size_t k) { return leaving(chain[k], vids[k]); };
            auto real = [&](size_t k, bool forward) -> std::optional<size_t> {
                for (size_t steps = 0; steps < N; ++steps, k = forward ? (k + 1) % N : (k + N - 1) % N)
                    if (!isTiny[chain[k]])
                        return k;
                return std::nullopt;
            };
            auto bisector = [&](size_t k) -> std::optional<Vertex> {
                std::optional<size_t> before = real((k + N - 1) % N, false), after = real(k % N, true);
                if (!before || !after)
                    return std::nullopt;
                return direction(*before) + direction(*after);
            };
            auto continuation = [&](size_t v, size_t self, const Vertex &d, bool atStart) -> std::optional<Vertex> {
                double best = mincos;
                std::optional<Vertex> t;
                for (size_t o : inc[v]) {
                    if (o == self)
                        continue;
                    Vertex l = leaving(o, v);
                    double c = Dot(l, atStart ? d * -1.0 : d);
                    if (c > best) {
                        best = c;
                        // a small neighbour is a chord of the same curve: the curve tangent bisects the two chords
                        if (isSmall[o])
                            t = atStart ? d - l : d + l;
                        else
                            t = atStart ? l * -1.0 : l;
                    }
                }
                return t;
            };
            auto run = [&](size_t b, size_t e, bool closed) {
                if (e - b < 2)
                    return;
                std::vector<PrimitiveEdge> oriented = Oriented(ch, b, e, c);
                std::vector<Vertex> pts = FitPoints(oriented, fit.ignore_edges_smaller);
                if (pts.size() < 3)
                    return;
                std::optional<Vertex> t0, t1;
                if (closed) {
                    t0 = bisector(b);
                    t1 = bisector(e);
                } else {
                    if (std::optional<size_t> k = real(b, true); k && *k < e)
                        t0 = continuation(vids[b], chain[b], direction(*k), true);
                    if (std::optional<size_t> k = real(e - 1, false); k && *k >= b)
                        t1 = continuation(vids[e], chain[e - 1], direction(*k), false);
                }
                std::optional<Spline> s = Spline::Fitted(oriented, pts, fit.tol, t0, t1);
                // a curve through one midpoint per arc cannot follow a whole arc: retry with more points on the
                // arcs until their chords are within tol
                for (size_t n = 4; !s && n <= 64 && coarse(oriented, n / 2); n *= 2) {
                    std::vector<Vertex> dense = FitPoints(oriented, fit.ignore_edges_smaller, n);
                    if (dense.size() == pts.size())
                        break;
                    pts = std::move(dense);
                    s = Spline::Fitted(oriented, pts, fit.tol, t0, t1);
                }
                if (!s)
                    return;
                s->ends = ChainEnds(c, ch, b, e);
                SplitPoints marked;
                std::vector<Properties> markedProps;
                for (size_t k = b + 1; k < e; ++k)
                    if (Properties p = MergedProperties(PropsAtVertex(c, chain[k - 1], vids[k]), PropsAtVertex(c, chain[k], vids[k]))) {
                        marked.Add((double)k, s->ExactNearestPoint(oriented[k - b].Start()));
                        markedProps.push_back(std::move(p));
                    }
                std::vector<Spline> pieces = marked.IsEmpty() ? std::vector<Spline>{std::move(*s)} : s->Split(marked, s->ApproximationTol());
                for (size_t j = 0; j < markedProps.size() && j + 1 < pieces.size(); ++j) {
                    pieces[j].ends = MakeEnds(PropsAtEnd(pieces[j].ends, false), markedProps[j]);
                    pieces[j + 1].ends = MakeEnds(markedProps[j], PropsAtEnd(pieces[j + 1].ends, true));
                }
                for (Spline &piece : pieces) {
                    fitted.push_back(std::move(piece));
                    fittedOwner.push_back(Owner(chain[b]));
                }
                for (size_t k = b; k < e; ++k)
                    removed[chain[k]] = true;
            };
            if (vids.front() == vids.back()) {
                run(0, N / 2, true);
                run(N / 2, N, true);
            } else {
                run(0, N, false);
            }
        }
        Erase(removed);
        for (size_t k = 0; k < fitted.size(); ++k)
            Append(std::move(fitted[k]), fittedOwner[k]);
    }

}
