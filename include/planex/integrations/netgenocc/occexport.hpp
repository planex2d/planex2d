// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <utility>
#include <vector>

#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_Curve.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include "planex/core/config.hpp"
#include "planex/curves/edge.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/wire/crossing.hpp"
#include "planex/wire/loops.hpp"
#include "planex/integrations/netgenocc/compound.hpp"
#include "planex/integrations/netgenocc/occprops.hpp"
#include "planex/core/timer.hpp"

namespace planex2d {

    using ExportPieces = std::vector<std::pair<PrimitiveEdge, bool>>;

    // the pieces of an edge in the order it runs: back, last piece first and each run from its end
    template <class Parts>
    ExportPieces PiecesOf(const Parts &parts, bool back) {
        ExportPieces pieces;
        if (back)
            for (auto p = parts.rbegin(); p != parts.rend(); ++p)
                pieces.emplace_back(*p, true);
        else
            for (const auto &p : parts)
                pieces.emplace_back(p, false);
        return pieces;
    }

    struct ExportEdge : LoopEdge {
        TopoDS_Edge edge;
        ExportPieces pieces;  // true: piece runs from v1 to v0
        size_t source;        // the planex edge it came from
    };

    inline ExportEdge MakeExportEdge(const TopoDS_Edge &edge, size_t v0, size_t v1, ExportPieces pieces, size_t source) {
        const Departure d0 = Leaving(pieces.front().first, !pieces.front().second);
        const Departure d1 = Leaving(pieces.back().first, pieces.back().second);
        return {{v0, v1, d0, d1}, edge, std::move(pieces), source};
    }

    // e between the same vertices with its curve running the other way: netgen's 2d mesh takes a boundary segment's
    // direction from the curve, not from the edge's orientation in its face
    inline TopoDS_Edge ReversedCurve(const TopoDS_Edge &e, const TopoDS_Vertex &start, const TopoDS_Vertex &end) {
        double f, l;
        Handle(Geom_Curve) c = BRep_Tool::Curve(e, f, l);
        Handle(Geom_Curve) r = c->Reversed();
        return BRepBuilderAPI_MakeEdge(r, start, end, c->ReversedParameter(l), c->ReversedParameter(f)).Edge();
    }

    TopoDS_Wire MakeOCCWire(const std::vector<ExportEdge> &edges, const Loop &loop, bool reversed);

    void ExportPartition(const TopoDS_Edge &edge, const Edge &source);

    TopoDS_Shape BuildFaces(const std::vector<ExportEdge> &edges, const std::vector<Loop> &loops,
                            const Properties &faceProps = nullptr);

}
