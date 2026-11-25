// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "planex/wire/wire.hpp"

namespace planex2d {

    // no chord lies closer to a curve than the rounding of its coordinates
    template <class E>
    void CheckSampleLimit(const char *call, const E &x, const char *kind, double tol) {
        const double limit = Tol(x.LengthScale());
        if (tol < limit) {
            std::ostringstream text;
            text << call << "(tol): tol " << tol << " is below the rounding of this " << kind << ", " << limit;
            throw std::invalid_argument(text.str());
        }
    }

    std::vector<Vertex> PolylineOf(const Edge &e, double tol, const char *call);
    std::vector<std::vector<Vertex>> PolylinesOf(const Wire &w, double tol);
    std::vector<std::vector<Vertex>> FacePolylinesOf(const Wire &edges, double tol);

    // lines from point to point, a point that repeats the one before it left out; closed: back to the first
    Wire WireOfPolyline(const std::vector<Vertex> &points, bool closed, const AxisSystem &axes, const Properties &props, const std::string &call);

}
