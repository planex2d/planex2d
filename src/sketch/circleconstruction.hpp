// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "core/config.hpp"
#include "curves/circlecarrier.hpp"
#include "curves/edge.hpp"
#include "sketch/choices.hpp"
#include "sketch/pen.hpp"
#include "sketch/target.hpp"
#include "sketch/touchingcircles.hpp"

namespace planex2d::sketch {

    // a center (or the pen's place) and one of radius, congruent, through, a tangent; or a radius and two tangents
    struct CircleRefs : Refs {
        std::optional<double> radius;
        std::optional<Vertex> center, through;
        std::optional<Edge> congruent;

        CircleCarrier Circle(const Pen &pen) const {
            if (tangent.size() > 2)
                Fail("tangent is one target or two");
            Choices choices{reverse};
            if (tangent.size() == 2)
                return TouchingBoth(pen, choices);
            if (radius.has_value() + through.has_value() + congruent.has_value() + !tangent.empty() != 1)
                Fail("give exactly one of r, coincident, through and tangent");
            const Vertex c = center.value_or(pen.pos);
            const double rho = RadiusAround(c, pen, choices);
            if (!(rho > 0.0))
                Fail("the radius is zero");
            choices.Warn(call);
            return {c, rho};
        }

    private:
        CircleCarrier TouchingBoth(const Pen &pen, Choices &choices) const {
            if (!radius)
                Fail("a circle touching two elements needs a radius");
            if (center || through || congruent)
                Fail("a circle touching two elements has its own center");
            const double rho = std::abs(*radius);
            const double tol = Tol(std::max(Scale(pen.pos), rho));
            const std::vector<TouchingCircle> found = TouchingCircles(tangent[0], tangent[1], rho, near, pen, tol, call).Ranked(choices);
            if (found.empty())
                Fail("no circle of that radius touches both on those sides");
            return {found.front().center, rho};
        }
        double RadiusAround(const Vertex &c, const Pen &pen, Choices &choices) const {
            if (tangent.size() == 1)
                return tangent[0].RadiusAround(c, near, Tol(std::max(Scale(pen.pos), NormInf(c))), choices, call);
            if (congruent) {
                const std::optional<CircleCarrier> k = CarrierOf(*congruent);
                if (!k)
                    Fail("coincident needs an arc or a circle");
                return k->radius;
            }
            if (through)
                return Norm(*through - c);
            return std::abs(*radius);
        }
    };

}
