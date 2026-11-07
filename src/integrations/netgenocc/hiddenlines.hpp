// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <BRepLib.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax2.hxx>

#include "core/grid.hpp"
#include "integrations/netgenocc/project.hpp"

namespace planex2d {

    // OCC's hidden-line projector seeing along the normal of axes, in axes' frame
    inline HLRAlgo_Projector ProjectorOf(const AxisSystem &axes) {
        return HLRAlgo_Projector(gp_Ax2(gp_Pnt(ToOCC(axes.Origin())), gp_Dir(ToOCC(axes.Normal())), gp_Dir(ToOCC(axes.XDirection()))));
    }

    // the edges of a result of OCC's hidden-line algorithm, in the projector's frame on its xy plane: OCC gives them as
    // curves on that plane, without 3d curves until they are built
    inline std::vector<TopoDS_Edge> ResultEdges(TopoDS_Shape c) {
        std::vector<TopoDS_Edge> out;
        if (c.IsNull() || !BRepLib::BuildCurves3d(c))
            return out;
        for (TopExp_Explorer e(c, TopAbs_EDGE); e.More(); e.Next())
            out.push_back(TopoDS::Edge(e.Current()));
        return out;
    }

    inline void AddResultEdges(const TopoDS_Shape &c, Wire &w) {
        for (const TopoDS_Edge &e : ResultEdges(c))
            for (Edge &x : ProjectEdge(e, AxisSystem(), nullptr).edges)
                w.Add(std::move(x));
    }

    // the point of surface s at (u, v) moved onto its exact fold seen along n: the least Newton steps on (Su x Sv).n = 0
    // (the fold: where the surface turns from the viewer); none if they do not settle
    inline std::optional<gp_Pnt> OnFold(const Handle(Geom_Surface) &s, double u, double v, const gp_Vec &n) {
        for (int k = 0; k < 30; ++k) {
            gp_Pnt q;
            gp_Vec su, sv, suu, svv, suv;
            s->D2(u, v, q, su, sv, suu, svv, suv);
            const gp_Vec normal = su ^ sv;
            const double g = normal.Dot(n);
            // the normal square to the view up to rounding: on the fold
            if (std::abs(g) <= 1e-13 * normal.Magnitude())
                return q;
            const double gu = ((suu ^ sv) + (su ^ suv)).Dot(n), gv = ((suv ^ sv) + (su ^ svv)).Dot(n);
            const double norm2 = gu * gu + gv * gv;
            if (!(norm2 > 0.0))
                return std::nullopt;
            u -= g * gu / norm2;
            v -= g * gv / norm2;
        }
        return std::nullopt;
    }

    // a surface whose fold lines OCC traces only approximately: every surface but the plane and the quadrics (their
    // folds OCC gives exactly) and the torus (within about 1e-7 of its size)
    inline bool HasTracedFolds(const TopoDS_Face &f) {
        switch (BRepAdaptor_Surface(f).GetType()) {
        case GeomAbs_Plane:
        case GeomAbs_Cylinder:
        case GeomAbs_Cone:
        case GeomAbs_Sphere:
        case GeomAbs_Torus: return false;
        default: return true;
        }
    }

    // a face whose fold lines OCC traces, with what finds points on its surface (set up once: OCC's projection is slow
    // to start on a long b-spline surface) and its parameter bounds
    struct TracedFace {
        TopoDS_Face face;
        Handle(ShapeAnalysis_Surface) surface;
        double u0, u1, v0, v1;
        Bnd_Box box;
    };

    // the face among faces that the 3d curve c (t0 to t1) of OCC's fold line lies on: its middle within the face's
    // parameter bounds and within tol of it
    inline const TracedFace *FaceUnder(const Handle(Geom_Curve) &c, double t0, double t1, const std::vector<TracedFace> &faces, double tol) {
        const gp_Pnt middle = c->Value(0.5 * (t0 + t1));
        for (const TracedFace &f : faces) {
            if (f.box.IsOut(middle))
                continue;
            const gp_Pnt2d uv = f.surface->ValueOfUV(middle, tol);
            if (f.surface->Gap() <= tol && uv.X() >= f.u0 - tol && uv.X() <= f.u1 + tol && uv.Y() >= f.v0 - tol && uv.Y() <= f.v1 + tol)
                return &f;
        }
        return nullptr;
    }

