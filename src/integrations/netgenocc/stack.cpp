// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/stack.hpp"

namespace planex2d {

    Interface InterfaceAt(const Shape &shape, double z, double q, const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup) {
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
    std::vector<TopoDS_Face> SideWalls(BRepPrimAPI_MakePrism &prism, const TopoDS_Shape &face) {
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

    std::optional<TopoDS_Compound> ChainAlong(const TopoDS_Edge &e, const Interface &in, double q) {
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

    bool IsOnAnySide(const Wire &region, const std::vector<size_t> &sides, const Vertex &p, double tol) {
        for (size_t i : sides) {
            const bool on = std::visit([&](const auto &e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (IsCircle<E>)
                    return e.OnCarrier(p, tol);
                else if constexpr (std::is_same_v<E, Spline>)
                    return false;
                else
                    return e.Locate(p, tol) != Location::Off;
            }, region[i]);
            if (on)
                return true;
        }
        return false;
    }

    static void SplitCirclesAtEnds(Wire &r, const ShapeList &interfaces, double tol) {
        std::vector<Vertex> ends;
        for (const Shape &s : interfaces)
            for (const Edge &e : s.Edges()) {
                ends.push_back(StartOf(e));
                if (!IsClosedEdge(e))
                    ends.push_back(EndOf(e));
            }
        std::vector<std::vector<Vertex>> on(r.Size());
        Grid endGrid = Grid::Of(ends, tol);
        std::vector<uint32_t> nearby;
        for (size_t i = 0; i < r.Size(); ++i)
            if (const Circle *c = std::get_if<Circle>(&r[i])) {
                nearby.clear();
                endGrid.InBox(c->Box().Grown(tol), nearby);
                for (uint32_t k : nearby)
                    if (c->OnCarrier(ends[k], tol) && !ends[k].IsClose(c->Start(), tol))
                        on[i].push_back(ends[k]);
            }
        r.SplitAt(on, tol);
    }

    static void SplitAtCorners(ShapeList &interfaces, const std::vector<Vertex> &corners, Grid &cornerGrid, double tol) {
        std::vector<uint32_t> nearby;
        for (Shape &s : interfaces) {
            Wire &w = s.Edges();
            std::vector<std::vector<Vertex>> inside(w.Size());
            for (size_t i = 0; i < w.Size(); ++i) {
                nearby.clear();
                cornerGrid.InBox(BoxOf(w[i]).Grown(tol), nearby);
                std::visit([&](const auto &e) {
                    using E = std::decay_t<decltype(e)>;
                    for (uint32_t k : nearby) {
                        const Vertex &p = corners[k];
                        if constexpr (IsCircle<E>) {
                            if (e.OnCarrier(p, tol) && !p.IsClose(e.Start(), tol))
                                inside[i].push_back(p);
                        } else if constexpr (!std::is_same_v<E, Spline>) {
                            if (e.Locate(p, tol) == Location::Inside)
                                inside[i].push_back(p);
                        }
                    }
                }, w[i]);
            }
            w.SplitAt(inside, tol);
        }
    }

    static void SnapToCorners(ShapeList &interfaces, const Wire &r, const std::vector<Vertex> &corners, Grid &cornerGrid, double tol, double snap) {
        std::vector<uint32_t> nearby;
        std::vector<std::vector<size_t>> sidesAt(corners.size());
        for (size_t i = 0; i < r.Size(); ++i)
            for (const Vertex &p : {StartOf(r[i]), EndOf(r[i])})
                cornerGrid.ForCell(p, [&](size_t k) {
                    if (corners[k] == p)
                        sidesAt[k].push_back(i);
                });
        for (Shape &s : interfaces) {
            std::vector<std::pair<Vertex, Vertex>> moves;
            for (const Vertex &p : s.Edges().Endpoints()) {
                nearby.clear();
                cornerGrid.InBox(BBox::Of(p, p).Grown(snap), nearby);
                for (uint32_t k : nearby)
                    if (corners[k] != p && p.IsClose(corners[k], snap) && IsOnAnySide(r, sidesAt[k], p, tol)) {
                        moves.emplace_back(p, corners[k]);
                        break;
                    }
            }
            s.MoveVertices(moves, tol, snap);
        }
    }

    void ShareCorners(Shape &region, ShapeList &interfaces, double tol, double snap) {
        Wire &r = region.Edges();
        SplitCirclesAtEnds(r, interfaces, tol);
        const std::vector<Vertex> corners = r.Endpoints();
        Grid cornerGrid = Grid::Of(corners, tol);
        SplitAtCorners(interfaces, corners, cornerGrid, tol);
        if (snap > tol)
            SnapToCorners(interfaces, r, corners, cornerGrid, tol, snap);
    }

    static TopoDS_Solid PrismBetween(const Shape &shared, const std::vector<Interface> &at, size_t i, const std::vector<double> &z, double q, const ArcFit &arcs,
                                     const SplineFit &fit, const Cleanup &inner) {
        BRep_Builder builder;
        Shape base = shared;
        base.Translate(Vector3{0.0, 0.0, z[i + 1]});
        const TopoDS_Shape face = ToOCCShape(base, arcs, fit, inner, false);
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
        return solid;
    }

    // a stack of prisms of one region: interface k (faces, the copper of a PCB layer glued into the region) lies at
    // z[k] from the top, prism i between z[i] and z[i + 1] is the region extruded. Every interface is exported once and is
    // the top of the prism below and the bottom of the one above; the walls' edges and corners are the interfaces' own
    TopoDS_Shape StackedPrisms(const ShapeList &interfaces, const Shape &region, const std::vector<double> &z,
                               const std::vector<std::string> &names, const ArcFit &arcs,
                               const SplineFit &fit, const Cleanup &cleanup) {
        const size_t n = interfaces.size();
        if (n < 2 || z.size() != n || names.size() != n - 1)
            throw std::invalid_argument("StackedPrisms: n interfaces need n heights and n - 1 names");
        // coordinates one rounding apart share a cell, distinct points of the geometry are far more than this apart
        const double q = 1e-9 * std::max(1.0, region.Box().NormInf());
        Shape shared = region;
        ShapeList faces = interfaces;
        Cleanup inner = cleanup;
        inner.merge_slivers_thinner = 0.0;
        if (cleanup.merge_slivers_thinner > 0.0)
            for (Shape &f : faces) {
                Shape merged = f.WithoutSlivers(cleanup.merge_slivers_thinner, true);
                if (cleanup.remove_faces_smaller > 0.0)
                    merged = merged.WithoutSmallFaces(cleanup.remove_faces_smaller, cleanup.merge_small_faces_instead);
                merged.Ids() = f.Ids();
                f = std::move(merged);
            }
        ShareCorners(shared, faces, q, cleanup.remove_edges_smaller);
        std::vector<Interface> at;
        for (size_t k = 0; k < n; ++k)
            at.push_back(InterfaceAt(faces[k], z[k], q, arcs, fit, inner));
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (size_t i = 0; i + 1 < n; ++i) {
            const TopoDS_Solid solid = PrismBetween(shared, at, i, z, q, arcs, fit, inner);
            netgen::OCCGeometry::GetProperties(solid).name = names[i];
            builder.Add(all, solid);
        }
        return all;
    }

}
