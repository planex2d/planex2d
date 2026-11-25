// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace planex2d {

    // normalised as the turtle Sketch's Direction did: the helpers place bit for bit as they did through it
    template <class Store>
    sketch::Pen BasicSketch<Store>::HelperPen() const {
        const double n = std::hypot(pen.dir.x, pen.dir.y);
        return {pen.pos, {pen.dir.x / n, pen.dir.y / n}, Tol(NormInf(pen.pos))};
    }

    template <class Store>
    EdgeList BasicSketch<Store>::UntilEdges(Until until, const std::optional<sketch::Target> &target) {
        return target ? target->edges : until == Until::No ? EdgeList() : sketch::HelperView<BasicSketch>{*this}.EveryEdge();
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::ConstructCircle(const sketch::CircleRefs &r, const std::vector<sketch::Kind> &flags, const Options &opt) {
        const CircleCarrier c = r.Circle(HelperPen());
        // the radius of the first half of the turtle Sketch's circle
        const planex2d::Arc half({c.center.x + c.radius, c.center.y}, {c.center.x - c.radius, c.center.y}, c.center, true);
        return Circle(half.Center(), half.Radius(), flags, opt);
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::ConstructLine(const sketch::LineRefs &r, const std::vector<sketch::Kind> &flags, const Options &opt) {
        const sketch::Pen from = HelperPen();
        const sketch::ConstructedLine h = sketch::LineConstruction(r, from, axes, UntilEdges(r.until, r.untilTarget)).Drawn();
        sketch::Magnitude size;
        size.Add(from.pos);
        for (const Vertex &p : {h.start, h.end})
            size.Add(p);
        size.Add(r.from);
        size.Add(r.to);
        size.Add(r.tangent);
        for (const std::optional<Edge> *e : {&r.parallel, &r.perpendicular, &r.congruent, &r.angleRef})
            size.Add(*e);
        size.Add(r.through);
        if (!h.start.IsClose(from.pos, Tol(size.m)))
            MoveTo(h.start, {});
        return Line(h.end, flags, opt);
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::ConstructArc(const sketch::ArcRefs &r, const std::vector<sketch::Kind> &flags, const Options &opt) {
        const sketch::Pen from = HelperPen();
        const sketch::ConstructedArc h = sketch::ArcConstruction(r, from, axes, UntilEdges(r.until, r.untilTarget)).Drawn();
        if (h.full) {
            if (h.move)
                MoveTo(h.start, {});
            return FullTurn(h.center, h.ccw, flags, opt);
        }
        const planex2d::Arc a(h.start, h.end, h.center, h.ccw);
        sketch::Magnitude size;
        size.Add(from.pos);
        for (const Vertex &p : {a.Start(), a.End(), a.Center()})
            size.Add(p);
        size.Add(r.center);
        size.Add(r.congruent);
        size.Add(r.from);
        size.Add(r.to);
        size.Add(r.tangent);
        size.Add(r.through);
        if (!a.Start().IsClose(from.pos, Tol(size.m)))
            MoveTo(a.Start(), {});
        const Vertex c = a.Center(), p = a.Start();
        return Arc(a.End(), a.CCW() ? Vertex{c.y - p.y, p.x - c.x} : Vertex{p.y - c.y, c.x - p.x}, flags, opt);
    }

}
