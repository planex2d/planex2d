// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include <IntCurvesFace_ShapeIntersector.hxx>
#include <gp_Lin.hxx>

#include "planex/boolean/arrangement.hpp"
#include "planex/integrations/netgenocc/hiddenlines.hpp"
#include "planex/integrations/netgenocc/project.hpp"

namespace planex2d {

    // how far p lies from e: from a spline's exact curve, not its approximation (a fold line ends on the curve)
    inline double ExactDistance(const Vertex &p, const Edge &e) {
        if (const Spline *s = std::get_if<Spline>(&e))
            return Norm(s->ExactNearestPoint(p) - p);
        return Distance(p, e);
    }

    std::vector<Spline> SplitExactly(const Spline &s, const std::vector<Vertex> &at, double tol);

    void CutAtFoldEnds(Wire &w, const Wire &folds, double tol);

    std::vector<bool> ShadedCells(const Wire &w, const std::vector<HalfEdges> &cells, const std::vector<size_t> &cellOf,
                                  const TopoDS_Shape &shape, const AxisSystem &axes, double tol);

    void JoinCutPieces(Wire &w, double tol);

    Wire Shadow(const TopoDS_Shape &shape, const AxisSystem &axes, bool ids = false);

}
