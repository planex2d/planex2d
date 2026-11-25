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

#include "planex/integrations/netgenocc/shapeexport.hpp"
#include "planex/shape/shape.hpp"

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
        std::multimap<PointKey, TopoDS_Edge> edgesFrom;
    };

    Interface InterfaceAt(const Shape &shape, double z, double q, const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup);

    std::vector<TopoDS_Face> SideWalls(BRepPrimAPI_MakePrism &prism, const TopoDS_Shape &face);

    std::optional<TopoDS_Compound> ChainAlong(const TopoDS_Edge &e, const Interface &in, double q);

    inline std::string PointText(const gp_Pnt &p) {
        return "(" + SolvedText(p.X()) + ", " + SolvedText(p.Y()) + ", " + SolvedText(p.Z()) + ")";
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
                throw std::runtime_error("StackedPrisms: a corner of the side walls is no vertex of the interface: " + PointText(p));
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
                    throw std::runtime_error("StackedPrisms: an edge of the side walls is no edge of the interface: from " +
                                             PointText(pa) + " to " + PointText(pb));
                reshape.Replace(TopoDS::Edge(e.Oriented(TopAbs_FORWARD)), *chain);
                continue;
            }
            TopoDS_Vertex c, d;
            TopExp::Vertices(it->second, c, d);
            const bool same = KeyOf(BRep_Tool::Pnt(c), q) == KeyOf(pa, q);
            reshape.Replace(TopoDS::Edge(e.Oriented(TopAbs_FORWARD)), same ? it->second.Oriented(TopAbs_FORWARD) : it->second.Oriented(TopAbs_REVERSED));
        }
    }

    bool IsOnAnySide(const Wire &region, const std::vector<size_t> &sides, const Vertex &p, double tol);

    void ShareCorners(Shape &region, ShapeList &interfaces, double tol, double snap);

    TopoDS_Shape StackedPrisms(const ShapeList &interfaces, const Shape &region, const std::vector<double> &z,
                               const std::vector<std::string> &names, const ArcFit &arcs = config::state->arc_fit,
                               const SplineFit &fit = config::state->spline_fit, const Cleanup &cleanup = config::state->cleanup);

}
