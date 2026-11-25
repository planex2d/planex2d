// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include "planex/integrations/netgenocc/export.hpp"
#include "planex/integrations/netgenocc/identify.hpp"
#include "planex/shape/shape.hpp"

namespace planex2d {

    inline void AddFaces(BRep_Builder &builder, TopoDS_Compound &all, const TopoDS_Shape &faces) {
        if (faces.ShapeType() != TopAbs_COMPOUND) {
            builder.Add(all, faces);
            return;
        }
        for (TopoDS_Iterator it(faces); it.More(); it.Next())
            builder.Add(all, it.Value());
    }

    inline TopoDS_Shape ToOCCFace(const Face &f, const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                                  const Cleanup &cleanup = config::state->cleanup) {
        TopoDS_Shape s = ToOCCFace(f.Edges(), arcs, fit, cleanup, f.Props());
        ApplyIdentifications(s, f.Axes(), f.Ids());
        return s;
    }

    std::vector<TopoDS_Shape> ToOCCParts(const Shape &shape, const ArcFit &arcs = config::state->arc_fit,
                                         const SplineFit &fit = config::state->spline_fit, const Cleanup &cleanup = config::state->cleanup,
                                         bool wires = true);

    TopoDS_Shape ToOCCShape(const Shape &shape, const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                            const Cleanup &cleanup = config::state->cleanup, bool wires = true);

}
