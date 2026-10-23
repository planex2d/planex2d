// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
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

#include "core/config.hpp"
#include "curves/edge.hpp"
#include "curves/primitiveedge.hpp"
#include "wire/crossing.hpp"
#include "wire/loops.hpp"
#include "integrations/netgenocc/occprops.hpp"
#include "core/timer.hpp"

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

    inline TopoDS_Wire MakeOCCWire(const std::vector<ExportEdge> &edges, const Loop &loop, bool reversed) {
        BRep_Builder builder;
        TopoDS_Wire w;
        builder.MakeWire(w);
        for (size_t k = 0; k < loop.edges.size(); ++k) {
            auto [i, forward] = loop.edges[reversed ? loop.edges.size() - 1 - k : k];
            builder.Add(w, forward != reversed ? edges[i].edge : TopoDS::Edge(edges[i].edge.Reversed()));
        }
        w.Closed(loop.closed);
        return w;
    }

    // the partition as fractions of the OCC curve's parameter range
    inline void ExportPartition(const TopoDS_Edge &edge, const Edge &source) {
        const Properties &p = PropsOf(source);
        if (!HasPartition(p))
            return;
        double s0, s1;
        Handle(Geom_Curve) c = BRep_Tool::Curve(edge, s0, s1);
        if (c.IsNull())
            return;
        double length = LengthOf(source);
        std::vector<double> params;
        for (double f : *p->partition) {
            Vertex q = PointAtFraction(source, f);
            GeomAPI_ProjectPointOnCurve projection(gp_Pnt(q.x, q.y, 0.0), c, s0, s1);
            // a point of another approximation piece has no foot on this one, or a far one
            if (projection.NbPoints() == 0 || projection.LowerDistance() > 1e-3 * length)
                continue;
            double t = (projection.LowerDistanceParameter() - s0) / (s1 - s0);
            if (t > 0.0 && t < 1.0)
                params.push_back(t);
        }
        std::sort(params.begin(), params.end());
        SetPartition(edge, params);
    }

    inline TopoDS_Shape BuildFaces(const std::vector<ExportEdge> &edges, const std::vector<Loop> &loops,
                                   const Properties &faceProps = nullptr) {
        static Timer t("planex BuildFaces");
        Region reg(t);
        struct Info {
            const Loop *loop;
            double area = 0.0;
            Vertex point;
            BBox box;
            std::vector<size_t> inside;
        };
        std::vector<Info> info;
        for (const Loop &loop : loops) {
            if (!loop.closed) {
                Warning() << "open wire left out of the face";
                continue;
            }
            Info in{&loop};
            bool first = true;
            const Vertex o = edges[loop.edges.front().first].pieces.front().first.Start();
            for (auto [i, forward] : loop.edges)
                for (const auto &[piece, backwards] : edges[i].pieces) {
                    in.area += (forward != backwards ? 1.0 : -1.0) * piece.SignedArea(o);
                    BBox b = BoxOf(piece);
                    if (first) {
                        in.point = piece.MidPoint();
                        in.box = b;
                        first = false;
                    } else {
                        in.box.Grow(b);
                    }
                }
            info.push_back(std::move(in));
        }
        auto encloses = [&](const Info &l, const Vertex &p) {
            if (!l.box.IsInside(p))
                return false;
            bool odd = false;
            for (auto [i, forward] : l.loop->edges)
                for (const auto &[piece, backwards] : edges[i].pieces)
                    odd ^= RayParity<false, false>(piece, p);
            return odd;
        };
        for (size_t i = 0; i < info.size(); ++i)
            for (size_t j = 0; j < info.size(); ++j)
                if (j != i && encloses(info[j], info[i].point))
                    info[i].inside.push_back(j);
        std::vector<TopoDS_Face> faces;
        for (size_t k = 0; k < info.size(); ++k) {
            size_t depth = info[k].inside.size();
            if (depth % 2 == 1)
                continue;
            BRepBuilderAPI_MakeFace face(gp_Pln(gp::XOY()), MakeOCCWire(edges, *info[k].loop, info[k].area < 0.0), true);
            for (const Info &hole : info)
                if (hole.inside.size() == depth + 1 && std::find(hole.inside.begin(), hole.inside.end(), k) != hole.inside.end())
                    face.Add(MakeOCCWire(edges, *hole.loop, hole.area > 0.0));
            SetProperties(face.Face(), faceProps);
            faces.push_back(face.Face());
        }
        if (faces.empty())
            throw std::runtime_error("no closed wire");
        if (faces.size() == 1)
            return faces.front();
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const TopoDS_Face &f : faces)
            builder.Add(compound, f);
        return compound;
    }

}
