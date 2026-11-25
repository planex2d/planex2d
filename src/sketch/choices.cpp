// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/choices.hpp"

namespace planex2d::sketch {

    bool Preference::IsBetter(const Solution &a, const Solution &b) const {
        if (a.onEdge != b.onEdge)
            return a.onEdge;
        const double da = Norm(a.p - ref), db = Norm(b.p - ref);
        if (std::abs(da - db) > tol)
            return da < db;
        // directions closer than 1e-9 rad count as equal, as in BuildLoops
        const double ta = Turn(a.p), tb = Turn(b.p);
        if (std::abs(ta - tb) > 1e-9)
            return ta < tb;
        return Cross(dir, a.p - from) > Cross(dir, b.p - from);
    }

}
