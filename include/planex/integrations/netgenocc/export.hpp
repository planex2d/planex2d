// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>

#include "planex/wire/wire.hpp"
#include "planex/core/grid.hpp"
#include "planex/integrations/netgenocc/edges.hpp"
#include "planex/integrations/netgenocc/occexport.hpp"
#include "planex/integrations/netgenocc/placement.hpp"
#include "planex/core/timer.hpp"

namespace planex2d {

    std::vector<ExportEdge> ExportEdges(const Wire &w, bool approximation, size_t &nverts, const std::vector<char> &reverse = {});

    Loop InWireOrder(const std::vector<ExportEdge> &edges, Loop loop);

    Handle(TopTools_HSequenceOfShape) ToOCCWires(const Wire &w, bool approximation = false,
                                                 const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                                                 const Cleanup &cleanup = config::state->cleanup);

    inline TopoDS_Shape ToOCCFace(const Wire &w, const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                                  const Cleanup &cleanup = config::state->cleanup, const Properties &faceProps = nullptr) {
        size_t nverts;
        const Wire c = w.Cleaned(arcs, fit, cleanup);
        std::vector<ExportEdge> out = ExportEdges(c, false, nverts, c.FaceOrientation());
        return Placed(BuildFaces(out, BuildLoops(out, nverts), faceProps), w.Axes());
    }

    inline TopoDS_Wire ToOCCWire(const Wire &w, bool approximation = false, const ArcFit &arcs = config::state->arc_fit,
                                 const SplineFit &fit = config::state->spline_fit, const Cleanup &cleanup = config::state->cleanup) {
        auto wires = ToOCCWires(w, approximation, arcs, fit, cleanup);
        if (wires->Length() != 1)
            throw std::runtime_error("edges do not form a single wire");
        return TopoDS::Wire(wires->Value(1));
    }

}
