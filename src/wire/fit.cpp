// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/fit.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace planex2d {

    Wire::Chains Wire::ChainCandidates(const ChainRule &rule) const {
        Chains c;
        const size_t n = edges.size(), nv = VertexIds(c.id);
        c.inc.assign(nv, {});
        c.isSmall.assign(n, false);
        c.isTiny.assign(n, false);
        DisjointSets connected(nv);
        for (size_t i = 0; i < n; ++i)
            connected.Join(c.Start(i), c.End(i));
        std::vector<double> runLength(nv, 0.0);
        for (size_t i = 0; i < n; ++i)
            runLength[connected.Find(c.Start(i))] += LengthOf(edges[i]);
        for (size_t i = 0; i < n; ++i) {
            c.inc[c.Start(i)].push_back(i);
            c.inc[c.End(i)].push_back(i);
            // none: shorter than 1/20 of the length of the loop or open wire the edge lies in, so a tessellation is fitted
            // at any scale and a polygon of few sides stays
            const double limit = rule.maxLength ? *rule.maxLength : runLength[connected.Find(c.Start(i))] / 20.0;
            std::visit([&](const auto &x) {
                using E = std::decay_t<decltype(x)>;
                if constexpr (!std::is_same_v<E, Spline> && !IsCircle<E>) {
                    // without arcsJoin only lines chain, so a fit run again leaves the arcs it made
                    c.isSmall[i] = (rule.arcsJoin || std::is_same_v<E, Line>) &&
                                   ((rule.fromSplineIsSmall && x.fromSpline) || x.Length() < limit) && !HasPartition(x.props);
                    c.isTiny[i] = x.Length() < rule.ignore;
                }
            }, edges[i]);
        }
        return c;
    }

    Wire::Chains Wire::FindChains(const ChainRule &rule) const {
        Chains c = ChainCandidates(rule);
        const size_t n = edges.size();
        const std::vector<bool> &isSmall = c.isSmall;
        std::vector<bool> used(n, false);
        auto leaving = [&](size_t i, size_t v) { return Leaving(edges[i], v == c.Start(i)).dir; };
        auto other = [&](size_t i, size_t v) { return v == c.Start(i) ? c.End(i) : c.Start(i); };
        const double mincos = std::cos(Radians(rule.maxAngle));
        auto next = [&](size_t i, size_t v, size_t ref, size_t refv) -> std::optional<size_t> {
            if (c.inc[v].size() != 2)
                return std::nullopt;
            size_t o = c.inc[v][0] == i ? c.inc[v][1] : c.inc[v][0];
            if (!isSmall[o] || used[o] || Owner(o) != Owner(i) ||
                !SameProperties(PropsOf(edges[o]), PropsOf(edges[i])) ||
                (rule.stopAtVertexProps && (Marked(PropsAtVertex(c, i, v)) || Marked(PropsAtVertex(c, o, v)))))
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

    std::vector<PrimitiveEdge> Wire::Oriented(const Chain &ch, size_t b, size_t e, const Chains &c) const {
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

    void Wire::FitArcsBetween(const Chains &c, const Chain &ch, size_t b, size_t e, double tol, double ignore, std::vector<ChainPiece> &out) const {
        for (size_t k = b; e - k >= 3;) {
            std::optional<Edge> best;
            size_t end = k;
            for (size_t j = k + 3; j <= e; ++j) {
                const std::vector<PrimitiveEdge> chain = Oriented(ch, k, j, c);
                std::optional<Edge> a = FitArc(chain, tol, ignore);
                // a chain whose chord is within tol is too short to fit yet, a longer one may fit
                if (!a && !best && Norm(chain.back().End() - chain.front().Start()) <= tol)
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
    }

    // a ring rarely splits into two arcs of at most 180 degrees at a vertex: thirds if halves leave edges
    std::vector<Wire::ChainPiece> Wire::FitRingArcs(const Chains &c, const Chain &ch, double tol, double ignore) const {
        const size_t N = ch.edges.size();
        std::vector<double> cum{0.0};
        for (size_t k = 0; k < N; ++k)
            cum.push_back(cum.back() + LengthOf(edges[ch.edges[k]]));
        auto nearest = [&](double s) {
            size_t k = (size_t)(std::lower_bound(cum.begin(), cum.end(), s) - cum.begin());
            if (k == cum.size() || (k > 0 && s - cum[k - 1] <= cum[k] - s))
                --k;
            return std::clamp<size_t>(k, 1, N - 1);
        };
        auto parts = [&](size_t n, std::vector<ChainPiece> &out) {
            for (size_t i = 1, b = 0; i <= n; ++i) {
                size_t e = i == n ? N : nearest(cum.back() * (double)i / (double)n);
                FitArcsBetween(c, ch, b, e, tol, ignore, out);
                b = e;
            }
        };
        auto coverage = [](const std::vector<ChainPiece> &pieces) {
            size_t n = 0;
            for (const ChainPiece &p : pieces)
                n += p.e - p.b;
            return n;
        };
        std::vector<ChainPiece> pieces;
        parts(2, pieces);
        if (coverage(pieces) < N) {
            std::vector<ChainPiece> thirds;
            parts(3, thirds);
            if (coverage(thirds) > coverage(pieces))
                pieces.swap(thirds);
        }
        return pieces;
    }

    void Wire::FitArcs(const ArcFit &fit) {
        static Timer t("planex FitArcs");
        Region reg(t);
        const BBox box = Box();
        const double tol = FitLength(fit.tol, box), ignore = FitLength(fit.ignore_edges_smaller, box);
        const Chains c = FindChains({.maxLength = fit.max_edge_length.value_or(std::numeric_limits<double>::infinity()),
                                     .maxAngle = fit.max_edge_angle, .ignore = ignore, .stopAtVertexProps = true});
        std::vector<bool> removed(edges.size(), false);
        EdgeList fitted;
        std::vector<uint32_t> fittedOwner;
        for (const Chain &ch : c.chains) {
            std::vector<ChainPiece> pieces;
            if (ch.vids.front() != ch.vids.back())
                FitArcsBetween(c, ch, 0, ch.edges.size(), tol, ignore, pieces);
            else
                pieces = FitRingArcs(c, ch, tol, ignore);
            for (ChainPiece &p : pieces) {
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

    // a curve through one midpoint per arc cannot follow a whole arc: more points on the arcs until their chords are
    // within tol
    static std::optional<Spline> FittedThrough(const std::vector<PrimitiveEdge> &oriented, double tol, double ignore,
                                               const std::optional<Vertex> &t0, const std::optional<Vertex> &t1) {
        std::vector<Vertex> pts = FitPoints(oriented, ignore);
        if (pts.size() < 3)
            return std::nullopt;
        const auto coarse = [&](size_t n) {
            return std::ranges::any_of(oriented, [&](const PrimitiveEdge &x) {
                const Arc *a = std::get_if<Arc>(&x);
                return a && a->Radius() * (1.0 - std::cos(a->Sweep() / (2.0 * (double)n))) > tol;
            });
        };
        std::optional<Spline> s = Spline::Fitted(oriented, pts, tol, t0, t1);
        for (size_t n = 4; !s && n <= 64 && coarse(n / 2); n *= 2) {
            std::vector<Vertex> dense = FitPoints(oriented, ignore, n);
            if (dense.size() == pts.size())
                break;
            pts = std::move(dense);
            s = Spline::Fitted(oriented, pts, tol, t0, t1);
        }
        return s;
    }

    // the vertices with properties move onto the curve (by at most tol)
    std::vector<Spline> Wire::SplitAtMarkedVertices(const Chains &c, const Chain &ch, size_t b, size_t e, Spline s,
                                                    const std::vector<PrimitiveEdge> &oriented) const {
        SplitPoints marked;
        std::vector<Properties> markedProps;
        for (size_t k = b + 1; k < e; ++k)
            if (Properties p = MergedProperties(PropsAtVertex(c, ch.edges[k - 1], ch.vids[k]), PropsAtVertex(c, ch.edges[k], ch.vids[k]))) {
                marked.Add((double)k, s.ExactNearestPoint(oriented[k - b].Start()));
                markedProps.push_back(std::move(p));
            }
        std::vector<Spline> pieces = marked.IsEmpty() ? std::vector<Spline>{std::move(s)} : s.Split(marked, s.ApproximationTol());
        for (size_t j = 0; j < markedProps.size() && j + 1 < pieces.size(); ++j) {
            pieces[j].ends = MakeEnds(PropsAtEnd(pieces[j].ends, false), markedProps[j]);
            pieces[j + 1].ends = MakeEnds(markedProps[j], PropsAtEnd(pieces[j + 1].ends, true));
        }
        return pieces;
    }

    void Wire::FitSplines(const SplineFit &fit) {
        static Timer t("planex FitSplines");
        Region reg(t);
        if (!config::state->spline_fitter)
            throw std::runtime_error("fit_splines: fitting splines needs planex's OCC integration (netgen and OCC), which is not "
                                     "loaded; pip install planex2d[occ] (planex2d-occ, with netgen)");
        const BBox box = Box();
        const double tol = FitLength(fit.tol, box), ignore = FitLength(fit.ignore_edges_smaller, box);
        const Chains c = FindChains({.maxLength = fit.max_edge_length, .maxAngle = fit.max_edge_angle, .ignore = ignore,
                                     .fromSplineIsSmall = true, .arcsJoin = true});
        const std::vector<bool> &isSmall = c.isSmall, &isTiny = c.isTiny;
        const std::vector<std::vector<size_t>> &inc = c.inc;
        auto leaving = [&](size_t i, size_t v) { return Leaving(edges[i], v == c.Start(i)).dir; };
        const double mincos = std::cos(Radians(fit.max_edge_angle));
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
                const std::vector<PrimitiveEdge> oriented = Oriented(ch, b, e, c);
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
                std::optional<Spline> s = FittedThrough(oriented, tol, ignore, t0, t1);
                if (!s)
                    return;
                s->ends = ChainEnds(c, ch, b, e);
                for (Spline &piece : SplitAtMarkedVertices(c, ch, b, e, std::move(*s), oriented)) {
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

    Properties Wire::PropsAtVertex(const Chains &c, size_t i, size_t v) const {
        const Ends &x = EndsOf(edges[i]);
        if (!x)
            return nullptr;
        return c.Start(i) == v && x->start ? x->start : c.End(i) == v ? x->end : nullptr;
    }

    Ends Wire::ChainEnds(const Chains &c, const Chain &ch, size_t b, size_t e) const {
        return MakeEnds(PropsAtVertex(c, ch.edges[b], ch.vids[b]), PropsAtVertex(c, ch.edges[e - 1], ch.vids[e]));
    }

}
