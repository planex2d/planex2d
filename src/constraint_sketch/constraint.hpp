// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "constraint_sketch/expression.hpp"
#include "sketch/geometry.hpp"

namespace planex2d::cs {

    // a name in an expression's text that measures an element ("base.end.h"): as it stands in the text, the measure, the
    // unknown that holds the measure's value and the element it names
    struct Written {
        std::string token, measure;
        size_t unknown, element;
    };

    // a relation between elements, a dimension or an expression, as the solver sees it
    struct Constraint {
        sketch::Kind kind;
        std::vector<sketch::Ref> refs;
        // a length, or an angle in degrees
        double value = 0.0;
        // a reference dimension: measures, drives nothing
        bool reference = false;
        // a contradicting constraint kept but not solved (the sketch is over-constrained)
        bool suspended = false;
        // frozen from the drawing when it is made: the side (+1 or -1), the pairing or which of two circles is outside
        double sign = 1.0;
        int variant = 0;
        // a copy in a pattern of two directions: its steps along the second (variant: along the first); a Distance on a
        // pattern: 2 for its second step
        int variant2 = 0;
        // Fix: the values held: of a whole line, circle or spline those of its points (heldPoints, followed through
        // merges) and of its other unknowns (held); Copy of a spline's tangent direction or position: the two unknowns
        std::vector<double> target;
        std::vector<size_t> held, heldPoints;
        // Tangent: the tangent directions it gave splines at the joint, (spline, point index)
        std::vector<std::pair<size_t, size_t>> madeTangents;
        // a point on a spline, its distance from one, or a touch away from its points: the positions along the
        // spline(s) there (chord length unknowns); a rigid group: where its frame is and how it turned (h, v, angle)
        std::vector<size_t> params;
        // a dimension an expression uses, and Value: the unknown that holds its value (an angle in radians)
        size_t valueAt = sketch::none;
        // Expression: its left side minus its right side, the value it drives (an unknown) and the text it was given
        std::vector<Node> equation;
        size_t drives = sketch::none;
        std::string text;
        // Expression: the names in text that measure an element, rewritten when the element is called otherwise
        std::vector<Written> written;
        // a reference made for a measure an expression uses, which goes with the last expression that uses it
        bool automatic = false;
        // a copy's tie to its original: solved, not listed (the copy's one tie is)
        bool tie = false;
    };

}
