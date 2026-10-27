// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <bit>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/planexformat.hpp"
#include "sketch/sketch.hpp"

namespace planex2d {

    // the names the file gives the values of the sketch's enums, in their order
    inline constexpr const char *sketchKindNames[] = {
        "Coincident", "Horizontal", "Vertical",   "Parallel",          "Perpendicular", "Tangent", "Equal",     "Symmetric",
        "Concentric", "Fix",        "Distance",   "Radius",            "Diameter",      "Angle",   "SymmetricCarriers",
        "OnCircle",   "Copy",       "RigidGroup", "Expression",        "Value",         "Offset",  "OnCurve",   "Parameter",
        "Curvature"};
    inline constexpr const char *elementKindNames[] = {"Point", "Line", "Circle", "Arc", "Axis", "Pattern", "Spline", "Offset", "OffsetCurve"};
    inline constexpr const char *partNames[] = {"Whole", "Start", "End", "Center", "Mid", "Point", "Tangent"};
    inline constexpr const char *roleNames[] = {"Drawn", "Construction", "Span", "Helper"};
    inline constexpr const char *splineStyleNames[] = {"Averaged", "Clamped"};
    inline constexpr const char *offsetEndNames[] = {"Round", "Flat", "Square"};
    inline constexpr const char *offsetCornerNames[] = {"Round", "Sharp", "Chamfer"};
    inline constexpr size_t opCount = 18;

    // a sketch's state as .px2 JSON: every member that lasts from one call to the next as it is (the constraint
    // solver's own data are rebuilt from them; those of a running call are empty between calls), unordered maps sorted
    // so that the same sketch gives the same text, the property objects in the writer's table
    class SketchWriter {
    public:
        explicit SketchWriter(PlanexWriter &w) : w(w) {}

        template <class S>
        Json Write(const S &sk) {
            // a constraint sketch: its store holds constraints (told by public members: a build without constraints lacks its type)
            constexpr bool constrained = requires(const S &x) { x.Geometry().constraints; };
            Json o{Json::Object{}};
            o.Add("type", constrained ? "Sketch" : "QuickSketch");
            o.Add("plane", PlanexWriter::Plane(sk.axes));
            o.Add("id_kind", sk.IdKind());
            o.Add("id", sk.AssignedId().empty() ? Json() : Json(sk.AssignedId()));
            o.Add("geometry", Geometry(sk.s));
            Json::Array props, roles, byId, counters, points;
            for (const Properties &p : sk.props)
                props.push_back(w.Index(p));
            for (sketch::Role r : sk.roles)
                roles.push_back(roleNames[(size_t)r]);
            for (const auto &[id, t] : std::map<std::string, sketch::IdTarget>(sk.byId.begin(), sk.byId.end()))
                byId.push_back(Json::Array{id, t.isElement, t.index});
            for (const auto &[kind, n] : std::map<std::string, size_t>(sk.counters.begin(), sk.counters.end()))
                counters.push_back(Json::Array{kind, n});
            for (const auto &[point, element] : std::map<size_t, size_t>(sk.pointElementOf.begin(), sk.pointElementOf.end()))
                points.push_back(Json::Array{point, element});
            o.Add("element_ids", Strings(sk.elementIds));
            o.Add("props", std::move(props));
            o.Add("roles", std::move(roles));
            o.Add("by_id", std::move(byId));
            o.Add("counters", std::move(counters));
            o.Add("id_scope", sk.idScope);
            o.Add("wires", Lists(sk.wires));
            o.Add("point_elements", std::move(points));
            o.Add("given_ids", Strings(sk.givenIds));
            o.Add("renamed", Pairs(sk.renamed));
            o.Add("groups", Pairs(sk.groups));
            o.Add("ids_under", Lists(std::map<std::string, std::set<std::string>>(sk.idsUnder.begin(), sk.idsUnder.end())));
            o.Add("pen", Pen(sk.pen));
            o.Add("parked", sk.parked ? Pen(*sk.parked) : Json());
            o.Add("normal_pen", sk.normalPen ? Pen(*sk.normalPen) : Json());
            if constexpr (constrained)
                o.Add("constraints", Constraints(sk));
            return o;
        }

    private:
        PlanexWriter &w;

