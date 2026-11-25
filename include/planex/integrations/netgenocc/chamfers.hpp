// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include "planex/core/disjointsets.hpp"

namespace planex2d {

    struct Chamfer {
        double legA = 0.0, legB = 0.0;
        std::vector<std::vector<gp_Pnt>> lines, sharp;
    };

    class ChamferFinder {
        public:
            ChamferFinder(const TopoDS_Shape &shape, double floor) : floor(floor) {
                Bnd_Box box;
                BRepBndLib::Add(shape, box);
                tol = 1e-6 * std::sqrt(box.SquareExtent());
                TopExp::MapShapes(shape, TopAbs_FACE, faces);
                TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, owners);
            }

            std::vector<Chamfer> Chains() const;

        private:
            struct Piece {
                double legA, legB;
                std::vector<gp_Pnt> sharp;
            };
            struct Section {
                double legA, legB, widthA, widthB;
                gp_Pnt corner;
            };

            int Other(const TopoDS_Edge &e, int f) const {
                if (!owners.Contains(e))
                    return 0;
                for (const TopoDS_Shape &g : owners.FindFromKey(e))
                    if (const int k = faces.FindIndex(g); k != f && k > 0)
                        return k;
                return 0;
            }
            static gp_Pnt Centre(const TopoDS_Shape &face) {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(face, props);
                return props.CentreOfMass();
            }
            gp_Vec Normal(int f, const gp_Pnt &p) const;
            double Gap(int f, const gp_Pnt &p) const {
                const Handle(ShapeAnalysis_Surface) s = new ShapeAnalysis_Surface(BRep_Tool::Surface(TopoDS::Face(faces(f))));
                s->ValueOfUV(p, tol);
                return s->Gap();
            }
            gp_Vec Across(int f, const gp_Pnt &p, const gp_Vec &t) const {
                gp_Vec d = Normal(f, p).Crossed(t);
                if (d.Magnitude() <= 0.0)
                    return d;
                d.Normalize();
                return d.Dot(gp_Vec(p, Centre(faces(f)))) < 0.0 ? -d : d;
            }
            double Width(int f, const gp_Pnt &p, const gp_Vec &d) const {
                double most = 0.0;
                for (TopExp_Explorer v(faces(f), TopAbs_VERTEX); v.More(); v.Next())
                    most = std::max(most, gp_Vec(p, BRep_Tool::Pnt(TopoDS::Vertex(v.Current()))).Dot(d));
                return most;
            }
            static std::optional<gp_Pnt> InPlane(const TopoDS_Edge &e, const gp_Pnt &p, const gp_Vec &t);
            std::optional<Section> SectionAt(int f, const TopoDS_Edge &ea, int a, const TopoDS_Edge &eb, int b, double s) const;
            std::optional<Piece> PieceOf(int f) const;
            // legs alike within 1e-3 of their size, or the part's rounding
            double Same(double leg) const { return 1e-3 * leg + tol; }
            bool IsAlike(const Piece &x, const Piece &y) const {
                return (std::abs(x.legA - y.legA) <= Same(x.legA) && std::abs(x.legB - y.legB) <= Same(x.legB)) ||
                       (std::abs(x.legA - y.legB) <= Same(x.legA) && std::abs(x.legB - y.legA) <= Same(x.legB));
            }
            static std::vector<gp_Pnt> Sampled(const TopoDS_Edge &e) {
                const BRepAdaptor_Curve c(e);
                std::vector<gp_Pnt> out;
                constexpr int pieces = 32;
                for (int k = 0; k <= pieces; ++k)
                    out.push_back(c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * k / pieces));
                return out;
            }

            const double floor;
            double tol = 0.0;
            TopTools_IndexedMapOfShape faces;
            TopTools_IndexedDataMapOfShapeListOfShape owners;
    };

}
