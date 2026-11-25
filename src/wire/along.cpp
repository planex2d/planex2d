// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/along.hpp"

#include <limits>
#include <numbers>
#include <variant>

namespace planex2d {

    // e is no circle
    ChainPlace MeasuredChain::OnEdge(const Edge &e, double local) {
        if (const Line *l = std::get_if<Line>(&e)) {
            const Vertex d = l->End() - l->Start();
            return {l->Start() + d * (local / l->Length()), Normalized(d), 0.0, 0.0, 0};
        }
        if (const Arc *a = std::get_if<Arc>(&e)) {
            const Vertex p = a->PointAt(local / a->Radius());
            return {p, a->Tangent<false>(p), (a->CCW() ? 1.0 : -1.0) / a->Radius(), 0.0, 0};
        }
        const Spline &s = std::get<Spline>(e);
        const double u = s.ExactParamAt(local);
        return {s.ExactPointAt(u), s.ExactDirectionAt(u), s.ExactCurvatureAt(u), 0.0, 0};
    }

    // e is no circle
    std::pair<double, Vertex> MeasuredChain::NearestOn(const Edge &e, const Vertex &p) {
        if (const Line *l = std::get_if<Line>(&e)) {
            const Vertex q = l->NearestPoint(p);
            return {l->LengthFraction(q) * l->Length(), q};
        }
        if (const Arc *a = std::get_if<Arc>(&e)) {
            const Vertex q = a->NearestPoint(p);
            double t = a->Param(q);
            // at an end Param may come out just below 2 pi instead of 0, or just past the sweep
            if (t > a->Sweep())
                t = t - a->Sweep() < 2.0 * std::numbers::pi - t ? a->Sweep() : 0.0;
            return {t * a->Radius(), q};
        }
        const auto [u, q] = std::get<Spline>(e).ExactNearest(p);
        return {std::get<Spline>(e).ExactLengthTo(u), q};
    }

    ChainPlace MeasuredChain::Place(size_t k, double local) const {
        const auto [part, along] = walk.edges[k];
        ChainPlace c = OnEdge(parts[part], along ? local : lengths[k] - local);
        if (!along) {
            c.tangent = c.tangent * -1.0;
            c.curvature = -c.curvature;
        }
        c.arcLength = before[k] + local;
        c.edge = owners[part];
        return c;
    }

    MeasuredChain::MeasuredChain(const EdgeList &edges, const std::string &call) {
        for (size_t i = 0; i < edges.size(); ++i)
            if (const Circle *c = std::get_if<Circle>(&edges[i]))
                for (const Arc &half : c->Halves()) {
                    parts.push_back(half);
                    owners.push_back(i);
                }
            else {
                parts.push_back(edges[i]);
                owners.push_back(i);
            }
        tol = edges.JoinTol(std::nullopt);
        walk = WalkChain(parts, tol, call, "wire.Wires() gives the chains; measure one of them");
        double sum = 0.0;
        for (const auto &[part, along] : walk.edges) {
            before.push_back(sum);
            lengths.push_back(ExactLengthOf(parts[part]));
            sum += lengths.back();
        }
    }

    // the place nearest p; of places equally near (within rounding), the one with the smallest arc length
    ChainPlace MeasuredChain::Nearest(const Vertex &p) const {
        size_t best = 0;
        double local = 0.0, d = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < walk.edges.size(); ++k) {
            const auto [part, along] = walk.edges[k];
            const auto [l, q] = NearestOn(parts[part], p);
            if (const double e = Norm(q - p); e < d - tol) {
                d = e;
                best = k;
                local = along ? l : lengths[k] - l;
            }
        }
        return Place(best, local);
    }

}
