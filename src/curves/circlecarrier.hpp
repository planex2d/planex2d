// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <variant>

#include "core/vertex.hpp"
#include "curves/edge.hpp"

namespace planex2d {

    struct CircleCarrier {
        Vertex center;
        double radius;

        double LengthScale() const { return NormInf(center) + radius; }
    };

    inline std::optional<CircleCarrier> CarrierOf(const Edge &e) {
        if (const Arc *a = std::get_if<Arc>(&e))
            return CircleCarrier{a->Center(), a->Radius()};
        if (const Circle *c = std::get_if<Circle>(&e))
            return CircleCarrier{c->Center(), c->Radius()};
        return std::nullopt;
    }

}
