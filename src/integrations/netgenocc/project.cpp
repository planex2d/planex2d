// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/project.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planex2d {

    Properties PropertiesOf(const TopoDS_Shape &s) {
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

    Numbering::Numbering(const TopoDS_Shape &shape, TopAbs_ShapeEnum kind) {
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

    static std::vector<Spline> ProjectedSpline(const Handle(Geom_Curve) &g, double f, double l, const AxisSystem &axes) {
        Handle(Geom_BSplineCurve) bs = GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(g, f, l));
        TColgp_Array1OfPnt2d poles(1, bs->NbPoles());
        for (int k = 1; k <= bs->NbPoles(); ++k) {
            const Vertex p = axes.ToHV(Vector3Of(bs->Pole(k).XYZ()));
            poles.SetValue(k, gp_Pnt2d(p.x, p.y));
        }
        Handle(Geom2d_BSplineCurve) flat =
            bs->IsRational() ? new Geom2d_BSplineCurve(poles, *bs->Weights(), bs->Knots(), bs->Multiplicities(),
                                                       bs->Degree(), bs->IsPeriodic())
                             : new Geom2d_BSplineCurve(poles, bs->Knots(), bs->Multiplicities(), bs->Degree(), bs->IsPeriodic());
        return Spline::FromCurve(CurveOf(flat));
    }

    static Properties VertexProperties(const TopoDS_Vertex &v, const Numbering *vertices) {
        if (v.IsNull())
            return nullptr;
        if (!vertices)
            return PropertiesOf(v);
        return ChangedProperties(PropertiesOf(v), [&](ShapeProperties &q) { q.id = vertices->Id("Vertex", v); });
    }

    class EdgeProjection {
        public:
            EdgeProjection(const TopoDS_Edge &edge, const AxisSystem &axes, const Properties &props, const Numbering *vertices)
                : edge(edge), axes(axes), props(props), c(edge), t0(c.FirstParameter()), t1(c.LastParameter()) {
                TopExp::Vertices(edge, first, last);
                firstProps = VertexProperties(first, vertices);
                lastProps = VertexProperties(last, vertices);
                for (int k = 0; k <= count; ++k)
                    samples.push_back(HV(c.Value(k == count ? t1 : t0 + (t1 - t0) * k / count)));
                // the ends from the vertices: exact, and shared with the neighbouring edges
                start = first.IsNull() ? samples.front() : HV(BRep_Tool::Pnt(first));
                end = last.IsNull() ? samples.back() : HV(BRep_Tool::Pnt(last));
            }

            ProjectedEdge Projected() {
                if (!AddFlat() && !AddCircle())
                    AddSplines();
                return std::move(out);
            }

        private:
            static constexpr int count = 64;
            // OCC's model precision: what is closer counts as the same point
            const double tol = Precision::Confusion();
            const TopoDS_Edge &edge;
            const AxisSystem &axes;
            const Properties &props;
            BRepAdaptor_Curve c;
            const double t0, t1;
            TopoDS_Vertex first, last;
            Properties firstProps, lastProps;
            std::vector<Vertex> samples;
            Vertex start, end;
            ProjectedEdge out;

            Vertex HV(const gp_Pnt &p) const { return axes.ToHV(Vector3Of(p.XYZ())); }

            void Add(Edge e) {
                SetProps(e, props);
                if ((firstProps || lastProps) && !IsClosedEdge(e)) {
                    const Vertex s = HV(BRep_Tool::Pnt(first.IsNull() ? last : first));
                    const Vertex t = HV(BRep_Tool::Pnt(last.IsNull() ? first : last));
                    std::visit([&](auto &x) {
                        x.ends = MakeEnds(x.Start().IsClose(s, tol) ? firstProps : x.Start().IsClose(t, tol) ? lastProps : nullptr,
                                          x.End().IsClose(t, tol) ? lastProps : x.End().IsClose(s, tol) ? firstProps : nullptr);
                    }, e);
                }
                out.edges.push_back(std::move(e));
            }

            Vertex Farthest(const Vertex &from) const {
                return *std::max_element(samples.begin(), samples.end(), [&](const Vertex &a, const Vertex &b) {
                    return Norm(a - from) < Norm(b - from);
                });
            }

            // flat: all of the projection within tol of the line through its two farthest samples
            bool AddFlat() {
                const Vertex a = Farthest(samples.front()), b = Farthest(a);
                if (Norm(b - a) <= tol) {
                    out.point = (a + b) * 0.5;
                    return true;
                }
                const Vertex u = Normalized(b - a);
                if (!std::all_of(samples.begin(), samples.end(), [&](const Vertex &p) { return std::abs(Cross(u, p - a)) <= tol; }))
                    return false;
                if (c.GetType() == GeomAbs_Line)
                    Add(Line(start, end));
                else
                    Add(Line(a, b));
                return true;
            }

            bool AddCircle() {
                if (c.GetType() != GeomAbs_Circle)
                    return false;
                const gp_Circ circle = c.Circle();
                const double along = circle.Axis().Direction().XYZ().Dot(ToOCC(axes.Normal()));
                if (!(std::abs(std::abs(along) - 1.0) <= Precision::Angular()))
                    return false;
                const Vertex center = HV(circle.Location());
                const bool ccw = along > 0.0;
                if (t1 - t0 >= 2.0 * std::numbers::pi - Precision::Angular())
                    Add(Circle(center, start, ccw));
                else
                    Add(Arc(start, end, center, ccw));
                return true;
            }

            void AddSplines() {
                double f, l;
                Handle(Geom_Curve) g = BRep_Tool::Curve(edge, f, l);
                try {
                    std::vector<Spline> pieces = ProjectedSpline(g, f, l, axes);
                    // an OCC edge's curve misses its vertices by the edge's tolerance (5e-6 for a section curve), far above
                    // the snap: the ends go onto the vertices so that neighbouring edges meet exactly (a closed curve has no neighbours)
                    if (!first.IsSame(last)) {
                        pieces.front().Trim(start, pieces.front().End());
                        pieces.back().Trim(pieces.back().Start(), end);
                    }
                    for (Spline &piece : pieces)
                        Add(std::move(piece));
                } catch (const Standard_Failure &e) {
                    Warning() << "projection of an edge failed, left out: " << e.GetMessageString();
                } catch (const std::runtime_error &e) {
                    Warning() << "projection of an edge failed, left out: " << e.what();
                }
            }
    };

    ProjectedEdge ProjectEdge(const TopoDS_Edge &edge, const AxisSystem &axes, const Properties &props,
                              const Numbering *vertices) {
        return EdgeProjection(edge, axes, props, vertices).Projected();
    }

    std::vector<ProjectedPoint> PointsOffEdges(std::vector<ProjectedPoint> points, const Wire &w, double tol) {
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

    Projection Project(const TopoDS_Shape &shape, const AxisSystem &axes, bool ids) {
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
        ArrangeProjected(out.wire, tol);
        // a point at a vertex of the projected edges adds nothing (the side edges of a prism seen end-on)
        out.points = PointsOffEdges(std::move(out.points), out.wire, tol);
        return out;
    }

}
