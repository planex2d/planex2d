// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
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

#include "core/disjointsets.hpp"

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

            std::vector<Chamfer> Chains() const {
                std::vector<std::optional<Piece>> pieces(faces.Extent() + 1);
                for (int f = 1; f <= faces.Extent(); ++f)
                    pieces[f] = PieceOf(f);
                DisjointSets joined(pieces.size());
                for (int f = 1; f <= faces.Extent(); ++f)
                    if (pieces[f])
                        for (TopExp_Explorer e(faces(f), TopAbs_EDGE); e.More(); e.Next())
                            if (const int g = Other(TopoDS::Edge(e.Current()), f); g > 0 && pieces[g] && IsAlike(*pieces[f], *pieces[g]))
                                joined.Join((size_t)f, (size_t)g);
                std::vector<Chamfer> out;
                std::vector<int> chainOf(pieces.size(), -1);
                for (int f = 1; f <= faces.Extent(); ++f) {
                    if (!pieces[f])
                        continue;
                    const size_t root = joined.Find((size_t)f);
                    if (chainOf[root] < 0) {
                        chainOf[root] = (int)out.size();
                        out.push_back({pieces[f]->legA, pieces[f]->legB, {}, {}});
                    }
                    Chamfer &c = out[(size_t)chainOf[root]];
                    for (TopExp_Explorer e(faces(f), TopAbs_EDGE); e.More(); e.Next())
                        c.lines.push_back(Sampled(TopoDS::Edge(e.Current())));
                    c.sharp.push_back(pieces[f]->sharp);
                }
                return out;
            }

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
            gp_Vec Normal(int f, const gp_Pnt &p) const {
                const TopoDS_Face &face = TopoDS::Face(faces(f));
                const Handle(ShapeAnalysis_Surface) s = new ShapeAnalysis_Surface(BRep_Tool::Surface(face));
                const gp_Pnt2d uv = s->ValueOfUV(p, tol);
                gp_Pnt q;
                gp_Vec du, dv;
                BRepAdaptor_Surface(face).D1(uv.X(), uv.Y(), q, du, dv);
                gp_Vec n = du.Crossed(dv);
                if (n.Magnitude() <= 0.0)
                    return n;
                n.Normalize();
                return face.Orientation() == TopAbs_REVERSED ? -n : n;
            }
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
            static std::optional<gp_Pnt> InPlane(const TopoDS_Edge &e, const gp_Pnt &p, const gp_Vec &t) {
                const BRepAdaptor_Curve c(e);
                const double a = c.FirstParameter(), b = c.LastParameter();
                const auto side = [&](double s) { return gp_Vec(p, c.Value(s)).Dot(t); };
                constexpr int samples = 64;
                std::optional<gp_Pnt> nearest;
                for (int k = 0; k < samples; ++k) {
                    double lo = a + (b - a) * k / samples, hi = a + (b - a) * (k + 1) / samples;
                    if ((side(lo) > 0.0) == (side(hi) > 0.0) && side(hi) != 0.0)
                        continue;
                    for (int n = 0; n < 60; ++n) {
                        const double m = 0.5 * (lo + hi);
                        ((side(lo) > 0.0) == (side(m) > 0.0) ? lo : hi) = m;
                    }
                    const gp_Pnt q = c.Value(0.5 * (lo + hi));
                    if (!nearest || q.Distance(p) < nearest->Distance(p))
                        nearest = q;
                }
                return nearest;
            }
            std::optional<Section> SectionAt(int f, const TopoDS_Edge &ea, int a, const TopoDS_Edge &eb, int b, double s) const {
                const BRepAdaptor_Curve c(ea);
                gp_Pnt p;
                gp_Vec t;
                c.D1(c.FirstParameter() + s * (c.LastParameter() - c.FirstParameter()), p, t);
                if (t.Magnitude() <= 0.0)
                    return std::nullopt;
                t.Normalize();
                const std::optional<gp_Pnt> q = InPlane(eb, p, t);
                if (!q || p.Distance(*q) <= tol)
                    return std::nullopt;
                const gp_Pnt middle((p.XYZ() + q->XYZ()) / 2.0);
                if (Gap(f, middle) > 1e-3 * p.Distance(*q) + tol)
                    return std::nullopt;
                const gp_Vec da = Across(a, p, t), db = Across(b, *q, t), pq(p, *q);
                const double aa = da.Dot(da), ab = -da.Dot(db), bb = db.Dot(db), ra = -pq.Dot(da), rb = pq.Dot(db);
                const double det = aa * bb - ab * ab;
                if (std::abs(det) <= 1e-12)
                    return std::nullopt;
                const double u = (ra * bb - ab * rb) / det, v = (aa * rb - ab * ra) / det;
                if (u <= tol || v <= tol)
                    return std::nullopt;
                const gp_Pnt corner = p.Translated(-u * da);
                if (corner.Distance(q->Translated(-v * db)) > 1e-3 * p.Distance(*q) + tol)
                    return std::nullopt;
                return Section{u, v, Width(a, p, da), Width(b, *q, db), corner};
            }
            std::optional<Piece> PieceOf(int f) const {
                std::vector<std::pair<double, TopoDS_Edge>> edges;
                for (TopExp_Explorer e(faces(f), TopAbs_EDGE); e.More(); e.Next()) {
                    const TopoDS_Edge &edge = TopoDS::Edge(e.Current());
                    GProp_GProps props;
                    BRepGProp::LinearProperties(edge, props);
                    edges.push_back({props.Mass(), edge});
                }
                if (edges.size() < 3)
                    return std::nullopt;
                std::sort(edges.begin(), edges.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
                const TopoDS_Edge &ea = edges[0].second, &eb = edges[1].second;
                const int a = Other(ea, f), b = Other(eb, f);
                if (a <= 0 || b <= 0 || a == b)
                    return std::nullopt;
                Piece out{0.0, 0.0, {}};
                for (const double s : {0.25, 0.5, 0.75}) {
                    const std::optional<Section> x = SectionAt(f, ea, a, eb, b, s);
                    if (!x)
                        return std::nullopt;
                    const double limit = std::max(0.1 * std::min(x->widthA, x->widthB), floor);
                    if (std::max(x->legA, x->legB) > limit)
                        return std::nullopt;
                    if (!out.sharp.empty() && (std::abs(x->legA - out.legA) > Same(out.legA) || std::abs(x->legB - out.legB) > Same(out.legB)))
                        return std::nullopt;
                    out.legA = x->legA;
                    out.legB = x->legB;
                    out.sharp.push_back(x->corner);
                }
                return out;
            }
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
