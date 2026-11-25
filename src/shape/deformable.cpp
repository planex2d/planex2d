// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/deformable.hpp"

#include <array>
#include <map>
#include <numbers>
#include <tuple>

namespace planex2d {

    void DeformableShape::Sample(const PrimitiveEdge &piece, double h, std::vector<Vertex> &out) {
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

    void DeformableShape::Sample(const Edge &e, bool forward, double h, std::vector<Vertex> &out) {
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

    Wire DeformableShape::Lines(std::vector<Properties> *props) const {
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

    struct LineGraph {
        struct Leaving {
            double angle;
            size_t line;
        };
        std::vector<size_t> from, to, degree;
        std::vector<std::vector<Leaving>> leaving;
    };

    static LineGraph GraphOf(const Wire &w, const std::vector<bool> &keep) {
        LineGraph g;
        std::map<std::pair<double, double>, size_t> ids;
        auto id = [&](const Vertex &v) { return ids.try_emplace({v.x, v.y}, ids.size()).first->second; };
        for (size_t k = 0; k < w.Size(); ++k) {
            g.from.push_back(id(std::get<Line>(w[k]).Start()));
            g.to.push_back(id(std::get<Line>(w[k]).End()));
        }
        g.degree.assign(ids.size(), 0);
        g.leaving.resize(ids.size());
        for (size_t k = 0; k < w.Size(); ++k) {
            ++g.degree[g.from[k]];
            ++g.degree[g.to[k]];
            if (keep[k]) {
                const Vertex d = std::get<Line>(w[k]).End() - std::get<Line>(w[k]).Start();
                g.leaving[g.from[k]].push_back({std::atan2(d.y, d.x), k});
            }
        }
        return g;
    }

    // at a vertex the walk continues with the leaving line closest clockwise to the reversed incoming direction, which
    // keeps the material on the left and loops simple
    static std::vector<size_t> TrailFrom(size_t start, const Wire &w, const LineGraph &g, std::vector<bool> &used) {
        std::vector<size_t> trail;
        for (size_t e = start;;) {
            used[e] = true;
            trail.push_back(e);
            if (g.to[e] == g.from[start])
                break;
            const Line &l = std::get<Line>(w[e]);
            const double back = std::atan2(l.Start().y - l.End().y, l.Start().x - l.End().x);
            const LineGraph::Leaving *next = nullptr;
            double best = 0.0;
            for (const LineGraph::Leaving &o : g.leaving[g.to[e]]) {
                if (used[o.line])
                    continue;
                const double turn = std::fmod(back - o.angle + 4.0 * std::numbers::pi, 2.0 * std::numbers::pi);
                if (!next || turn < best) {
                    next = &o;
                    best = turn;
                }
            }
            if (!next)
                break;
            e = next->line;
        }
        return trail;
    }

    static DeformableShape::Ring RingOf(std::vector<size_t> trail, const Wire &w, const LineGraph &g, const std::vector<Properties> &props) {
        auto corner = [&](size_t i) {
            const size_t prev = trail[(i + trail.size() - 1) % trail.size()];
            return w.Owner(trail[i]) != w.Owner(prev) || g.degree[g.from[trail[i]]] > 2;
        };
        size_t rot = 0;
        for (size_t i = 0; i < trail.size(); ++i)
            if (corner(i)) {
                rot = i;
                break;
            }
        std::rotate(trail.begin(), trail.begin() + (long)rot, trail.end());
        DeformableShape::Ring r;
        for (size_t i = 0; i < trail.size(); ++i) {
            r.cur.push_back(std::get<Line>(w[trail[i]]).Start());
            if (i == 0 || corner(i)) {
                r.starts.push_back(i);
                r.props.push_back(props[w.Owner(trail[i])]);
            }
        }
        r.ref = r.cur;
        return r;
    }

    std::vector<DeformableShape::Ring> DeformableShape::Trace(const Wire &w, const std::vector<bool> &keep, const std::vector<Properties> &props) {
        const LineGraph g = GraphOf(w, keep);
        std::vector<bool> used(w.Size(), false);
        std::vector<Ring> rings;
        for (size_t start = 0; start < w.Size(); ++start) {
            if (!keep[start] || used[start])
                continue;
            std::vector<size_t> trail = TrailFrom(start, w, g, used);
            if (g.to[trail.back()] != g.from[start]) {
                Warning() << "open boundary trail of " << trail.size() << " pieces dropped";
                continue;
            }
            if (trail.size() >= 3)
                rings.push_back(RingOf(std::move(trail), w, g, props));
        }
        return rings;
    }

    // outer loops counterclockwise, holes clockwise: a loop inside an even number of others is an outer one
    void DeformableShape::Orient() {
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

    bool DeformableShape::Resolve() {
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

    // vertex to vertex, which is symmetric: both sides of a thin part are marked, or none. A pair of the own
    // ring counts only if its distance is below half its arc distance, which excludes straight stretches and
    // corners of 60 degrees or more at any scale. If anything moved since the reference, only pairs that
    // came closer count, so a thin part of the given shape stays until it gets thinner
    DeformableShape::Marks DeformableShape::Mark() const {
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
        std::vector<uint32_t> nearby;
        for (size_t k = 0; k < a.w.Size(); ++k) {
            nearby.clear();
            grid.InBox(BBox::Of(a.At(k), a.At(k)).Grown(m), nearby);
            for (uint32_t j : nearby) {
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
    void DeformableShape::Resample(const Marks &a) {
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

    DeformableShape::DeformableShape(const Wire &w, double h, double minDistance, bool clean): axes(w.Axes()), h(h), minDistance(minDistance) {
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

    std::vector<std::string> DeformableShape::VertexNames() const {
        std::vector<std::string> names;
        for (const Ring &r : rings) {
            for (size_t k = 0; k < r.starts.size(); ++k) {
                size_t end = k + 1 < r.starts.size() ? r.starts[k + 1] : r.cur.size();
                std::string own = NameOf(r.props[k]);
                names.push_back(own.empty() ? NameOf(r.props[(k + r.starts.size() - 1) % r.starts.size()]) : own);
                names.insert(names.end(), end - r.starts[k] - 1, own);
            }
        }
        return names;
    }

    // current segments longer than h are halved, in both polylines; interior vertices closer than h/4 to
    // the previous kept vertex go
    void DeformableShape::Refine() {
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

    bool DeformableShape::Clean() {
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

    // both ends inclusive
    std::vector<DeformableShape::Path> DeformableShape::UnmarkedPaths(const Marks &a, std::vector<Ring> &whole) {
        std::vector<Path> paths;
        for (size_t r = 0; r < rings.size(); ++r) {
            auto b = a.marked.begin() + (long)a.offset[r], e = a.marked.begin() + (long)a.offset[r + 1];
            auto first = std::find(b, e, true);
            if (first == e) {
                whole.push_back(std::move(rings[r]));
                continue;
            }
            if (std::find(b, e, false) == e)
                continue;
            size_t v = (size_t)(first - a.marked.begin());
            while (a.marked[v])
                v = a.Next(v);
            for (size_t start = v;;) {
                Path p{v, v};
                while (!a.marked[a.Next(v)])
                    p.last = v = a.Next(v);
                paths.push_back(p);
                v = a.Next(v);
                while (a.marked[v])
                    v = a.Next(v);
                if (v == start)
                    break;
            }
        }
        return paths;
    }

    // end 2p is the first vertex of path p, 2p + 1 its last
    std::vector<size_t> DeformableShape::EndPartners(const Marks &a, const std::vector<Path> &paths) {
        auto endAt = [&](size_t e) -> const Vertex & {
            const Path &p = paths[e / 2];
            return a.At(e % 2 ? p.last : p.first);
        };
        const size_t ends = 2 * paths.size();
        std::vector<std::tuple<double, size_t, size_t>> pairs;
        for (size_t i = 0; i < ends; ++i)
            for (size_t j = i + 1; j < ends; ++j)
                pairs.emplace_back(Norm(endAt(i) - endAt(j)), i, j);
        std::sort(pairs.begin(), pairs.end());
        std::vector<size_t> partner(ends, ends);
        for (const auto &[d, i, j] : pairs)
            if (partner[i] == ends && partner[j] == ends) {
                partner[i] = j;
                partner[j] = i;
            }
        return partner;
    }

    void DeformableShape::JoinPaths(const Marks &a, const std::vector<Path> &paths, const std::vector<size_t> &partner, std::vector<Ring> &out) {
        auto joined = [](const Properties &x, const Properties &y) {
            return x && y && x->name && x->name == y->name ? x : Properties{};
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
                const Properties &arriving = a.props[a.w.Owner(to % 2 ? q.last : a.Prev(q.first))];
                size_t piece = a.props.size();
                for (size_t v = forward ? p.first : p.last;; v = forward ? a.Next(v) : a.Prev(v)) {
                    size_t leaving = a.w.Owner(forward ? v : a.Prev(v));
                    bool last = v == (forward ? p.last : p.first);
                    if (last || leaving != piece) {
                        r.starts.push_back(r.cur.size());
                        r.props.push_back(last ? joined(a.props[leaving], arriving) : a.props[leaving]);
                        piece = leaving;
                    }
                    r.cur.push_back(a.At(v));
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
    }

    bool DeformableShape::Defeature() {
        Marks a = Mark();
        if (std::find(a.marked.begin(), a.marked.end(), true) == a.marked.end())
            return false;
        // the connections join vertices, so the chains and their neighbouring segments are sampled finer
        // and marked again before the chains are taken
        Resample(a);
        a = Mark();
        std::vector<Ring> out;
        const std::vector<Path> paths = UnmarkedPaths(a, out);
        JoinPaths(a, paths, EndPartners(a, paths), out);
        std::erase_if(out, [](const Ring &r) { return r.cur.size() < 3; });
        rings = std::move(out);
        Orient();
        return true;
    }

    Wire DeformableShape::Edges() const {
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
                for (Spline &sp : Spline::Through(pts, false, Tol(S), {}, SplineStyle::Averaged)) {
                    sp.props = r.props[k];
                    w.Add(sp);
                }
            }
        }
        return w;
    }

}
