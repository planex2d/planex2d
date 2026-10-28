// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <vector>

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>

#include "wire/wire.hpp"
#include "core/grid.hpp"
#include "integrations/netgenocc/edges.hpp"
#include "integrations/netgenocc/occexport.hpp"
#include "integrations/netgenocc/placement.hpp"
#include "core/timer.hpp"

namespace planex2d {

    // edges share OCC vertices instead of relying on OCC's 1e-7 proximity; reverse: per edge, its OCC edge runs from its
    // end to its start
    inline std::vector<ExportEdge> ExportEdges(const Wire &w, bool approximation, size_t &nverts, const std::vector<char> &reverse = {}) {
        static Timer t("planex Export");
        Region reg(t);
        const EdgeList &edges = w.Edges();
        if (edges.empty())
            throw std::runtime_error("no edges");
        const double tol = edges.Tol();
        BBox all;
        for (const Edge &e : edges)
            all.Grow(BoxOf(e));
        Grid grid(all, Grid::CellSize(all, 2 * edges.size(), tol));
        std::vector<TopoDS_Vertex> verts;
        std::vector<Vertex> vpos;
        auto vid = [&](const Vertex &p) {
            size_t found = verts.size();
            grid.ForNear(p, [&](size_t v) {
                if (found == verts.size() && Norm(vpos[v] - p) <= tol)
                    found = v;
            });
            if (found < verts.size())
                return found;
            verts.push_back(BRepBuilderAPI_MakeVertex(gp_Pnt(p.x, p.y, 0.0)).Vertex());
            vpos.push_back(p);
            grid.Insert(verts.size() - 1, p);
            return verts.size() - 1;
        };
        std::vector<PrimitiveEdge> items;
        std::vector<size_t> itemSource;
        std::vector<Spline> curves;
        std::vector<size_t> curveSource;
        std::vector<const Circle *> circles;
        std::vector<size_t> circleSource;
        for (size_t i = 0; i < edges.size(); ++i) {
            const Edge &e = edges[i];
            if (const Line *l = std::get_if<Line>(&e)) {
                items.emplace_back(*l);
                itemSource.push_back(i);
            } else if (const Arc *a = std::get_if<Arc>(&e)) {
                items.emplace_back(*a);
                itemSource.push_back(i);
            } else if (const Circle *c = std::get_if<Circle>(&e)) {
                circles.push_back(c);
                circleSource.push_back(i);
            } else if (approximation) {
                for (PrimitiveEdge a : std::get<Spline>(e).Approximation()) {
                    SetProps(a, std::get<Spline>(e).props);
                    items.push_back(a);
                    itemSource.push_back(i);
                }
            } else if (const Spline &s = std::get<Spline>(e); s.IsClosed()) {
                // as two edges: OCC got a single closed one wrong
                for (const Spline &half : s.Halves()) {
                    curves.push_back(half);
                    curveSource.push_back(i);
                }
            } else {
                curves.push_back(s);
                curveSource.push_back(i);
            }
        }
        std::vector<size_t> vs, ve, curvevertices, seams;
        for (const auto &e : items) {
            vs.push_back(vid(e.Start()));
            ve.push_back(vid(e.End()));
        }
        for (const Spline &s : curves) {
            curvevertices.push_back(vid(s.Start()));
            curvevertices.push_back(vid(s.End()));
        }
        for (const Circle *c : circles)
            seams.push_back(vid(c->Origin()));
        std::vector<Properties> vertexProps(verts.size());
        for (const Edge &e : edges)
            if (const Ends &x = EndsOf(e)) {
                Properties &s = vertexProps[vid(Wire::EndOf(e, false))];
                s = MergedProperties(s, x->start);
                Properties &t = vertexProps[vid(Wire::EndOf(e, true))];
                t = MergedProperties(t, x->end);
            }
        for (size_t v = 0; v < verts.size(); ++v)
            SetProperties(verts[v], vertexProps[v]);
        std::vector<ExportEdge> out;
        auto reversed = [&](size_t source) { return source < reverse.size() && reverse[source]; };
        for (size_t i = 0; i < items.size(); ++i) {
            TopoDS_Edge edge = std::visit([&](const auto &x) { return ToOCC(x, verts[vs[i]], verts[ve[i]]); }, items[i]);
            const bool back = reversed(itemSource[i]);
            if (back)
                edge = ReversedCurve(edge, verts[ve[i]], verts[vs[i]]);
            SetProperties(edge, PropsOf(items[i]));
            ExportPartition(edge, edges[itemSource[i]]);
            out.push_back(back ? MakeExportEdge(edge, ve[i], vs[i], {{items[i], true}}, itemSource[i])
                               : MakeExportEdge(edge, vs[i], ve[i], {{items[i], false}}, itemSource[i]));
        }
        for (size_t k = 0; k < curves.size(); ++k) {
            size_t v0 = curvevertices[2 * k], v1 = curvevertices[2 * k + 1];
            const auto &a = curves[k].Approximation();
            TopoDS_Edge edge = ToOCC(curves[k], verts[v0], verts[v1]);
            const bool back = reversed(curveSource[k]);
            if (back)
                edge = ReversedCurve(edge, verts[v1], verts[v0]);
            SetProperties(edge, curves[k].props);
            ExportPartition(edge, edges[curveSource[k]]);
            out.push_back(back ? MakeExportEdge(edge, v1, v0, PiecesOf(a, true), curveSource[k])
                               : MakeExportEdge(edge, v0, v1, PiecesOf(a, false), curveSource[k]));
        }
        for (size_t k = 0; k < circles.size(); ++k) {
            const TopoDS_Vertex &seam = verts[seams[k]];
            TopoDS_Edge edge = ToOCC(*circles[k], seam);
            const bool back = reversed(circleSource[k]);
            if (back)
                edge = ReversedCurve(edge, seam, seam);
            SetProperties(edge, circles[k]->props);
            ExportPartition(edge, edges[circleSource[k]]);
            out.push_back(MakeExportEdge(edge, seams[k], seams[k], PiecesOf(circles[k]->Halves(), back), circleSource[k]));
        }
        nverts = verts.size();
        return out;
    }

