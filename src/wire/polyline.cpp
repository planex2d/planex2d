// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/polyline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace planex2d {

    static std::vector<Vertex> ArcPoints(const Arc &a, double tol, const char *call, const char *kind) {
        CheckSampleLimit(call, a, kind, tol);
        // the sagitta r (1 - cos(t / 2)) as 2 r sin^2(t / 4): no cancellation for a small tol
        const double step = std::min(std::numbers::pi, 4.0 * std::asin(std::min(1.0, std::sqrt(tol / (2.0 * a.Radius())))));
        const double chords = std::ceil(a.Sweep() / step);
        std::vector<Vertex> pts{a.Start()};
        // config.eps and config.k may lower the limit without bound
        if (!(chords < (double)pts.max_size()))
            throw std::invalid_argument(std::string(call) + "(tol): too many points for this " + kind);
        const size_t n = std::max<size_t>(1, (size_t)chords);
        for (size_t k = 1; k < n; ++k)
            pts.push_back(a.PointAt(a.Sweep() * (double)k / (double)n));
        pts.push_back(a.End());
        return pts;
    }

    std::vector<Vertex> PolylineOf(const Edge &e, double tol, const char *call) {
        if (const Arc *a = std::get_if<Arc>(&e))
            return ArcPoints(*a, tol, call, "arc");
        if (const Circle *c = std::get_if<Circle>(&e)) {
            const std::array<Arc, 2> halves = c->Halves();
            std::vector<Vertex> pts = ArcPoints(halves[0], tol, call, "circle");
            const std::vector<Vertex> rest = ArcPoints(halves[1], tol, call, "circle");
            pts.insert(pts.end(), rest.begin() + 1, rest.end());
            return pts;
        }
        if (const Spline *s = std::get_if<Spline>(&e)) {
            CheckSampleLimit(call, *s, "spline", tol);
            return s->Sample(tol);
        }
        const Line &l = std::get<Line>(e);
        return {l.Start(), l.End()};
    }

    static std::vector<Vertex> LoopPoints(const Wire &w, const Loop &loop, bool reversed, double tol) {
        std::vector<Vertex> out;
        const size_t m = loop.edges.size();
        for (size_t k = 0; k < m; ++k) {
            const auto [i, forward] = loop.edges[reversed ? m - 1 - k : k];
            std::vector<Vertex> pts = PolylineOf(w[i], tol, "Polylines");
            if (forward == reversed)
                std::reverse(pts.begin(), pts.end());
            out.insert(out.end(), out.empty() ? pts.begin() : pts.begin() + 1, pts.end());
        }
        if (loop.closed && !out.empty())
            out.back() = out.front();
        return out;
    }

    // a loop runs the way its first edge (lowest index) runs, start to end
    static bool RunsBackwards(const Loop &loop) {
        const auto first = std::min_element(loop.edges.begin(), loop.edges.end());
        return !first->second;
    }

    std::vector<std::vector<Vertex>> PolylinesOf(const Wire &w, double tol) {
        std::vector<std::vector<Vertex>> out;
        for (const Loop &loop : w.Loops())
            out.push_back(LoopPoints(w, loop, RunsBackwards(loop), tol));
        return out;
    }

    // the loops of a face: the outer one counterclockwise, then the holes clockwise
    std::vector<std::vector<Vertex>> FacePolylinesOf(const Wire &edges, double tol) {
        std::vector<std::vector<Vertex>> out;
        const std::vector<NestedLoop> nested = edges.Nesting();
        for (size_t depth = 0; depth < 2; ++depth)
            for (const NestedLoop &n : nested)
                if (n.depth == depth)
                    out.push_back(LoopPoints(edges, n.loop, (edges.LoopArea(n.loop.edges) > 0.0) == (depth == 1), tol));
        return out;
    }

    Wire WireOfPolyline(const std::vector<Vertex> &points, bool closed, const AxisSystem &axes, const Properties &props, const std::string &call) {
        std::vector<Vertex> kept;
        for (const Vertex &p : points)
            if (kept.empty() || !(p == kept.back()))
                kept.push_back(p);
        if (closed && kept.size() > 1 && kept.back() == kept.front())
            kept.pop_back();
        if (kept.size() < 2)
            throw std::invalid_argument(call + ": the points hold " + std::to_string(kept.size()) + " different point(s), a line needs two");
        Wire w(axes);
        const auto add = [&](const Vertex &a, const Vertex &b) {
            Line l(a, b);
            l.props = props;
            w.Add(std::move(l));
        };
        for (size_t k = 0; k + 1 < kept.size(); ++k)
            add(kept[k], kept[k + 1]);
        if (closed)
            add(kept.back(), kept.front());
        w.CheckChain(call);
        return w;
    }

}
