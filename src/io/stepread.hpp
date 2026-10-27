// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "boolean/snap.hpp"
#include "curves/ellipse.hpp"
#include "io/steptext.hpp"
#include "shape/shape.hpp"
#include "shape/shapeops.hpp"

namespace planex2d {

    // the planar faces and curves of a STEP file as planex objects, without OCC. Lengths come in millimetres, as OCC
    // reads them (a file in metres or inches is converted; MillimetresPerUnit is the file's unit). A part is what
    // planex's writer marked as one (its planex_part entities), else each top-level item of a shape representation (a
    // shell, solid, face or curve set); each part lies in one plane, its faces cleaned one by one and glued as FromOCC
    // does, its curves exact (lines, arcs, circles, ellipses and b-splines between the edge's vertices); names, netgen's
    // maxh and hpref and the faces' colours (sRGB in the file, linear in planex) come along
    class StepReader {
    public:
        struct Part {
            std::string kind, path;
            Shape shape;
        };

        explicit StepReader(std::string_view text) : file(text) {
            scale = LengthScale();
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *r = e.Record("COMPOUND_REPRESENTATION_ITEM"); r && r->params.size() >= 2)
                    Netgen(*r);
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *r = e.Record("STYLED_ITEM"))
                    Styled(*r);
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *r = e.Record("COMPOUND_REPRESENTATION_ITEM"); r && r->params.size() >= 2 &&
                                                                                      Text(r->params[0]) == "netgen_geometry_identification")
                    NetgenIdentifications(*r);
        }

        double MillimetresPerUnit() const { return scale; }
        std::vector<Part> Parts() {
            std::vector<Part> parts;
            std::vector<size_t> markers;
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *r = e.Record("COMPOUND_REPRESENTATION_ITEM"); r && r->params.size() >= 2 &&
                                                                                      Text(r->params[0]).rfind("planex_part ", 0) == 0)
                    markers.push_back(id);
            std::sort(markers.begin(), markers.end());
            if (file.originatingSystem == "planex" && !markers.empty()) {
                for (size_t id : markers) {
                    const StepRecord &r = *file.At(id).Record("COMPOUND_REPRESENTATION_ITEM");
                    const std::string name = Text(r.params[0]).substr(12);
                    const size_t space = name.find(' ');
                    std::vector<size_t> items;
                    std::optional<AxisSystem> plane;
                    for (const StepValue &v : List(r.params[1]))
                        if (file.At(Ref(v)).Is("PLANE"))
                            plane = PlaneOf(Ref(v));
                        else
                            items.push_back(Ref(v));
                    parts.push_back({name.substr(0, space), space == std::string::npos ? "" : name.substr(space + 1), ShapeOf(items, plane)});
                }
                return parts;
            }
            std::vector<size_t> representations;
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *r = e.Record("SHAPE_DEFINITION_REPRESENTATION"))
                    representations.push_back(Ref(r->params[1]));
            std::sort(representations.begin(), representations.end());
            for (size_t rep : representations)
                for (const StepValue &v : List(file.At(rep).records.front().params[1])) {
                    const StepEntity &item = file.At(Ref(v));
                    if (item.Is("AXIS2_PLACEMENT_3D") || item.Is("MAPPED_ITEM"))
                        continue;
                    parts.push_back({"Shape", std::to_string(parts.size()), ShapeOf({item.id}, std::nullopt)});
                }
            return parts;
        }

    private:
        StepFile file;
        double scale = 1.0;
        std::unordered_map<size_t, ShapeProperties> props;
        AxisSystem axes;
        std::unordered_map<size_t, EdgeList> edgesOf;
        // netgen's identifications by the item of their edge: the name, whether it is the from edge, the other edge, the
        // 3d map (matrix row by row, shift), the type
        struct Linked {
            std::string name;
            bool isFrom;
            size_t other;
            std::array<double, 12> map;
            int type;
        };
        std::unordered_map<size_t, std::vector<Linked>> linked;

        void NetgenIdentifications(const StepRecord &r) {
            const std::vector<StepValue> &all = List(r.params[1]);
            if (all.empty())
                return;
            const size_t item = Ref(all.front());
            for (size_t k = 1; k < all.size(); ++k) {
                const StepRecord &id = Only(Ref(all[k]));
                const std::vector<StepValue> &v = List(id.params[1]);
                if (v.size() != 15)
                    Fail("an identification of netgen has " + std::to_string(v.size()) + " items, not 15");
                Linked l{Text(id.params[0]), Number(Only(Ref(v[0])).params[1]) != 0.0, Ref(v[1]), {}, (int)Number(Only(Ref(v[14])).params[1])};
                for (size_t j = 0; j < 12; ++j)
                    l.map[j] = Number(Only(Ref(v[2 + j])).params[1]);
                linked[item].push_back(std::move(l));
            }
        }
        // the identifications of the edges read for the part: their map in the plane, from the 3d map
        Identifications IdentificationsOfPart(const std::vector<size_t> &items) const {
            Identifications out;
            for (size_t item : items) {
                const auto it = linked.find(item);
                if (it == linked.end())
                    continue;
                for (const Linked &l : it->second) {
                    // netgen's map works in millimetres, as the coordinates here are
                    const auto map2 = [&](const Vertex &uv) {
                        const Vector3 p = axes.ToXYZ(uv);
                        const Vector3 q{l.map[0] * p.x + l.map[1] * p.y + l.map[2] * p.z + l.map[9],
                                        l.map[3] * p.x + l.map[4] * p.y + l.map[5] * p.z + l.map[10],
                                        l.map[6] * p.x + l.map[7] * p.y + l.map[8] * p.z + l.map[11]};
                        return axes.ToHV(q);
                    };
                    const Vertex t = map2({0.0, 0.0}), ex = map2({1.0, 0.0}) - t, ey = map2({0.0, 1.0}) - t;
                    const std::string a = NameOf(item), b = NameOf(l.other);
                    Identification id{l.name, l.isFrom ? a : b, l.isFrom ? b : a, Affine2{{ex.x, ey.x, ex.y, ey.y}, t}, l.type};
                    const bool known = std::any_of(out.begin(), out.end(), [&](const Identification &x) {
                        return x.name == id.name && x.from == id.from && x.to == id.to;
                    });
                    if (!known)
                        out.push_back(std::move(id));
                }
            }
            return out;
        }

        [[noreturn]] static void Fail(const std::string &what) { throw std::invalid_argument("FromStep: " + what); }
        // what reads curved and 3d shapes (FromOCC reads planar ones only)
        static constexpr const char *inNetgen = "; netgen.occ reads the file (OCCGeometry(path).shape), px.Intersect3d(plane, shape) cuts a section from it, px.Project(shape, plane) projects it";
        static size_t Ref(const StepValue &v) {
            if (v.kind != StepValue::Kind::Ref)
                Fail("expected a reference to an entity");
            return v.ref;
        }
        static const std::vector<StepValue> &List(const StepValue &v) {
            if (v.kind != StepValue::Kind::List)
                Fail("expected a list");
            return v.list;
        }
        static std::string Text(const StepValue &v) { return v.kind == StepValue::Kind::String ? v.text : ""; }
        static double Number(const StepValue &v) {
            if (v.kind == StepValue::Kind::Typed && v.list.size() == 1)
                return Number(v.list.front());
            if (v.kind != StepValue::Kind::Number)
                Fail("expected a number");
            return v.number;
        }
        static bool Flag(const StepValue &v) { return v.kind == StepValue::Kind::Enum && v.text == "T"; }
        const StepRecord &Only(size_t id) const { return file.At(id).records.front(); }

        // millimetres per length unit of the file: the length unit its context assigns (a converted unit such as the
        // inch refers to a millimetre unit of its own)
        double LengthScale() const {
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *g = e.Record("GLOBAL_UNIT_ASSIGNED_CONTEXT"); g && !g->params.empty() && g->params[0].kind == StepValue::Kind::List)
                    for (const StepValue &u : g->params[0].list)
                        if (u.kind == StepValue::Kind::Ref && file.At(u.ref).Is("LENGTH_UNIT"))
                            return UnitScale(file.At(u.ref));
            return 1.0;
        }
        double UnitScale(const StepEntity &e) const {
            if (const StepRecord *si = e.Record("SI_UNIT")) {
                const StepValue &prefix = si->params[0];
                if (prefix.kind != StepValue::Kind::Enum)
                    return 1000.0;
                static const std::map<std::string, double> prefixes{{"MILLI", 1.0}, {"CENTI", 10.0}, {"DECI", 100.0}, {"KILO", 1e6},
                                                                    {"MICRO", 1e-3}, {"NANO", 1e-6}};
                const auto it = prefixes.find(prefix.text);
                return it == prefixes.end() ? 1.0 : it->second;
            }
            if (const StepRecord *c = e.Record("CONVERSION_BASED_UNIT")) {
                const StepRecord &measure = Only(Ref(c->params[1]));
                return Number(measure.params[0]) * UnitScale(file.At(Ref(measure.params[1])));
            }
            return 1.0;
        }

        // netgen's maxh and hpref of an item (occgeom.cpp ReadProperties)
        void Netgen(const StepRecord &r) {
            if (Text(r.params[0]) != "netgen_geometry_properties" || r.params[1].kind != StepValue::Kind::List || r.params[1].list.empty())
                return;
            const size_t item = Ref(r.params[1].list.front());
            for (size_t k = 1; k < r.params[1].list.size(); ++k) {
                const StepRecord &v = Only(Ref(r.params[1].list[k]));
                if (v.type != "VALUE_REPRESENTATION_ITEM")
                    continue;
                if (Text(v.params[0]) == "maxh")
                    props[item].maxh = Number(v.params[1]);
                if (Text(v.params[0]) == "hpref")
                    props[item].hpref = Number(v.params[1]);
            }
        }
        // the colour of a styled face: sRGB in the file, linear in planex as OCC reads it
        static double Linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
        void Styled(const StepRecord &r) {
            if (r.params.size() < 3 || r.params[2].kind != StepValue::Kind::Ref)
                return;
            std::vector<size_t> todo;
            for (const StepValue &v : List(r.params[1]))
                if (v.kind == StepValue::Kind::Ref)
                    todo.push_back(v.ref);
            while (!todo.empty()) {
                const StepEntity &e = file.At(todo.back());
                todo.pop_back();
                if (const StepRecord *rgb = e.Record("COLOUR_RGB")) {
                    props[r.params[2].ref].col = std::array<double, 4>{Linear(Number(rgb->params[1])), Linear(Number(rgb->params[2])),
                                                                       Linear(Number(rgb->params[3])), 1.0};
                    return;
                }
                if (e.Is("CURVE_STYLE"))
                    continue;
                for (const StepRecord &rec : e.records)
                    for (const StepValue &v : rec.params) {
                        if (v.kind == StepValue::Kind::Ref)
                            todo.push_back(v.ref);
                        for (const StepValue &w : v.list)
                            if (w.kind == StepValue::Kind::Ref)
                                todo.push_back(w.ref);
                    }
            }
        }
        // an entity's name: its first parameter, of a complex entity its REPRESENTATION_ITEM's
        std::string NameOf(size_t id) const {
            const StepEntity &e = file.At(id);
            const StepRecord *r = e.records.size() == 1 ? &e.records.front() : e.Record("REPRESENTATION_ITEM");
            return r && !r->params.empty() ? Text(r->params[0]) : "";
        }
        Properties PropsOfItem(size_t id) const {
            const std::string name = NameOf(id);
            const auto it = props.find(id);
            if (name.empty() && it == props.end())
                return nullptr;
            ShapeProperties p = it == props.end() ? ShapeProperties{} : it->second;
            if (!name.empty())
                p.name = name;
            return std::make_shared<const ShapeProperties>(std::move(p));
        }

        Vector3 PointOf(size_t id) const {
            const std::vector<StepValue> &c = List(Only(id).params[1]);
            if (c.size() != 3)
                Fail("a point of the shape has " + std::to_string(c.size()) + " coordinates, not 3");
            return Vector3{Number(c[0]), Number(c[1]), Number(c[2])} * scale;
        }
        Vector3 DirectionOf(const StepValue &v, const Vector3 &otherwise) const {
            if (v.kind != StepValue::Kind::Ref)
                return otherwise;
            const std::vector<StepValue> &c = List(Only(v.ref).params[1]);
            return Normalized(Vector3{Number(c[0]), Number(c[1]), Number(c[2])});
        }
        // origin, axis and x direction of an AXIS2_PLACEMENT_3D
        std::tuple<Vector3, Vector3, Vector3> Placement(size_t id) const {
            const StepRecord &p = Only(id);
            const Vector3 axis = DirectionOf(p.params[2], {0.0, 0.0, 1.0});
            return {PointOf(Ref(p.params[1])), axis, DirectionOf(p.params[3], std::abs(axis.x) < 0.9 ? Vector3{1.0, 0.0, 0.0} : Vector3{0.0, 1.0, 0.0})};
        }
        AxisSystem PlaneOf(size_t id) const {
            const StepRecord &plane = Only(id);
            if (plane.type != "PLANE")
                Fail("a face lies on a " + plane.type + ", not a plane" + inNetgen);
            const auto [o, n, x] = Placement(Ref(plane.params[1]));
            return AxisSystem(o, n, x);
        }
        // a point of the part in its plane, checked to lie in it
        Vertex InPlane(const Vector3 &p) const {
            const double off = Dot(p - axes.Origin(), axes.Normal());
            // OCC's confusion, plus the rounding of the 12 digits other programs write
            if (std::abs(off) > config::state->axes_distance + 1e-10 * Norm(p - axes.Origin()))
                Fail("a point lies " + NumberText(std::abs(off)) + " off the plane of its part" + inNetgen);
            return axes.ToHV(p);
        }
        bool SamePlane(const AxisSystem &a) const {
            return std::abs(std::abs(Dot(a.Normal(), axes.Normal())) - 1.0) <= 1e-10 &&
                   std::abs(Dot(a.Origin() - axes.Origin(), axes.Normal())) <= config::state->axes_distance + 1e-10 * Norm(a.Origin() - axes.Origin());
        }

        // the planex edges of a curve from the point a to the point b (same: along the curve's direction)
        EdgeList CurveEdges(size_t id, const Vector3 &a, const Vector3 &b, bool same) {
            const StepEntity &e = file.At(id);
            const StepRecord &r = e.records.front();
            const Vertex p = InPlane(a), q = InPlane(b);
            EdgeList out;
            if (r.type == "SURFACE_CURVE" || r.type == "SEAM_CURVE" || r.type == "BOUNDED_SURFACE_CURVE")
                return CurveEdges(Ref(r.params[1]), a, b, same);
            if (r.type == "LINE" || r.type == "POLYLINE") {
                if (p != q)
                    out.push_back(Line(p, q));
                return out;
            }
            if (r.type == "CIRCLE") {
                const auto [c, axis, x] = Placement(Ref(r.params[1]));
                const Vertex center = InPlane(c);
                const bool ccw = (Dot(axis, axes.Normal()) > 0.0) == same;
                if (p == q)
                    out.push_back(Circle(center, p, ccw));
                else
                    out.push_back(Arc(p, q, center, ccw));
                return out;
            }
            if (r.type == "ELLIPSE") {
                const auto [c, axis, x] = Placement(Ref(r.params[1]));
                const Vector3 y = Cross(axis, x);
                const Vertex center = InPlane(c), A = InPlane(c + x * (Number(r.params[2]) * scale)) - center,
                             B = InPlane(c + y * (Number(r.params[3]) * scale)) - center;
                const auto angle = [&](const Vertex &v) {
                    const Vertex d = v - center;
                    const double det = Cross(A, B);
                    return std::atan2(Cross(A, d) / det, Cross(d, B) / det);
                };
                double t0 = angle(same ? p : q), t1 = angle(same ? q : p);
                if (p == q)
                    t1 = t0 + 2.0 * std::numbers::pi;
                else if (t1 <= t0)
                    t1 += 2.0 * std::numbers::pi;
                return Pieces(BSplineCurve(EllipseData(center, A, B, t0, t1), false), p, q, p == q);
            }
            const StepRecord *bs = e.Record("B_SPLINE_CURVE_WITH_KNOTS");
            const StepRecord *shape = e.Record("B_SPLINE_CURVE");
            if (bs && (shape || r.type == "B_SPLINE_CURVE_WITH_KNOTS")) {
                // a simple entity has the b_spline_curve parameters first, the complex one in its own record
                const std::vector<StepValue> &head = shape ? shape->params : bs->params;
                const size_t o = shape ? 0 : 1;
                BSplineData d;
                d.degree = (int)Number(head[o]);
                for (const StepValue &pole : List(head[o + 1]))
                    d.poles.push_back(InPlane(PointOf(Ref(pole))));
                const size_t k = shape ? 0 : 6;
                const std::vector<StepValue> &mults = List(bs->params[k]), &knots = List(bs->params[k + 1]);
                for (size_t i = 0; i < knots.size(); ++i)
                    for (int m = 0; m < (int)Number(mults[i]); ++m)
                        d.knots.push_back(Number(knots[i]));
                if (const StepRecord *rational = e.Record("RATIONAL_B_SPLINE_CURVE"))
                    for (const StepValue &w : List(rational->params[0]))
                        d.weights.push_back(Number(w));
                return Pieces(BSplineCurve(d, false), same ? p : q, same ? q : p, p == q);
            }
            Fail("an edge is a " + r.type + ", which FromStep does not read; FromOCC reads it");
        }
        // the spline pieces of an exact curve, from a to b: its ends set onto the vertices, a part cut out where the
        // vertices lie inside it
        static EdgeList Pieces(const Curve<double, Vertex> &c, const Vertex &a, const Vertex &b, bool closed) {
            std::vector<Spline> pieces = Spline::FromCurve(c);
            EdgeList out;
            if (!closed) {
                const double tol = 1e-9 * std::max(1.0, NormInf(pieces.front().Start()) + NormInf(pieces.back().End()));
                if (!a.IsClose(pieces.front().Start(), tol) || !b.IsClose(pieces.back().End(), tol)) {
                    if (pieces.size() != 1)
                        throw std::invalid_argument("FromStep: an edge on a part of a spline of several pieces is not read yet; FromOCC reads it");
                    pieces.front() = pieces.front().Part(a.IsClose(pieces.front().Start(), tol) ? std::nullopt : std::optional<Vertex>(a),
                                                         b.IsClose(pieces.front().End(), tol) ? std::nullopt : std::optional<Vertex>(b));
                }
                pieces.front().Trim(a, pieces.front().End());
                pieces.back().Trim(pieces.back().Start(), b);
            }
            for (Spline &s : pieces)
                out.push_back(std::move(s));
            return out;
        }

        // an edge of the faces, once: its curve between its vertices, with the edge's and the vertices' properties
        const EdgeList &EdgeOf(size_t id) {
            if (const auto it = edgesOf.find(id); it != edgesOf.end())
                return it->second;
            const StepRecord &r = Only(id);
            const size_t v0 = Ref(r.params[1]), v1 = Ref(r.params[2]);
            EdgeList edges = CurveEdges(Ref(r.params[3]), PointOf(Ref(Only(v0).params[1])), PointOf(Ref(Only(v1).params[1])), Flag(r.params[4]));
            const Properties p = PropsOfItem(id), start = PropsOfItem(v0), end = PropsOfItem(v1);
            for (size_t k = 0; k < edges.size(); ++k) {
                SetProps(edges[k], p);
                SetEnds(edges[k], MakeEnds(k == 0 ? start : nullptr, k + 1 == edges.size() ? end : nullptr));
            }
            return edgesOf[id] = std::move(edges);
        }

        // the faces under an item (a shell, a solid, a face) and its curves (a curve set or a wireframe)
        void Collect(size_t id, std::vector<size_t> &faces, std::vector<size_t> &curves) const {
            const StepEntity &e = file.At(id);
            const std::string &type = e.records.front().type;
            if (type == "ADVANCED_FACE" || type == "FACE_SURFACE") {
                faces.push_back(id);
            } else if (type == "SHELL_BASED_SURFACE_MODEL" || type == "OPEN_SHELL" || type == "CLOSED_SHELL" || type == "CONNECTED_FACE_SET") {
                for (const StepValue &v : List(e.records.front().params[1]))
                    Collect(Ref(v), faces, curves);
            } else if (type == "MANIFOLD_SOLID_BREP" || type == "BREP_WITH_VOIDS") {
                Collect(Ref(e.records.front().params[1]), faces, curves);
            } else if (type == "GEOMETRIC_CURVE_SET" || type == "GEOMETRIC_SET") {
                for (const StepValue &v : List(e.records.front().params[1]))
                    curves.push_back(Ref(v));
            } else if (type == "EDGE_BASED_WIREFRAME_MODEL" || type == "CONNECTED_EDGE_SET") {
                for (const StepValue &v : List(e.records.front().params[1]))
                    if (file.At(Ref(v)).Is("CONNECTED_EDGE_SET"))
                        Collect(Ref(v), faces, curves);
                    else
                        curves.push_back(Ref(v));
            } else {
                Fail("an item is a " + type + ", which FromStep does not read; FromOCC reads it");
            }
        }

        // a free curve: a trimmed curve between its trims, an edge between its vertices, a whole b-spline
        EdgeList FreeCurve(size_t id) {
            const StepEntity &e = file.At(id);
            const StepRecord &r = e.records.front();
            if (r.type == "EDGE_CURVE")
                return EdgeOf(id);
            EdgeList edges;
            if (r.type == "TRIMMED_CURVE") {
                const auto end = [&](const StepValue &trim) -> Vector3 {
                    for (const StepValue &t : List(trim))
                        if (t.kind == StepValue::Kind::Ref && file.At(t.ref).Is("CARTESIAN_POINT"))
                            return PointOf(t.ref);
                    Fail("a trimmed curve without a point at its ends is not read yet");
                };
                edges = CurveEdges(Ref(r.params[1]), end(r.params[2]), end(r.params[3]), Flag(r.params[4]));
            } else if (r.type == "POLYLINE") {
                const std::vector<StepValue> &pts = List(r.params[1]);
                for (size_t k = 0; k + 1 < pts.size(); ++k)
                    if (const Vertex a = InPlane(PointOf(Ref(pts[k]))), b = InPlane(PointOf(Ref(pts[k + 1]))); a != b)
                        edges.push_back(Line(a, b));
            } else if (e.Is("B_SPLINE_CURVE_WITH_KNOTS")) {
                const StepRecord *shape = e.Record("B_SPLINE_CURVE");
                const std::vector<StepValue> &poles = List(shape ? shape->params[1] : r.params[2]);
                edges = CurveEdges(id, PointOf(Ref(poles.front())), PointOf(Ref(poles.back())), true);
            } else {
                Fail("a curve is a " + r.type + ", which FromStep does not read; FromOCC reads it");
            }
            const Properties p = PropsOfItem(id);
            for (Edge &x : edges)
                SetProps(x, p);
            return edges;
        }

        // the plane of a part without faces: of its first circle or ellipse, else of three points not on a line
        AxisSystem CurvesPlane(const std::vector<size_t> &curves) const {
            std::vector<Vector3> pts;
            std::vector<size_t> todo = curves;
            while (!todo.empty() && pts.size() < 64) {
                const StepEntity &e = file.At(todo.back());
                todo.pop_back();
                const StepRecord &r = e.records.front();
                if (r.type == "CIRCLE" || r.type == "ELLIPSE") {
                    const auto [c, n, x] = Placement(Ref(r.params[1]));
                    return AxisSystem(c, n, x);
                }
                if (r.type == "CARTESIAN_POINT") {
                    pts.push_back(PointOf(e.id));
                    continue;
                }
                for (const StepRecord &rec : e.records)
                    for (const StepValue &v : rec.params) {
                        if (v.kind == StepValue::Kind::Ref)
                            todo.push_back(v.ref);
                        for (const StepValue &w : v.list)
                            if (w.kind == StepValue::Kind::Ref)
                                todo.push_back(w.ref);
                    }
            }
            if (pts.empty())
                Fail("a part has no points");
            for (size_t i = 1; i < pts.size(); ++i)
                for (size_t j = i + 1; j < pts.size(); ++j)
                    if (const Vector3 n = Cross(pts[i] - pts[0], pts[j] - pts[0]); Norm(n) > 1e-9 * Dot(pts[i] - pts[0], pts[i] - pts[0]))
                        return AxisSystem(pts[0], n, pts[i] - pts[0]);
            return AxesAlong(pts[0], std::abs(pts.size() > 1 ? Normalized(pts[1] - pts[0]).z : 0.0) < 0.9 ? Vector3{0.0, 0.0, 1.0}
                                                                                                    : Vector3{1.0, 0.0, 0.0});
        }

        Shape ShapeOf(const std::vector<size_t> &items, const std::optional<AxisSystem> &plane) {
            std::vector<size_t> faces, curves;
            for (size_t id : items)
                Collect(id, faces, curves);
            if (plane)
                axes = *plane;
            else if (!faces.empty())
                axes = PlaneOf(Ref(Only(faces.front()).params[2]));
            else
                axes = CurvesPlane(curves);
            edgesOf.clear();
            ShapeList parts;
            for (size_t f : faces) {
                const StepRecord &face = Only(f);
                if (!SamePlane(PlaneOf(Ref(face.params[2]))))
                    Fail(std::string("the faces of a part lie on several planes") + inNetgen);
                Wire w(axes);
                for (const StepValue &bound : List(face.params[1])) {
                    const StepRecord &loop = Only(Ref(Only(Ref(bound)).params[1]));
                    if (loop.type != "EDGE_LOOP")
                        continue;
                    for (const StepValue &oriented : List(loop.params[1]))
                        for (const Edge &x : EdgeOf(Ref(Only(Ref(oriented)).params[3])))
                            w.Add(x);
                }
                if (Wire region = InitialClean(w); !region.Empty())
                    parts.emplace_back(std::move(region), PropsOfItem(f));
            }
            Wire free(axes);
            for (size_t c : curves)
                for (Edge &x : FreeCurve(c))
                    free.Add(std::move(x));
            if (!free.Empty())
                parts.emplace_back(axes, FaceList{}, WireList{free});
            Shape shape = parts.empty() ? Shape(axes) : parts.size() == 1 ? parts.front() : Glue(parts);
            std::vector<size_t> edgeItems(curves);
            for (const auto &[id, edges] : edgesOf)
                edgeItems.push_back(id);
            std::sort(edgeItems.begin(), edgeItems.end());
            shape.Ids() = IdentificationsOfPart(edgeItems);
            return shape;
        }
    };

}
