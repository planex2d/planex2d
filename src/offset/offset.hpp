// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

#include "boolean/boolean.hpp"
#include "offset/offsetpath.hpp"

namespace planex2d {

    // the band within d of one edge, its ends round
    template <class E>
    Wire EdgeOffset(const E &e, double d) {
        Wire w;
        w.Add(e);
        return w.Offset(d);
    }
    inline Wire Line::Offset(double d) const { return EdgeOffset(*this, d); }
    inline Wire Arc::Offset(double d) const { return EdgeOffset(*this, d); }
    // the band within d of the circle: the concentric circle of radius r + d, and r - d inside it unless d reaches r
    inline Wire Circle::Offset(double d) const { return EdgeOffset(*this, d); }
    inline Wire Spline::Offset(double d) const { return EdgeOffset(*this, d); }

    // the band of width 2 d around the wire's edges: per chain of it (Loops) its raw band, the edges' offsets joined
    // around the corners and capped at the open ends, all of them cleaned at once by their winding
    inline Wire Wire::Offset(double d, OffsetEnd ends, OffsetCorner corners) const {
        if (!(d > 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be positive and finite");
        const double tol = planex2d::Tol(Box().NormInf() + d);
        Wire raw(axes);
        for (const Loop &loop : Loops()) {
            EdgeList chain;
            for (auto [i, along] : loop.edges)
                chain.push_back(along ? edges[i] : ReversedOf(edges[i]));
            for (EdgeList &part : RawBand(std::move(chain), loop.closed, d, ends, corners, false, tol))
                for (Edge &e : part)
                    raw.Add(std::move(e));
        }
        Wire result = PositiveRegion(raw);
        result.SetAxes(axes);
        return result;
    }

    // the loop of a closed chain offset by d (> 0 outside, < 0 inside), as Face.Offset makes it: the region its raw
    // offset winds around positively (a circle: the concentric one); nothing where it shrinks away
    inline Wire LoopOffset(const EdgeList &chain, double d, OffsetCorner corners, const AxisSystem &axes, double tol) {
        Wire out(axes);
        if (const Circle *c = std::get_if<Circle>(&chain.front())) {
            if (c->Radius() + d > tol) {
                Circle o(c->Center(), c->Center() + (c->Origin() - c->Center()) * ((c->Radius() + d) / c->Radius()), true);
                o.props = WithoutPartition(PropsOf(chain.front()));
                out.Add(o);
            }
            return out;
        }
        const bool ccw = ChainArea(chain) > 0.0;
        OffsetPath path = OffsetSide(chain, true, ccw ? -d : d, corners, false, tol);
        if (!ccw)
            path.Reverse();
        for (Edge &e : path.edges)
            out.Add(std::move(e));
        return PositiveRegion(out);
    }

    // pieces in order along the runs they form, each run from a piece where no other ends
    inline EdgeList InRunOrder(const EdgeList &pieces, double tol) {
        EdgeList out;
        std::vector<bool> used(pieces.size(), false);
        const auto after = [&](const Vertex &p, bool atEnd) {
            for (size_t k = 0; k < pieces.size(); ++k)
                if (!used[k] && (atEnd ? EndOf(pieces[k]) : StartOf(pieces[k])).IsClose(p, tol))
                    return k;
            return pieces.size();
        };
        while (out.size() < pieces.size()) {
            size_t k = 0;
            while (used[k])
                ++k;
            for (size_t j = k; j < pieces.size(); ++j)
                if (!used[j] && after(StartOf(pieces[j]), true) == pieces.size()) {
                    k = j;
                    break;
                }
            for (; k < pieces.size(); k = after(EndOf(pieces[k]), false)) {
                used[k] = true;
                out.push_back(pieces[k]);
            }
        }
        return out;
    }

    // an open chain's offset on one side (s > 0 its left, < 0 its right), without caps, running the chain's way: of the
    // region between the chain and its raw offset (closed by lines across the ends) wound positively, the pieces that
    // come from the offset; nothing where it shrinks away
    inline EdgeList OpenSideOffset(const EdgeList &chain, double s, OffsetCorner corners, const AxisSystem &axes, double tol) {
        OffsetPath path = OffsetSide(chain, false, s, corners, false, tol);
        if (path.edges.empty())
            return {};
        const Properties back = ChangedProperties(nullptr, [](ShapeProperties &q) { q.layer = -1; });
        const Vertex first = StartOf(path.edges.front());
        path.AddLine(EndOf(path.edges.back()), EndOf(chain.back()), back, tol);
        for (size_t k = chain.size(); k-- > 0;)
            path.Add(ReversedOf(chain[k]), back, tol);
        path.AddLine(StartOf(chain.front()), first, back, tol);
        path.Close();
        // on the left the loop runs clockwise, back along the chain on its right
        if (s > 0.0)
            path.Reverse();
        Wire raw(axes);
        for (Edge &e : path.edges)
            raw.Add(std::move(e));
        EdgeList pieces;
        for (const Edge &e : PositiveRegion(raw))
            if (PropsOf(e) != back)
                pieces.push_back(s > 0.0 ? ReversedOf(e) : e);
        return InRunOrder(pieces, tol);
    }

    // the offset as edges, as the sketch's Offset makes it, per chain: both sides (an open chain's band, its ends and
    // corners as given; a closed chain's two loops) or one side (an open chain on its left for d > 0, its right for d < 0,
    // without caps; a closed chain outside for d > 0, inside for d < 0); bothSides none: both sides of an open chain, one
    // of a closed one. The bands taken both sides are one region where they overlap, as Offset makes it
    inline Wire Wire::OffsetCurves(double d, std::optional<bool> bothSides, OffsetEnd ends, OffsetCorner corners) const {
        if (!(d != 0.0) || !std::isfinite(d))
            throw std::invalid_argument("offset distance must be nonzero and finite");
        const double tol = planex2d::Tol(Box().NormInf() + std::abs(d));
        Wire bands(axes), out(axes);
        for (const Loop &loop : Loops()) {
            EdgeList chain;
            for (auto [i, along] : loop.edges)
                chain.push_back(along ? edges[i] : ReversedOf(edges[i]));
            if (bothSides.value_or(!loop.closed)) {
                for (EdgeList &part : RawBand(std::move(chain), loop.closed, std::abs(d), ends, corners, false, tol))
                    for (Edge &e : part)
                        bands.Add(std::move(e));
                continue;
            }
            const EdgeList side = loop.closed ? LoopOffset(chain, d, corners, axes, tol).Edges() : OpenSideOffset(chain, d, corners, axes, tol);
            for (const Edge &e : side)
                out.Add(e);
        }
        if (!bands.Empty())
            for (const Edge &e : PositiveRegion(bands))
                out.Add(e);
        Wire ordered(axes);
        for (Edge &e : InRunOrder(out.Edges(), tol))
            ordered.Add(std::move(e));
        return ordered;
    }

}
