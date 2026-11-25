// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/curves/edge.hpp"
#include "planex/curves/line.hpp"
#include "planex/curves/arc.hpp"
#include "planex/curves/circle.hpp"
#include "planex/curves/spline.hpp"

namespace planex2d {

    struct EdgeList : std::vector<Edge> {
        using vector::vector;

        double LengthScale() const {
            double s = 0.0;
            for (const Edge &e : *this)
                s = std::max(s, LengthScaleOf(e));
            return s;
        }
        double Tol() const { return planex2d::Tol(LengthScale()); }
        double JoinTol(std::optional<double> tol) const { return std::max(tol.value_or(0.0), Tol()); }
        bool HasCircle() const {
            return std::any_of(begin(), end(), [](const Edge &e) { return std::holds_alternative<Circle>(e); });
        }
        EdgeList Expanded() const;
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
        bool IsOnOneCarrier() const;
        BBox Box() const {
            BBox b;
            for (const Edge &e : *this)
                b.Grow(BoxOf(e));
            return b;
        }
    };

}
