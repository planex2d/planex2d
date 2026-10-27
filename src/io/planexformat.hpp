// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/objectid.hpp"
#include "io/json.hpp"
#include "shape/shape.hpp"

namespace planex2d {

    inline constexpr size_t planexFormatVersion = 3;

    class SketchWriter;
    class SketchReader;

    // planex objects as the .px2 JSON, exactly: every member as it is (derived ones too, so another build reads the same
    // bits), the property objects, vertex properties and spline data each once in a table, referred to by index
    class PlanexWriter {
        friend class SketchWriter;

    public:
        Json Write(const AxisSystem &a) {
            Json o{Json::Object{}};
            o.Add("type", "Plane");
            o.Add("plane", Plane(a));
            return o;
        }
        Json Write(const Wire &w) {
            Json o = Head("Wire", w);
            o.Add("edges", Edges(w));
            return o;
        }
        Json Write(const Face &f) {
            Json o = Head("Face", f.Edges());
            o.Add("props", Index(f.Props()));
            o.Add("identifications", Ids(f.Ids()));
            o.Add("edges", Edges(f.Edges()));
            return o;
        }
        Json Write(const Shape &s) {
            Json o = Head("Shape", s.edges);
            Json::Array owners, parts, groups;
            for (uint32_t k : s.edges.owners)
                owners.push_back((size_t)k);
            for (const std::vector<uint32_t> &p : s.ownerParts) {
                Json::Array part;
                for (uint32_t k : p)
                    part.push_back((size_t)k);
                parts.push_back(std::move(part));
            }
            for (const Properties &p : s.groupProps)
                groups.push_back(Index(p));
            o.Add("owners", std::move(owners));
            o.Add("face_owners", (size_t)s.edges.faceOwners);
            o.Add("owner_parts", std::move(parts));
            o.Add("group_props", std::move(groups));
            o.Add("wire_count", s.wireCount);
            o.Add("identifications", Ids(s.ids));
            o.Add("edges", Edges(s.edges));
            return o;
        }
        // the file: the version, its unit (metres per unit of its numbers), the tables and the object
        std::string Document(Json object, double unit = 1.0) const {
            Json d{Json::Object{}};
            d.Add("format", "planex");
            d.Add("version", planexFormatVersion);
            d.Add("unit", unit);
            d.Add("properties", properties);
            d.Add("ends", ends);
            d.Add("splines", splines);
            d.Add("object", std::move(object));
            return d.Text() + "\n";
        }

    private:
        Json::Array properties, ends, splines;
        std::unordered_map<const void *, size_t> propertyIndex, endsIndex, splineIndex;

