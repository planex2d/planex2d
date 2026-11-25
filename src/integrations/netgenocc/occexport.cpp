// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/occexport.hpp"

#include <algorithm>

namespace planex2d {

    TopoDS_Wire MakeOCCWire(const std::vector<ExportEdge> &edges, const Loop &loop, bool reversed) {
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
    void ExportPartition(const TopoDS_Edge &edge, const Edge &source) {
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

    struct LoopInfo {
        const Loop *loop;
        double area = 0.0;
        Vertex point;
        BBox box;
        std::vector<size_t> inside;
    };

    static std::vector<LoopInfo> ClosedLoops(const std::vector<ExportEdge> &edges, const std::vector<Loop> &loops) {
        std::vector<LoopInfo> info;
        for (const Loop &loop : loops) {
            if (!loop.closed) {
                Warning() << "open wire left out of the face";
                continue;
            }
            LoopInfo in{&loop};
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
        return info;
    }

    static bool Encloses(const std::vector<ExportEdge> &edges, const LoopInfo &l, const Vertex &p) {
        if (!l.box.IsInside(p))
            return false;
        bool odd = false;
        for (auto [i, forward] : l.loop->edges)
            for (const auto &[piece, backwards] : edges[i].pieces)
                odd ^= RayParity<false, false>(piece, p);
        return odd;
    }

    TopoDS_Shape BuildFaces(const std::vector<ExportEdge> &edges, const std::vector<Loop> &loops,
                            const Properties &faceProps) {
        static Timer t("planex BuildFaces");
        Region reg(t);
        std::vector<LoopInfo> info = ClosedLoops(edges, loops);
        for (size_t i = 0; i < info.size(); ++i)
            for (size_t j = 0; j < info.size(); ++j)
                if (j != i && Encloses(edges, info[j], info[i].point))
                    info[i].inside.push_back(j);
        std::vector<TopoDS_Face> faces;
        for (size_t k = 0; k < info.size(); ++k) {
            size_t depth = info[k].inside.size();
            if (depth % 2 == 1)
                continue;
            BRepBuilderAPI_MakeFace face(gp_Pln(gp::XOY()), MakeOCCWire(edges, *info[k].loop, info[k].area < 0.0), true);
            for (const LoopInfo &hole : info)
                if (hole.inside.size() == depth + 1 && std::find(hole.inside.begin(), hole.inside.end(), k) != hole.inside.end())
                    face.Add(MakeOCCWire(edges, *hole.loop, hole.area > 0.0));
            SetProperties(face.Face(), faceProps);
            faces.push_back(face.Face());
        }
        if (faces.empty())
            throw std::runtime_error("no closed wire");
        return OneOrCompound(faces);
    }

}
