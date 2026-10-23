// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "core/config.hpp"
#include "curves/edge.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/circle.hpp"
#include "curves/spline.hpp"

namespace planex2d {

    struct EdgeList : std::vector<Edge> {
        using vector::vector;

        double LengthScale() const {
            double s = 0.0;
            for (const Edge &e : *this)
                s = std::max(s, LengthScaleOf(e));
            return s;
        }
        // the rounding tolerance of the largest coordinate the edges involve
        double Tol() const { return planex2d::Tol(LengthScale()); }
        // ends within tol (default rounding) are joined
        double JoinTol(std::optional<double> tol) const { return std::max(tol.value_or(0.0), Tol()); }
        bool HasCircle() const {
            return std::any_of(begin(), end(), [](const Edge &e) { return std::holds_alternative<Circle>(e); });
        }
        // a circle by its two halves (the lines and arcs the core cuts)
        EdgeList Expanded() const {
            EdgeList r;
            for (const Edge &e : *this) {
                if (const Circle *c = std::get_if<Circle>(&e))
                    for (const Arc &half : c->Halves())
                        r.push_back(half);
                else
                    r.push_back(e);
            }
            return r;
        }
        // endpoint 2i is the start, 2i + 1 the end of edge i
        std::vector<Vertex> Endpoints() const {
            std::vector<Vertex> pts;
            for (const Edge &e : *this)
                std::visit([&](const auto &x) {
                    pts.push_back(x.Start());
                    pts.push_back(x.End());
                }, e);
            return pts;
        }
        // all on one infinite line, or all on one circle (as the two halves of a Circle): nothing to choose between
        bool IsOnOneCarrier() const {
            const double tol = Tol();
            const EdgeList parts = Expanded();
            if (const Line *first = std::get_if<Line>(&parts.front())) {
                for (const Edge &e : parts) {
                    const Line *l = std::get_if<Line>(&e);
                    if (!l || !first->OnCarrier(l->Start(), tol) || !first->OnCarrier(l->End(), tol))
                        return false;
                }
                return true;
            }
            if (const Arc *first = std::get_if<Arc>(&parts.front())) {
                for (const Edge &e : parts) {
                    const Arc *a = std::get_if<Arc>(&e);
                    if (!a || !a->Center().IsClose(first->Center(), tol) || std::abs(a->Radius() - first->Radius()) > tol)
                        return false;
                }
                return true;
            }
            return false;
        }
        BBox Box() const {
            BBox b;
            for (const Edge &e : *this)
                b.Grow(BoxOf(e));
            return b;
        }
    };

}