        static Json Index(size_t k) { return k == sketch::none ? Json() : Json(k); }
        // the sign of a zero counts: -0 is not the default 0
        static bool IsDefault(double v, double d) { return std::bit_cast<uint64_t>(v) == std::bit_cast<uint64_t>(d); }
        template <class C>
        static Json Strings(const C &c) {
            Json::Array a;
            for (const std::string &s : c)
                a.push_back(s);
            return a;
        }
        template <class C>
        static Json Indices(const C &c) {
            Json::Array a;
            for (size_t k : c)
                a.push_back(Index(k));
            return a;
        }
        static Json Numbers(const std::vector<double> &v) { return PlanexWriter::Numbers(v); }
        static Json Bools(const std::vector<char> &v) {
            Json::Array a;
            for (char b : v)
                a.push_back((bool)b);
            return a;
        }
        static Json Point(const Vertex &p) { return Json::Array{p.x, p.y}; }
        // a map or list of (text, text) pairs
        template <class C>
        static Json Pairs(const C &c) {
            Json::Array a;
            for (const auto &[x, y] : c)
                a.push_back(Json::Array{x, y});
            return a;
        }
        // a map of a text to texts, in the map's order
        template <class M>
        static Json Lists(const M &m) {
            Json::Array a;
            for (const auto &[key, list] : m)
                a.push_back(Json::Array{key, Strings(list)});
            return a;
        }
        static Json Pen(const sketch::PenState &p) {
            const auto optional = [](const std::optional<size_t> &k) { return k ? Json(*k) : Json(); };
            Json o{Json::Object{}};
            o.Add("pos", Point(p.pos));
            o.Add("dir", Point(p.dir));
            o.Add("start", Point(p.start));
            o.Add("point", optional(p.point));
            o.Add("first", optional(p.first));
            o.Add("last", optional(p.last));
            o.Add("chain", Indices(p.chain));
            o.Add("wire", p.wire);
            return o;
        }
        static Json CurveOf(const Curve<double, Vertex> &c) {
            Json::Array poles;
            for (const Vertex &p : c.poles) {
                poles.push_back(p.x);
                poles.push_back(p.y);
            }
            Json o{Json::Object{}};
            o.Add("degree", c.degree);
            o.Add("closed", c.closed);
            o.Add("knots", Numbers(c.knots));
            o.Add("params", Numbers(c.params));
            o.Add("tangent_at", Indices(c.tangentAt));
            o.Add("poles", std::move(poles));
            o.Add("weights", Numbers(c.weights));
            return o;
        }
        // an expression's postfix nodes, flat: operation, number, unknown of each
        static Json Nodes(const std::vector<cs::Node> &nodes) {
            Json::Array a;
            for (const cs::Node &n : nodes) {
                a.push_back(cs::OpName(n.op));
                a.push_back(n.number);
                a.push_back(n.unknown);
            }
            return a;
        }
        static Json Spline(const sketch::SplineData &d) {
            Json o{Json::Object{}};
            o.Add("points", Indices(d.points));
            o.Add("tangent_at", Indices(d.tangentAt));
            o.Add("tangent", Indices(d.tangent));
            o.Add("closed", d.closed);
            o.Add("style", splineStyleNames[(size_t)d.style]);
            if (d.fitted) {
                const sketch::FittedShape &f = *d.fitted;
                Json fitted{Json::Object{}};
                fitted.Add("shape", CurveOf(f.shape));
                fitted.Add("origin", Index(f.origin));
                fitted.Add("angle", Index(f.angle));
                fitted.Add("mirrored", f.mirrored);
                fitted.Add("scale", f.scale);
                if (f.formula) {
                    const sketch::CurveFormula &c = *f.formula;
                    Json formula{Json::Object{}};
                    formula.Add("text", c.text);
                    formula.Add("h", Nodes(c.h));
                    formula.Add("v", Nodes(c.v));
                    formula.Add("from", Nodes(c.from));
                    formula.Add("to", Nodes(c.to));
                    formula.Add("ids", Strings(c.ids));
                    formula.Add("dimensions", Indices(c.dimensions));
                    formula.Add("used", Numbers(c.used));
                    fitted.Add("formula", std::move(formula));
                }
                fitted.Add("copy_of", Index(f.copyOf));
                o.Add("fitted", std::move(fitted));
            }
            if (d.blend) {
                Json blend{Json::Object{}};
                blend.Add("arms", Indices(d.blend->arms));
                blend.Add("helper", Index(d.blend->helper));
                blend.Add("curvature", d.blend->curvature);
                o.Add("blend", std::move(blend));
            }
            return o;
        }
        static Json Piece(const sketch::SplinePiece &p) { return Json::Array{Index(p.from), Index(p.to), Index(p.fromPoint), Index(p.toPoint)}; }
        static Json Geometry(const sketch::Geometry &g) {
            Json::Array elements, splines, pieces, offsetCurves;
            for (const sketch::Element &e : g.elements)
                elements.push_back(Json::Array{elementKindNames[(size_t)e.kind], Index(e.p[0]), Index(e.p[1]), Index(e.p[2]), Index(e.p[3]), e.ccw});
            for (const sketch::SplineData &d : g.splines)
                splines.push_back(Spline(d));
            for (const sketch::SplinePiece &p : g.pieces)
                pieces.push_back(Piece(p));
            for (const sketch::OffsetCurveData &c : g.offsetCurves) {
                Json o{Json::Object{}};
                o.Add("spline", c.spline);
                o.Add("piece", c.piece);
                o.Add("distance", c.distance);
                o.Add("side", c.side);
                o.Add("range", Piece(c.range));
                o.Add("reversed", c.reversed);
                offsetCurves.push_back(std::move(o));
            }
            Json o{Json::Object{}};
            o.Add("values", Numbers(g.values));
            o.Add("angles", Bools(g.isAngle));
            o.Add("held", Bools(g.held));
            o.Add("own_coords", Indices(g.ownCoordsOf));
            o.Add("coords", Indices(g.coordsOf));
            o.Add("elements", std::move(elements));
            o.Add("splines", std::move(splines));
            o.Add("pieces", std::move(pieces));
            o.Add("offset_curves", std::move(offsetCurves));
            return o;
        }
        static Json Ref(const sketch::Ref &r) { return Json::Array{r.element, partNames[(size_t)r.part], r.index}; }
        // a constraint: its kind and references, then only the members that differ from their defaults
        template <class C>
        static Json Constraint(const C &c) {
            Json::Array refs, tangents, written;
            for (const sketch::Ref &r : c.refs)
                refs.push_back(Ref(r));
            for (const auto &[spline, point] : c.madeTangents)
                tangents.push_back(Json::Array{spline, point});
            for (const auto &x : c.written) {
                Json o{Json::Object{}};
                o.Add("token", x.token);
                o.Add("measure", x.measure);
                o.Add("unknown", x.unknown);
                o.Add("element", x.element);
                written.push_back(std::move(o));
            }
            Json o{Json::Object{}};
            o.Add("kind", sketchKindNames[(size_t)c.kind]);
            o.Add("refs", std::move(refs));
            if (!IsDefault(c.value, 0.0))
                o.Add("value", c.value);
            if (c.reference)
                o.Add("reference", true);
            if (c.suspended)
                o.Add("suspended", true);
            if (!IsDefault(c.sign, 1.0))
                o.Add("sign", c.sign);
            if (c.variant != 0)
                o.Add("variant", c.variant);
            if (c.variant2 != 0)
                o.Add("variant2", c.variant2);
            if (!c.target.empty())
                o.Add("target", Numbers(c.target));
            if (!c.held.empty())
                o.Add("held", Indices(c.held));
            if (!c.heldPoints.empty())
                o.Add("held_points", Indices(c.heldPoints));
            if (!tangents.empty())
                o.Add("made_tangents", std::move(tangents));
            if (!c.params.empty())
                o.Add("params", Indices(c.params));
            if (c.valueAt != sketch::none)
                o.Add("value_at", c.valueAt);
            if (!c.equation.empty())
                o.Add("equation", Nodes(c.equation));
            if (c.drives != sketch::none)
                o.Add("drives", c.drives);
            if (!c.text.empty())
                o.Add("text", c.text);
            if (!written.empty())
                o.Add("written", std::move(written));
            if (c.automatic)
                o.Add("automatic", true);
            if (c.tie)
                o.Add("tie", true);
            return o;
        }
        template <class P>
        static Json Pattern(const P &p) {
            Json::Array copies, before, steps;
            for (const auto &[originals, made] : p.copies)
                copies.push_back(Json::Array{Strings(originals), Strings(made)});
            for (const auto &[counters, k] : p.before) {
                Json::Array c;
                for (const auto &[kind, n] : counters)
                    c.push_back(Json::Array{kind, n});
                before.push_back(Json::Array{std::move(c), k});
            }
            for (const auto &[a, b] : p.steps)
                steps.push_back(Json::Array{a, b});
            Json o{Json::Object{}};
            o.Add("originals", Strings(p.originals));
            o.Add("ref", p.ref);
            o.Add("linear", p.linear);
            o.Add("equal", p.equal);
            o.Add("ties", p.ties);
            o.Add("copies", std::move(copies));
            o.Add("before", std::move(before));
            o.Add("given", Strings(p.given));
            o.Add("internal", p.internal);
            o.Add("ref2", p.ref2);
            o.Add("count", p.count);
            o.Add("count2", p.count2);
            o.Add("steps", std::move(steps));
            return o;
        }
        template <class O>
        static Json OffsetRecord(const O &f) {
            Json::Array towards, pieces, rounds;
            for (int t : f.towards)
                towards.push_back(t);
            for (const auto &[kind, k, side] : f.pieces)
                pieces.push_back(Json::Array{kind, k, side});
            for (const auto &[id, n] : f.rounds)
                rounds.push_back(Json::Array{id, n});
            Json o{Json::Object{}};
            o.Add("sources", Strings(f.sources));
            o.Add("elements", Strings(f.elements));
            o.Add("ties", Strings(f.ties));
            o.Add("forward", Bools(f.forward));
            o.Add("closed", f.closed);
            o.Add("towards", std::move(towards));
            o.Add("shape", f.shape);
            o.Add("pieces", std::move(pieces));
            o.Add("rounds", std::move(rounds));
            o.Add("checked", Numbers(f.checked));
            o.Add("curves", Pairs(f.curves));
            o.Add("ends", offsetEndNames[(size_t)f.forms.ends]);
            o.Add("corners", offsetCornerNames[(size_t)f.forms.corners]);
            return o;
        }
        // a map of a text to a record, in the map's order
        template <class M, class F>
        static Json Records(const M &m, F record) {
            Json::Array a;
            for (const auto &[id, r] : m)
                a.push_back(Json::Array{id, record(r)});
            return a;
        }
        // the constraint sketch's own: its constraints, their ids, the records of what follows its source, its settings
        template <class S>
        static Json Constraints(const S &sk) {
            Json::Array constraints, fixedCorners, values, counts, flips;
            for (const auto &c : sk.s.constraints)
                constraints.push_back(Constraint(c));
            for (const auto &[id, r] : sk.fixedCorners)
                fixedCorners.push_back(Json::Array{id, Ref(r)});
            for (const auto &[id, v] : sk.pending.values)
                values.push_back(Json::Array{id, v});
            for (const auto &[id, n] : sk.pending.counts)
                counts.push_back(Json::Array{id, n});
            for (const auto &[id, b] : sk.pending.flips)
                flips.push_back(Json::Array{id, b});
            Json autoConstraint;
            if (sk.autoConstraint) {
                autoConstraint = Json{Json::Object{}};
                for (const auto &[key, member] : autoConstraintKeys)
                    autoConstraint.Add(key, (*sk.autoConstraint).*member);
            }
            Json pending{Json::Object{}};
            pending.Add("values", std::move(values));
            pending.Add("counts", std::move(counts));
            pending.Add("flips", std::move(flips));
            Json o{Json::Object{}};
            o.Add("constraints", std::move(constraints));
            o.Add("constraint_ids", Strings(sk.constraintIds));
            o.Add("implied_ids", Strings(sk.impliedIds));
            o.Add("companions", Lists(sk.companions));
            o.Add("patterns", Records(sk.patterns, [](const auto &p) { return Pattern(p); }));
            o.Add("chamfers", Records(sk.chamfers, [](const auto &c) {
                Json r{Json::Object{}};
                r.Add("arm", Strings(c.arm));
                r.Add("cut", Strings(c.cut));
                r.Add("corner", Strings(c.corner));
                r.Add("reverse", c.reverse);
                r.Add("angled", c.angled);
                return r;
            }));
            o.Add("offsets", Records(sk.offsetInfos, [](const auto &f) { return OffsetRecord(f); }));
            o.Add("mirrors", Records(sk.mirrors, [](const auto &m) {
                Json r{Json::Object{}};
                r.Add("given", Strings(m.given));
                r.Add("axis", m.axis);
                r.Add("tie", m.tie);
                r.Add("internal", m.internal);
                return r;
            }));
            o.Add("fixed_corners", std::move(fixedCorners));
            o.Add("conflicts", Lists(sk.conflicts));
            o.Add("pending", std::move(pending));
            o.Add("auto_constraint", std::move(autoConstraint));
            o.Add("amplified", sk.amplified);
            o.Add("error_on_branch_change", sk.errorOnBranchChange);
            o.Add("strict", sk.strict);
            return o;
        }
    };