    // OCC's fold line c (t0 to t1) on face f moved onto the exact fold point by point (each found on the surface from the
    // one before) and fitted again through the points, in the plane of axes; a point that does not settle within tol of
    // OCC's keeps OCC's place
    inline void AddRefinedFold(const Handle(Geom_Curve) &c, double t0, double t1, const TracedFace &f, const AxisSystem &axes,
                               double tol, const Properties &props, Wire &w) {
        // a point every 2e-3 of the size (tol is 1e-3 of it), 8 to 128: the spline through them stays within about 1e-8
        double length = 0.0;
        for (int k = 0; k < 8; ++k)
            length += c->Value(t0 + (t1 - t0) * k / 8.0).Distance(c->Value(t0 + (t1 - t0) * (k + 1) / 8.0));
        const int samples = std::clamp((int)(length / (2.0 * tol)), 8, 128);
        const gp_Vec n(ToOCC(axes.Normal()));
        std::vector<Vertex> pts;
        gp_Pnt2d uv = f.surface->ValueOfUV(c->Value(t0), tol);
        for (int k = 0; k <= samples; ++k) {
            const gp_Pnt p = c->Value(t0 + (t1 - t0) * k / samples);
            if (k > 0)
                uv = f.surface->NextValueOfUV(uv, p, tol);
            // the steps correct OCC's tracing; one that lands farther off has found another fold of the surface
            const std::optional<gp_Pnt> q = OnFold(f.surface->Surface(), uv.X(), uv.Y(), n);
            const Vertex x = axes.ToHV(Vector3Of((q && q->Distance(p) <= tol ? *q : p).XYZ()));
            // two samples settling on one point (a short stretch): it counts once
            if (pts.empty() || !x.IsClose(pts.back(), 1e-6 * tol))
                pts.push_back(x);
        }
        if (pts.size() < 2)
            return;
        const double same = Tol(NormInf(pts.front()) + NormInf(pts.back()));
        const bool closed = pts.front().IsClose(pts.back(), same);
        if (closed)
            pts.pop_back();
        for (Spline &piece : Spline::Through(pts, closed, same, {}, SplineStyle::Averaged)) {
            piece.props = props;
            w.Add(std::move(piece));
        }
    }

