// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/chamfers.hpp"

namespace planex2d {

    std::vector<Chamfer> ChamferFinder::Chains() const {
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

    gp_Vec ChamferFinder::Normal(int f, const gp_Pnt &p) const {
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

    std::optional<gp_Pnt> ChamferFinder::InPlane(const TopoDS_Edge &e, const gp_Pnt &p, const gp_Vec &t) {
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

    std::optional<ChamferFinder::Section> ChamferFinder::SectionAt(int f, const TopoDS_Edge &ea, int a, const TopoDS_Edge &eb, int b, double s) const {
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

    std::optional<ChamferFinder::Piece> ChamferFinder::PieceOf(int f) const {
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

}
