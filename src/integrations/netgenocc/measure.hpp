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

#include "integrations/netgenocc/export.hpp"
#include "shape/measure.hpp"

namespace planex2d {

    // the OCC shapes of a planar set: the faces of the region and the wires of the curves
    inline TopoDS_Shape OCCOf(const Planar &s, const AxisSystem &axes) {
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        Wire region = s.region, curves = s.curves;
        region.SetAxes(axes);
        curves.SetAxes(axes);
        if (!region.Empty())
            builder.Add(all, ToOCCFace(region));
        if (!curves.Empty()) {
            const Cleanup &c = config::state->cleanup;
            auto wires = ToOCCWires(curves, false, config::state->arc_fit, config::state->spline_fit,
                                    Cleanup{c.remove_edges_smaller, c.close_gaps_smaller, false, c.merge_collinear_closer});
            for (int i = 1; i <= wires->Length(); ++i)
                builder.Add(all, wires->Value(i));
        }
        return all;
    }

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
