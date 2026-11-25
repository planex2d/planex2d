// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/geometry.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "planex/core/bbox.hpp"
#include "planex/core/config.hpp"
#include "planex/curves/curvefit.hpp"
#include "planex/curves/primitivefit.hpp"

namespace planex2d::sketch {

    std::vector<std::pair<double, Vertex>> Element::Nodes(const Geometry &g, const planex2d::Curve<double, Vertex> &f) const {
        const auto [from, to] = Range(g, f);
        const double end = f.params.back(), same = samePlace * end;
        const std::vector<Vertex> q = SplinePoints(g);
        const std::array<Vertex, 2> ends = EndCoords(g);
        std::vector<std::pair<double, Vertex>> out{{from, ends[0]}};
        for (size_t k = 0; k < f.params.size(); ++k)
            for (double u : {f.params[k], f.params[k] + end})
                if (u > from + same && u < to - same)
                    out.push_back({u, q[k % q.size()]});
        std::sort(out.begin(), out.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
        out.erase(std::unique(out.begin(), out.end(), [&](const auto &x, const auto &y) { return y.first - x.first <= same; }), out.end());
        out.push_back({to, ends[1]});
        return out;
    }

    std::vector<std::pair<double, Vertex>> Element::OffsetNodes(const Geometry &g, const OffsetRun &run) const {
        const std::array<size_t, 2> ends = Ends(g);
        const double same = samePlace * run.g.f.params.back();
        std::vector<double> inner;
        const auto add = [&](double t) {
            if (t > run.from + same && t < run.to - same)
                inner.push_back(run.reversed ? run.to - t : t - run.from);
        };
        for (double t : run.g.f.params)
            add(t);
        // past a closed spline's seam: its first place (0) is its last
        for (size_t k = 1; k < run.g.f.params.size(); ++k)
            add(run.g.f.params[k] + run.end);
        std::sort(inner.begin(), inner.end());
        std::vector<std::pair<double, Vertex>> out{{0.0, g.Coords(ends[0])}};
        for (double w : inner)
            out.push_back({w, run.At(w)});
        out.push_back({run.to - run.from, g.Coords(ends[1])});
        return out;
    }

    std::vector<PrimitiveEdge> Element::OffsetCurveArcs(const Geometry &g, double *tolerance) const {
        const OffsetRun run = Run(g);
        std::vector<double> u;
        std::vector<Vertex> nodes;
        BBox box;
        for (const auto &[w, p] : OffsetNodes(g, run)) {
            u.push_back(w);
            nodes.push_back(p);
            box.Grow(p);
        }
        const double atol = config::state->spline_tol * Norm(box.Size());
        if (tolerance)
            *tolerance = atol;
        return planex2d::Approximated(run, u, nodes, atol);
    }

    EdgeList Element::Edges(const Geometry &g, const std::string &id) const {
        if (kind == ElementKind::Line)
            return {planex2d::Line(g.Coords(Start()), g.Coords(End()))};
        if (kind == ElementKind::Arc)
            return {planex2d::Arc(g.Coords(Start()), g.Coords(End()), g.Coords(Center()), ccw)};
        if (kind == ElementKind::Circle)
            return {planex2d::Circle(g.Coords(Center()), Origin(g), ccw)};
        if (kind == ElementKind::OffsetCurve)
            return OffsetCurveEdges(g, id);
        EdgeList out;
        if (kind == ElementKind::Spline)
            for (planex2d::Spline &piece : SplineEdges(g))
                out.push_back(std::move(piece));
        return out;
    }

    // a spline element approximated from the whole spline's curve with its cut points as nodes, so the approximation
    // runs through them and neighbouring pieces share their cut exactly; with the whole's tolerance, the spans between
    // the whole's nodes are approximated as in the whole. A closed whole spline is one closed edge, a piece over its seam
    // one edge on the curve taken on past its end
    std::vector<planex2d::Spline> Element::SplineEdges(const Geometry &g) const {
        const planex2d::Curve<double, Vertex> f = Curve(g);
        const std::vector<Vertex> q = SplinePoints(g);
        std::vector<Vertex> nodes;
        BBox all;
        for (size_t k = 0; k < f.params.size(); ++k) {
            nodes.push_back(q[k % q.size()]);
            all.Grow(nodes.back());
        }
        // a spline that closes only up to rounding closes exactly
        if (nodes.front() != nodes.back() && nodes.front().IsClose(nodes.back(), Tol(all.NormInf())))
            nodes.back() = nodes.front();
        const auto [from, to] = Range(g, f);
        const double begin = f.params.front(), end = f.params.back(), same = samePlace * end;
        const bool cutStart = from > begin + same, cutEnd = to < end - same;
        if (!cutStart && to <= end + same && !cutEnd)
            return planex2d::Spline::FromNodes(f, f.params, nodes);
        const std::array<Vertex, 2> ends = EndCoords(g);
        const double period = end - begin;
        // the points past the end once more, a period on (the first is the last)
        const auto part = [&](const planex2d::Curve<double, Vertex> &curve, double a, double b, const Vertex &pa, const Vertex &pb) {
            std::vector<double> u{a};
            std::vector<Vertex> p{pa};
            for (const double shift : {0.0, period})
                for (size_t k = shift > 0.0 ? 1 : 0; k < f.params.size(); ++k)
                    if (const double t = f.params[k] + shift; t > a + same && t < b - same) {
                        u.push_back(t);
                        p.push_back(nodes[k]);
                    }
            u.push_back(b);
            p.push_back(pb);
            return planex2d::Spline::FromNodes(curve, u, p, config::state->spline_tol * Norm(all.Size())).front();
        };
        if (to <= end + same)
            return {part(f, cutStart ? from : begin, cutEnd ? to : end, cutStart ? ends[0] : nodes.front(), cutEnd ? ends[1] : nodes.back())};
        return {part(f.Segment(from, end).Joined(f.Segment(begin, to - period).Shifted(period)), from, to, ends[0], ends[1])};
    }

    std::vector<planex2d::Spline> Element::DirectedSplineEdges(const Geometry &g, bool along) const {
        const planex2d::Curve<double, Vertex> f = Curve(g);
        std::vector<double> u;
        std::vector<Vertex> nodes;
        for (const auto &[t, v] : Nodes(g, f)) {
            u.push_back(t);
            nodes.push_back(v);
        }
        if (along)
            return planex2d::Spline::FromNodes(f, u, nodes);
        const double sum = f.First() + f.Last();
        std::reverse(u.begin(), u.end());
        std::reverse(nodes.begin(), nodes.end());
        for (double &t : u)
            t = sum - t;
        return planex2d::Spline::FromNodes(f.Reversed(), u, nodes);
    }

    // an offset curve's edges: one spline, its arcs within spline_tol of the exact curve and its B-spline fitted to them
    // (as the spline fit at export); if no B-spline fits them, the arcs and lines themselves, with a warning; without
    // the spline fit, the quintic through its points
    EdgeList Element::OffsetCurveEdges(const Geometry &g, const std::string &id) const {
        if (!config::state->spline_fitter)
            return {OffsetCurveSpline(g)};
        double atol = 0.0;
        const std::vector<PrimitiveEdge> arcs = OffsetCurveArcs(g, &atol);
        std::optional<planex2d::Spline> fitted;
        for (size_t n = 2; n <= 64 && !fitted; n *= 2)
            fitted = planex2d::Spline::Fitted(arcs, planex2d::FitPoints(arcs, atol, n), atol, arcs.front().StartTangent(), arcs.back().EndTangent());
        if (fitted)
            return {std::move(*fitted)};
        Warning() << id << ": no B-spline fits it within spline_tol, it goes out as its " << arcs.size() << " arcs and lines";
        EdgeList out;
        for (const PrimitiveEdge &a : arcs)
            out.push_back(std::visit([](const auto &x) -> Edge { return x; }, a));
        return out;
    }

    planex2d::Spline Element::OffsetCurveSpline(const Geometry &g) const {
        const OffsetRun run = Run(g);
        const std::array<size_t, 2> ends = Ends(g);
        const bool closed = ends[0] == ends[1];
        const Vertex a = g.Coords(ends[0]), b = g.Coords(ends[1]);
        const std::vector<double> t = run.g.FitParams(run.reversed ? run.to : run.from, run.reversed ? run.from : run.to, a, b, closed, SplineStyle::Quintic);
        std::vector<Vertex> nodes = run.g.Places(t, a, b, closed);
        const planex2d::Curve<double, Vertex> c = run.g.Through(t, nodes, closed, SplineStyle::Quintic);
        if (closed)
            nodes.push_back(a);
        return planex2d::Spline::FromNodes(c, c.params, nodes).front();
    }

}
