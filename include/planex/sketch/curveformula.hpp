// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "planex/constraint_sketch/expression.hpp"
#include "planex/constraint_sketch/jet.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/curve.hpp"
#include "planex/curves/parametricfit.hpp"

namespace planex2d::sketch {

    // postfix nodes whose value 0 is the parameter t and value k the value of the dimension ids[k - 1]; text: the call, for
    // messages
    struct CurveFormula {
        std::string text;
        std::vector<cs::Node> h, v, from, to;
        std::vector<std::string> ids;
        std::vector<size_t> dimensions;
        std::vector<double> used;

        Curve<double, Vertex> Shape(const std::vector<double> &values) const;
    };

}
