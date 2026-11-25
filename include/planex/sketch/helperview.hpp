// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d::sketch {

    template <class S>
    struct HelperView {
        S &s;
        double reach = 0.0;

        const AxisSystem &Axes() const { return s.Axes(); }
        Vertex Position() const { return s.Position(); }
        EdgeList NamedEdges(const std::string &id) {
            if (!s.Book().IsElement(id))
                return {};
            const auto [e, r, angle, d] = s.ElementOf(id);
            if (e.kind == sketch::ElementKind::Axis) {
                if (reach == 0.0)
                    reach = 10.0 * std::max(1.0, s.Size());
                const double t = Radians(angle);
                const Vertex n{std::cos(t), std::sin(t)}, u{-n.y, n.x};
                return {Line(n * d - u * reach, n * d + u * reach)};
            }
            if (e.kind == sketch::ElementKind::Point || e.kind == sketch::ElementKind::Pattern || e.kind == sketch::ElementKind::Offset)
                return {};
            const Wire w = s.ElementGeometry(id);
            return EdgeList(w.begin(), w.end());
        }
        // of several the one nearest near, of equally near ones the first
        Edge NamedEdge(const std::string &id, const Vertex &nearPoint) {
            std::optional<Edge> best;
            double bestDistance = 0.0;
            for (const Edge &e : NamedEdges(id)) {
                const double d = Distance(nearPoint, e);
                const double tol = Tol(std::max(NormInf(nearPoint), std::visit([](const auto &x) { return x.LengthScale(); }, e)));
                if (!best || d < bestDistance - tol) {
                    best = e;
                    bestDistance = d;
                }
            }
            if (!best)
                throw std::invalid_argument("no edge named '" + id + "'");
            return *best;
        }
        std::optional<Vertex> FindPoint(const std::string &id) {
            if (!s.Book().IsElement(id))
                return std::nullopt;
            const sketch::Element &e = s.ElementOf(id).element;
            if (e.kind != sketch::ElementKind::Point)
                return std::nullopt;
            return s.Coords(e.Point());
        }
        Vertex PointNamed(const std::string &id, const std::string &call) {
            const std::optional<Vertex> p = FindPoint(id);
            if (!p)
                throw std::invalid_argument(call + ": no point named '" + id + "'");
            return *p;
        }
        EdgeList EveryEdge() {
            EdgeList drawn, construction;
            for (const auto &[kind, ids] : s.Elements())
                for (const std::string &id : kind == "Axis" ? std::vector<std::string>() : ids)
                    for (const Edge &e : NamedEdges(id))
                        (s.IsConstruction(id) ? construction : drawn).push_back(e);
            drawn.insert(drawn.end(), construction.begin(), construction.end());
            return drawn;
        }
    };

}
