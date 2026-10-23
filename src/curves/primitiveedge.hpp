// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <variant>

#include "core/vertex.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"

namespace planex2d {

    struct PrimitiveEdge : std::variant<Line, Arc> {
        using variant::variant;

        const Vertex &Start() const {
            return std::visit([](const auto &x) -> const Vertex & { return x.Start(); }, *this);
        }
        const Vertex &End() const {
            return std::visit([](const auto &x) -> const Vertex & { return x.End(); }, *this);
        }
        Location Locate(const Vertex &p, double tol) const {
            return std::visit([&](const auto &x) { return x.Locate(p, tol); }, *this);
        }
        Vertex MidPoint() const {
            return std::visit([](const auto &x) { return x.MidPoint(); }, *this);
        }
        double Length() const {
            return std::visit([](const auto &x) { return x.Length(); }, *this);
        }
        double LengthScale() const {
            return std::visit([](const auto &x) { return x.LengthScale(); }, *this);
        }
        Vertex StartTangent() const {
            return std::visit([](const auto &x) { return x.Tangent(x.Start()); }, *this);
        }
        Vertex EndTangent() const {
            return std::visit([](const auto &x) { return x.Tangent(x.End()); }, *this);
        }
        // p on the curve: its direction there along the edge
        Vertex Tangent(const Vertex &p) const {
            return std::visit([&](const auto &x) { return x.template Tangent<false>(p); }, *this);
        }
        Vertex NearestPoint(const Vertex &p) const {
            return std::visit([&](const auto &x) { return x.NearestPoint(p); }, *this);
        }
        double Distance(const Vertex &q) const {
            return std::visit([&](const auto &x) { return x.Distance(q); }, *this);
        }
        double SignedArea(const Vertex &o) const {
            return std::visit([&](const auto &x) { return x.SignedArea(o); }, *this);
        }
        double LengthFraction(const Vertex &p) const {
            return std::visit([&](const auto &x) { return x.LengthFraction(p); }, *this);
        }
        Vertex PointAtFraction(double t) const {
            return std::visit([&](const auto &x) { return x.PointAtFraction(t); }, *this);
        }
        Vertex TangentAtFraction(double t) const {
            return std::visit([&](const auto &x) { return x.TangentAtFraction(t); }, *this);
        }
    };

}
