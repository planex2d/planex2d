// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/symmetryaxis.hpp"

#include <cmath>
#include <stdexcept>

namespace planex2d::sketch {

    Line SymmetryAxis(const SymRef &a, const SymRef &b, bool reverse, const std::string &call) {
        if (const Vertex *p = std::get_if<Vertex>(&a), *q = std::get_if<Vertex>(&b); p && q) {
            if (*p == *q)
                throw std::invalid_argument(call + ": the two points coincide");
            return Bisector(*p, *q, 0.5 * Norm(*q - *p));
        }
        if (const CircleCarrier *p = std::get_if<CircleCarrier>(&a), *q = std::get_if<CircleCarrier>(&b); p && q) {
            const double tol = Tol(std::max(p->LengthScale(), q->LengthScale()));
            if (std::abs(p->radius - q->radius) > tol)
                throw std::invalid_argument(call + ": circles of different radii have no symmetry line");
            if (Norm(q->center - p->center) <= tol)
                throw std::invalid_argument(call + ": both lie on the same circle");
            return Bisector(p->center, q->center, p->radius);
        }
        const Line *la = std::get_if<Line>(&a), *lb = std::get_if<Line>(&b);
        if (!la || !lb)
            throw std::invalid_argument(call + ": a symmetry line lies between two points, two lines or two circles of the same radius");
        // the sine of the angle between the two directions
        if (std::abs(Cross(Normalized(la->End() - la->Start()), Normalized(lb->End() - lb->Start()))) > 1e-12)
            return AngleBisector(*la, *lb, reverse);
        if (reverse)
            Warning() << call << ": reverse found only one solution";
        return MidLine(*la, *lb);
    }

}
