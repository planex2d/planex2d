// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/curveformula.hpp"

#include <cmath>
#include <stdexcept>

namespace planex2d::sketch {

    Curve<double, Vertex> CurveFormula::Shape(const std::vector<double> &values) const {
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

}
