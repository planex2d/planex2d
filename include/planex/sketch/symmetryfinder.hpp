// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/sketch/geometry.hpp"

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
            nearby = tol ? *tol : Tol(size);
            turn = size > 0.0 ? nearby / size : 0.0;
            Touching();
        }

        std::vector<FoundAxis> Axes() const;

    private:
        struct EdgeData {
            size_t element;
            ElementKind kind;
            bool ccw;
            Vertex a, b, c;
            double size;

            bool IsCircle() const { return kind == ElementKind::Circle; }
            Vertex Middle() const;
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

        void AddEdge(size_t k);
        void Touching();

        static FoundAxis Bisector(const Vertex &p, const Vertex &q) { return {(p + q) * 0.5, Normalized(Vertex{p.y - q.y, q.x - p.x})}; }
        bool IsAlike(const EdgeData &x, const EdgeData &y) const { return x.kind == y.kind && y.size - x.size <= nearby; }

        std::vector<Candidate> Candidates() const;
        void Mirrors(size_t i, size_t j, std::vector<Candidate> &out) const;

        std::vector<Cluster> Clusters(std::vector<Candidate> candidates) const;
        void Split(std::vector<Candidate> &run, std::vector<Cluster> &out) const;

        bool IsConnected(const Cluster &c, const std::vector<char> &used) const;

        FoundAxis AxisOf(const Cluster &c, const std::vector<char> &used) const;
        size_t LineOn(const FoundAxis &axis) const;

        const Geometry &g;
        std::vector<EdgeData> edges;
        std::vector<std::vector<size_t>> touching;
        double size = 0.0, nearby = 0.0, turn = 0.0;
    };

}