    // a loop as the wire runs: from its earliest edge, that one forward. BuildLoops starts where the directions at a vertex
    // sort first, and a rounding of a direction at pi decides that (a loft pairs its sections' wires by their order)
    inline Loop InWireOrder(const std::vector<ExportEdge> &edges, Loop loop) {
        std::vector<std::pair<size_t, bool>> &e = loop.edges;
        const size_t n = e.size();
        size_t first = 0;
        for (size_t k = 1; k < n; ++k)
            if (edges[e[k].first].source < edges[e[first].first].source)
                first = k;
        if (!e[first].second) {
            std::reverse(e.begin(), e.end());
            for (auto &x : e)
                x.second = !x.second;
            first = n - 1 - first;
        }
        if (!loop.closed)
            return loop;
        // the earliest edge's first piece (a spline's approximation has several)
        for (size_t k = 0; k + 1 < n && edges[e[(first + n - 1) % n].first].source == edges[e[first].first].source; ++k)
            first = (first + n - 1) % n;
        std::rotate(e.begin(), e.begin() + (std::ptrdiff_t)first, e.end());
        return loop;
    }

    inline Handle(TopTools_HSequenceOfShape) ToOCCWires(const Wire &w, bool approximation = false,
                                                        const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                                                        const Cleanup &cleanup = config::state->cleanup) {
        size_t nverts;
        std::vector<ExportEdge> out = ExportEdges(w.Cleaned(arcs, fit, cleanup), approximation, nverts);
        Handle(TopTools_HSequenceOfShape) wires = new TopTools_HSequenceOfShape;
        for (const Loop &loop : BuildLoops(out, nverts))
            wires->Append(MakeOCCWire(out, InWireOrder(out, loop), false));
        Place(wires, w.Axes());
        return wires;
    }

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
