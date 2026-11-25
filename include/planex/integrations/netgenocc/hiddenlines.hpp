// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <array>
#include <optional>
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

#include "planex/core/grid.hpp"
#include "planex/integrations/netgenocc/project.hpp"

namespace planex2d {

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

    std::optional<gp_Pnt> OnFold(const Handle(Geom_Surface) &s, double u, double v, const gp_Vec &n);

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

    const TracedFace *FaceUnder(const Handle(Geom_Curve) &c, double t0, double t1, const std::vector<TracedFace> &faces, double tol);

    void AddRefinedFold(const Handle(Geom_Curve) &c, double t0, double t1, const TracedFace &f, const AxisSystem &axes,
                        double tol, const Properties &props, Wire &w);

    void AddFolds(HLRBRep_HLRToShape &r, bool visible, const std::vector<TracedFace> &traced, double nearTol, const AxisSystem &axes,
                  bool ids, size_t &n, Wire &w);

    inline HLRBRep_HLRToShape HiddenLines(const TopoDS_Shape &shape, const AxisSystem &axes) {
        Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
        algo->Add(shape);
        algo->Projector(ProjectorOf(axes));
        algo->Update();
        algo->Hide();
        return HLRBRep_HLRToShape(algo);
    }

    std::vector<TracedFace> TracedFaces(const TopoDS_Shape &shape, double nearTol);

    // how far OCC's traced fold curve may stray from its surface: far more than it does
    inline double FoldNear(const TopoDS_Shape &shape) {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        return 1e-3 * std::sqrt(box.SquareExtent());
    }

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

    class DepthCheck {
        public:
            DepthCheck(const TopoDS_Shape &shape, const AxisSystem &axes, double deflection);
            bool IsHidden(const gp_Pnt &p, double tol);

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

    void DropCovered(Wire &hidden, const Wire &visible, double tol);

    struct FoldAxis {
        bool round = false, anyAxis = false;
        gp_Ax1 axis;
        bool convex = false;
        int face = -1;
    };
    struct RoundFaces {
        std::vector<TracedFace> faces;
        std::vector<FoldAxis> axes;
    };
    RoundFaces RoundFacesOf(const TopoDS_Shape &shape, double nearTol);
    FoldAxis TurningOf(const Handle(Geom_Curve) &c, double t0, double t1, const RoundFaces &round, double tol);

    struct ViewEdges {
        Wire visible, hidden, smooth, folds;
        std::vector<FoldAxis> foldAxes;
    };

    ViewEdges ViewLines(const TopoDS_Shape &shape, const AxisSystem &axes);

    struct Shading {
        std::vector<std::array<Vertex, 3>> triangles;
        std::vector<double> light;
        std::vector<std::array<double, 3>> depth;
    };

    Shading ShadedTriangles(const TopoDS_Shape &shape, const AxisSystem &axes, double deflection);

    struct ShadedImage {
        Vertex low;
        size_t width = 0, height = 0;
        std::vector<double> light;
    };

    ShadedImage Rasterized(const Shading &s, double pixel);

}
