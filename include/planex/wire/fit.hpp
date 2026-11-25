// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>

#include "planex/wire/wire.hpp"
#include "planex/core/disjointsets.hpp"
#include "planex/curves/curvefit.hpp"
#include "planex/core/timer.hpp"

namespace planex2d {

    // a fit's tol or ignore_edges_smaller: none takes 1e-4 of the diagonal of what is fitted (a 100 x 80 mm board: 12.8 um)
    inline double FitLength(const std::optional<double> &given, const BBox &box) { return given ? *given : 1e-4 * Norm(box.Size()); }

}
