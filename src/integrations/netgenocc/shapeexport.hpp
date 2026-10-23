// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <stdexcept>
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

#include "integrations/netgenocc/export.hpp"
#include "integrations/netgenocc/identify.hpp"
#include "shape/shape.hpp"

namespace planex2d {

    // a face, or the faces of a compound, into the compound
    inline void AddFaces(BRep_Builder &builder, TopoDS_Compound &all, const TopoDS_Shape &faces) {
        if (faces.ShapeType() != TopAbs_COMPOUND) {
            builder.Add(all, faces);
            return;
        }
        for (TopoDS_Iterator it(faces); it.More(); it.Next())
            builder.Add(all, it.Value());
    }

    // the face with its properties and identifications
    inline TopoDS_Shape ToOCCFace(const Face &f, const ArcFit &arcs = config::arc_fit, const SplineFit &fit = config::spline_fit,
                                  const Cleanup &cleanup = config::cleanup) {
        TopoDS_Shape s = ToOCCFace(f.Edges(), arcs, fit, cleanup, f.Props());
        ApplyIdentifications(s, f.Axes(), f.Ids());
        return s;
    }

    // the faces first, then one wire per connected chain of every wire part; an edge on several parts is one OCC edge in
    // all of them
    inline std::vector<TopoDS_Shape> ToOCCParts(const Shape &shape, const ArcFit &arcs = config::arc_fit, const SplineFit &fit = config::spline_fit,
                                                const Cleanup &cleanup = config::cleanup, bool wires = true) {
        const Shape::ExportPlan plan = shape.Export(arcs, fit, cleanup);
        const Wire &c = plan.edges;
        const size_t groups = plan.groupProps.size();
        size_t nverts;
        std::vector<ExportEdge> out = ExportEdges(c, false, nverts, plan.reverse);
        std::vector<std::vector<ExportEdge>> byPart(plan.parts);
        for (const ExportEdge &e : out)
            for (uint32_t p : plan.partsOf[e.source])
                byPart[p].push_back(e);
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        // a wire edge on no face but inside one is an internal edge of that face, which the mesh follows
        std::vector<const ExportEdge *> inner;
        for (const ExportEdge &e : out)
            if (!plan.IsOnGroup(e.source))
                inner.push_back(&e);
        for (uint32_t k = 0; k < groups; ++k) {
            if (byPart[k].empty())
                continue;
            // every face from its own edges, so faces touching at points stay apart
            TopoDS_Compound faces;
            builder.MakeCompound(faces);
            const std::vector<std::vector<size_t>> &groupFaces = plan.facesOfGroup[k];
            std::vector<std::vector<size_t>> facesOf(c.Size());
            for (size_t f = 0; f < groupFaces.size(); ++f)
                for (size_t i : groupFaces[f])
                    facesOf[i].push_back(f);
            std::vector<std::vector<ExportEdge>> own(groupFaces.size());
            for (const ExportEdge &e : out)
                for (size_t f : facesOf[e.source])
                    own[f].push_back(e);
            for (const std::vector<ExportEdge> &edgesOfFace : own)
                AddFaces(builder, faces, BuildFaces(edgesOfFace, BuildLoops(edgesOfFace, nverts), plan.groupProps[k]));
            for (TopExp_Explorer f(faces, TopAbs_FACE); f.More(); f.Next()) {
                TopoDS_Face face = TopoDS::Face(f.Current());
                for (const ExportEdge *e : inner) {
                    const Vertex m = MidPointOf(c[e->source]);
                    BRepClass_FaceClassifier where(face, gp_Pnt(m.x, m.y, 0.0), Precision::Confusion());
                    if (where.State() != TopAbs_IN)
                        continue;
                    TopoDS_Wire w;
                    builder.MakeWire(w);
                    builder.Add(w, e->edge);
                    w.Orientation(TopAbs_INTERNAL);
                    face.Free(true);
                    builder.Add(face, w);
                }
            }
            AddFaces(builder, all, faces);
        }
        for (size_t k = groups; wires && k < byPart.size(); ++k)
            for (const Loop &loop : BuildLoops(byPart[k], nverts))
                builder.Add(all, MakeOCCWire(byPart[k], loop, false));
        TopoDS_Shape placed = Placed(all, c.Axes());
        ApplyIdentifications(placed, c.Axes(), plan.ids);
        std::vector<TopoDS_Shape> parts;
        for (TopoDS_Iterator it(placed); it.More(); it.Next())
            parts.push_back(it.Value());
        return parts;
    }

    // a single part as itself, else a compound of the parts
    inline TopoDS_Shape ToOCCShape(const Shape &shape, const ArcFit &arcs = config::arc_fit, const SplineFit &fit = config::spline_fit,
                                   const Cleanup &cleanup = config::cleanup, bool wires = true) {
        std::vector<TopoDS_Shape> parts = ToOCCParts(shape, arcs, fit, cleanup, wires);
        if (parts.empty())
            throw std::runtime_error("no edges to export");
        if (parts.size() == 1)
            return parts.front();
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (const TopoDS_Shape &s : parts)
            builder.Add(all, s);
        return all;
    }

}