    // the fold lines, visible or hidden: as OCC gives them on the plane, or, where a face's folds are traced (traced),
    // from OCC's 3d result, those on such a face refined (within near of it), the others projected. ids: each is Fold.N,
    // n counting them
    inline void AddFolds(HLRBRep_HLRToShape &r, bool visible, const std::vector<TracedFace> &traced, double nearTol, const AxisSystem &axes,
                         bool ids, size_t &n, Wire &w) {
        const auto props = [&]() -> Properties {
            const std::string id = "Fold." + std::to_string(++n);
            return ids ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.id = id; }) : nullptr;
        };
        if (traced.empty()) {
            for (const TopoDS_Edge &e : ResultEdges(visible ? r.OutLineVCompound() : r.OutLineHCompound()))
                for (Edge &x : ProjectEdge(e, AxisSystem(), props()).edges)
                    w.Add(std::move(x));
            return;
        }
        for (TopExp_Explorer e(r.CompoundOfEdges(HLRBRep_OutLine, visible, true), TopAbs_EDGE); e.More(); e.Next()) {
            const TopoDS_Edge &edge = TopoDS::Edge(e.Current());
            double t0, t1;
            const Handle(Geom_Curve) c = BRep_Tool::Curve(edge, t0, t1);
            if (c.IsNull())
                continue;
            const Properties p = props();
            if (const TracedFace *f = FaceUnder(c, t0, t1, traced, nearTol))
                AddRefinedFold(c, t0, t1, *f, axes, nearTol, p, w);
            else
                for (Edge &x : ProjectEdge(edge, axes, p).edges)
                    w.Add(std::move(x));
        }
    }

    // OCC's hidden-line algorithm run on shape seen along the normal of axes
    inline HLRBRep_HLRToShape HiddenLines(const TopoDS_Shape &shape, const AxisSystem &axes) {
        Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
        algo->Add(shape);
        algo->Projector(ProjectorOf(axes));
        algo->Update();
        algo->Hide();
        return HLRBRep_HLRToShape(algo);
    }

    // the faces of shape whose fold lines OCC traces, each with its box grown by near (a fold line farther off lies on
    // another face)
    inline std::vector<TracedFace> TracedFaces(const TopoDS_Shape &shape, double nearTol) {
        std::vector<TracedFace> traced;
        for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
            const TopoDS_Face &f = TopoDS::Face(e.Current());
            if (!HasTracedFolds(f))
                continue;
            TracedFace x{f, new ShapeAnalysis_Surface(BRep_Tool::Surface(f)), 0.0, 0.0, 0.0, 0.0, {}};
            BRepTools::UVBounds(f, x.u0, x.u1, x.v0, x.v1);
            BRepBndLib::Add(f, x.box);
            x.box.Enlarge(nearTol);
            traced.push_back(x);
        }
        return traced;
    }

    // how far OCC's traced fold curve may stray from its surface: far more than it does
    inline double FoldNear(const TopoDS_Shape &shape) {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        return 1e-3 * std::sqrt(box.SquareExtent());
    }

    // the fold lines of shape's curved faces seen along the normal of axes, in its coordinates, visible and hidden: exact
    // where the surface has exact ones (lines of a cylinder or cone, the circle of a sphere), the torus's as OCC traces
    // them (within about 1e-7 of its size), the others refined onto their surface. ids: each is Fold.N
    inline Wire FoldLines(const TopoDS_Shape &shape, const AxisSystem &axes, bool ids) {
        HLRBRep_HLRToShape r = HiddenLines(shape, axes);
        const double nearTol = FoldNear(shape);
        const std::vector<TracedFace> traced = TracedFaces(shape, nearTol);
        Wire folds(axes);
        size_t n = 0;
        AddFolds(r, true, traced, nearTol, axes, ids, n, folds);
        AddFolds(r, false, traced, nearTol, axes, ids, n, folds);
        return folds;
    }

    // whether something of a shape lies in front of a point, for a viewer along the normal of axes: OCC's triangulation of
    // it within deflection, its triangles in a grid over the view (every line of sight runs along the normal, so the
    // triangles in front of a point are those over its place in the view)
    class DepthCheck {
        public:
            DepthCheck(const TopoDS_Shape &shape, const AxisSystem &axes, double deflection) : axes(axes) {
                BRepMesh_IncrementalMesh mesh(shape, deflection);
                std::vector<BBox> boxes;
                for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
                    TopLoc_Location at;
                    const Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(TopoDS::Face(e.Current()), at);
                    if (t.IsNull())
                        continue;
                    for (int k = 1; k <= t->NbTriangles(); ++k) {
                        int i[3];
                        t->Triangle(k).Get(i[0], i[1], i[2]);
                        Triangle x;
                        for (int j = 0; j < 3; ++j) {
                            const Vector3 p = Vector3Of(t->Node(i[j]).Transformed(at.Transformation()).XYZ());
                            x.p[j] = axes.ToHV(p);
                            x.depth[j] = Dot(p - axes.Origin(), axes.Normal());
                        }
                        triangles.push_back(x);
                        boxes.push_back(BBox::Of(x.p[0], x.p[1]));
                        boxes.back().Grow(x.p[2]);
                    }
                }
                if (!boxes.empty())
                    grid.emplace(Grid::OfBoxes(boxes));
            }
            // something of the shape lies more than tol in front of p
            bool IsHidden(const gp_Pnt &p, double tol) {
                if (!grid)
                    return false;
                const Vector3 q = Vector3Of(p.XYZ());
                const Vertex x = axes.ToHV(q);
                const double depth = Dot(q - axes.Origin(), axes.Normal());
                found.clear();
                grid->InBox(BBox::Of(x, x), found);
                for (const uint32_t k : found) {
                    const Triangle &t = triangles[k];
                    const Vertex u = t.p[1] - t.p[0], v = t.p[2] - t.p[0], w = x - t.p[0];
                    const double area = Cross(u, v);
                    // a triangle seen edge-on covers nothing
                    if (std::abs(area) <= 1e-14 * (Dot(u, u) + Dot(v, v)))
                        continue;
                    const double b = Cross(w, v) / area, c = Cross(u, w) / area, a = 1.0 - b - c;
                    if (a >= 0.0 && b >= 0.0 && c >= 0.0 && a * t.depth[0] + b * t.depth[1] + c * t.depth[2] > depth + tol)
                        return true;
                }
                return false;
            }

        private:
            struct Triangle {
                Vertex p[3];
                double depth[3];
            };
            AxisSystem axes;
            std::vector<Triangle> triangles;
            std::optional<Grid> grid;
            std::vector<uint32_t> found;
    };

    struct Stretch {
        double from, to;
        bool hidden;
    };

    // the stretches from t0 to t1 of a line and whether each is hidden by hidden(t), from samples points along it; where
    // two points disagree, the change found by halving
    template <class F>
    std::vector<Stretch> Stretches(double t0, double t1, int samples, F hidden) {
        std::vector<Stretch> out;
        double from = t0, last = t0 + (t1 - t0) * 0.5 / samples;
        bool before = hidden(last);
        for (int k = 1; k < samples; ++k) {
            const double t = t0 + (t1 - t0) * (k + 0.5) / samples;
            const bool h = hidden(t);
            if (h != before) {
                double a = last, b = t;
                // halvings to a millionth of the line, far below what a drawing shows
                for (int i = 0; i < 20 && b - a > 1e-6 * (t1 - t0); ++i) {
                    const double m = 0.5 * (a + b);
                    (hidden(m) == before ? a : b) = m;
                }
                out.push_back({from, 0.5 * (a + b), before});
                from = 0.5 * (a + b);
            }
            last = t;
            before = h;
        }
        out.push_back({from, t1, before});
        return out;
    }

    // whether the point p of a fold line (its tangent t there) is hidden: a line of sight from the fold itself only grazes
    // the surface, so those from aside of it on both sides; the one outside clears the surface, the one inside meets it:
    // hidden when both meet the shape (more than tol in front)
    inline bool IsFoldHidden(DepthCheck &depth, const gp_Pnt &p, const gp_Vec &t, const gp_Dir &n, double aside, double tol) {
        gp_Vec across = gp_Vec(n) ^ t;
        if (!(across.Magnitude() > 0.0))
            return depth.IsHidden(p, tol);
        across.Normalize();
        return depth.IsHidden(p.Translated(across * aside), tol) && depth.IsHidden(p.Translated(across * -aside), tol);
    }

    // the edges of hidden that lie on edges of visible (within tol at five points along them) left out: a drawing shows
    // the visible line there (OCC gives the edge behind it too, the back face's outline behind the front face's)
    inline void DropCovered(Wire &hidden, const Wire &visible, double tol) {
        std::vector<BBox> boxes;
        for (const Edge &e : visible)
            boxes.push_back(BoxOf(e).Grown(tol));
        const auto covered = [&](const Vertex &p) {
            for (size_t k = 0; k < visible.Size(); ++k)
                if (boxes[k].IsInside(p) && Distance(p, visible[k]) <= tol)
                    return true;
            return false;
        };
        std::vector<bool> drop(hidden.Size(), false);
        for (size_t i = 0; i < hidden.Size(); ++i) {
            bool all = true;
            for (int k = 0; k < 5 && all; ++k)
                all = covered(PointAtFraction(hidden[i], (k + 0.5) / 5.0));
            drop[i] = all;
        }
        hidden.Erase(drop);
    }

    struct ViewEdges {
        Wire visible, hidden, smooth;
    };

    // the lines of a view of shape along the normal of axes, in its coordinates: OCC's exact hidden-line algorithm's
    // edges and fold lines in 3d, projected (the fold lines OCC traces only approximately refined onto the exact surface);
    // the smooth edges (where faces meet tangentially) visible only. OCC's visibility of an edge is checked by a ray from
    // its middle (a fold line's from beside it, IsFoldHidden), and where the ray disagrees the line is split by rays
    // along it (OCC's classification fails where many outlines fall together, a spring)
    inline ViewEdges ViewLines(const TopoDS_Shape &shape, const AxisSystem &axes) {
        HLRBRep_HLRToShape r = HiddenLines(shape, axes);
        const double nearTol = FoldNear(shape);
        const std::vector<TracedFace> traced = TracedFaces(shape, nearTol);
        // the triangulation lies within deflection of the faces: a face less than two of that in front of a point is the
        // point's own; a fold's lines of sight start three of it aside
        const double deflection = 0.5 * nearTol, tol = 2.0 * deflection, aside = 3.0 * deflection;
        DepthCheck depth(shape, axes, deflection);
        const gp_Dir n(ToOCC(axes.Normal()));
        ViewEdges out{Wire(axes), Wire(axes), Wire(axes)};
        for (const HLRBRep_TypeOfResultingEdge type : {HLRBRep_Sharp, HLRBRep_Rg1Line, HLRBRep_OutLine})
            for (const bool visible : {true, false})
                for (TopExp_Explorer e(r.CompoundOfEdges(type, visible, true), TopAbs_EDGE); e.More(); e.Next()) {
                    double t0, t1;
                    const Handle(Geom_Curve) c = BRep_Tool::Curve(TopoDS::Edge(e.Current()), t0, t1);
                    if (c.IsNull())
                        continue;
                    const bool fold = type == HLRBRep_OutLine;
                    const TracedFace *f = fold ? FaceUnder(c, t0, t1, traced, nearTol) : nullptr;
                    // the point found on the surface for the last test: the next one starts from it
                    std::optional<gp_Pnt2d> last;
                    const auto hidden = [&](double t) {
                        if (!fold)
                            return depth.IsHidden(c->Value(t), tol);
                        gp_Pnt p;
                        gp_Vec tangent;
                        c->D1(t, p, tangent);
                        // on a face whose folds OCC traces, its point on the exact fold
                        if (f) {
                            const gp_Pnt2d uv = last ? f->surface->NextValueOfUV(*last, p, nearTol) : f->surface->ValueOfUV(p, nearTol);
                            last = uv;
                            if (const std::optional<gp_Pnt> q = OnFold(f->surface->Surface(), uv.X(), uv.Y(), gp_Vec(n)); q && q->Distance(p) <= nearTol)
                                p = *q;
                        }
                        return IsFoldHidden(depth, p, tangent, n, aside, tol);
                    };
                    // a fold OCC traces may run behind other parts of its surface where OCC does not split it (a spring's
                    // back behind its front coils): checked along it every 2e-2 of the size; any other line by its middle
                    std::vector<Stretch> stretches;
                    if (f) {
                        double length = 0.0;
                        for (int k = 0; k < 16; ++k)
                            length += c->Value(t0 + (t1 - t0) * k / 16.0).Distance(c->Value(t0 + (t1 - t0) * (k + 1) / 16.0));
                        stretches = Stretches(t0, t1, std::clamp((int)(length / (20.0 * nearTol)), 4, 200), hidden);
                    } else
                        stretches = hidden(0.5 * (t0 + t1)) != visible ? std::vector<Stretch>{{t0, t1, !visible}} : Stretches(t0, t1, 4, hidden);
                    for (const Stretch &s : stretches) {
                        if (type == HLRBRep_Rg1Line && s.hidden)
                            continue;
                        Wire &w = type == HLRBRep_Rg1Line ? out.smooth : s.hidden ? out.hidden : out.visible;
                        if (f)
                            AddRefinedFold(c, s.from, s.to, *f, axes, nearTol, nullptr, w);
                        else
                            for (Edge &x : ProjectEdge(BRepBuilderAPI_MakeEdge(c, s.from, s.to).Edge(), axes, nullptr).edges)
                                w.Add(std::move(x));
                    }
                }
        // the same projected point up to rounding: 1e-9 of the size
        DropCovered(out.hidden, out.visible, 1e-6 * nearTol);
        return out;
    }

    struct Shading {
        std::vector<std::array<Vertex, 3>> triangles;
        std::vector<double> light;
        std::vector<std::array<double, 3>> depth;
    };

    // a pictorial's shading: the triangles of OCC's triangulation of shape within deflection that face a viewer along
    // the normal of axes, in its coordinates, back to front, each with its light (how squarely it faces a light from the
    // viewer's upper left: 0 edge-on, 1 square)
    inline Shading ShadedTriangles(const TopoDS_Shape &shape, const AxisSystem &axes, double deflection) {
        BRepMesh_IncrementalMesh mesh(shape, deflection);
        TopTools_IndexedMapOfShape bounding;
        for (TopExp_Explorer s(shape, TopAbs_SOLID); s.More(); s.Next())
            TopExp::MapShapes(s.Current(), TopAbs_FACE, bounding);
        const gp_Vec n(ToOCC(axes.Normal())), toLight = (n + gp_Vec(ToOCC(axes.YDirection() - axes.XDirection())) * 0.5).Normalized();
        std::vector<std::pair<double, size_t>> order;
        Shading out;
        for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
            const TopoDS_Face &f = TopoDS::Face(e.Current());
            TopLoc_Location at;
            const Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(f, at);
            if (t.IsNull())
                continue;
            const bool reversed = f.Orientation() == TopAbs_REVERSED, bothSides = !bounding.Contains(f);
            for (int k = 1; k <= t->NbTriangles(); ++k) {
                int a, b, c;
                t->Triangle(k).Get(a, b, c);
                const gp_Pnt p[3] = {t->Node(a).Transformed(at.Transformation()), t->Node(b).Transformed(at.Transformation()),
                                     t->Node(c).Transformed(at.Transformation())};
                gp_Vec normal = gp_Vec(p[0], p[1]) ^ gp_Vec(p[0], p[2]);
                if (bothSides ? normal.Dot(n) < 0.0 : reversed)
                    normal.Reverse();
                if (!(normal.Dot(n) > 0.0))
                    continue;
                std::array<Vertex, 3> flat;
                std::array<double, 3> depth;
                for (int i = 0; i < 3; ++i) {
                    flat[i] = axes.ToHV(Vector3Of(p[i].XYZ()));
                    depth[i] = gp_Vec(p[i].XYZ()).Dot(n);
                }
                order.push_back({(depth[0] + depth[1] + depth[2]) / 3.0, out.triangles.size()});
                out.triangles.push_back(flat);
                out.light.push_back(std::max(0.0, normal.Normalized().Dot(toLight)));
                out.depth.push_back(depth);
            }
        }
        std::sort(order.begin(), order.end());
        Shading sorted;
        for (const auto &[depth, k] : order) {
            sorted.triangles.push_back(out.triangles[k]);
            sorted.light.push_back(out.light[k]);
            sorted.depth.push_back(out.depth[k]);
        }
        return sorted;
    }

    struct ShadedImage {
        Vertex low;
        size_t width = 0, height = 0;
        std::vector<double> light;
    };

    inline ShadedImage Rasterized(const Shading &s, double pixel) {
        ShadedImage out;
        if (s.triangles.empty())
            return out;
        Vertex low = s.triangles[0][0], high = low;
        for (const std::array<Vertex, 3> &t : s.triangles)
            for (const Vertex &p : t) {
                low = {std::min(low.x, p.x), std::min(low.y, p.y)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y)};
            }
        out.low = low;
        out.width = (size_t)std::ceil((high.x - low.x) / pixel) + 1;
        out.height = (size_t)std::ceil((high.y - low.y) / pixel) + 1;
        out.light.assign(out.width * out.height, std::numeric_limits<double>::quiet_NaN());
        std::vector<double> nearest(out.light.size(), -std::numeric_limits<double>::infinity());
        // a pixel's centre on an edge two triangles share belongs to both, up to rounding
        const double onEdge = -1e-9;
        for (size_t k = 0; k < s.triangles.size(); ++k) {
            const std::array<Vertex, 3> &t = s.triangles[k];
            const double area = Cross(t[1] - t[0], t[2] - t[0]);
            if (area == 0.0)
                continue;
            const size_t i0 = (size_t)((std::min({t[0].x, t[1].x, t[2].x}) - low.x) / pixel);
            const size_t i1 = std::min(out.width - 1, (size_t)((std::max({t[0].x, t[1].x, t[2].x}) - low.x) / pixel));
            const size_t j0 = (size_t)((std::min({t[0].y, t[1].y, t[2].y}) - low.y) / pixel);
            const size_t j1 = std::min(out.height - 1, (size_t)((std::max({t[0].y, t[1].y, t[2].y}) - low.y) / pixel));
            for (size_t j = j0; j <= j1; ++j)
                for (size_t i = i0; i <= i1; ++i) {
                    const Vertex p = low + Vertex{((double)i + 0.5) * pixel, ((double)j + 0.5) * pixel};
                    const double w0 = Cross(t[1] - p, t[2] - p) / area, w1 = Cross(t[2] - p, t[0] - p) / area, w2 = 1.0 - w0 - w1;
                    if (w0 < onEdge || w1 < onEdge || w2 < onEdge)
                        continue;
                    const double z = w0 * s.depth[k][0] + w1 * s.depth[k][1] + w2 * s.depth[k][2];
                    const size_t at = (out.height - 1 - j) * out.width + i;
                    if (z > nearest[at]) {
                        nearest[at] = z;
                        out.light[at] = s.light[k];
                    }
                }
        }
        return out;
    }

}
