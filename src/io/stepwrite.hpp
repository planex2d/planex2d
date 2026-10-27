// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "io/faceloops.hpp"
#include "io/steptext.hpp"
#include "shape/identification.hpp"
#include "shape/shape.hpp"

namespace planex2d {

    // a length unit a STEP file names: metre with an SI prefix, or a unit converted from the millimetre (inch, foot, mil)
    struct StepLengthUnit {
        // the SI prefix (".MILLI.", "$" for the metre itself), or the name of a converted unit
        std::string prefix, name;
        double mm = 1.0;
    };
    // the unit of length that many metres; none if STEP has no name for it
    inline std::optional<StepLengthUnit> StepLengthUnitOf(double metres) {
        static const std::pair<double, StepLengthUnit> units[] = {
            {1e3, {".KILO.", "", 1e6}},     {1.0, {"$", "", 1e3}},        {1e-1, {".DECI.", "", 100.0}},  {1e-2, {".CENTI.", "", 10.0}},
            {1e-3, {".MILLI.", "", 1.0}},   {1e-6, {".MICRO.", "", 1e-3}}, {1e-9, {".NANO.", "", 1e-6}}, {0.0254, {"", "INCH", 25.4}},
            {0.3048, {"", "FOOT", 304.8}}, {2.54e-5, {"", "MIL", 0.0254}}};
        for (const auto &[m, u] : units)
            if (std::abs(metres - m) <= 1e-9 * m)
                return u;
        return std::nullopt;
    }

