// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <compare>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <BRepPrimAPI_MakePrism.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <BRepTools_ReShape.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <Geom2d_Curve.hxx>
#include <ShapeFix_Edge.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_DataMapOfShapeShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include "integrations/netgenocc/shapeexport.hpp"
#include "shape/shape.hpp"

namespace planex2d {

    // a point on a grid of cell q: the same point computed twice (one rounding apart) falls into the same cell
    struct PointKey {
        long long x, y, z;
        auto operator<=>(const PointKey &) const = default;
    };
    inline PointKey KeyOf(const gp_Pnt &p, double q) { return {std::llround(p.X() / q), std::llround(p.Y() / q), std::llround(p.Z() / q)}; }

    // an edge by its two end points and its middle, in either direction
    inline std::array<PointKey, 3> EdgeKey(const TopoDS_Edge &e, double q) {
        TopoDS_Vertex a, b;
        TopExp::Vertices(e, a, b);
        double f, l;
        Handle(Geom_Curve) c = BRep_Tool::Curve(e, f, l);
        PointKey ka = KeyOf(BRep_Tool::Pnt(a), q), kb = KeyOf(BRep_Tool::Pnt(b), q);
        if (kb < ka)
            std::swap(ka, kb);
        return {ka, kb, KeyOf(c->Value(0.5 * (f + l)), q)};
    }

    struct Interface {
        std::vector<TopoDS_Face> caps;
        std::map<std::array<PointKey, 3>, TopoDS_Edge> edgeAt;
        std::map<PointKey, TopoDS_Vertex> vertexAt;
        // every edge at both its ends
        std::multimap<PointKey, TopoDS_Edge> edgesFrom;
    };

