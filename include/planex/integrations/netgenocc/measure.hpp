// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <stdexcept>

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include "planex/integrations/netgenocc/export.hpp"
#include "planex/shape/measure.hpp"

namespace planex2d {

    TopoDS_Shape OCCOf(const Planar &s, const AxisSystem &axes);

    struct Nearest3 {
        double d;
        gp_Pnt a, b;
    };
    inline Nearest3 NearestOCC(const TopoDS_Shape &a, const TopoDS_Shape &b) {
        BRepExtrema_DistShapeShape dist(a, b);
        if (!dist.IsDone() || dist.NbSolution() == 0)
            throw std::runtime_error("the distance could not be computed");
        return {dist.Value(), dist.PointOnShape1(1), dist.PointOnShape2(1)};
    }
    inline TopoDS_Shape OCCPoint(const gp_Pnt &p) { return BRepBuilderAPI_MakeVertex(p).Vertex(); }

}
