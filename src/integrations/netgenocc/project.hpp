// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GeomConvert.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>

#include "integrations/netgenocc/convert.hpp"
#include "integrations/netgenocc/occprops.hpp"
#include "integrations/netgenocc/occspline.hpp"
#include "boolean/boolean.hpp"
#include "sketch/projection.hpp"

namespace planex2d {

    // a name read in: "|" joins names in a selection, so it becomes "_"
    inline std::optional<std::string> ReadName(std::optional<std::string> name) {
        if (!name || name->find('|') == std::string::npos)
            return name;
        const std::string given = *name;
        std::replace(name->begin(), name->end(), '|', '_');
        Warning() << "name '" << given << "' read as '" << *name << "': '|' joins names in a selection";
        return name;
    }

    // the name, color and maxh netgen keeps for an OCC shape
    inline Properties PropertiesOf(const TopoDS_Shape &s) {
        if (!netgen::OCCGeometry::HaveProperties(s))
            return nullptr;
        const netgen::ShapeProperties &p = netgen::OCCGeometry::GetProperties(s);
        return ChangedProperties(nullptr, [&](ShapeProperties &q) {
            q.name = ReadName(p.name);
            // netgen's default maxh means none
            if (p.maxh < 1e99)
                q.maxh = p.maxh;
            if (p.hpref > 0.0)
                q.hpref = p.hpref;
            if (p.col)
                q.col = std::array<double, 4>{(*p.col)[0], (*p.col)[1], (*p.col)[2], (*p.col)[3]};
        });
    }

    // the id netgen's numbers give an edge or a vertex (Edge.1): its first index in shape.edges or shape.vertices,
    // which list it once per face it bounds, as the webgui shows them
    struct Numbering {
        TopTools_IndexedMapOfShape map;
        std::vector<size_t> first;

        Numbering(const TopoDS_Shape &shape, TopAbs_ShapeEnum kind) {
            TopExp::MapShapes(shape, kind, map);
            first.assign((size_t)map.Extent(), 0);
            std::vector<bool> seen(first.size(), false);
            size_t k = 0;
            for (TopExp_Explorer x(shape, kind); x.More(); x.Next(), ++k) {
                const size_t m = (size_t)map.FindIndex(x.Current()) - 1;
                if (!seen[m]) {
                    seen[m] = true;
                    first[m] = k;
                }
            }
        }
        std::string Id(const char *kind, const TopoDS_Shape &s) const {
            return std::string(kind) + "." + std::to_string(first[(size_t)map.FindIndex(s) - 1]);
        }
    };