    inline Interface InterfaceAt(const Shape &shape, double z, double q, const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup) {
        Interface in;
        Shape s = shape;
        s.Translate(Vector3{0.0, 0.0, z});
        for (const TopoDS_Shape &part : ToOCCParts(s, arcs, fit, cleanup, false)) {
            for (TopExp_Explorer f(part, TopAbs_FACE); f.More(); f.Next())
                in.caps.push_back(TopoDS::Face(f.Current()));
            for (TopExp_Explorer e(part, TopAbs_EDGE); e.More(); e.Next()) {
                const TopoDS_Edge &edge = TopoDS::Edge(e.Current());
                in.edgeAt.emplace(EdgeKey(edge, q), edge);
                TopoDS_Vertex a, b;
                TopExp::Vertices(edge, a, b);
                in.edgesFrom.emplace(KeyOf(BRep_Tool::Pnt(a), q), edge);
                in.edgesFrom.emplace(KeyOf(BRep_Tool::Pnt(b), q), edge);
            }
            for (TopExp_Explorer v(part, TopAbs_VERTEX); v.More(); v.Next())
                in.vertexAt.emplace(KeyOf(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())), q), TopoDS::Vertex(v.Current()));
        }
        return in;
    }

    // the side walls of the prism as its shell uses them (Generated gives them without that orientation), with the
    // properties of the region's edges they come from
    inline std::vector<TopoDS_Face> SideWalls(BRepPrimAPI_MakePrism &prism, const TopoDS_Shape &face) {
        TopTools_IndexedMapOfShape baseEdges, capFaces;
        TopExp::MapShapes(face, TopAbs_EDGE, baseEdges);
        TopTools_DataMapOfShapeShape edgeOf;
        for (int k = 1; k <= baseEdges.Extent(); ++k)
            for (const TopoDS_Shape &w : prism.Generated(baseEdges(k)))
                edgeOf.Bind(w, baseEdges(k));
        TopExp::MapShapes(prism.FirstShape(), TopAbs_FACE, capFaces);
        TopExp::MapShapes(prism.LastShape(), TopAbs_FACE, capFaces);
        std::vector<TopoDS_Face> walls;
        for (TopExp_Explorer f(prism.Shape(), TopAbs_FACE); f.More(); f.Next()) {
            if (capFaces.Contains(f.Current()))
                continue;
            if (edgeOf.IsBound(f.Current()) && netgen::OCCGeometry::HaveProperties(edgeOf(f.Current())))
                netgen::OCCGeometry::GetProperties(f.Current()).Merge(netgen::OCCGeometry::GetProperties(edgeOf(f.Current())));
            walls.push_back(TopoDS::Face(f.Current()));
        }
        return walls;
    }

    // the interface's edges that run along e from its first vertex to its second, oriented as e runs: where the copper of
    // an interface meets the outline, it splits the outline's edge there; none if they do not reach e's end
    inline std::optional<TopoDS_Compound> ChainAlong(const TopoDS_Edge &e, const Interface &in, double q) {
        TopoDS_Vertex a, b;
        TopExp::Vertices(e, a, b);
        double f, l;
        Handle(Geom_Curve) c = BRep_Tool::Curve(e, f, l);
        const PointKey end = KeyOf(BRep_Tool::Pnt(b), q);
        PointKey at = KeyOf(BRep_Tool::Pnt(a), q);
        double along = f;
        BRep_Builder builder;
        TopoDS_Compound chain;
        builder.MakeCompound(chain);
        for (size_t steps = 0; at != end; ++steps) {
            std::optional<TopoDS_Edge> next;
            const auto [lo, hi] = in.edgesFrom.equal_range(at);
            for (auto it = lo; it != hi && !next; ++it) {
                double xf, xl;
                Handle(Geom_Curve) xc = BRep_Tool::Curve(it->second, xf, xl);
                GeomAPI_ProjectPointOnCurve on(xc->Value(0.5 * (xf + xl)), c, f, l);
                // a piece of e's curve, on from where the chain is
                if (on.NbPoints() > 0 && on.LowerDistance() <= q && on.LowerDistanceParameter() > along) {
                    next = it->second;
                    along = on.LowerDistanceParameter();
                }
            }
            if (!next || steps > in.edgesFrom.size())
                return std::nullopt;
            TopoDS_Vertex xa, xb;
            TopExp::Vertices(*next, xa, xb);
            const bool forward = KeyOf(BRep_Tool::Pnt(xa), q) == at;
            builder.Add(chain, next->Oriented(forward ? TopAbs_FORWARD : TopAbs_REVERSED));
            at = KeyOf(BRep_Tool::Pnt(forward ? xb : xa), q);
        }
        return chain;
    }

    // the walls' corners and horizontal edges swapped for those of the interface at their level (above or below); an
    // edge the interface splits for the chain of its pieces (BRepTools_ReShape puts a compound's edges in its place)
    template <class Level>
    void SwapForInterfaces(BRepTools_ReShape &reshape, const std::vector<TopoDS_Face> &walls, const std::vector<Interface> &at,
                           Level level, double q) {
        TopTools_IndexedMapOfShape wallEdges, wallVertices;
        for (const TopoDS_Face &w : walls) {
            TopExp::MapShapes(w, TopAbs_EDGE, wallEdges);
            TopExp::MapShapes(w, TopAbs_VERTEX, wallVertices);
        }
        for (int k = 1; k <= wallVertices.Extent(); ++k) {
            const TopoDS_Vertex &v = TopoDS::Vertex(wallVertices(k));
            const gp_Pnt p = BRep_Tool::Pnt(v);
            const auto &vertexAt = at[level(p)].vertexAt;
            auto it = vertexAt.find(KeyOf(p, q));
            if (it == vertexAt.end())
                throw std::runtime_error("StackedPrisms: a corner of the side walls is no vertex of the interface");
            reshape.Replace(v.Oriented(TopAbs_FORWARD), it->second.Oriented(TopAbs_FORWARD));
        }
        for (int k = 1; k <= wallEdges.Extent(); ++k) {
            const TopoDS_Edge &e = TopoDS::Edge(wallEdges(k));
            TopoDS_Vertex a, b;
            TopExp::Vertices(e, a, b);
            const gp_Pnt pa = BRep_Tool::Pnt(a), pb = BRep_Tool::Pnt(b);
            // the vertical edges stay, their corners are swapped above
            if (std::abs(pa.Z() - pb.Z()) > q)
                continue;
            const auto &edgeAt = at[level(pa)].edgeAt;
            auto it = edgeAt.find(EdgeKey(e, q));
            if (it == edgeAt.end()) {
                const std::optional<TopoDS_Compound> chain = ChainAlong(TopoDS::Edge(e.Oriented(TopAbs_FORWARD)), at[level(pa)], q);
                if (!chain)
                    throw std::runtime_error("StackedPrisms: an edge of the side walls is no edge of the interface");
                reshape.Replace(TopoDS::Edge(e.Oriented(TopAbs_FORWARD)), *chain);
                continue;
            }
            TopoDS_Vertex c, d;
            TopExp::Vertices(it->second, c, d);
            const bool same = KeyOf(BRep_Tool::Pnt(c), q) == KeyOf(pa, q);
            reshape.Replace(TopoDS::Edge(e.Oriented(TopAbs_FORWARD)), same ? it->second.Oriented(TopAbs_FORWARD) : it->second.Oriented(TopAbs_REVERSED));
        }
    }

    // a stack of prisms of one region: interface k (faces, the copper of a PCB layer glued into the region) lies at
    // z[k] from the top, prism i between z[i] and z[i + 1] is the region extruded. Every interface is exported once and is
    // the top of the prism below and the bottom of the one above; the walls' edges and corners are the interfaces' own
    inline TopoDS_Shape StackedPrisms(const ShapeList &interfaces, const Shape &region, const std::vector<double> &z,
                                      const std::vector<std::string> &names, const ArcFit &arcs = config::state->arc_fit,
                                      const SplineFit &fit = config::state->spline_fit, const Cleanup &cleanup = config::state->cleanup) {
        const size_t n = interfaces.size();
        if (n < 2 || z.size() != n || names.size() != n - 1)
            throw std::invalid_argument("StackedPrisms: n interfaces need n heights and n - 1 names");
        // coordinates one rounding apart share a cell, distinct points of the geometry are far more than this apart
        const double q = 1e-9 * std::max(1.0, region.Box().NormInf());
        std::vector<Interface> at;
        for (size_t k = 0; k < n; ++k)
            at.push_back(InterfaceAt(interfaces[k], z[k], q, arcs, fit, cleanup));
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (size_t i = 0; i + 1 < n; ++i) {
            Shape base = region;
            base.Translate(Vector3{0.0, 0.0, z[i + 1]});
            const TopoDS_Shape face = ToOCCShape(base, arcs, fit, cleanup, false);
            BRepPrimAPI_MakePrism prism(face, gp_Vec(0.0, 0.0, z[i] - z[i + 1]));
            const std::vector<TopoDS_Face> walls = SideWalls(prism, face);
            BRepTools_ReShape reshape;
            SwapForInterfaces(reshape, walls, at, [&](const gp_Pnt &p) { return std::abs(p.Z() - z[i]) < std::abs(p.Z() - z[i + 1]) ? i : i + 1; }, q);
            TopoDS_Shell shell;
            builder.MakeShell(shell);
            ShapeFix_Edge fix;
            for (const TopoDS_Face &w : walls) {
                const TopoDS_Face swapped = TopoDS::Face(reshape.Apply(w));
                // the interfaces' edges know their curves on the flat caps only: a wall on a cylinder needs them on its
                // surface too, added to the same edges so they stay shared
                for (TopExp_Explorer e(swapped, TopAbs_EDGE); e.More(); e.Next()) {
                    double f, l;
                    if (BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()), swapped, f, l).IsNull())
                        fix.FixAddPCurve(TopoDS::Edge(e.Current()), swapped, false);
                }
                if (netgen::OCCGeometry::HaveProperties(w))
                    netgen::OCCGeometry::GetProperties(swapped).Merge(netgen::OCCGeometry::GetProperties(w));
                builder.Add(shell, swapped);
            }
            for (const TopoDS_Face &f : at[i].caps)
                builder.Add(shell, f);
            for (const TopoDS_Face &f : at[i + 1].caps)
                builder.Add(shell, f.Reversed());
            shell.Closed(true);
            TopoDS_Solid solid;
            builder.MakeSolid(solid);
            builder.Add(solid, shell);
            netgen::OCCGeometry::GetProperties(solid).name = names[i];
            builder.Add(all, solid);
        }
        return all;
    }

}
