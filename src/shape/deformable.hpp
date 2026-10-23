// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "boolean/boolean.hpp"

namespace planex2d {

    // a face as dense closed polylines whose vertices follow a deformation field. Outer loops run counterclockwise
    // and holes clockwise, so the material is on the left of every segment and the winding number tells inside.
    // Boundary parts closer than the minimum distance to another part are cut out and the loose ends reconnected.
    class DeformableShape {
        public:
            struct Ring {
                std::vector<Vertex> ref, cur;   // the first point is not repeated at the end
                std::vector<size_t> starts;     // first point of every original edge
                std::vector<Properties> props;  // of every original edge
            };

        private:
            std::vector<Ring> rings;
            AxisSystem axes;
            double h, minDistance;

            // the points of the piece after its start, at most h apart
            static void Sample(const PrimitiveEdge &piece, double h, std::vector<Vertex> &out) {
                std::visit([&](const auto &x) {
                    size_t n = std::max<size_t>(1, (size_t)(std::ceil(x.Length() / h)));
                    for (size_t k = 1; k < n; ++k) {
                        double s = (double)k / (double)n;
                        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Line>)
                            out.push_back(x.Start() + (x.End() - x.Start()) * s);
                        else
                            out.push_back(x.PointAt(s * x.Sweep()));
                    }
                    out.push_back(x.End());
                }, piece);
            }
            static void Sample(const Edge &e, bool forward, double h, std::vector<Vertex> &out) {
                std::visit([&](const auto &x) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>) {
                        std::span<const PrimitiveEdge> pieces = x.Approximation();
                        if (forward)
                            for (const PrimitiveEdge &p : pieces)
                                Sample(p, h, out);
                        else
                            for (size_t k = pieces.size(); k-- > 0;)
                                Sample(std::visit([](const auto &y) -> PrimitiveEdge { return y.Reversed(); }, pieces[k]), h, out);
                    } else if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Circle>) {
                        const std::array<Arc, 2> halves = x.Halves();
                        for (size_t k = 0; k < 2; ++k) {
                            const Arc &half = halves[forward ? k : 1 - k];
                            Sample(forward ? PrimitiveEdge(half) : PrimitiveEdge(half.Reversed()), h, out);
                        }
                    } else {
                        Sample(forward ? PrimitiveEdge(x) : PrimitiveEdge(x.Reversed()), h, out);
                    }
                }, e);
            }

            static double SignedArea(const std::vector<Vertex> &pts) {
                double a = 0.0;
                for (size_t i = 0; i < pts.size(); ++i)
                    a += Cross(pts[i], pts[(i + 1) % pts.size()]);
                return 0.5 * a;
            }

            // the first point stays, the others follow in reverse; the pieces come in reverse order
            static void Reverse(Ring &r) {
                size_t n = r.cur.size();
                std::reverse(r.cur.begin() + 1, r.cur.end());
                std::reverse(r.ref.begin() + 1, r.ref.end());
                std::reverse(r.props.begin(), r.props.end());
                std::vector<size_t> starts{0};
                for (size_t k = r.starts.size(); k-- > 1;)
                    starts.push_back(n - r.starts[k]);
                r.starts = std::move(starts);
            }

            // the current polyline as lines; owners number the original edges over all rings, props collects theirs
            Wire Lines(std::vector<Properties> *props) const {
                Wire w(axes);
                std::vector<uint32_t> owners;
                for (const Ring &r : rings) {
                    size_t n = r.cur.size();
                    for (size_t k = 0; k < r.starts.size(); ++k) {
                        size_t end = k + 1 < r.starts.size() ? r.starts[k + 1] : n;
                        for (size_t i = r.starts[k]; i < end; ++i) {
                            Line l(r.cur[i], r.cur[(i + 1) % n]);
                            l.props = r.props[k];
                            w.Add(l);
                            if (props)
                                owners.push_back((uint32_t)(props->size()));
                        }
                        if (props)
                            props->push_back(r.props[k]);
                    }
                }
                if (props)
                    w.SetOwners(std::move(owners));
                return w;
            }

            // signed crossing of the ray from p: +1 upward, -1 downward (vertical ray: the frame is turned so the
            // ray points to +x, up is then -x)
            template <bool vertical>
            static int Crossing(const Line &l, const Vertex &p) {
                auto along = [](const Vertex &v) { return vertical ? v.y : v.x; };
                auto across = [](const Vertex &v) { return vertical ? -v.x : v.y; };
                const Vertex &a = l.Start(), &b = l.End();
                bool aAbove = across(a) > across(p), bAbove = across(b) > across(p);
                if (aAbove == bAbove)
                    return 0;
                double t = (across(p) - across(a)) / (across(b) - across(a));
                if (along(a) + t * (along(b) - along(a)) <= along(p))
                    return 0;
                return bAbove ? 1 : -1;
            }
            template <bool vertical>
            static int Winding(const Wire &w, Grid &grid, const Vertex &p, size_t skip) {
                int total = 0;
                grid.ForRay<vertical>(p, [&](uint32_t k) {
                    if (k != skip)
                        total += Crossing<vertical>(std::get<Line>(w[k]), p);
                });
                return total;
            }
            // material (winding >= 1) exactly on the left of piece k: the ray from its midpoint, perpendicular-ish
            // to it, counts the other pieces; the piece itself adds one on its left
            static bool Keep(const Wire &w, Grid &grid, size_t k) {
                const Line &l = std::get<Line>(w[k]);
                Vertex d = l.End() - l.Start();
                Vertex m = (l.Start() + l.End()) * 0.5;
                if (std::abs(d.y) >= std::abs(d.x))
                    return Winding<false>(w, grid, m, k) == (d.y > 0.0 ? 0 : 1);
                return Winding<true>(w, grid, m, k) == (d.x < 0.0 ? 0 : 1);
            }

            // oriented loops of the kept lines: at a vertex the walk continues with the outgoing line closest
            // clockwise to the reversed incoming direction, which keeps the material on the left and loops simple
            static std::vector<Ring> Trace(const Wire &w, const std::vector<bool> &keep, const std::vector<Properties> &props) {
                std::map<std::pair<double, double>, size_t> ids;
                auto id = [&](const Vertex &v) { return ids.try_emplace({v.x, v.y}, ids.size()).first->second; };
                std::vector<size_t> from(w.Size()), to(w.Size());
                for (size_t k = 0; k < w.Size(); ++k) {
                    from[k] = id(std::get<Line>(w[k]).Start());
                    to[k] = id(std::get<Line>(w[k]).End());
                }
                // a crossing has more than two pieces around it and becomes a corner even within one original edge
                std::vector<size_t> degree(ids.size(), 0);
                for (size_t k = 0; k < w.Size(); ++k) {
                    ++degree[from[k]];
                    ++degree[to[k]];
                }
                struct Out {
                    double angle;
                    size_t edge;
                };
                std::vector<std::vector<Out>> out(ids.size());
                for (size_t k = 0; k < w.Size(); ++k)
                    if (keep[k]) {
                        Vertex d = std::get<Line>(w[k]).End() - std::get<Line>(w[k]).Start();
                        out[from[k]].push_back({std::atan2(d.y, d.x), k});
                    }
                std::vector<bool> used(w.Size(), false);
                std::vector<Ring> rings;
                for (size_t start = 0; start < w.Size(); ++start) {
                    if (!keep[start] || used[start])
                        continue;
                    std::vector<size_t> trail;
                    size_t e = start;
                    while (true) {
                        used[e] = true;
                        trail.push_back(e);
                        if (to[e] == from[start])
                            break;
                        const Line &l = std::get<Line>(w[e]);
                        double back = std::atan2(l.Start().y - l.End().y, l.Start().x - l.End().x);
                        const Out *next = nullptr;
                        double best = 0.0;
                        for (const Out &o : out[to[e]]) {
                            if (used[o.edge])
                                continue;
                            double turn = std::fmod(back - o.angle + 4.0 * std::numbers::pi, 2.0 * std::numbers::pi);
                            if (!next || turn < best) {
                                next = &o;
                                best = turn;
                            }
                        }
                        if (!next)
                            break;
                        e = next->edge;
                    }
                    if (to[trail.back()] != from[start]) {
                        Warning() << "open boundary trail of " << trail.size() << " pieces dropped";
                        continue;
                    }
                    if (trail.size() < 3)
                        continue;
                    auto corner = [&](size_t i) {
                        size_t prev = trail[(i + trail.size() - 1) % trail.size()];
                        return w.Owner(trail[i]) != w.Owner(prev) || degree[from[trail[i]]] > 2;
                    };
                    // start at a corner, if there is one
                    size_t rot = 0;
                    for (size_t i = 0; i < trail.size(); ++i)
                        if (corner(i)) {
                            rot = i;
                            break;
                        }
                    std::rotate(trail.begin(), trail.begin() + (long)rot, trail.end());
                    Ring r;
                    for (size_t i = 0; i < trail.size(); ++i) {
                        r.cur.push_back(std::get<Line>(w[trail[i]]).Start());
                        if (i == 0 || corner(i)) {
                            r.starts.push_back(i);
                            r.props.push_back(props[w.Owner(trail[i])]);
                        }
                    }
                    r.ref = r.cur;
                    rings.push_back(std::move(r));
                }
                return rings;
            }

            // outer loops counterclockwise, holes clockwise: a loop inside an even number of others is an outer one
            void Orient() {
                Wire w = Lines(nullptr);
                Grid grid = w.EdgeGrid();
                std::vector<size_t> ring;
                for (size_t r = 0; r < rings.size(); ++r)
                    ring.insert(ring.end(), rings[r].cur.size(), r);
                size_t first = 0;
                for (size_t r = 0; r < rings.size(); ++r) {
                    const Line &l = std::get<Line>(w[first]);
                    Vertex p = (l.Start() + l.End()) * 0.5;
                    std::vector<bool> odd(rings.size(), false);
                    grid.ForRay<false>(p, [&](uint32_t k) {
                        if (ring[k] != r && RayParity<false, false>(std::get<Line>(w[k]), p))
                            odd[ring[k]] = !odd[ring[k]];
                    });
                    size_t depth = (size_t)(std::count(odd.begin(), odd.end(), true));
                    if ((SignedArea(rings[r].cur) > 0.0) != (depth % 2 == 0))
                        Reverse(rings[r]);
                    first += rings[r].cur.size();
                }
            }

            // cuts the polylines against each other and keeps the boundary of the material; loops wound the wrong
            // way go even without a cut. true if anything changed
            bool Resolve() {
                std::vector<Properties> props;
                Wire w = Lines(&props);
                size_t before = w.Size();
                std::vector<CutState> state(before);
                Grid grid = w.EdgeGrid();
                CutAll(w, grid, state, false);
                std::vector<bool> removed(w.Size());
                for (size_t k = 0; k < w.Size(); ++k)
                    removed[k] = state[k].excluded;
                bool cut = w.Size() != before || std::find(removed.begin(), removed.end(), true) != removed.end();
                if (!cut) {
                    std::vector<Ring> kept;
                    size_t first = 0;
                    for (Ring &r : rings) {
                        if (Keep(w, grid, first))
                            kept.push_back(std::move(r));
                        first += r.cur.size();
                    }
                    bool changed = kept.size() != rings.size();
                    rings = std::move(kept);
                    return changed;
                }
                w.Erase(removed);
                Erase(state, removed);
                SnapEndpoints(w, w.Tol(), [](size_t) { return true; });
                w.Erase(EpsEdges(w));
                grid = w.EdgeGrid();
                std::vector<bool> keep(w.Size());
                for (size_t k = 0; k < w.Size(); ++k)
                    keep[k] = Keep(w, grid, k);
                rings = Trace(w, keep, props);
                Refine();
                return true;
            }

            struct Marks {
                Wire w;                            // vertex k starts line k
                std::vector<Properties> props;     // of the original edges, w's owners
                std::vector<size_t> ring, offset;  // the ring of every vertex, the rings' vertex ranges
                std::vector<bool> marked;          // closer than the minimum distance to another part of the boundary
                size_t Next(size_t v) const { return v + 1 < offset[ring[v] + 1] ? v + 1 : offset[ring[v]]; }
                size_t Prev(size_t v) const { return v > offset[ring[v]] ? v - 1 : offset[ring[v] + 1] - 1; }
                const Vertex &At(size_t v) const { return std::get<Line>(w[v]).Start(); }
            };

            // vertex to vertex, which is symmetric: both sides of a thin part are marked, or none. A pair of the own
            // ring counts only if its distance is below half its arc distance, which excludes straight stretches and
            // corners of 60 degrees or more at any scale. If anything moved since the reference, only pairs that
            // came closer count, so a thin part of the given shape stays until it gets thinner
            Marks Mark() const {
                std::vector<Vertex> cur = Vertices(), ref = ReferenceVertices();
                bool moved = false;
                for (size_t k = 0; k < cur.size() && !moved; ++k)
                    moved = cur[k].x != ref[k].x || cur[k].y != ref[k].y;
                Marks a;
                a.w = Lines(&a.props);
                a.offset.push_back(0);
                std::vector<double> arc, total;
                for (size_t r = 0; r < rings.size(); ++r) {
                    size_t n = rings[r].cur.size();
                    double s = 0.0;
                    for (size_t i = 0; i < n; ++i) {
                        a.ring.push_back(r);
                        arc.push_back(s);
                        s += std::get<Line>(a.w[a.offset[r] + i]).Length();
                    }
                    a.offset.push_back(a.offset[r] + n);
                    total.push_back(s);
                }
                const double m = minDistance;
                Grid grid = Grid::Of(Vertices(), m);
                a.marked.assign(a.w.Size(), false);
                std::vector<uint32_t> near;
                for (size_t k = 0; k < a.w.Size(); ++k) {
                    near.clear();
                    grid.InBox(BBox::Of(a.At(k), a.At(k)).Grown(m), near);
                    for (uint32_t j : near) {
                        double d = Norm(cur[j] - cur[k]);
                        if (d >= m || (moved && d >= Norm(ref[j] - ref[k])))
                            continue;
                        if (a.ring[j] == a.ring[k]) {
                            double s = std::abs(arc[j] - arc[k]);
                            if (d >= 0.5 * std::min(s, total[a.ring[k]] - s))
                                continue;
                        }
                        a.marked[k] = true;
                        break;
                    }
                }
                return a;
            }

            // segments with a marked end, the chains and the segment before and after each, get points h/4 apart
            void Resample(const Marks &a) {
                const double fine = 0.25 * h;
                for (size_t r = 0; r < rings.size(); ++r) {
                    auto b = a.marked.begin() + (long)a.offset[r], e = a.marked.begin() + (long)a.offset[r + 1];
                    if (std::find(b, e, true) == e)
                        continue;
                    Ring &o = rings[r];
                    Ring s;
                    s.props = o.props;
                    size_t n = o.cur.size(), k = 0;
                    for (size_t i = 0; i < n; ++i) {
                        if (k < o.starts.size() && o.starts[k] == i) {
                            s.starts.push_back(s.cur.size());
                            ++k;
                        }
                        s.cur.push_back(o.cur[i]);
                        s.ref.push_back(o.ref[i]);
                        size_t v = a.offset[r] + i, j = (i + 1) % n;
                        if (!a.marked[v] && !a.marked[a.Next(v)])
                            continue;
                        size_t parts = (size_t)(std::ceil(Norm(o.cur[j] - o.cur[i]) / fine));
                        for (size_t p = 1; p < parts; ++p) {
                            double t = (double)p / (double)parts;
                            s.cur.push_back(o.cur[i] + (o.cur[j] - o.cur[i]) * t);
                            s.ref.push_back(o.ref[i] + (o.ref[j] - o.ref[i]) * t);
                        }
                    }
                    o = std::move(s);
                }
            }

        public:
            DeformableShape(const Wire &w, double h, double minDistance, bool clean = false)
                : axes(w.Axes()), h(h), minDistance(minDistance) {
                if (!(h > 0.0) || !(minDistance > 0.0))
                    throw std::invalid_argument("h and the minimum distance must be positive");
                for (const Loop &l : w.Loops()) {
                    if (!l.closed)
                        continue;
                    Ring r;
                    for (auto [i, forward] : l.edges) {
                        if (r.cur.empty())
                            r.cur.push_back(forward ? StartOf(w[i]) : EndOf(w[i]));
                        r.starts.push_back(r.cur.size() - 1);
                        r.props.push_back(PropsOf(w[i]));
                        Sample(w[i], forward, h, r.cur);
                    }
                    r.cur.pop_back();
                    r.ref = r.cur;
                    rings.push_back(std::move(r));
                }
                Orient();
                if (clean)
                    Clean();
            }

            double H() const { return h; }
            double MinDistance() const { return minDistance; }
            const AxisSystem &Axes() const { return axes; }
            const std::vector<Ring> &Rings() const { return rings; }
            size_t Size() const {
                size_t n = 0;
                for (const Ring &r : rings)
                    n += r.cur.size();
                return n;
            }
            std::vector<size_t> RingSizes() const {
                std::vector<size_t> n;
                for (const Ring &r : rings)
                    n.push_back(r.cur.size());
                return n;
            }
            // the name of the original edge every vertex lies on: a corner counts to the edge it starts, unless that
            // edge has no name (a connection) and the edge it ends has one
            std::vector<std::string> VertexNames() const {
                std::vector<std::string> names;
                for (const Ring &r : rings) {
                    auto name = [&](size_t k) {
                        const Properties &p = r.props[k];
                        return p && p->name ? *p->name : std::string();
                    };
                    for (size_t k = 0; k < r.starts.size(); ++k) {
                        size_t end = k + 1 < r.starts.size() ? r.starts[k + 1] : r.cur.size();
                        std::string own = name(k);
                        names.push_back(own.empty() ? name((k + r.starts.size() - 1) % r.starts.size()) : own);
                        names.insert(names.end(), end - r.starts[k] - 1, own);
                    }
                }
                return names;
            }
            std::vector<Vertex> Vertices() const {
                std::vector<Vertex> v;
                for (const Ring &r : rings)
                    v.insert(v.end(), r.cur.begin(), r.cur.end());
                return v;
            }
            std::vector<Vertex> ReferenceVertices() const {
                std::vector<Vertex> v;
                for (const Ring &r : rings)
                    v.insert(v.end(), r.ref.begin(), r.ref.end());
                return v;
            }
            double Area() const {
                double a = 0.0;
                for (const Ring &r : rings)
                    a += SignedArea(r.cur);
                return a;
            }
            BBox Box() const {
                BBox b;
                for (const Ring &r : rings)
                    for (const Vertex &v : r.cur)
                        b.Grow(v);
                return b;
            }

            // current segments longer than h are halved, in both polylines; interior vertices closer than h/4 to
            // the previous kept vertex go
            void Refine() {
                // a millionth of slack: segments of exactly h survive the noise of a deformation field, which would
                // otherwise split them and never merge them back
                const double limit = h * (1.0 + 1e-6);
                for (Ring &r : rings) {
                    for (bool again = true; again;) {
                        again = false;
                        Ring s;
                        s.props = r.props;
                        size_t n = r.cur.size(), k = 0;
                        for (size_t i = 0; i < n; ++i) {
                            if (k < r.starts.size() && r.starts[k] == i) {
                                s.starts.push_back(s.cur.size());
                                ++k;
                            }
                            s.cur.push_back(r.cur[i]);
                            s.ref.push_back(r.ref[i]);
                            size_t j = (i + 1) % n;
                            double l = Norm(r.cur[j] - r.cur[i]);
                            if (l > limit) {
                                s.cur.push_back((r.cur[i] + r.cur[j]) * 0.5);
                                s.ref.push_back((r.ref[i] + r.ref[j]) * 0.5);
                                again = again || 0.5 * l > limit;
                            }
                        }
                        r = std::move(s);
                    }
                    Ring s;
                    s.props = r.props;
                    size_t n = r.cur.size(), k = 0;
                    for (size_t i = 0; i < n; ++i) {
                        bool corner = k < r.starts.size() && r.starts[k] == i;
                        if (!corner && Norm(r.cur[i] - s.cur.back()) < 0.25 * h)
                            continue;
                        if (corner) {
                            s.starts.push_back(s.cur.size());
                            ++k;
                        }
                        s.cur.push_back(r.cur[i]);
                        s.ref.push_back(r.ref[i]);
                    }
                    while (s.cur.size() > 3 && s.starts.back() != s.cur.size() - 1 && Norm(s.cur.back() - s.cur.front()) < 0.25 * h) {
                        s.cur.pop_back();
                        s.ref.pop_back();
                    }
                    r = std::move(s);
                }
                // dust: a sliver that collapsed to fewer than three vertices, or a ring smaller than the minimum
                // distance squared
                std::erase_if(rings, [&](const Ring &r) {
                    return r.cur.size() < 3 || std::abs(SignedArea(r.cur)) < minDistance * minDistance;
                });
            }

            void Commit() {
                for (Ring &r : rings)
                    r.ref = r.cur;
            }

            // cur = ref + d, refined and cleaned; true if the topology changed
            bool SetDeformation(const std::vector<Vertex> &d) {
                if (d.size() != Size())
                    throw std::invalid_argument("one displacement per vertex expected");
                size_t k = 0;
                for (Ring &r : rings)
                    for (size_t i = 0; i < r.cur.size(); ++i)
                        r.cur[i] = r.ref[i] + d[k++];
                Refine();
                return Clean();
            }

            bool IsValid() const {
                Wire w = Lines(nullptr);
                size_t before = w.Size();
                std::vector<CutState> state(before);
                Grid grid = w.EdgeGrid();
                CutAll(w, grid, state, false);
                return w.Size() == before && std::none_of(state.begin(), state.end(), [](const CutState &s) { return s.excluded; });
            }

            // the boundary of the material after cutting the loops against each other, without parts closer than
            // the minimum distance to each other; true if anything changed, after which ref equals cur
            bool Clean() {
                bool changed = Resolve();
                if (Defeature()) {
                    changed = true;
                    Refine();
                    Resolve();
                }
                if (changed)
                    Commit();
                return changed;
            }

            // boundary vertices closer than the minimum distance to another part of the boundary go, and the loose
            // ends are joined by straight segments, nearest pairs first: a thin neck splits, a thin slit or horn
            // vanishes, two loops closer than the minimum distance merge. true if anything changed
            bool Defeature() {
                Marks a = Mark();
                if (std::find(a.marked.begin(), a.marked.end(), true) == a.marked.end())
                    return false;
                // the connections join vertices, so the chains and their neighbouring segments are sampled finer
                // and marked again before the chains are taken
                Resample(a);
                a = Mark();
                const Wire &w = a.w;
                const std::vector<Properties> &props = a.props;
                const std::vector<size_t> &offset = a.offset;
                const std::vector<bool> &marked = a.marked;
                auto at = [&](size_t v) -> const Vertex & { return a.At(v); };
                auto next = [&](size_t v) { return a.Next(v); };
                auto prev = [&](size_t v) { return a.Prev(v); };

                // the unmarked runs of the rings with marks, forward along the ring, both ends inclusive
                struct Path {
                    size_t first, last;
                };
                std::vector<Path> paths;
                std::vector<Ring> out;
                for (size_t r = 0; r < rings.size(); ++r) {
                    auto b = marked.begin() + (long)offset[r], e = marked.begin() + (long)offset[r + 1];
                    auto first = std::find(b, e, true);
                    if (first == e) {
                        out.push_back(std::move(rings[r]));
                        continue;
                    }
                    if (std::find(b, e, false) == e)
                        continue;
                    size_t v = (size_t)(first - marked.begin());
                    while (marked[v])
                        v = next(v);
                    for (size_t start = v;;) {
                        Path p{v, v};
                        while (!marked[next(v)])
                            p.last = v = next(v);
                        paths.push_back(p);
                        v = next(v);
                        while (marked[v])
                            v = next(v);
                        if (v == start)
                            break;
                    }
                }

                // end 2p is the first vertex of path p, 2p + 1 its last; every end joins its nearest free end
                auto endAt = [&](size_t e) -> const Vertex & {
                    const Path &p = paths[e / 2];
                    return at(e % 2 ? p.last : p.first);
                };
                size_t ends = 2 * paths.size();
                std::vector<std::tuple<double, size_t, size_t>> pairs;
                for (size_t a = 0; a < ends; ++a)
                    for (size_t b = a + 1; b < ends; ++b)
                        pairs.emplace_back(Norm(endAt(a) - endAt(b)), a, b);
                std::sort(pairs.begin(), pairs.end());
                std::vector<size_t> partner(ends, ends);
                for (const auto &[d, a, b] : pairs)
                    if (partner[a] == ends && partner[b] == ends) {
                        partner[a] = b;
                        partner[b] = a;
                    }

                // the loops: along a path from the entered end to the other, then over to the partner of that end.
                // the connection is a new piece: it keeps the name the two removed segments it joins share, if any
                auto joined = [](const Properties &a, const Properties &b) {
                    return a && b && a->name && a->name == b->name ? a : Properties{};
                };
                std::vector<bool> done(paths.size(), false);
                for (size_t p0 = 0; p0 < paths.size(); ++p0) {
                    if (done[p0])
                        continue;
                    Ring r;
                    for (size_t e = 2 * p0;;) {
                        const Path &p = paths[e / 2];
                        bool forward = e % 2 == 0;
                        done[e / 2] = true;
                        size_t to = partner[e ^ 1];
                        const Path &q = paths[to / 2];
                        const Properties &arriving = props[w.Owner(to % 2 ? q.last : prev(q.first))];
                        size_t piece = props.size();
                        for (size_t v = forward ? p.first : p.last;; v = forward ? next(v) : prev(v)) {
                            size_t leaving = w.Owner(forward ? v : prev(v));
                            bool last = v == (forward ? p.last : p.first);
                            if (last || leaving != piece) {
                                r.starts.push_back(r.cur.size());
                                r.props.push_back(last ? joined(props[leaving], arriving) : props[leaving]);
                                piece = leaving;
                            }
                            r.cur.push_back(std::get<Line>(w[v]).Start());
                            if (last)
                                break;
                        }
                        e = to;
                        if (e == 2 * p0)
                            break;
                    }
                    r.ref = r.cur;
                    out.push_back(std::move(r));
                }
                std::erase_if(out, [](const Ring &r) { return r.cur.size() < 3; });
                rings = std::move(out);
                Orient();
                return true;
            }

            // the current polyline as lines with the original edges' properties
            Wire Lines() const { return Lines(nullptr); }

            // one edge per original edge: a line through two or collinear points, else the interpolating spline
            Wire Edges() const {
                Wire w(axes);
                for (const Ring &r : rings) {
                    size_t n = r.cur.size();
                    for (size_t k = 0; k < r.starts.size(); ++k) {
                        size_t b = r.starts[k], e = k + 1 < r.starts.size() ? r.starts[k + 1] : n;
                        std::vector<Vertex> pts(r.cur.begin() + (long)b, r.cur.begin() + (long)e);
                        pts.push_back(r.cur[e % n]);
                        double S = 0.0;
                        for (const Vertex &p : pts)
                            S = std::max({S, std::abs(p.x), std::abs(p.y)});
                        Vertex d = pts.back() - pts.front();
                        double len = Norm(d);
                        bool straight = len > 0.0;
                        for (size_t i = 1; i + 1 < pts.size() && straight; ++i)
                            straight = std::abs(Cross(pts[i] - pts.front(), d)) / len <= Tol(S);
                        if (straight) {
                            Line l(pts.front(), pts.back());
                            l.props = r.props[k];
                            w.Add(l);
                            continue;
                        }
                        for (Spline &sp : Spline::Through(pts, false, Tol(S), {})) {
                            sp.props = r.props[k];
                            w.Add(sp);
                        }
                    }
                }
                return w;
            }
    };

}