    // a sketch's state from .px2 JSON into a sketch of its kind, every member set as it was written, then (constraint
    // sketch) the solver's data rebuilt; ids kept and the counter of the sketch ids moved past its own
    class SketchReader {
    public:
        explicit SketchReader(const PlanexReader &r) : r(r) {}

        template <class S>
        void Read(S &sk, const Json &node) const {
            sk.axes = r.PlaneOf(node["plane"]);
            sk.SetIdKind(node["id_kind"].AsString());
            if (!node["id"].IsNull()) {
                sk.SetId(node["id"].AsString());
                AdvanceIdCounter(sk.IdKind(), sk.AssignedId());
            }
            Geometry(sk.s, node["geometry"]);
            sk.elementIds = Strings(node["element_ids"]);
            sk.props.clear();
            for (const Json &p : node["props"].AsArray())
                sk.props.push_back(r.PropertiesAt(p));
            sk.roles.clear();
            for (const Json &k : node["roles"].AsArray())
                sk.roles.push_back(EnumOf<sketch::Role>(k, roleNames, "element role"));
            sk.byId.clear();
            for (const Json &e : node["by_id"].AsArray())
                sk.byId[e.AsArray().at(0).AsString()] = {e.AsArray().at(1).AsBool(), e.AsArray().at(2).AsIndex()};
            sk.counters.clear();
            for (const Json &e : node["counters"].AsArray())
                sk.counters[e.AsArray().at(0).AsString()] = e.AsArray().at(1).AsIndex();
            sk.idScope = node["id_scope"].AsString();
            sk.wires = Lists<std::vector<std::string>>(node["wires"]);
            sk.pointElementOf.clear();
            for (const Json &e : node["point_elements"].AsArray())
                sk.pointElementOf[e.AsArray().at(0).AsIndex()] = e.AsArray().at(1).AsIndex();
            const std::vector<std::string> given = Strings(node["given_ids"]);
            sk.givenIds = std::set<std::string>(given.begin(), given.end());
            sk.renamed.clear();
            for (const auto &[from, to] : Pairs(node["renamed"]))
                sk.renamed[from] = to;
            sk.groups = Pairs(node["groups"]);
            sk.idsUnder.clear();
            for (const auto &[id, under] : Lists<std::set<std::string>>(node["ids_under"]))
                sk.idsUnder[id] = under;
            sk.pen = Pen(node["pen"]);
            sk.parked = node["parked"].IsNull() ? std::nullopt : std::optional(Pen(node["parked"]));
            sk.normalPen = node["normal_pen"].IsNull() ? std::nullopt : std::optional(Pen(node["normal_pen"]));
            if constexpr (requires(const S &x) { x.Geometry().constraints; })
                Constraints(sk, node["constraints"]);
            if (sk.elementIds.size() != sk.s.elements.size() || sk.props.size() != sk.s.elements.size() || sk.roles.size() != sk.s.elements.size())
                throw std::invalid_argument("planex file: a sketch's ids, properties and roles do not fit its elements");
        }

