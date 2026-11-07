// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::sketch {

    struct FoundAxis {
        Vertex at, dir;
        double from = 0.0, to = 0.0;
        size_t line = none;
        bool isNew = false;

        Vertex Mirrored(const Vertex &p) const { return planex2d::Mirrored(p, at, dir); }
        double Along(const Vertex &p) const { return Dot(p - at, dir); }
        bool IsOn(const Vertex &p, double tol) const { return std::abs(Cross(dir, p - at)) <= tol; }
        Vertex At(double t) const { return at + dir * t; }
    };

    class SymmetryFinder {
    public:
        SymmetryFinder(const Geometry &g, const std::vector<char> &skip, std::optional<double> tol) : g(g) {
            for (size_t k = 0; k < g.elements.size(); ++k)
                if (!skip[k])
                    AddEdge(k);
            near = tol ? *tol : Tol(size);
            turn = size > 0.0 ? near / size : 0.0;
            Touching();
        }

        std::vector<FoundAxis> Axes() const {
            const std::vector<Cluster> clusters = Clusters(Candidates());
            std::vector<char> used(edges.size(), 0);
            std::vector<FoundAxis> out;
            for (;;) {
                const Cluster *best = nullptr;
                size_t most = 0;
                for (const Cluster &c : clusters) {
                    const size_t n = c.Unused(used);
                    if (n > most && IsConnected(c, used)) {
                        best = &c;
                        most = n;
                    }
                }
                if (!best)
                    return out;
                const FoundAxis axis = AxisOf(*best, used);
                if (axis.line != none || axis.to - axis.from > near)
                    out.push_back(axis);
                for (size_t i : best->edges)
                    used[i] = 1;
            }
        }

    private:
        struct EdgeData {
            size_t element;
            ElementKind kind;
            bool ccw;
            Vertex a, b, c;
            double size;

            bool IsCircle() const { return kind == ElementKind::Circle; }
            Vertex Middle() const {
                if (kind == ElementKind::Line)
                    return (a + b) * 0.5;
                const Vertex u = a - c, v = b - c;
                double sweep = std::atan2(Cross(u, v), Dot(u, v));
                if (ccw && sweep <= 0.0)
                    sweep += 2.0 * std::numbers::pi;
                if (!ccw && sweep >= 0.0)
                    sweep -= 2.0 * std::numbers::pi;
                return Rotated(a, c, std::cos(sweep / 2.0), std::sin(sweep / 2.0));
            }
        };
        struct Candidate {
            FoundAxis axis;
            size_t edge, other;
        };
        struct Cluster {
            FoundAxis axis;
            std::vector<size_t> edges, selves;

            size_t Unused(const std::vector<char> &used) const {
                size_t n = 0;
                for (size_t i : edges)
                    n += used[i] ? 0 : 1;
                return n;
            }
        };

        void AddEdge(size_t k) {
            const Element &e = g.elements[k];
            if (e.kind == ElementKind::Line) {
                const Vertex a = g.Coords(e.Start()), b = g.Coords(e.End());
                edges.push_back({k, e.kind, e.ccw, a, b, {}, Norm(b - a)});
                size = std::max({size, NormInf(a), NormInf(b)});
            }
            if (e.kind == ElementKind::Arc || e.kind == ElementKind::Circle) {
                const Vertex c = g.Coords(e.Center());
                const double r = std::abs(g.values[e.Radius()]);
                const bool arc = e.kind == ElementKind::Arc;
                edges.push_back({k, e.kind, e.ccw, arc ? g.Coords(e.Start()) : c, arc ? g.Coords(e.End()) : c, c, r});
                size = std::max(size, NormInf(c) + r);
            }
        }
        void Touching() {
            struct End {
                Vertex at;
                size_t edge;
            };
            std::vector<End> ends;
            for (size_t i = 0; i < edges.size(); ++i)
                if (!edges[i].IsCircle()) {
                    ends.push_back({edges[i].a, i});
                    ends.push_back({edges[i].b, i});
                }
            std::sort(ends.begin(), ends.end(), [](const End &x, const End &y) { return x.at.x < y.at.x; });
            touching.assign(edges.size(), {});
            for (size_t m = 0; m < ends.size(); ++m)
                for (size_t n = m + 1; n < ends.size() && ends[n].at.x - ends[m].at.x <= near; ++n)
                    if (ends[m].edge != ends[n].edge && ends[m].at.IsClose(ends[n].at, near)) {
                        touching[ends[m].edge].push_back(ends[n].edge);
                        touching[ends[n].edge].push_back(ends[m].edge);
                    }
        }

        static FoundAxis Bisector(const Vertex &p, const Vertex &q) { return {(p + q) * 0.5, Normalized(Vertex{p.y - q.y, q.x - p.x})}; }
        bool IsAlike(const EdgeData &x, const EdgeData &y) const { return x.kind == y.kind && y.size - x.size <= near; }

        std::vector<Candidate> Candidates() const {
            std::vector<size_t> order(edges.size());
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t i, size_t j) {
                return edges[i].kind != edges[j].kind ? edges[i].kind < edges[j].kind : edges[i].size < edges[j].size;
            });
            std::vector<Candidate> out;
            for (size_t i = 0; i < order.size(); ++i)
                for (size_t j = i; j < order.size() && IsAlike(edges[order[i]], edges[order[j]]); ++j)
                    Mirrors(order[i], order[j], out);
            return out;
        }
        void Mirrors(size_t i, size_t j, std::vector<Candidate> &out) const {
            const EdgeData &x = edges[i], &y = edges[j];
            if (x.IsCircle()) {
                if (i != j && !x.c.IsClose(y.c, near))
                    out.push_back({Bisector(x.c, y.c), i, j});
                return;
            }
            if (i == j) {
                if (!x.a.IsClose(x.b, near))
                    out.push_back({Bisector(x.a, x.b), i, j});
                return;
            }
            const bool arc = x.kind == ElementKind::Arc;
            for (const bool reversed : {false, true}) {
                if (arc && reversed != (x.ccw == y.ccw))
                    continue;
                const Vertex ta = reversed ? y.b : y.a, tb = reversed ? y.a : y.b;
                if (x.a.IsClose(ta, near) && x.b.IsClose(tb, near))
                    continue;
                const FoundAxis axis = x.a.IsClose(ta, near) ? Bisector(x.b, tb) : Bisector(x.a, ta);
                if (axis.Mirrored(x.a).IsClose(ta, near) && axis.Mirrored(x.b).IsClose(tb, near) &&
                    (!arc || axis.Mirrored(x.c).IsClose(y.c, near)))
                    out.push_back({axis, i, j});
            }
        }

        std::vector<Cluster> Clusters(std::vector<Candidate> candidates) const {
            for (Candidate &c : candidates)
                if (c.axis.dir.y < 0.0 || (c.axis.dir.y == 0.0 && c.axis.dir.x < 0.0))
                    c.axis.dir = -c.axis.dir;
            std::sort(candidates.begin(), candidates.end(), [](const Candidate &p, const Candidate &q) { return p.axis.dir.x > q.axis.dir.x; });
            std::vector<std::vector<Candidate>> runs;
            for (const Candidate &c : candidates)
                if (!runs.empty() && std::abs(Cross(runs.back().front().axis.dir, c.axis.dir)) <= turn)
                    runs.back().push_back(c);
                else
                    runs.push_back({c});
            if (runs.size() > 1 && std::abs(Cross(runs.back().front().axis.dir, runs.front().front().axis.dir)) <= turn) {
                runs.front().insert(runs.front().end(), runs.back().begin(), runs.back().end());
                runs.pop_back();
            }
            std::vector<Cluster> out;
            for (std::vector<Candidate> &run : runs)
                Split(run, out);
            for (Cluster &c : out) {
                std::sort(c.edges.begin(), c.edges.end());
                c.edges.erase(std::unique(c.edges.begin(), c.edges.end()), c.edges.end());
            }
            return out;
        }
        void Split(std::vector<Candidate> &run, std::vector<Cluster> &out) const {
            const Vertex d = run.front().axis.dir, n{-d.y, d.x};
            std::sort(run.begin(), run.end(), [&](const Candidate &p, const Candidate &q) { return Dot(n, p.axis.at) < Dot(n, q.axis.at); });
            for (size_t m = 0; m < run.size(); ++m) {
                if (m == 0 || Dot(n, run[m].axis.at) - Dot(n, run[m - 1].axis.at) > near)
                    out.push_back({run[m].axis, {}, {}});
                Cluster &c = out.back();
                c.edges.push_back(run[m].edge);
                c.edges.push_back(run[m].other);
                if (run[m].edge == run[m].other)
                    c.selves.push_back(run[m].edge);
            }
        }

        bool IsConnected(const Cluster &c, const std::vector<char> &used) const {
            std::vector<char> in(edges.size(), 0);
            for (size_t i : c.edges)
                in[i] = !used[i];
            for (size_t i : c.edges)
                if (in[i])
                    for (size_t j : touching[i])
                        if (in[j])
                            return true;
            return false;
        }

        FoundAxis AxisOf(const Cluster &c, const std::vector<char> &used) const {
            FoundAxis axis = c.axis;
            std::vector<double> anchors, along;
            for (size_t i : c.edges) {
                const EdgeData &e = edges[i];
                if (used[i])
                    continue;
                for (const Vertex &p : {e.a, e.b}) {
                    along.push_back(axis.Along(p));
                    if (!e.IsCircle() && axis.IsOn(p, near))
                        anchors.push_back(axis.Along(p));
                }
            }
            for (size_t i : c.selves)
                if (!used[i])
                    anchors.push_back(axis.Along(edges[i].Middle()));
            for (const EdgeData &e : edges)
                if (e.IsCircle() && axis.IsOn(e.c, near))
                    anchors.push_back(axis.Along(e.c));
            if (anchors.size() < 2)
                along.insert(along.end(), anchors.begin(), anchors.end());
            const std::vector<double> &ends = anchors.size() < 2 ? along : anchors;
            axis.from = *std::min_element(ends.begin(), ends.end());
            axis.to = *std::max_element(ends.begin(), ends.end());
            axis.line = LineOn(axis);
            return axis;
        }
        size_t LineOn(const FoundAxis &axis) const {
            for (size_t k = 0; k < g.elements.size(); ++k) {
                const Element &e = g.elements[k];
                if (e.kind == ElementKind::Axis) {
                    const double a = g.values[e.AxisAngle()];
                    const Vertex n{std::cos(a), std::sin(a)};
                    if (std::abs(Dot(n, axis.dir)) <= turn && std::abs(Dot(n, axis.at) - g.values[e.AxisDistance()]) <= near)
                        return k;
                }
                if (e.kind == ElementKind::Line && axis.IsOn(g.Coords(e.Start()), near) && axis.IsOn(g.Coords(e.End()), near))
                    return k;
            }
            return none;
        }

        const Geometry &g;
        std::vector<EdgeData> edges;
        std::vector<std::vector<size_t>> touching;
        double size = 0.0, near = 0.0, turn = 0.0;
    };

}