        static Json Numbers(std::initializer_list<double> values) {
            Json::Array a;
            for (double v : values)
                a.push_back(v);
            return a;
        }
        static Json Numbers(const std::vector<double> &values) {
            Json::Array a;
            for (double v : values)
                a.push_back(v);
            return a;
        }
        static Json Plane(const AxisSystem &a) {
            const auto xyz = [](const Vector3 &v) { return Numbers({v.x, v.y, v.z}); };
            Json o{Json::Object{}};
            o.Add("origin", xyz(a.origin));
            o.Add("x", xyz(a.xDirection));
            o.Add("y", xyz(a.yDirection));
            o.Add("normal", xyz(a.normal));
            return o;
        }
        static Json Head(const char *type, const Wire &w) {
            Json o{Json::Object{}};
            o.Add("type", type);
            o.Add("plane", Plane(w.Axes()));
            o.Add("id_kind", w.IdKind());
            o.Add("id", w.AssignedId().empty() ? Json() : Json(w.AssignedId()));
            return o;
        }
        static Json Ids(const Identifications &ids) {
            Json::Array a;
            for (const Identification &i : ids) {
                Json o{Json::Object{}};
                o.Add("name", i.name);
                o.Add("from", i.from);
                o.Add("to", i.to);
                o.Add("trafo", i.trafo ? Numbers({i.trafo->m[0], i.trafo->m[1], i.trafo->m[2], i.trafo->m[3], i.trafo->t.x, i.trafo->t.y}) : Json());
                o.Add("type", i.type);
                a.push_back(std::move(o));
            }
            return a;
        }
        Json Index(const Properties &p) {
            if (!p)
                return nullptr;
            if (const auto it = propertyIndex.find(p.get()); it != propertyIndex.end())
                return it->second;
            Json o{Json::Object{}};
            if (p->name)
                o.Add("name", *p->name);
            if (p->maxh)
                o.Add("maxh", *p->maxh);
            if (p->minh)
                o.Add("minh", *p->minh);
            if (p->hpref)
                o.Add("hpref", *p->hpref);
            if (p->layer)
                o.Add("layer", *p->layer);
            if (p->col)
                o.Add("col", Numbers({(*p->col)[0], (*p->col)[1], (*p->col)[2], (*p->col)[3]}));
            if (p->partition)
                o.Add("partition", Numbers(*p->partition));
            if (p->quad_dominated)
                o.Add("quad_dominated", *p->quad_dominated);
            if (p->id)
                o.Add("id", *p->id);
            properties.push_back(std::move(o));
            return propertyIndex[p.get()] = properties.size() - 1;
        }
        Json Index(const Ends &e) {
            if (!e)
                return nullptr;
            if (const auto it = endsIndex.find(e.get()); it != endsIndex.end())
                return it->second;
            Json o{Json::Object{}};
            o.Add("start", Index(e->start));
            o.Add("end", Index(e->end));
            ends.push_back(std::move(o));
            return endsIndex[e.get()] = ends.size() - 1;
        }
        Json Index(const std::shared_ptr<const SplineData> &d) {
            if (const auto it = splineIndex.find(d.get()); it != splineIndex.end())
                return it->second;
            const Curve<double, Vertex> &c = d->curve;
            Json::Array tangentAt, poles, nodes, approx;
            for (size_t k : c.tangentAt)
                tangentAt.push_back(k);
            for (const Vertex &p : c.poles) {
                poles.push_back(p.x);
                poles.push_back(p.y);
            }
            for (const PointOnCurve &n : d->nodes) {
                nodes.push_back(n.t);
                nodes.push_back(n.p.x);
                nodes.push_back(n.p.y);
            }
            for (const PrimitiveEdge &e : d->approx)
                approx.push_back(std::visit([&](const auto &x) { return EdgeOf(x); }, static_cast<const std::variant<Line, Arc> &>(e)));
            Json o{Json::Object{}};
            o.Add("degree", c.degree);
            o.Add("closed", c.closed);
            o.Add("knots", Numbers(c.knots));
            o.Add("params", Numbers(c.params));
            o.Add("tangent_at", std::move(tangentAt));
            o.Add("poles", std::move(poles));
            o.Add("weights", Numbers(c.weights));
            o.Add("nodes", std::move(nodes));
            o.Add("approximation", std::move(approx));
            o.Add("approximation_params", Numbers(d->params));
            splines.push_back(std::move(o));
            return splineIndex[d.get()] = splines.size() - 1;
        }
        void Common(Json &o, const Properties &props, const Ends &e, bool fromSpline) {
            if (props)
                o.Add("props", Index(props));
            if (e)
                o.Add("ends", Index(e));
            if (fromSpline)
                o.Add("from_spline", true);
        }
        Json EdgeOf(const Line &l) {
            Json o{Json::Object{}};
            o.Add("line", Numbers({l.Start().x, l.Start().y, l.End().x, l.End().y}));
            Common(o, l.props, l.ends, l.fromSpline);
            return o;
        }
        Json EdgeOf(const Arc &a) {
            Json o{Json::Object{}};
            o.Add("arc", Numbers({a.start.x, a.start.y, a.end.x, a.end.y, a.center.x, a.center.y, a.radius, a.sweep}));
            o.Add("ccw", a.ccw);
            Common(o, a.props, a.ends, a.fromSpline);
            return o;
        }
        Json EdgeOf(const Circle &c) {
            Json o{Json::Object{}};
            o.Add("circle", Numbers({c.center.x, c.center.y, c.origin.x, c.origin.y, c.radius}));
            o.Add("ccw", c.ccw);
            Common(o, c.props, c.ends, false);
            return o;
        }
        Json EdgeOf(const Spline &s) {
            Json o{Json::Object{}};
            o.Add("spline", Index(s.data));
            o.Add("first", s.first);
            o.Add("last", s.last);
            Common(o, s.props, s.ends, false);
            return o;
        }
        Json Edges(const Wire &w) {
            Json::Array a;
            for (const Edge &e : w)
                a.push_back(std::visit([&](const auto &x) { return EdgeOf(x); }, e));
            return a;
        }
    };