    // one OCC edge straight onto the plane of axes, in its coordinates, with props (and its vertices' properties at its
    // ends, with vertices their ids Vertex.X): lines stay lines, circles parallel to the plane arcs,
    // other curves exact splines (a parallel projection maps a b-spline's poles, weights stay); a curve seen edge-on
    // becomes the segment it covers, an edge seen end-on a point
    struct ProjectedEdge {
        EdgeList edges;
        std::optional<Vertex> point;
    };
    inline ProjectedEdge ProjectEdge(const TopoDS_Edge &edge, const AxisSystem &axes, const Properties &props,
                                     const Numbering *vertices = nullptr) {
        const gp_XYZ normal = ToOCC(axes.Normal());
        auto uv = [&](const gp_Pnt &p) { return axes.ToHV(Vector3Of(p.XYZ())); };
        // OCC's model precision: what is closer counts as the same point
        const double tol = Precision::Confusion();
        ProjectedEdge out;
        BRepAdaptor_Curve c(edge);
        const double t0 = c.FirstParameter(), t1 = c.LastParameter();
        TopoDS_Vertex first, last;
        TopExp::Vertices(edge, first, last);
        const auto endProps = [&](const TopoDS_Vertex &v) -> Properties {
            if (v.IsNull())
                return nullptr;
            if (!vertices)
                return PropertiesOf(v);
            return ChangedProperties(PropertiesOf(v), [&](ShapeProperties &q) { q.id = vertices->Id("Vertex", v); });
        };
        const Properties firstProps = endProps(first), lastProps = endProps(last);
        auto add = [&](Edge e) {
            SetProps(e, props);
            if ((firstProps || lastProps) && !IsClosedEdge(e)) {
                const Vertex s = uv(BRep_Tool::Pnt(first.IsNull() ? last : first));
                const Vertex t = uv(BRep_Tool::Pnt(last.IsNull() ? first : last));
                std::visit([&](auto &x) {
                    x.ends = MakeEnds(x.Start().IsClose(s, tol) ? firstProps : x.Start().IsClose(t, tol) ? lastProps : nullptr,
                                      x.End().IsClose(t, tol) ? lastProps : x.End().IsClose(s, tol) ? firstProps : nullptr);
                }, e);
            }
            out.edges.push_back(std::move(e));
        };

        // flat: all of the projection within tol of the line through its two farthest samples
        const int samples = 64;
        std::vector<Vertex> s;
        for (int k = 0; k <= samples; ++k)
            s.push_back(uv(c.Value(k == samples ? t1 : t0 + (t1 - t0) * k / samples)));
        auto farthest = [&](const Vertex &from) {
            return *std::max_element(s.begin(), s.end(), [&](const Vertex &a, const Vertex &b) {
                return Norm(a - from) < Norm(b - from);
            });
        };
        // the ends from the vertices: exact, and shared with the neighbouring edges
        const Vertex start = first.IsNull() ? s.front() : uv(BRep_Tool::Pnt(first));
        const Vertex end = last.IsNull() ? s.back() : uv(BRep_Tool::Pnt(last));
        const bool closed = first.IsSame(last);
        Vertex a = farthest(s.front()), b = farthest(a);
        if (Norm(b - a) <= tol) {
            out.point = (a + b) * 0.5;
            return out;
        }
        Vertex u = Normalized(b - a);
        if (std::all_of(s.begin(), s.end(), [&](const Vertex &p) { return std::abs(Cross(u, p - a)) <= tol; })) {
            if (c.GetType() == GeomAbs_Line)
                add(Line(start, end));
            else
                add(Line(a, b));
            return out;
        }

        if (c.GetType() == GeomAbs_Circle) {
            gp_Circ circle = c.Circle();
            double along = circle.Axis().Direction().XYZ().Dot(normal);
            if (std::abs(std::abs(along) - 1.0) <= Precision::Angular()) {
                Vertex center = uv(circle.Location());
                bool ccw = along > 0.0;
                if (t1 - t0 >= 2.0 * std::numbers::pi - Precision::Angular()) {
                    add(Circle(center, start, ccw));
                } else {
                    add(Arc(start, end, center, ccw));
                }
                return out;
            }
        }

        double f, l;
        Handle(Geom_Curve) g = BRep_Tool::Curve(edge, f, l);
        try {
            Handle(Geom_BSplineCurve) bs = GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(g, f, l));
            TColgp_Array1OfPnt2d poles(1, bs->NbPoles());
            for (int k = 1; k <= bs->NbPoles(); ++k) {
                Vertex p = uv(bs->Pole(k));
                poles.SetValue(k, gp_Pnt2d(p.x, p.y));
            }
            Handle(Geom2d_BSplineCurve) flat =
                bs->IsRational() ? new Geom2d_BSplineCurve(poles, *bs->Weights(), bs->Knots(), bs->Multiplicities(),
                                                           bs->Degree(), bs->IsPeriodic())
                                 : new Geom2d_BSplineCurve(poles, bs->Knots(), bs->Multiplicities(), bs->Degree(), bs->IsPeriodic());
            std::vector<Spline> pieces = Spline::FromCurve(CurveOf(flat));
            // an OCC edge's curve misses its vertices by the edge's tolerance (5e-6 for a section curve), far above
            // the snap: the ends go onto the vertices so that neighbouring edges meet exactly (a closed curve has no neighbours)
            if (!closed) {
                pieces.front().Trim(start, pieces.front().End());
                pieces.back().Trim(pieces.back().Start(), end);
            }
            for (Spline &piece : pieces)
                add(std::move(piece));
        } catch (const Standard_Failure &e) {
            Warning() << "projection of an edge failed, left out: " << e.GetMessageString();
        } catch (const std::runtime_error &e) {
            Warning() << "projection of an edge failed, left out: " << e.what();
        }
        return out;
    }

    inline Wire ProjectedFaceEdges(const TopoDS_Face &face, const AxisSystem &axes) {
        Wire w(axes);
        for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next())
            if (!BRep_Tool::Degenerated(TopoDS::Edge(e.Current())))
                for (Edge &x : ProjectEdge(TopoDS::Edge(e.Current()), axes, PropertiesOf(e.Current())).edges)
                    w.Add(std::move(x));
        return w;
    }

    // the points that lie at no end of w's edges, those falling together within tol once
    inline std::vector<ProjectedPoint> PointsOffEdges(std::vector<ProjectedPoint> points, const Wire &w, double tol) {
        const std::vector<Vertex> known = w.Endpoints();
        const std::optional<Grid> grid = known.empty() ? std::nullopt : std::optional<Grid>(Grid::Of(known, tol));
        std::vector<ProjectedPoint> out;
        for (ProjectedPoint &p : points) {
            bool seen = std::any_of(out.begin(), out.end(), [&](const ProjectedPoint &q) { return q.p.IsClose(p.p, tol); });
            if (grid)
                grid->ForNear(p.p, [&](size_t k) { seen = seen || known[k].IsClose(p.p, tol); });
            if (!seen)
                out.push_back(std::move(p));
        }
        return out;
    }

    // what OCC counts as one point in shape: its largest edge or vertex tolerance, at least the model precision
    inline double PointTolerance(const TopoDS_Shape &shape) {
        double tol = Precision::Confusion();
        for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next())
            tol = std::max(tol, BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        for (TopExp_Explorer v(shape, TopAbs_VERTEX); v.More(); v.Next())
            tol = std::max(tol, BRep_Tool::Tolerance(TopoDS::Vertex(v.Current())));
        return tol;
    }

    // straight onto the plane of axes, in its coordinates. Lines stay lines, circles parallel to the plane stay arcs,
    // other curves become exact splines (a parallel projection maps a b-spline's poles, weights stay). A curve seen
    // edge-on becomes the segment it covers, an edge seen end-on a point. Edges that fall onto each other stay once
    // with their properties merged, those of the edge nearest to a viewer on the normal's side first; crossings are cut.
    // Points at a vertex of the projected edges are left out. ids: the edges' and points' ids say where they come from,
    // Edge.X and Vertex.X as netgen numbers them (Numbering)
    inline Projection Project(const TopoDS_Shape &shape, const AxisSystem &axes, bool ids = false) {
        const gp_XYZ origin = ToOCC(axes.Origin()), normal = ToOCC(axes.Normal());
        const double tol = PointTolerance(shape);
        Projection out{Wire(axes), {}};
        const Numbering edges(shape, TopAbs_EDGE), vertices(shape, TopAbs_VERTEX);
        std::vector<std::pair<double, Edge>> projected;
        for (int i = 1; i <= edges.map.Extent(); ++i) {
            const TopoDS_Edge &edge = TopoDS::Edge(edges.map(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            const std::optional<std::string> id = ids ? std::optional(edges.Id("Edge", edge)) : std::nullopt;
            const Properties props = id ? ChangedProperties(PropertiesOf(edge), [&](ShapeProperties &q) { q.id = id; }) : PropertiesOf(edge);
            BRepAdaptor_Curve c(edge);
            const double height = (c.Value(0.5 * (c.FirstParameter() + c.LastParameter())).XYZ() - origin).Dot(normal);
            ProjectedEdge p = ProjectEdge(edge, axes, props, ids ? &vertices : nullptr);
            if (p.point)
                out.points.push_back({*p.point, NameOf(props), id});
            for (Edge &e : p.edges)
                projected.emplace_back(height, std::move(e));
        }
        for (TopExp_Explorer v(shape, TopAbs_VERTEX, TopAbs_EDGE); v.More(); v.Next())
            out.points.push_back({axes.ToHV(Vector3Of(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())).XYZ())), NameOf(PropertiesOf(v.Current())),
                                  ids ? std::optional(vertices.Id("Vertex", v.Current())) : std::nullopt});
        // the edge nearest to a viewer on the normal's side comes first, its properties win where edges fall together
        std::stable_sort(projected.begin(), projected.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (auto &[height, e] : projected)
            out.wire.Add(std::move(e));
        // neighbouring edges end in the same projected point, up to rounding
        out.wire.RemoveSmallGaps(tol);
        std::vector<CutState> state(out.wire.Size());
        Arrange(out.wire, state);
        // a crossing cut near an end leaves a piece shorter than OCC's tolerance: it goes, its neighbours meet again
        out.wire.Erase(EpsEdges(out.wire, tol, [](size_t) { return true; }));
        out.wire.RemoveSmallGaps(tol);
        out.wire.MergeOnCarriers();
        // a point at a vertex of the projected edges adds nothing (the side edges of a prism seen end-on)
        out.points = PointsOffEdges(std::move(out.points), out.wire, tol);
        return out;
    }

}