    // planex objects as a STEP file (AP214) that OCC and netgen read as they read the files of OCC's own export: the
    // numbers as they are, in the length unit given; one product and one representation whose
    // items are the parts' open shells (the faces of a part sharing their edges and vertices) and curve sets (free wires
    // as named trimmed curves); names on faces, edges and vertices; a face's col as an sRGB colour; maxh and hpref as
    // netgen's properties. Which items make one planex part, and which part of a Compound it is, a marker entity says:
    // COMPOUND_REPRESENTATION_ITEM('planex_part <kind> <path>', (items)), kind Face, Shape or Wire, path the indices of the
    // part in the nested Compounds (empty for an object on its own); netgen and OCC skip it, as netgen's own properties.
    // netgen reads every file in millimetres: its maxh and the shifts of its identifications are written in millimetres
    class StepWriter {
    public:
        explicit StepWriter(const StepLengthUnit &unit = {".MILLI.", "", 1.0}) : mmPerUnit(unit.mm) {
            app = Add("APPLICATION_CONTEXT('core data for automotive mechanical design processes')");
            Add("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000," + Ref(app) + ")");
            size_t length;
            if (unit.name.empty())
                length = Add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(" + unit.prefix + ",.METRE.) )");
            else {
                const size_t mm = Add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )");
                const size_t measure = Add("LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(" + StepReal(unit.mm) + ")," + Ref(mm) + ")");
                const size_t exponents = Add("DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.)");
                length = Add("( CONVERSION_BASED_UNIT(" + StepString(unit.name) + "," + Ref(measure) + ") LENGTH_UNIT() NAMED_UNIT(" +
                             Ref(exponents) + ") )");
            }
            const size_t angle = Add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )");
            const size_t solid = Add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )");
            const size_t uncertainty = Add("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(" + StepReal(1e-7 / mmPerUnit) + ")," + Ref(length) +
                                           ",'distance_accuracy_value','confusion accuracy')");
            context = Add("( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((" + Ref(uncertainty) +
                          ")) GLOBAL_UNIT_ASSIGNED_CONTEXT((" + Ref(length) + "," + Ref(angle) + "," + Ref(solid) +
                          ")) REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT and UNCERTAINTY') )");
            productContext = Add("PRODUCT_CONTEXT(''," + Ref(app) + ",'mechanical')");
            definitionContext = Add("PRODUCT_DEFINITION_CONTEXT('part definition'," + Ref(app) + ",'design')");
        }

        // one part: the faces and free wires of the shape as its export with these options has them; kind its planex
        // type, path where it is in the nested Compounds ("2.0": part 0 of part 2)
        void Part(const Shape &shape, const std::string &kind, const std::string &path, const ArcFit &arcs, const SplineFit &fit,
                  const Cleanup &cleanup) {
            const Shape::ExportPlan plan = shape.Export(arcs, fit, cleanup);
            const Wire &c = plan.edges;
            const AxisSystem &axes = c.Axes();
            vertices.clear();
            edgeCurves.assign(c.Size(), 0);
            std::vector<size_t> items, faces, curves;
            const size_t plane = Add("PLANE(''," + Ref(Placement(axes.Origin(), axes.Normal(), axes.XDirection())) + ")");
            for (size_t k = 0; k < plan.groupProps.size(); ++k)
                for (const std::vector<size_t> &faceEdges : plan.facesOfGroup[k])
                    faces.push_back(FaceOf(c, faceEdges, plane, plan.groupProps[k]));
            if (!faces.empty())
                items.push_back(Add("SHELL_BASED_SURFACE_MODEL('',(" + Ref(Add("OPEN_SHELL('',(" + Refs(faces) + "))")) + "))"));
            std::vector<size_t> itemOf(c.Size(), 0);
            for (size_t i = 0; i < c.Size(); ++i)
                if (!plan.IsOnGroup(i))
                    curves.push_back(itemOf[i] = Trimmed(c[i], axes));
            for (size_t i = 0; i < c.Size(); ++i)
                if (edgeCurves[i])
                    itemOf[i] = edgeCurves[i];
            IdentificationsOf(c, plan.ids, itemOf);
            if (!curves.empty())
                items.push_back(Add("GEOMETRIC_CURVE_SET('',(" + Refs(curves) + "))"));
            if (!items.empty()) {
                // the plane last: a part of wires only has its plane nowhere else
                std::vector<size_t> marked = items;
                marked.push_back(plane);
                Add("COMPOUND_REPRESENTATION_ITEM(" + StepString("planex_part " + kind + " " + path) + ",(" + Refs(marked) + "))");
            }
            all.insert(all.end(), items.begin(), items.end());
        }

        std::string Text() {
            Product();
            if (!styled.empty())
                Add("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',(" + Refs(styled) + ")," + Ref(context) + ")");
            styled.clear();
            std::string out = "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('planex'),'2;1');\n"
                              "FILE_NAME('','',(''),(''),'planex','planex','');\n"
                              "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n";
            for (size_t k = 0; k < entities.size(); ++k)
                out += "#" + std::to_string(k + 1) + " = " + entities[k] + ";\n";
            return out + "ENDSEC;\nEND-ISO-10303-21;\n";
        }

    private:
        std::vector<std::string> entities;
        std::vector<size_t> all;
        std::vector<size_t> styled;
        std::map<std::pair<double, double>, size_t> vertices;
        std::vector<size_t> edgeCurves;
        size_t app = 0, context = 0, productContext = 0, definitionContext = 0;
        // millimetres per unit of the file: netgen's lengths are in millimetres
        double mmPerUnit = 1.0;

        size_t Add(std::string record) {
            entities.push_back(std::move(record));
            return entities.size();
        }
        static std::string Ref(size_t id) { return "#" + std::to_string(id); }
        static std::string Refs(const std::vector<size_t> &ids) {
            std::string out;
            for (size_t k = 0; k < ids.size(); ++k)
                out += (k > 0 ? "," : "") + Ref(ids[k]);
            return out;
        }
        static std::string Name(const Properties &p) { return StepString(p && p->name ? *p->name : ""); }
        static std::string Triple(const Vector3 &v) { return "(" + StepReal(v.x) + "," + StepReal(v.y) + "," + StepReal(v.z) + ")"; }
        size_t Point(const Vector3 &p) { return Add("CARTESIAN_POINT(''," + Triple(p) + ")"); }
        size_t Direction(const Vector3 &d) { return Add("DIRECTION(''," + Triple(d) + ")"); }
        size_t Placement(const Vector3 &origin, const Vector3 &axis, const Vector3 &x) {
            const size_t o = Point(origin), a = Direction(axis), r = Direction(x);
            return Add("AXIS2_PLACEMENT_3D(''," + Ref(o) + "," + Ref(a) + "," + Ref(r) + ")");
        }

        void Product() {
            const size_t product = Add("PRODUCT('planex','planex','',(" + Ref(productContext) + "))");
            const size_t formation = Add("PRODUCT_DEFINITION_FORMATION('',''," + Ref(product) + ")");
            const size_t definition = Add("PRODUCT_DEFINITION('design',''," + Ref(formation) + "," + Ref(definitionContext) + ")");
            const size_t shape = Add("PRODUCT_DEFINITION_SHAPE('',''," + Ref(definition) + ")");
            std::vector<size_t> items{Placement({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0})};
            items.insert(items.end(), all.begin(), all.end());
            const size_t representation = Add("SHAPE_REPRESENTATION('',(" + Refs(items) + ")," + Ref(context) + ")");
            Add("SHAPE_DEFINITION_REPRESENTATION(" + Ref(shape) + "," + Ref(representation) + ")");
            Add("PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(" + Ref(product) + "))");
        }

        // netgen's properties of an item: maxh and hpref (occgeom.cpp WriteProperties)
        void NetgenProperties(size_t item, const Properties &p) {
            if (!p)
                return;
            std::vector<size_t> values{item};
            if (p->maxh)
                values.push_back(Add("VALUE_REPRESENTATION_ITEM('maxh'," + StepReal(*p->maxh * mmPerUnit) + ")"));
            if (p->hpref)
                values.push_back(Add("VALUE_REPRESENTATION_ITEM('hpref'," + StepReal(*p->hpref) + ")"));
            if (values.size() > 1)
                Add("COMPOUND_REPRESENTATION_ITEM('netgen_geometry_properties',(" + Refs(values) + "))");
        }
        // OCC writes a colour as sRGB and reads it back as linear RGB, as planex keeps it
        static double SRGB(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }
        void Colour(size_t item, const Properties &p) {
            if (!p || !p->col)
                return;
            const std::array<double, 4> &c = *p->col;
            const size_t rgb = Add("COLOUR_RGB(''," + StepReal(SRGB(c[0])) + "," + StepReal(SRGB(c[1])) + "," + StepReal(SRGB(c[2])) + ")");
            const size_t fill = Add("FILL_AREA_STYLE('',(" + Ref(Add("FILL_AREA_STYLE_COLOUR(''," + Ref(rgb) + ")")) + "))");
            const size_t side = Add("SURFACE_SIDE_STYLE('',(" + Ref(Add("SURFACE_STYLE_FILL_AREA(" + Ref(fill) + ")")) + "))");
            const size_t usage = Add("SURFACE_STYLE_USAGE(.BOTH.," + Ref(side) + ")");
            const size_t font = Add("DRAUGHTING_PRE_DEFINED_CURVE_FONT('continuous')");
            const size_t curve = Add("CURVE_STYLE(''," + Ref(font) + ",POSITIVE_LENGTH_MEASURE(0.1)," + Ref(rgb) + ")");
            const size_t style = Add("PRESENTATION_STYLE_ASSIGNMENT((" + Ref(usage) + "," + Ref(curve) + "))");
            styled.push_back(Add("STYLED_ITEM('color',(" + Ref(style) + ")," + Ref(item) + ")"));
        }

        size_t VertexAt(const Vertex &uv, const AxisSystem &axes, const Properties &p) {
            const auto [it, added] = vertices.try_emplace({uv.x, uv.y}, 0);
            if (added) {
                it->second = Add("VERTEX_POINT(" + Name(p) + "," + Ref(Point(axes.ToXYZ(uv))) + ")");
                NetgenProperties(it->second, p);
            }
            return it->second;
        }

        // the 3d curve of an edge: a line, a circle turning with the edge, or the exact b-spline of a spline piece
        size_t CurveOf(const Edge &e, const AxisSystem &axes) {
            if (const Line *l = std::get_if<Line>(&e)) {
                const Vector3 a = axes.ToXYZ(l->Start()), b = axes.ToXYZ(l->End());
                const size_t vector = Add("VECTOR(''," + Ref(Direction(Normalized(b - a))) + ",1.)");
                return Add("LINE(''," + Ref(Point(a)) + "," + Ref(vector) + ")");
            }
            if (const Arc *a = std::get_if<Arc>(&e))
                return CircleOf(axes, a->Center(), a->Start(), a->Radius(), a->CCW());
            if (const Circle *c = std::get_if<Circle>(&e))
                return CircleOf(axes, c->Center(), c->Origin(), c->Radius(), c->CCW());
            const BSplineData d = std::get<Spline>(e).BSpline();
            std::vector<size_t> poles;
            for (const Vertex &p : d.poles)
                poles.push_back(Point(axes.ToXYZ(p)));
            std::vector<double> knots;
            std::vector<size_t> multiplicities;
            for (double k : d.knots) {
                if (!knots.empty() && knots.back() == k) {
                    ++multiplicities.back();
                } else {
                    knots.push_back(k);
                    multiplicities.push_back(1);
                }
            }
            std::string m = "(", u = "(";
            for (size_t k = 0; k < knots.size(); ++k) {
                m += (k > 0 ? "," : "") + std::to_string(multiplicities[k]);
                u += (k > 0 ? "," : "") + StepReal(knots[k]);
            }
            m += ")";
            u += ")";
            const std::string closed = d.poles.front() == d.poles.back() ? ".T." : ".F.";
            if (d.weights.empty())
                return Add("B_SPLINE_CURVE_WITH_KNOTS(''," + std::to_string(d.degree) + ",(" + Refs(poles) + "),.UNSPECIFIED.," + closed +
                           ",.F.," + m + "," + u + ",.UNSPECIFIED.)");
            std::string w = "(";
            for (size_t k = 0; k < d.weights.size(); ++k)
                w += (k > 0 ? "," : "") + StepReal(d.weights[k]);
            return Add("( BOUNDED_CURVE() B_SPLINE_CURVE(" + std::to_string(d.degree) + ",(" + Refs(poles) + "),.UNSPECIFIED.," + closed +
                       ",.F.) B_SPLINE_CURVE_WITH_KNOTS(" + m + "," + u + ",.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
                       "RATIONAL_B_SPLINE_CURVE(" + w + ")) REPRESENTATION_ITEM('') )");
        }
        size_t CircleOf(const AxisSystem &axes, const Vertex &center, const Vertex &start, double radius, bool ccw) {
            const Vector3 c = axes.ToXYZ(center);
            const Vector3 x = Normalized(axes.ToXYZ(start) - c);
            return Add("CIRCLE(''," + Ref(Placement(c, ccw ? axes.Normal() : -axes.Normal(), x)) + "," + StepReal(radius) + ")");
        }

        // an edge of the faces once, shared by the faces on both sides
        size_t EdgeCurveOf(const Wire &c, size_t i) {
            if (edgeCurves[i])
                return edgeCurves[i];
            const Edge &e = c[i];
            const Vertex start = StartOf(e), end = EndOf(e);
            const size_t v0 = VertexAt(start, c.Axes(), PropsAtEnd(EndsOf(e), false));
            const size_t v1 = start == end ? v0 : VertexAt(end, c.Axes(), PropsAtEnd(EndsOf(e), true));
            const Properties &p = PropsOf(e);
            edgeCurves[i] = Add("EDGE_CURVE(" + Name(p) + "," + Ref(v0) + "," + Ref(v1) + "," + Ref(CurveOf(e, c.Axes())) + ",.T.)");
            NetgenProperties(edgeCurves[i], p);
            return edgeCurves[i];
        }

        // a face of the export: its loops as oriented edges
        size_t FaceOf(const Wire &c, const std::vector<size_t> &faceEdges, size_t plane, const Properties &props) {
            std::vector<size_t> bounds;
            for (const FaceLoop &loop : FaceLoops(c, faceEdges)) {
                std::vector<size_t> oriented;
                for (const auto &[i, forward] : loop.edges)
                    oriented.push_back(Add("ORIENTED_EDGE('',*,*," + Ref(EdgeCurveOf(c, i)) + (forward ? ",.T.)" : ",.F.)")));
                const size_t edgeLoop = Add("EDGE_LOOP('',(" + Refs(oriented) + "))");
                bounds.push_back(Add(std::string(loop.outer ? "FACE_OUTER_BOUND" : "FACE_BOUND") + "(''," + Ref(edgeLoop) + ",.T.)"));
            }
            const size_t face = Add("ADVANCED_FACE(" + Name(props) + ",(" + Refs(bounds) + ")," + Ref(plane) + ",.T.)");
            NetgenProperties(face, props);
            Colour(face, props);
            return face;
        }

        // netgen's identifications (occgeom.cpp WriteIdentifications), as planex's OCC export makes them: on every edge
        // named from, the edges named to that its transformation takes onto them (their ends and midpoints), with the
        // transformation in 3d, row by row, and its shift
        void IdentificationsOf(const Wire &c, const Identifications &ids, const std::vector<size_t> &itemOf) {
            const AxisSystem &axes = c.Axes();
            std::map<size_t, std::vector<size_t>> onEdge;
            for (const Identification &id : ids) {
                std::vector<size_t> from, to;
                EdgeList a, b;
                for (size_t i = 0; i < c.Size(); ++i) {
                    const Properties &p = PropsOf(c[i]);
                    if (!p || !p->name || (*p->name != id.from && *p->name != id.to))
                        continue;
                    const bool isFrom = *p->name == id.from;
                    (isFrom ? from : to).push_back(i);
                    (isFrom ? a : b).push_back(c[i]);
                }
                if (from.empty() || to.empty())
                    throw std::runtime_error("identification '" + id.name + "': no exported edge is named '" + (from.empty() ? id.from : id.to) + "'");
                const Affine2 t = id.trafo ? *id.trafo : GetTransformation(a, b, false, "'" + id.from + "'", "'" + id.to + "'");
                const Vector3 o = axes.Origin(), n = axes.Normal();
                const auto map3 = [&](const Vector3 &p) { return axes.ToXYZ(t(axes.ToHV(p))) + n * Dot(p - o, n); };
                const Vector3 shift = map3({0.0, 0.0, 0.0});
                const Vector3 columns[3] = {map3({1.0, 0.0, 0.0}) - shift, map3({0.0, 1.0, 0.0}) - shift, map3({0.0, 0.0, 1.0}) - shift};
                const double tol = 1e-7 + 1e-12 * NormInf(BoxOf(c[from.front()]).Size());
                size_t pairs = 0;
                for (size_t f : from)
                    for (size_t g : to) {
                        const Vertex s0 = t(StartOf(c[f])), s1 = t(EndOf(c[f])), m = t(PointAtFraction(c[f], 0.5));
                        const Vertex b0 = StartOf(c[g]), b1 = EndOf(c[g]);
                        const bool ends = (s0.IsClose(b0, tol) && s1.IsClose(b1, tol)) || (s0.IsClose(b1, tol) && s1.IsClose(b0, tol));
                        if (!ends || !m.IsClose(PointAtFraction(c[g], 0.5), tol))
                            continue;
                        ++pairs;
                        std::vector<size_t> items{Add("VALUE_REPRESENTATION_ITEM('',1.)"), itemOf[g]};
                        for (int r = 0; r < 3; ++r)
                            for (const Vector3 &col : columns)
                                items.push_back(Add("VALUE_REPRESENTATION_ITEM(''," + StepReal(r == 0 ? col.x : r == 1 ? col.y : col.z) + ")"));
                        for (double v : {shift.x, shift.y, shift.z})
                            items.push_back(Add("VALUE_REPRESENTATION_ITEM(''," + StepReal(v * mmPerUnit) + ")"));
                        items.push_back(Add("INTEGER_REPRESENTATION_ITEM(''," + std::to_string(id.type) + ")"));
                        onEdge[itemOf[f]].push_back(Add("COMPOUND_REPRESENTATION_ITEM(" + StepString(id.name) + ",(" + Refs(items) + "))"));
                    }
                if (pairs != from.size() || pairs != to.size())
                    throw std::runtime_error("identification '" + id.name + "': " + std::to_string(pairs) + " pairs of the " +
                                             std::to_string(from.size()) + " exported edges named '" + id.from + "' and the " +
                                             std::to_string(to.size()) + " named '" + id.to + "'; the transformation must take every "
                                             "piece of one side onto one of the other");
            }
            for (const auto &[item, list] : onEdge) {
                std::vector<size_t> all{item};
                all.insert(all.end(), list.begin(), list.end());
                Add("COMPOUND_REPRESENTATION_ITEM('netgen_geometry_identification',(" + Refs(all) + "))");
            }
        }

        // a free wire edge: its curve trimmed at its ends, named
        size_t Trimmed(const Edge &e, const AxisSystem &axes) {
            const size_t curve = CurveOf(e, axes);
            const std::string a = Ref(Point(axes.ToXYZ(StartOf(e)))), b = Ref(Point(axes.ToXYZ(EndOf(e))));
            return Add("TRIMMED_CURVE(" + Name(PropsOf(e)) + "," + Ref(curve) + ",(" + a + "),(" + b + "),.T.,.CARTESIAN.)");
        }
    };

}
