// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "constraint_sketch/solver.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::cs {

    // a system as the solver sees it with the drawn shapes kept: a row per shape value of an element (a line's end minus
    // its start, an arc's ends from its centre, a spline's steps from point to point, every value of an element that is
    // no point coordinate, every other unknown) whose change from ref goes into a slack unknown of its own, the slacks
    // counted 1 / moveWeight times as much as the system's own unknowns: the least change then moves the geometry as a
    // whole where it can and changes the drawn shapes as little as it can
    template <class S>
    class ShapedSystem {
        public:
            // a change of the geometry that keeps every shape counts this much of the same change of a shape
            static constexpr double moveWeight = 1e-2;

            ShapedSystem(const S &s, std::vector<double> ref) : system(s), base(s.Count()), n(s.values.size()), ref(std::move(ref)) {
                std::vector<char> point(n, 0), covered(n, 0);
                for (size_t c : s.coordsOf)
                    point[c] = point[c + 1] = 1;
                const auto step = [&](size_t a, size_t b) {
                    if (s.coordsOf[a] != s.coordsOf[b])
                        for (size_t i = 0; i < 2; ++i)
                            Add({{s.coordsOf[b] + i, 1.0}, {s.coordsOf[a] + i, -1.0}});
                };
                for (const sketch::Element &e : s.elements) {
                    if (e.kind == sketch::ElementKind::Line)
                        step(e.Start(), e.End());
                    if (e.kind == sketch::ElementKind::Arc) {
                        step(e.Center(), e.Start());
                        step(e.Center(), e.End());
                    }
                    for (size_t u : e.Values(s))
                        if (!point[u] && !covered[u]) {
                            covered[u] = 1;
                            Add({{u, 1.0 / s.scale[u]}});
                        }
                }
                for (const sketch::SplineData &d : s.splines) {
                    // a fitted spline's ends follow its frame, whose turn is its shape value
                    if (d.fitted)
                        continue;
                    for (size_t k = 0; k + 1 < d.points.size(); ++k)
                        step(d.points[k], d.points[k + 1]);
                    if (d.closed && d.points.size() > 2)
                        step(d.points.back(), d.points.front());
                }
                for (size_t u = 0; u < n; ++u)
                    if (!point[u] && !covered[u])
                        Add({{u, 1.0 / s.scale[u]}});
            }

            size_t Count() const { return base + shape.size(); }
            size_t Rows(size_t c) const { return c < base ? system.Rows(c) : 1; }
            const std::vector<size_t> &Indices(size_t c) const { return c < base ? system.Indices(c) : shape[c - base].indices; }
            const std::vector<RowBlock> &Blocks(size_t c) const { return c < base ? system.Blocks(c) : shape[c - base].blocks; }
            void Evaluate(size_t c, const std::vector<double> &x, double *r, double *J) const {
                if (c < base)
                    return system.Evaluate(c, x, r, J);
                const Row &row = shape[c - base];
                const size_t slack = row.indices.size() - 1;
                r[0] = -x[row.indices[slack]];
                for (size_t k = 0; k < slack; ++k) {
                    r[0] += row.coef[k] * (x[row.indices[k]] - ref[row.indices[k]]);
                    J[k] = row.coef[k];
                }
                J[slack] = -1.0;
            }

            // the system's unknowns at their own scale, then the slacks, each at its row's change from ref, at the scale
            // sqrt(moveWeight); the unknowns follow the solution in the solver's later pull rounds (Unknowns::follow), the
            // slacks keep their target, no change of shape: the shapes then end as close to ref as the constraints let
            // them, not moveWeight short of it
            Unknowns Extended(Unknowns x) const {
                const double slackScale = std::sqrt(moveWeight);
                x.follow.assign(x.value.size(), 1);
                for (const Row &row : shape) {
                    double v = 0.0;
                    for (size_t k = 0; k + 1 < row.indices.size(); ++k)
                        v += row.coef[k] * (x.value[row.indices[k]] - ref[row.indices[k]]);
                    x.value.push_back(v);
                    x.scale.push_back(slackScale);
                    x.fixed.push_back(0);
                    x.follow.push_back(0);
                }
                return x;
            }
            // ref with the slacks of no change
            std::vector<double> Origin() const {
                std::vector<double> o = ref;
                o.resize(n + shape.size(), 0.0);
                return o;
            }

        private:
            // sum of coef times the change of the unknown at indices, less the slack (the last index)
            struct Row {
                std::vector<size_t> indices;
                std::vector<double> coef;
                std::vector<RowBlock> blocks;
            };
            const S &system;
            size_t base, n;
            std::vector<double> ref;
            std::vector<Row> shape;

            void Add(const std::vector<std::pair<size_t, double>> &terms) {
                if (std::ranges::all_of(terms, [&](const auto &t) { return system.fixed[t.first] != 0; }))
                    return;
                Row row;
                for (const auto &[u, c] : terms) {
                    row.indices.push_back(u);
                    row.coef.push_back(c);
                }
                row.indices.push_back(n + shape.size());
                row.blocks = {{1, row.indices}};
                shape.push_back(std::move(row));
            }
    };

}