    private:
        const PlanexReader &r;

        template <class E, size_t N>
        static E EnumOf(const Json &j, const char *const (&names)[N], const char *what) {
            const std::string &name = j.AsString();
            for (size_t k = 0; k < N; ++k)
                if (name == names[k])
                    return (E)k;
            throw std::invalid_argument(std::string("planex file: no ") + what + " '" + name + "'");
        }
        static size_t Index(const Json &j) { return j.IsNull() ? sketch::none : j.AsIndex(); }
        static std::vector<size_t> Indices(const Json &j) {
            std::vector<size_t> out;
            for (const Json &k : j.AsArray())
                out.push_back(Index(k));
            return out;
        }
        static std::vector<double> Numbers(const Json &j) { return PlanexReader::NumbersOf(j); }
        static std::vector<char> Bools(const Json &j) {
            std::vector<char> out;
            for (const Json &b : j.AsArray())
                out.push_back(b.AsBool());
            return out;
        }
        static std::vector<std::string> Strings(const Json &j) {
            std::vector<std::string> out;
            for (const Json &s : j.AsArray())
                out.push_back(s.AsString());
            return out;
        }
        static Vertex Point(const Json &j) {
            const std::vector<double> v = Numbers(j);
            if (v.size() != 2)
                throw std::invalid_argument("planex file: a point has two numbers");
            return {v[0], v[1]};
        }
        static std::vector<std::pair<std::string, std::string>> Pairs(const Json &j) {
            std::vector<std::pair<std::string, std::string>> out;
            for (const Json &p : j.AsArray())
                out.push_back({p.AsArray().at(0).AsString(), p.AsArray().at(1).AsString()});
            return out;
        }
        template <class L>
        static std::map<std::string, L> Lists(const Json &j) {
            std::map<std::string, L> out;
            for (const Json &e : j.AsArray()) {
                const std::vector<std::string> list = Strings(e.AsArray().at(1));
                out[e.AsArray().at(0).AsString()] = L(list.begin(), list.end());
            }
            return out;
        }
        static sketch::PenState Pen(const Json &j) {
            const auto optional = [](const Json &k) { return k.IsNull() ? std::nullopt : std::optional<size_t>(k.AsIndex()); };
            sketch::PenState p;
            p.pos = Point(j["pos"]);
            p.dir = Point(j["dir"]);
            p.start = Point(j["start"]);
            p.point = optional(j["point"]);
            p.first = optional(j["first"]);
            p.last = optional(j["last"]);
            p.chain = Indices(j["chain"]);
            p.wire = j["wire"].AsString();
            return p;
        }
        static Curve<double, Vertex> CurveOf(const Json &j) {
            Curve<double, Vertex> c;
            c.degree = j["degree"].AsIndex();
            c.closed = j["closed"].AsBool();
            c.knots = Numbers(j["knots"]);
            c.params = Numbers(j["params"]);
            c.tangentAt = Indices(j["tangent_at"]);
            const std::vector<double> poles = Numbers(j["poles"]);
            if (poles.size() % 2 != 0)
                throw std::invalid_argument("planex file: a curve's poles are incomplete");
            for (size_t k = 0; k < poles.size(); k += 2)
                c.poles.push_back({poles[k], poles[k + 1]});
            c.weights = Numbers(j["weights"]);
            return c;
        }
        static std::vector<cs::Node> Nodes(const Json &j) {
            const Json::Array &a = j.AsArray();
            if (a.size() % 3 != 0)
                throw std::invalid_argument("planex file: an expression's nodes are incomplete");
            std::vector<cs::Node> out;
            for (size_t k = 0; k < a.size(); k += 3) {
                const std::string &name = a[k].AsString();
                size_t op = 0;
                while (op < opCount && name != cs::OpName((cs::Op)op))
                    ++op;
                if (op == opCount)
                    throw std::invalid_argument("planex file: no expression operation '" + name + "'");
                out.push_back({(cs::Op)op, a[k + 1].AsNumber(), a[k + 2].AsIndex()});
            }
            return out;
        }
        static sketch::SplineData Spline(const Json &j) {
            sketch::SplineData d;
            d.points = Indices(j["points"]);
            d.tangentAt = Indices(j["tangent_at"]);
            d.tangent = Indices(j["tangent"]);
            d.closed = j["closed"].AsBool();
            d.style = EnumOf<SplineStyle>(j["style"], splineStyleNames, "spline style");
            if (j.Has("fitted")) {
                const Json &f = j["fitted"];
                sketch::FittedShape fitted;
                fitted.shape = CurveOf(f["shape"]);
                fitted.origin = Index(f["origin"]);
                fitted.angle = Index(f["angle"]);
                fitted.mirrored = f["mirrored"].AsBool();
                fitted.scale = f["scale"].AsNumber();
                if (f.Has("formula")) {
                    const Json &c = f["formula"];
                    sketch::CurveFormula formula;
                    formula.text = c["text"].AsString();
                    formula.h = Nodes(c["h"]);
                    formula.v = Nodes(c["v"]);
                    formula.from = Nodes(c["from"]);
                    formula.to = Nodes(c["to"]);
                    formula.ids = Strings(c["ids"]);
                    formula.dimensions = Indices(c["dimensions"]);
                    formula.used = Numbers(c["used"]);
                    fitted.formula = std::move(formula);
                }
                fitted.copyOf = Index(f["copy_of"]);
                d.fitted = std::move(fitted);
            }
            if (j.Has("blend")) {
                const Json &b = j["blend"];
                const std::vector<size_t> arms = Indices(b["arms"]);
                if (arms.size() != 2)
                    throw std::invalid_argument("planex file: a blend has two arms");
                d.blend = sketch::BlendShape{{arms[0], arms[1]}, Index(b["helper"]), b["curvature"].AsBool()};
            }
            return d;
        }
        static sketch::SplinePiece Piece(const Json &j) {
            const std::vector<size_t> p = Indices(j);
            if (p.size() != 4)
                throw std::invalid_argument("planex file: a spline piece has four indices");
            return {p[0], p[1], p[2], p[3]};
        }
        static void Geometry(sketch::Geometry &g, const Json &j) {
            g.values = Numbers(j["values"]);
            g.isAngle = Bools(j["angles"]);
            g.held = Bools(j["held"]);
            g.ownCoordsOf = Indices(j["own_coords"]);
            g.coordsOf = Indices(j["coords"]);
            g.elements.clear();
            for (const Json &e : j["elements"].AsArray()) {
                const Json::Array &a = e.AsArray();
                if (a.size() != 6)
                    throw std::invalid_argument("planex file: a sketch element has a kind, four indices and a direction");
                g.elements.push_back({EnumOf<sketch::ElementKind>(a[0], elementKindNames, "element kind"), {Index(a[1]), Index(a[2]), Index(a[3]), Index(a[4])},
                                      a[5].AsBool()});
            }
            g.splines.clear();
            for (const Json &s : j["splines"].AsArray())
                g.splines.push_back(Spline(s));
            g.pieces.clear();
            for (const Json &p : j["pieces"].AsArray())
                g.pieces.push_back(Piece(p));
            g.offsetCurves.clear();
            for (const Json &c : j["offset_curves"].AsArray())
                g.offsetCurves.push_back({c["spline"].AsIndex(), c["piece"].AsIndex(), c["distance"].AsIndex(), c["side"].AsNumber(), Piece(c["range"]),
                                          c["reversed"].AsBool()});
            if (g.isAngle.size() != g.values.size() || g.held.size() != g.values.size())
                throw std::invalid_argument("planex file: a sketch's values, angles and held flags do not fit together");
        }
        static sketch::Ref Ref(const Json &j) {
            const Json::Array &a = j.AsArray();
            if (a.size() != 3)
                throw std::invalid_argument("planex file: a reference has an element, a part and an index");
            return {a[0].AsIndex(), EnumOf<sketch::Part>(a[1], partNames, "part"), a[2].AsIndex()};
        }
        template <class C>
        static C Constraint(const Json &j) {
            C c{EnumOf<sketch::Kind>(j["kind"], sketchKindNames, "constraint kind")};
            for (const Json &x : j["refs"].AsArray())
                c.refs.push_back(Ref(x));
            if (j.Has("value"))
                c.value = j["value"].AsNumber();
            c.reference = j.Has("reference") && j["reference"].AsBool();
            c.suspended = j.Has("suspended") && j["suspended"].AsBool();
            if (j.Has("sign"))
                c.sign = j["sign"].AsNumber();
            if (j.Has("variant"))
                c.variant = (int)j["variant"].AsNumber();
            if (j.Has("variant2"))
                c.variant2 = (int)j["variant2"].AsNumber();
            if (j.Has("target"))
                c.target = Numbers(j["target"]);
            if (j.Has("held"))
                c.held = Indices(j["held"]);
            if (j.Has("held_points"))
                c.heldPoints = Indices(j["held_points"]);
            if (j.Has("made_tangents"))
                for (const Json &t : j["made_tangents"].AsArray())
                    c.madeTangents.push_back({t.AsArray().at(0).AsIndex(), t.AsArray().at(1).AsIndex()});
            if (j.Has("params"))
                c.params = Indices(j["params"]);
            if (j.Has("value_at"))
                c.valueAt = j["value_at"].AsIndex();
            if (j.Has("equation"))
                c.equation = Nodes(j["equation"]);
            if (j.Has("drives"))
                c.drives = j["drives"].AsIndex();
            if (j.Has("text"))
                c.text = j["text"].AsString();
            if (j.Has("written"))
                for (const Json &x : j["written"].AsArray())
                    c.written.push_back({x["token"].AsString(), x["measure"].AsString(), x["unknown"].AsIndex(), x["element"].AsIndex()});
            c.automatic = j.Has("automatic") && j["automatic"].AsBool();
            c.tie = j.Has("tie") && j["tie"].AsBool();
            return c;
        }
        template <class P>
        static P Pattern(const Json &j) {
            P p;
            p.originals = Strings(j["originals"]);
            p.ref = j["ref"].AsString();
            p.linear = j["linear"].AsBool();
            p.equal = j["equal"].AsBool();
            p.ties = j["ties"].AsIndex();
            for (const Json &c : j["copies"].AsArray())
                p.copies.push_back({Strings(c.AsArray().at(0)), Strings(c.AsArray().at(1))});
            for (const Json &b : j["before"].AsArray()) {
                std::map<std::string, size_t> counters;
                for (const Json &c : b.AsArray().at(0).AsArray())
                    counters[c.AsArray().at(0).AsString()] = c.AsArray().at(1).AsIndex();
                p.before.push_back({std::move(counters), b.AsArray().at(1).AsIndex()});
            }
            p.given = Strings(j["given"]);
            p.internal = j["internal"].AsBool();
            p.ref2 = j["ref2"].AsString();
            p.count = j["count"].AsIndex();
            p.count2 = j["count2"].AsIndex();
            for (const Json &s : j["steps"].AsArray())
                p.steps.push_back({s.AsArray().at(0).AsIndex(), s.AsArray().at(1).AsIndex()});
            return p;
        }
        template <class O>
        static O OffsetRecord(const Json &j) {
            O f;
            f.sources = Strings(j["sources"]);
            f.elements = Strings(j["elements"]);
            f.ties = Strings(j["ties"]);
            f.forward = Bools(j["forward"]);
            f.closed = j["closed"].AsBool();
            for (const Json &t : j["towards"].AsArray())
                f.towards.push_back((int)t.AsNumber());
            f.shape = j["shape"].AsString();
            for (const Json &p : j["pieces"].AsArray())
                f.pieces.insert({(int)p.AsArray().at(0).AsNumber(), p.AsArray().at(1).AsIndex(), (int)p.AsArray().at(2).AsNumber()});
            for (const Json &x : j["rounds"].AsArray())
                f.rounds.push_back({x.AsArray().at(0).AsString(), (int)x.AsArray().at(1).AsNumber()});
            f.checked = Numbers(j["checked"]);
            f.curves = Pairs(j["curves"]);
            f.forms = {EnumOf<OffsetEnd>(j["ends"], offsetEndNames, "offset end"), EnumOf<OffsetCorner>(j["corners"], offsetCornerNames, "offset corner")};
            return f;
        }
        // a map of a text to a record
        template <class M, class F>
        static void Records(M &m, const Json &j, F record) {
            m.clear();
            for (const Json &e : j.AsArray())
                m[e.AsArray().at(0).AsString()] = record(e.AsArray().at(1));
        }
        template <class S>
        static void Constraints(S &sk, const Json &j) {
            using C = typename decltype(sk.s.constraints)::value_type;
            using PatternInfo = typename decltype(sk.patterns)::mapped_type;
            using ChamferInfo = typename decltype(sk.chamfers)::mapped_type;
            using OffsetInfo = typename decltype(sk.offsetInfos)::mapped_type;
            using MirrorInfo = typename decltype(sk.mirrors)::mapped_type;
            sk.s.constraints.clear();
            for (const Json &c : j["constraints"].AsArray())
                sk.s.constraints.push_back(Constraint<C>(c));
            sk.constraintIds = Strings(j["constraint_ids"]);
            const std::vector<std::string> implied = Strings(j["implied_ids"]);
            sk.impliedIds = std::set<std::string>(implied.begin(), implied.end());
            sk.companions = Lists<std::vector<std::string>>(j["companions"]);
            Records(sk.patterns, j["patterns"], [](const Json &x) { return Pattern<PatternInfo>(x); });
            Records(sk.chamfers, j["chamfers"], [](const Json &x) {
                const auto two = [](const Json &s) {
                    const std::vector<std::string> v = Strings(s);
                    if (v.size() != 2)
                        throw std::invalid_argument("planex file: a chamfer's record has two ids each");
                    return std::array<std::string, 2>{v[0], v[1]};
                };
                return ChamferInfo{two(x["arm"]), two(x["cut"]), two(x["corner"]), x["reverse"].AsBool(), x["angled"].AsBool()};
            });
            Records(sk.offsetInfos, j["offsets"], [](const Json &x) { return OffsetRecord<OffsetInfo>(x); });
            Records(sk.mirrors, j["mirrors"], [](const Json &x) {
                return MirrorInfo{Strings(x["given"]), x["axis"].AsString(), x["tie"].AsBool(), x["internal"].AsBool()};
            });
            sk.fixedCorners.clear();
            for (const Json &f : j["fixed_corners"].AsArray())
                sk.fixedCorners.push_back({f.AsArray().at(0).AsString(), Ref(f.AsArray().at(1))});
            sk.conflicts = Lists<std::vector<std::string>>(j["conflicts"]);
            sk.pending = {};
            for (const Json &v : j["pending"]["values"].AsArray())
                sk.pending.values.push_back({v.AsArray().at(0).AsString(), v.AsArray().at(1).AsNumber()});
            for (const Json &v : j["pending"]["counts"].AsArray())
                sk.pending.counts.push_back({v.AsArray().at(0).AsString(), v.AsArray().at(1).AsIndex()});
            for (const Json &v : j["pending"]["flips"].AsArray())
                sk.pending.flips.push_back({v.AsArray().at(0).AsString(), v.AsArray().at(1).AsBool()});
            sk.autoConstraint = std::nullopt;
            if (!j["auto_constraint"].IsNull()) {
                AutoConstraint a;
                for (const auto &[key, member] : autoConstraintKeys)
                    a.*member = j["auto_constraint"][key].AsBool();
                sk.autoConstraint = a;
            }
            sk.amplified = j["amplified"].AsNumber();
            sk.errorOnBranchChange = j["error_on_branch_change"].AsBool();
            sk.strict = j["strict"].AsBool();
            if (sk.constraintIds.size() != sk.s.constraints.size())
                throw std::invalid_argument("planex file: a sketch's constraint ids do not fit its constraints");
            sk.analysis = std::nullopt;
            sk.s.Build();
        }
    };

}