    // a .px2 text back as the objects it holds, bit for bit; ids kept, and the id counters moved past them so that later
    // auto ids do not repeat one
    class PlanexReader {
        friend class SketchReader;

    public:
        explicit PlanexReader(std::string_view text) : doc(JsonParser(text).Document()) {
            if (!doc.IsObject() || !doc.Has("format") || doc["format"].AsString() != "planex")
                throw std::invalid_argument("planex file: not a planex file (no \"format\": \"planex\")");
            const size_t version = doc["version"].AsIndex();
            if (version > planexFormatVersion)
                throw std::invalid_argument("planex file: format version " + std::to_string(version) + " is newer than this planex reads (" +
                                            std::to_string(planexFormatVersion) + "); update planex");
            for (const Json &p : doc["properties"].AsArray())
                properties.push_back(PropertiesOf(p));
            for (const Json &e : doc["ends"].AsArray())
                ends.push_back(MakeEnds(PropertiesAt(e["start"]), PropertiesAt(e["end"])));
            for (const Json &s : doc["splines"].AsArray())
                splines.push_back(SplineDataOf(s));
            if (version >= 2)
                unit = doc["unit"].AsNumber();
            if (!(unit > 0.0) || !std::isfinite(unit))
                throw std::invalid_argument("planex file: the unit is a positive length in meters");
        }

        const Json &Object() const { return doc["object"]; }
        // metres per unit of the file's numbers; a version 1 file counts in metres
        double Unit() const { return unit; }

        AxisSystem PlaneOf(const Json &node) const {
            const auto xyz = [](const Json &j) {
                const Json::Array &a = j.AsArray();
                if (a.size() != 3)
                    throw std::invalid_argument("planex file: a 3d vector has three numbers");
                const Vector3 v{a[0].AsNumber(), a[1].AsNumber(), a[2].AsNumber()};
                IsFinite("planex file", {v.x, v.y, v.z});
                return v;
            };
            AxisSystem a;
            a.origin = xyz(node["origin"]);
            a.xDirection = xyz(node["x"]);
            a.yDirection = xyz(node["y"]);
            a.normal = xyz(node["normal"]);
            const auto near = [](double v, double want) { return std::abs(v - want) <= 1e-12; };
            if (!near(Dot(a.normal, a.normal), 1.0) || !near(Dot(a.xDirection, a.xDirection), 1.0) || !near(Dot(a.normal, a.xDirection), 0.0) ||
                !(Norm(a.yDirection - Cross(a.normal, a.xDirection)) <= 1e-12))
                throw std::invalid_argument("planex file: a plane's directions are not orthonormal and right-handed");
            return a;
        }
        Wire WireOf(const Json &node) {
            Wire w(PlaneOf(node["plane"]));
            for (const Json &e : node["edges"].AsArray())
                w.Add(EdgeOf(e));
            Name(w, node);
            return w;
        }
        Face FaceOf(const Json &node) {
            Face f(WireOf(node), PropertiesAt(node["props"]));
            f.Ids() = IdsOf(node["identifications"]);
            return f;
        }
        Shape ShapeOf(const Json &node) {
            Shape s(PlaneOf(node["plane"]));
            for (const Json &e : node["edges"].AsArray())
                s.edges.Add(EdgeOf(e));
            for (const Json &k : node["owners"].AsArray())
                s.edges.owners.push_back((uint32_t)k.AsIndex());
            s.edges.faceOwners = (uint32_t)node["face_owners"].AsIndex();
            for (const Json &p : node["owner_parts"].AsArray()) {
                std::vector<uint32_t> part;
                for (const Json &k : p.AsArray())
                    part.push_back((uint32_t)k.AsIndex());
                s.ownerParts.push_back(std::move(part));
            }
            for (const Json &p : node["group_props"].AsArray())
                s.groupProps.push_back(PropertiesAt(p));
            s.wireCount = node["wire_count"].AsIndex();
            s.ids = IdsOf(node["identifications"]);
            const size_t partCount = s.groupProps.size() + s.wireCount;
            bool fits = (s.edges.owners.empty() || s.edges.owners.size() == s.edges.Size()) && s.edges.faceOwners <= s.ownerParts.size() &&
                        (!s.edges.owners.empty() || s.edges.Size() == 0 || !s.ownerParts.empty());
            for (uint32_t k : s.edges.owners)
                fits = fits && k < s.ownerParts.size();
            for (const std::vector<uint32_t> &p : s.ownerParts)
                for (uint32_t k : p)
                    fits = fits && k < partCount;
            if (!fits)
                throw std::invalid_argument("planex file: a shape's owners and parts do not fit its edges");
            Name(s.edges, node);
            return s;
        }

