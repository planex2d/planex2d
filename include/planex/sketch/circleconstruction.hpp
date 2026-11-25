// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>

#include "planex/core/config.hpp"
#include "planex/curves/circlecarrier.hpp"
#include "planex/curves/edge.hpp"
#include "planex/sketch/choices.hpp"
#include "planex/sketch/pen.hpp"
#include "planex/sketch/target.hpp"
#include "planex/sketch/touchingcircles.hpp"

namespace planex2d::sketch {

    struct CircleRefs : Refs {
        std::optional<double> radius;
        std::optional<Vertex> center, through;
        std::optional<Edge> congruent;

        CircleCarrier Circle(const Pen &pen) const;

    private:
        CircleCarrier TouchingBoth(const Pen &pen, Choices &choices) const;
        double RadiusAround(const Vertex &c, const Pen &pen, Choices &choices) const;
    };

}
