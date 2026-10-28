// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "constraint_sketch/expression.hpp"
#include "constraint_sketch/jet.hpp"
#include "core/vertex.hpp"
#include "curves/curve.hpp"
#include "curves/parametricfit.hpp"

namespace planex2d::sketch {

    // the formula of a curve: its point (h, v) and its range (from, to) as postfix nodes, whose value 0 is the parameter t
    // and value k the value of the dimension ids[k - 1] (an h or v in the other coordinate is written out already); in a
    // constraint sketch the constraints of those dimensions and their values at the last fit. text: the call, for messages
    struct CurveFormula {
        std::string text;
        std::vector<cs::Node> h, v, from, to;
        std::vector<std::string> ids;
        std::vector<size_t> dimensions;
        std::vector<double> used;

        // the curve the formula gives with the dimensions at values, fitted (ParametricFit)
        Curve<double, Vertex> Shape(const std::vector<double> &values) const {
            const auto end = [&](const std::vector<cs::Node> &nodes, const char *which) {
                const double t = cs::Evaluated<double>(nodes, [&](size_t u) { return values[u - 1]; });
                if (!std::isfinite(t)) {
                    std::string error;
                    cs::Evaluated<double>(nodes, [&](size_t u) { return values[u - 1]; }, &error);
                    throw std::invalid_argument(text + ": the range's " + which + ": " + error);
                }
                return t;
            };
            const auto coordinate = [&](const std::vector<cs::Node> &nodes, double t, const char *name) {
                const cs::Jet x = cs::Evaluated<cs::Jet>(nodes, [&](size_t u) { return u == 0 ? cs::Jet{t, 1.0, 0.0} : cs::Jet(values[u - 1]); });
                if (!std::isfinite(x.v)) {
                    std::string error;
                    cs::Evaluated<double>(nodes, [&](size_t u) { return u == 0 ? t : values[u - 1]; }, &error);
                    throw std::invalid_argument(text + ": " + name + " at t = " + NumberText(t) + ": " + error);
                }
                return x;
            };
            return ParametricFit([&](double t) {
                const cs::Jet x = coordinate(h, t, "h"), y = coordinate(v, t, "v");
                return CurveJet{{x.v, y.v}, {x.d, y.d}, {x.dd, y.dd}};
            }, end(from, "start"), end(to, "end"), text);
        }
    };

}
