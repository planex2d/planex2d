// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/hiddenlines.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace planex2d {

    // the point of surface s at (u, v) moved onto its exact fold seen along n: the least Newton steps on (Su x Sv).n = 0
    // (the fold: where the surface turns from the viewer); none if they do not settle
    std::optional<gp_Pnt> OnFold(const Handle(Geom_Surface) &s, double u, double v, const gp_Vec &n) {
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

    const TracedFace *FaceUnder(const Handle(Geom_Curve) &c, double t0, double t1, const std::vector<TracedFace> &faces, double tol) {
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

    void AddRefinedFold(const Handle(Geom_Curve) &c, double t0, double t1, const TracedFace &f, const AxisSystem &axes,
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

    void AddFolds(HLRBRep_HLRToShape &r, bool visible, const std::vector<TracedFace> &traced, double nearTol, const AxisSystem &axes,
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

    // the faces of shape whose fold lines OCC traces, each with its box grown by near (a fold line farther off lies on
    // another face)
    std::vector<TracedFace> TracedFaces(const TopoDS_Shape &shape, double nearTol) {
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

    DepthCheck::DepthCheck(const TopoDS_Shape &shape, const AxisSystem &axes, double deflection): axes(axes) {
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

    bool DepthCheck::IsHidden(const gp_Pnt &p, double tol) {
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

    // the edges of hidden that lie on edges of visible (within tol at five points along them) left out: a drawing shows
    // the visible line there (OCC gives the edge behind it too, the back face's outline behind the front face's)
    void DropCovered(Wire &hidden, const Wire &visible, double tol) {
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

    RoundFaces RoundFacesOf(const TopoDS_Shape &shape, double nearTol) {
        RoundFaces out;
        for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
            const TopoDS_Face &f = TopoDS::Face(e.Current());
            const BRepAdaptor_Surface s(f);
            FoldAxis axis{true, false, {}};
            switch (s.GetType()) {
            case GeomAbs_Cylinder: axis.axis = s.Cylinder().Axis(); break;
            case GeomAbs_Cone: axis.axis = s.Cone().Axis(); break;
            case GeomAbs_Torus: axis.axis = s.Torus().Axis(); break;
            case GeomAbs_Sphere: axis = {true, true, gp_Ax1(s.Sphere().Location(), gp_Dir(0.0, 0.0, 1.0))}; break;
            case GeomAbs_SurfaceOfRevolution: axis.axis = s.AxeOfRevolution(); break;
            default: axis.round = false;
            }
            if (!axis.round)
                continue;
            TracedFace x{f, new ShapeAnalysis_Surface(BRep_Tool::Surface(f)), 0.0, 0.0, 0.0, 0.0, {}};
            BRepTools::UVBounds(f, x.u0, x.u1, x.v0, x.v1);
            BRepBndLib::Add(f, x.box);
            x.box.Enlarge(nearTol);
            out.faces.push_back(x);
            out.axes.push_back(axis);
        }
        return out;
    }

    FoldAxis TurningOf(const Handle(Geom_Curve) &c, double t0, double t1, const RoundFaces &round, double tol) {
        const TracedFace *f = FaceUnder(c, t0, t1, round.faces, tol);
        if (!f)
            return {};
        FoldAxis out = round.axes[(size_t)(f - round.faces.data())];
        out.face = (int)(f - round.faces.data());
        const gp_Pnt p = c->Value(0.5 * (t0 + t1));
        const gp_Pnt2d uv = f->surface->ValueOfUV(p, tol);
        gp_Pnt q;
        gp_Vec du, dv;
        f->surface->Surface()->D1(uv.X(), uv.Y(), q, du, dv);
        gp_Vec normal = du.Crossed(dv);
        if (f->face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        gp_Vec radial(out.axis.Location(), p);
        if (!out.anyAxis)
            radial -= gp_Vec(out.axis.Direction()) * radial.Dot(gp_Vec(out.axis.Direction()));
        out.convex = normal.Dot(radial) > 0.0;
        return out;
    }

    class ViewTracer {
        public:
            ViewTracer(const TopoDS_Shape &shape, const AxisSystem &axes)
                : axes(axes), r(HiddenLines(shape, axes)), nearTol(FoldNear(shape)), traced(TracedFaces(shape, nearTol)),
                  round(RoundFacesOf(shape, nearTol)), deflection(0.5 * nearTol), tol(2.0 * deflection), aside(3.0 * deflection),
                  depth(shape, axes, deflection), n(ToOCC(axes.Normal())), out{Wire(axes), Wire(axes), Wire(axes), Wire(axes), {}} {}

            ViewEdges Lines() {
                for (const HLRBRep_TypeOfResultingEdge type : {HLRBRep_Sharp, HLRBRep_Rg1Line, HLRBRep_OutLine})
                    for (const bool visible : {true, false})
                        for (TopExp_Explorer e(r.CompoundOfEdges(type, visible, true), TopAbs_EDGE); e.More(); e.Next()) {
                            c = BRep_Tool::Curve(TopoDS::Edge(e.Current()), t0, t1);
                            if (!c.IsNull())
                                AddLine(type, visible);
                        }
                // the same projected point up to rounding: 1e-9 of the size
                DropCovered(out.hidden, out.visible, 1e-6 * nearTol);
                return std::move(out);
            }

        private:
            const AxisSystem &axes;
            HLRBRep_HLRToShape r;
            const double nearTol;
            const std::vector<TracedFace> traced;
            const RoundFaces round;
            // the triangulation lies within deflection of the faces: a face less than two of that in front of a point is the
            // point's own; a fold's lines of sight start three of it aside
            const double deflection, tol, aside;
            DepthCheck depth;
            const gp_Dir n;
            ViewEdges out;
            Handle(Geom_Curve) c;
            double t0 = 0.0, t1 = 0.0;
            bool fold = false;
            const TracedFace *f = nullptr;
            // the point found on the surface for the last test: the next one starts from it
            std::optional<gp_Pnt2d> last;

            void AddLine(HLRBRep_TypeOfResultingEdge type, bool visible) {
                fold = type == HLRBRep_OutLine;
                f = fold ? FaceUnder(c, t0, t1, traced, nearTol) : nullptr;
                const FoldAxis turning = fold ? TurningOf(c, t0, t1, round, nearTol) : FoldAxis{};
                last.reset();
                for (const Stretch &s : StretchesOf(visible)) {
                    if (type == HLRBRep_Rg1Line && s.hidden)
                        continue;
                    Wire &w = type == HLRBRep_Rg1Line ? out.smooth : s.hidden ? out.hidden : out.visible;
                    const size_t before = w.Size();
                    if (f)
                        AddRefinedFold(c, s.from, s.to, *f, axes, nearTol, nullptr, w);
                    else
                        for (Edge &x : ProjectEdge(BRepBuilderAPI_MakeEdge(c, s.from, s.to).Edge(), axes, nullptr).edges)
                            w.Add(std::move(x));
                    if (fold && (f || turning.round) && &w == &out.visible)
                        for (size_t k = before; k < w.Size(); ++k) {
                            out.folds.Add(w[k]);
                            out.foldAxes.push_back(turning);
                        }
                }
            }

            // a fold OCC traces may run behind other parts of its surface where OCC does not split it (a spring's
            // back behind its front coils): checked along it every 2e-2 of the size; any other line by its middle
            std::vector<Stretch> StretchesOf(bool visible) {
                const auto hidden = [&](double t) { return IsHidden(t); };
                if (!f)
                    return hidden(0.5 * (t0 + t1)) != visible ? std::vector<Stretch>{{t0, t1, !visible}} : Stretches(t0, t1, 4, hidden);
                double length = 0.0;
                for (int k = 0; k < 16; ++k)
                    length += c->Value(t0 + (t1 - t0) * k / 16.0).Distance(c->Value(t0 + (t1 - t0) * (k + 1) / 16.0));
                return Stretches(t0, t1, std::clamp((int)(length / (20.0 * nearTol)), 4, 200), hidden);
            }

            bool IsHidden(double t) {
                if (!fold)
                    return depth.IsHidden(c->Value(t), tol);
                gp_Pnt p;
                gp_Vec tangent;
                c->D1(t, p, tangent);
                if (f) {
                    const gp_Pnt2d uv = last ? f->surface->NextValueOfUV(*last, p, nearTol) : f->surface->ValueOfUV(p, nearTol);
                    last = uv;
                    if (const std::optional<gp_Pnt> q = OnFold(f->surface->Surface(), uv.X(), uv.Y(), gp_Vec(n)); q && q->Distance(p) <= nearTol)
                        p = *q;
                }
                return IsFoldHidden(depth, p, tangent, n, aside, tol);
            }
    };

    // the lines of a view of shape along the normal of axes, in its coordinates: OCC's exact hidden-line algorithm's
    // edges and fold lines in 3d, projected (the fold lines OCC traces only approximately refined onto the exact surface);
    // the smooth edges (where faces meet tangentially) visible only. OCC's visibility of an edge is checked by a ray from
    // its middle (a fold line's from beside it, IsFoldHidden), and where the ray disagrees the line is split by rays
    // along it (OCC's classification fails where many outlines fall together, a spring)
    ViewEdges ViewLines(const TopoDS_Shape &shape, const AxisSystem &axes) {
        return ViewTracer(shape, axes).Lines();
    }

    Shading ShadedTriangles(const TopoDS_Shape &shape, const AxisSystem &axes, double deflection) {
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

    ShadedImage Rasterized(const Shading &s, double pixel) {
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
