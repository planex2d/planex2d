// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/core/vertex.hpp"
#include "planex/curves/curve.hpp"

namespace planex2d {

    BSplineData EllipseData(const Vertex &c, const Vertex &a, const Vertex &b, double t0, double t1);

}