    private:
        Json doc;
        double unit = 1.0;
        std::vector<Properties> properties;
        std::vector<Ends> ends;
        std::vector<std::shared_ptr<const SplineData>> splines;

        static std::vector<double> NumbersOf(const Json &j) {
            std::vector<double> out;
            for (const Json &v : j.AsArray())
                out.push_back(v.AsNumber());
            return out;
        }
        template <class T>
        static const T &At(const std::vector<T> &table, const Json &j, const char *what) {
            const size_t k = j.AsIndex();
            if (k >= table.size())
                throw std::invalid_argument(std::string("planex file: no ") + what + " number " + std::to_string(k));
            return table[k];
        }
        Properties PropertiesAt(const Json &j) const { return j.IsNull() ? nullptr : At(properties, j, "properties"); }
        Ends EndsAt(const Json &j) const { return j.IsNull() ? nullptr : At(ends, j, "vertex properties"); }

        static Properties PropertiesOf(const Json &j) {
            ShapeProperties p;
            if (j.Has("name"))
                p.name = j["name"].AsString();
            if (j.Has("maxh"))
                p.maxh = j["maxh"].AsNumber();
            if (j.Has("minh"))
                p.minh = j["minh"].AsNumber();
            if (j.Has("hpref"))
                p.hpref = j["hpref"].AsNumber();
            if (j.Has("layer"))
                p.layer = (int)j["layer"].AsNumber();
            if (j.Has("col")) {
                const std::vector<double> c = NumbersOf(j["col"]);
                if (c.size() != 4)
                    throw std::invalid_argument("planex file: a color has four numbers");
                p.col = std::array<double, 4>{c[0], c[1], c[2], c[3]};
            }
            if (j.Has("partition"))
                p.partition = NumbersOf(j["partition"]);
            if (j.Has("quad_dominated"))
                p.quad_dominated = j["quad_dominated"].AsBool();
            if (j.Has("id"))
                p.id = j["id"].AsString();
            return std::make_shared<const ShapeProperties>(std::move(p));
        }
        static Identifications IdsOf(const Json &j) {
            Identifications out;
            for (const Json &i : j.AsArray()) {
                Identification id{i["name"].AsString(), i["from"].AsString(), i["to"].AsString(), std::nullopt, (int)i["type"].AsNumber()};
                if (!i["trafo"].IsNull()) {
                    const std::vector<double> t = NumbersOf(i["trafo"]);
                    if (t.size() != 6)
                        throw std::invalid_argument("planex file: a transformation has six numbers");
                    id.trafo = Affine2{{t[0], t[1], t[2], t[3]}, {t[4], t[5]}};
                }
                out.push_back(std::move(id));
            }
            return out;
        }
        // the object's id, and the counter of its kind moved past it
        static void Name(Wire &w, const Json &node) {
            w.SetIdKind(node["id_kind"].AsString());
            if (node["id"].IsNull())
                return;
            w.SetId(node["id"].AsString());
            AdvanceIdCounter(w.IdKind(), w.AssignedId());
        }
        std::shared_ptr<const SplineData> SplineDataOf(const Json &j) const {
            auto d = std::make_shared<SplineData>();
            Curve<double, Vertex> &c = d->curve;
            c.degree = j["degree"].AsIndex();
            c.closed = j["closed"].AsBool();
            c.knots = NumbersOf(j["knots"]);
            c.params = NumbersOf(j["params"]);
            for (const Json &k : j["tangent_at"].AsArray())
                c.tangentAt.push_back(k.AsIndex());
            const std::vector<double> poles = NumbersOf(j["poles"]), nodes = NumbersOf(j["nodes"]);
            if (poles.size() % 2 != 0 || nodes.size() % 3 != 0)
                throw std::invalid_argument("planex file: a spline's poles or nodes are incomplete");
            for (size_t k = 0; k < poles.size(); k += 2)
                c.poles.push_back({poles[k], poles[k + 1]});
            c.weights = NumbersOf(j["weights"]);
            for (size_t k = 0; k < nodes.size(); k += 3)
                d->nodes.push_back({nodes[k], {nodes[k + 1], nodes[k + 2]}});
            for (const Json &e : j["approximation"].AsArray()) {
                const Edge x = EdgeOf(e);
                if (const Line *l = std::get_if<Line>(&x))
                    d->approx.push_back(*l);
                else if (const Arc *a = std::get_if<Arc>(&x))
                    d->approx.push_back(*a);
                else
                    throw std::invalid_argument("planex file: a spline's approximation has lines and arcs only");
            }
            d->params = NumbersOf(j["approximation_params"]);
            if (c.poles.empty() || c.degree < 1 || (!c.weights.empty() && c.weights.size() != c.poles.size()) || d->approx.empty() ||
                d->params.size() != d->approx.size() + 1)
                throw std::invalid_argument("planex file: a spline's data do not fit together");
            return d;
        }
        Edge EdgeOf(const Json &j) const {
            const Properties props = PropertiesAt(j["props"]);
            const Ends e = EndsAt(j["ends"]);
            const bool fromSpline = j.Has("from_spline") && j["from_spline"].AsBool();
            const auto numbers = [&](const char *key, size_t n) {
                const std::vector<double> v = NumbersOf(j[key]);
                if (v.size() != n)
                    throw std::invalid_argument(std::string("planex file: ") + (std::string_view("aeiou").find(key[0]) == std::string_view::npos ? "a " : "an ") +
                                                key + " has " + std::to_string(n) + " numbers");
                for (double x : v)
                    IsFinite("planex file", {x});
                return v;
            };
            if (j.Has("line")) {
                const std::vector<double> v = numbers("line", 4);
                Line l({v[0], v[1]}, {v[2], v[3]});
                l.props = props;
                l.ends = e;
                l.fromSpline = fromSpline;
                return l;
            }
            if (j.Has("arc")) {
                const std::vector<double> v = numbers("arc", 8);
                Arc a({v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}, j["ccw"].AsBool());
                a.center = {v[4], v[5]};
                a.radius = v[6];
                a.sweep = v[7];
                a.props = props;
                a.ends = e;
                a.fromSpline = fromSpline;
                return a;
            }
            if (j.Has("circle")) {
                const std::vector<double> v = numbers("circle", 5);
                Circle c({v[0], v[1]}, {v[2], v[3]}, j["ccw"].AsBool());
                c.radius = v[4];
                c.props = props;
                c.ends = e;
                return c;
            }
            if (j.Has("spline")) {
                const std::shared_ptr<const SplineData> &d = At(splines, j["spline"], "spline");
                const size_t first = j["first"].AsIndex(), last = j["last"].AsIndex();
                if (!(first < last && last <= d->approx.size()))
                    throw std::invalid_argument("planex file: a spline piece lies outside its spline");
                Spline s(d, first, last, props);
                s.ends = e;
                return s;
            }
            throw std::invalid_argument("planex file: an edge is a line, arc, circle or spline");
        }
    };

}
