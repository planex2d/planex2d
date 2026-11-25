// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/curves/curvefit.hpp"

#include <numbers>

#include "planex/curves/circle.hpp"

namespace planex2d {

    std::vector<Vertex> FitPoints(std::span<const PrimitiveEdge> chain, double spacing, size_t arcPieces) {
        std::vector<Vertex> pts{chain.front().Start()};
        auto add = [&](const Vertex &p) {
            if (Norm(p - pts.back()) >= spacing)
                pts.push_back(p);
        };
        for (const PrimitiveEdge &e : chain) {
            if (const Arc *a = std::get_if<Arc>(&e))
                for (size_t k = 1; k < arcPieces; ++k)
                    add(a->PointAt(a->Sweep() * (double)k / (double)arcPieces));
            add(e.End());
        }
        const Vertex &end = chain.back().End();
        if (!(pts.back() == end)) {
            if (pts.size() > 1)
                pts.pop_back();
            pts.push_back(end);
        }
        return pts;
    }

    std::optional<Edge> FitArc(const std::vector<PrimitiveEdge> &chain, double tol, double ignore) {
        std::vector<Vertex> pts = FitPoints(chain, ignore);
        if (pts.size() < 3)
            return std::nullopt;
        const Vertex &p0 = pts.front(), &p1 = pts.back();
        Vertex chord = p1 - p0;
        double l = Norm(chord);
        if (l <= tol)
            return std::nullopt;
        Vertex n{-chord.y / l, chord.x / l};
        const Properties &props = PropsOf(chain.front());
        // every chain edge's midpoint within tol of the curve: the edges are lines and arcs, which cannot stray far
        // from the curve between their ends and midpoint
        auto close = [&](auto distance) {
            for (const PrimitiveEdge &e : chain)
                if (distance(e.MidPoint()) > tol)
                    return false;
            return true;
        };
        double offset = 0.0, aa = 0.0, ab = 0.0;
        for (size_t k = 1; k + 1 < pts.size(); ++k) {
            Vertex d = pts[k] - p0;
            double h = Dot(d, n);
            if (std::abs(h) > std::abs(offset))
                offset = h;
            aa += 4.0 * h * h;
            ab += 2.0 * h * Dot(d, pts[k] - p1);
        }
        if (std::abs(offset) <= tol) {
            if (!close([&](const Vertex &m) { return std::abs(Dot(m - p0, n)); }))
                return std::nullopt;
            Line line(p0, p1);
            line.props = props;
            return Edge(line);
        }
        Arc arc(p0, p1, (p0 + p1) * 0.5 + n * (ab / aa), offset < 0.0);
        arc.props = props;
        // at most a half circle, with slack: the halves of a closed chain are exactly half circles
        if (arc.Sweep() > std::numbers::pi * (1.0 + 1e-9))
            return std::nullopt;
        for (size_t k = 1; k + 1 < pts.size(); ++k)
            if (std::abs(Norm(pts[k] - arc.Center()) - arc.Radius()) > tol || arc.Param(pts[k]) >= arc.Sweep())
                return std::nullopt;
        if (!close([&](const Vertex &m) { return std::abs(Norm(m - arc.Center()) - arc.Radius()); }))
            return std::nullopt;
        return Edge(arc);
    }

}
