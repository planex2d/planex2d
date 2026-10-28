// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/disjointsets.hpp"
#include "core/objectid.hpp"
#include "core/properties.hpp"
#include "core/vertex.hpp"
#include "curves/edge.hpp"
#include "curves/interpolate.hpp"
#include "curves/spline.hpp"
#include "offset/offsetpieces.hpp"
#include "shape/fillet.hpp"
#include "shape/shape.hpp"
#include "sketch/copies.hpp"
#include "sketch/cornerparts.hpp"
#include "sketch/curveeval.hpp"
#include "sketch/cut.hpp"
#include "sketch/diagnostics.hpp"
#include "sketch/elementcutter.hpp"
#include "sketch/geometry.hpp"
#include "sketch/guards.hpp"
#include "sketch/offsetmaker.hpp"
#include "sketch/offsetsides.hpp"
#include "sketch/options.hpp"
#include "sketch/pen.hpp"
#include "sketch/projection.hpp"
#include "sketch/projectionmaker.hpp"
#include "wire/chain.hpp"

namespace planex2d {

    class SketchWriter;
    class SketchReader;

    // elements drawn like the turtle Sketch, on the storage Store: sketch::Geometry for the plain sketch, cs::System for
    // the constraint sketch (which adds the constraints). Ids name elements (and constraints)
    template <class Store>
    class BasicSketch {
        ObjectId objectId{"Sketch"};

    public:
        // the sketch's id: Sketch.N, one counter for both kinds of sketch
        const std::string &Id() { return objectId.Get(); }
        const std::string &AssignedId() const { return objectId.id; }
        void SetId(std::string id) { objectId.id = std::move(id); }
        const std::string &IdKind() const { return objectId.kind; }
        void SetIdKind(std::string kind) { objectId.kind = std::move(kind); }
        using Options = sketch::Options;
        explicit BasicSketch(const AxisSystem &a = AxisSystem()) : axes(a) {
            // directed along +x and +y: a positive distance is to the left of a line's direction, above H, left of V
            AddElement(s.AddAxis(-std::numbers::pi / 2.0, 0.0, true), "Axis", {"H", {}, true});
            AddElement(s.AddAxis(0.0, 0.0, true), "Axis", {"V", {}, true});
            AddElement(s.AddPoint(s.NewPoint({0.0, 0.0}, true)), "Point", {"origin", {}, true});
        }

        template <class Other>
        friend class BasicSketch;
        template <class>
        friend class sketch::OffsetMaker;
        template <class>
        friend class sketch::ElementCutter;
        template <class>
        friend class sketch::ProjectionMaker;
        friend class SketchWriter;
        friend class SketchReader;
        // the sketch of the other kind: the same elements (ids, properties, construction flags), groups, pen and id
        // counters, at the values the other has now; no constraints, dimensions or expressions
        template <class Other>
        explicit BasicSketch(const BasicSketch<Other> &o)
            : s(o.s), axes(o.axes), elementIds(o.elementIds), props(o.props), roles(o.roles), byId(o.byId),
              counters(o.counters), idScope(o.idScope), wireLog(o.wireLog), wires(o.wires),
              pointElementOf(o.pointElementOf), givenIds(o.givenIds), renamed(o.renamed), groups(o.groups), idsUnder(o.idsUnder), pen(o.pen),
              parked(o.parked), normalPen(o.normalPen) {
            if constexpr (std::is_same_v<Store, sketch::Geometry>)
                WithoutConstraints();
        }

        const AxisSystem &Axes() const { return axes; }
        const Store &Geometry() const { return s; }
        Vertex Position() const { return pen.pos; }
        Vertex StartPosition() const { return pen.start; }
        Vertex Heading() const { return pen.dir; }

        BasicSketch &MoveTo(const Vertex &p, const std::vector<sketch::Kind> &flags) {
            CheckFlags(flags);
            IsFinite("Move", {p.x, p.y});
            return Atomic([&] {
                Finish();
                pen.pos = pen.start = p;
                for (sketch::Kind k : flags) {
                    if (k != sketch::Kind::Fix)
                        throw std::invalid_argument("Move((h, v)): px." + std::string(sketch::KindName(k)) +
                                                    " is no flag here (a coincidence needs a point id: Move(id, px.Coincident))");
                    RelateGiven(k, {{PenPoint({})}});
                }
            });
        }
        // to an element's point; px.Coincident starts there, sharing that point
        BasicSketch &MoveTo(const std::string &ref, const std::vector<sketch::Kind> &flags) {
            CheckFlags(flags);
            return Atomic([&] {
                Finish();
                const sketch::Ref r = RefOf(ref);
                if (!r.IsPoint(s))
                    throw std::invalid_argument("Move('" + ref + "'): not a point");
                pen.pos = pen.start = r.PointValue(s);
                for (sketch::Kind k : flags) {
                    if (k != sketch::Kind::Fix && k != sketch::Kind::Coincident)
                        throw std::invalid_argument("Move('" + ref + "'): px." + std::string(sketch::KindName(k)) + " is no flag here");
                    RelateGiven(k, k == sketch::Kind::Fix ? std::vector<sketch::Ref>{{PenPoint({})}} : std::vector<sketch::Ref>{{PenPoint({})}, r});
                }
                Sync();
            });
        }
        BasicSketch &Line(const Vertex &end, const std::vector<sketch::Kind> &flags, const Options &opt) {
            CheckFlags(flags);
            return Drawing(opt, [&] { LineTo(end, flags, opt); });
        }
        BasicSketch &Line(double l, const std::vector<sketch::Kind> &flags, const Options &opt) {
            CheckFlags(flags);
            return Drawing(opt, [&] { LineTo(pen.pos + pen.dir * l, flags, opt); });
        }
        BasicSketch &Line(double dx, double dy, const std::vector<sketch::Kind> &flags, const Options &opt) {
            CheckFlags(flags);
            return Drawing(opt, [&] { LineTo(pen.pos + Vertex{dx, dy}, flags, opt); });
        }
        // inside a drawing call
        void LineTo(const Vertex &end, const std::vector<sketch::Kind> &flags, const Options &opt, bool inChain = true) {
            if (!ValidMove(end))
                return;
            const size_t a = PenPoint(opt), b = EndPoint(end, opt);
            const size_t e = AddElement(s.AddLine(s.elements[a].Point(), s.elements[b].Point()), "Line", opt);
            if (inChain)
                Extend(e);
            pen.dir = (end - pen.pos) * (1.0 / Norm(end - pen.pos));
            pen.pos = end;
            pen.point = b;
            Flags(e, flags);
            pen.last = e;
            Sync();
        }
        // radius r, turning by ang degrees (positive: to the left) from the drawing direction; a negative r puts the
        // centre on the other side, the arc starts backwards (as in Sketch)
        BasicSketch &Arc(double r, double ang, const std::vector<sketch::Kind> &flags, const Options &opt) {
            CheckFlags(flags);
            IsFinite("Arc", {r, ang});
            if (!(std::abs(r) > 0.0))
                throw std::invalid_argument("Arc(r, sweep): the radius must not be 0");
            return Drawing(opt, [&] {
                const Vertex left{-pen.dir.y, pen.dir.x};
                const Vertex center = pen.pos + left * (ang > 0.0 ? r : -r);
                if (ang != 0.0 && std::abs(std::remainder(ang, 360.0)) <= Tol(std::abs(ang))) {
                    FullTurn(center, ang > 0.0, flags, opt);
                    return;
                }
                const Vertex heading = Rotated(pen.dir, ang) * (r > 0.0 ? 1.0 : -1.0);
                ArcThrough(center, Rotated(pen.pos, center, ang), ang > 0.0, heading, flags, opt);
            });
        }
        // one circle through the pen, its origin there; the pen, its direction and the chain go on from it as they were
        BasicSketch &FullTurn(const Vertex &center, bool ccw, const std::vector<sketch::Kind> &flags, const Options &opt) {
            return Drawing(opt, [&] {
                const size_t home = PenPoint(opt);
                const Vertex from = pen.pos - center;
                const size_t e = AddElement(s.AddCircle(s.NewPoint(center), Norm(from), std::atan2(from.y, from.x), ccw, s.elements[home].Point()),
                                            "Circle", opt);
                Extend(e);
                const bool fix = std::find(flags.begin(), flags.end(), sketch::Kind::Fix) != flags.end();
                if (fix)
                    RelateGiven(sketch::Kind::Fix, {{home}});
                TurnTies(home, e, !fix, std::find(flags.begin(), flags.end(), sketch::Kind::Tangent) == flags.end() ? pen.last : std::nullopt);
                Flags(e, flags);
                pen.last = e;
                Sync();
            });
        }
        // to end, leaving the current position along t
        BasicSketch &Arc(const Vertex &end, const Vertex &t, const std::vector<sketch::Kind> &flags, const Options &opt) {
            CheckFlags(flags);
            IsFinite("Arc", {end.x, end.y, t.x, t.y});
            return Drawing(opt, [&] {
                const Vertex chord = end - pen.pos, n{-t.y, t.x};
                const double nc = Dot(n, chord);
                if (nc == 0.0)
                    throw std::invalid_argument("Arc(to, tangent=(dh, dv)): the direction at the start runs along the chord");
                const double k = Dot(chord, chord) / (2.0 * nc);
                const Vertex center = pen.pos + n * k;
                const bool ccw = k > 0.0;
                const Vertex r = end - center, d = ccw ? Vertex{-r.y, r.x} : Vertex{r.y, -r.x};
                ArcThrough(center, end, ccw, d * (1.0 / Norm(d)), flags, opt);
            });
        }
        // a spline from the pen (fromPen, else from the first point) through points, closed back to its start, with
        // tangent directions at some of them (indices into all its points, the start 0); its points are point elements,
        // the tangents angle unknowns
        BasicSketch &Spline(const std::vector<Vertex> &points, bool closed, const std::map<size_t, Vertex> &tangents, bool fromPen,
                                 const std::vector<sketch::Kind> &flags, const Options &opt, SplineStyle style = SplineStyle::Averaged) {
            CheckFlags(flags);
            for (const Vertex &p : points)
                IsFinite("Spline", {p.x, p.y});
            for (const auto &[k, t] : tangents)
                IsFinite("Spline", {t.x, t.y});
            return Drawing(opt, [&] {
                std::vector<Vertex> rest = points;
                if (!fromPen && !rest.empty()) {
                    Finish();
                    pen.pos = pen.start = rest.front();
                    rest.erase(rest.begin());
                }
                if (closed && !rest.empty() && rest.back().IsClose(pen.pos, Tol(std::max(NormInf(pen.pos), NormInf(rest.back())))))
                    rest.pop_back();
                if (rest.empty())
                    throw std::invalid_argument("Spline: two points at least");
                std::vector<size_t> ids{s.elements[PenPoint(opt)].Point()};
                for (size_t k = 0; k < rest.size(); ++k) {
                    if (!(Norm(rest[k] - pen.pos) > Tol(std::max(NormInf(pen.pos), NormInf(rest[k])))))
                        throw std::invalid_argument("Spline: two following points at one place");
                    const size_t e = k + 1 == rest.size() && !closed ? EndPoint(rest[k], opt)
                                                                     : AddElement(s.AddPoint(s.NewPoint(rest[k])), "Point", {std::nullopt, {}, opt.construction});
                    ids.push_back(s.elements[e].Point());
                    pen.pos = rest[k];
                    if (!closed)
                        pen.point = e;
                }
                std::vector<size_t> at;
                std::vector<double> angles;
                for (const auto &[k, t] : tangents) {
                    if (k >= ids.size() || !(Norm(t) > 0.0))
                        throw std::invalid_argument("Spline: tangent " + std::to_string(k) + (k >= ids.size() ? " at a point it does not have" : " has no direction"));
                    at.push_back(k);
                    angles.push_back(std::atan2(t.y, t.x));
                }
                const size_t e = AddElement(s.AddSpline(ids, at, angles, closed, style), "Spline", opt);
                SplineAdded(e);
                Extend(e);
                if (closed)
                    pen.pos = Coords(s.elements[*pen.point].Point());
                const Vertex t = s.elements[e].TangentAtFraction(s, closed ? 0.0 : 1.0);
                pen.dir = t * (1.0 / Norm(t));
                Flags(e, flags);
                pen.last = e;
                Sync();
                if (closed)
                    Finish();
            });
        }
        // a curve: the spline fitted to it, shape in sketch coordinates where its formula puts it, its frame at the
        // sketch's origin (it moves and turns as one, px.Fix by default; formula: what it refits from, none for a
        // callable's). It goes on from the pen if the pen is at its start (to rounding), else it starts a chain of its
        // own; the pen goes to its end
        BasicSketch &Curve(const planex2d::Curve<double, Vertex> &shape, std::optional<sketch::CurveFormula> formula, const std::vector<sketch::Kind> &flags,
                           const Options &opt) {
            CheckFlags(flags);
            return Drawing(opt, [&] {
                const Vertex start = shape.poles.front(), end = shape.poles.back();
                if (!start.IsClose(pen.pos, Tol(std::max(NormInf(pen.pos), NormInf(start))))) {
                    Finish();
                    pen.pos = pen.start = start;
                }
                const size_t a = PenPoint(opt), b = shape.closed ? a : EndPoint(end, opt);
                sketch::FittedShape f{shape};
                f.formula = std::move(formula);
                const size_t e = AddElement(s.AddFittedSpline(s.elements[a].Point(), s.elements[b].Point(), std::move(f), {0.0, 0.0}, 0.0), "Curve", opt);
                Extend(e);
                pen.point = b;
                pen.pos = Coords(s.elements[b].Point());
                const Vertex t = shape.At(shape.closed ? shape.params.front() : shape.params.back(), 1);
                pen.dir = t * (1.0 / Norm(t));
                Flags(e, flags);
                if (std::find(flags.begin(), flags.end(), sketch::Kind::Fix) == flags.end())
                    RelateGiven(sketch::Kind::Fix, {{e}});
                pen.last = e;
                Sync();
                if (shape.closed)
                    Finish();
            });
        }
        BasicSketch &Circle(const Vertex &center, double r, const std::vector<sketch::Kind> &flags, const Options &opt) {
            CheckFlags(flags);
            IsFinite("Circle", {center.x, center.y, r});
            if (!(r > 0.0))
                throw std::invalid_argument("Circle: the radius must be positive");
            return Drawing(opt, [&] {
                Finish();
                Flags(AddElement(s.AddCircle(s.NewPoint(center), r), "Circle", opt), flags);
            });
        }
        BasicSketch &Rectangle(double l, double w, const Options &opt) {
            IsFinite("Rectangle", {l, w});
            return Drawing(opt, [&] { RectangleSides(opt.id ? *opt.id : FreshId("Rectangle"), "Rectangle", l, w, opt); });
        }
        // a rectangle through its corners: two opposite ones, the sides along h and v, from the corner of the smallest h
        // and v; or three: p0 to p1 a side, the opposite side through p2, from p0 counterclockwise. A point given as at[k]
        // is tied there (p2 to the opposite side's line); the pen stays as it was
        BasicSketch &Rectangle(const std::vector<Vertex> &p, const std::vector<std::optional<std::string>> &at, const Options &opt) {
            for (const Vertex &q : p)
                IsFinite("Rectangle", {q.x, q.y});
            return Drawing(opt, [&] {
                const sketch::PenState saved = pen;
                pen.point = pen.first = pen.last = std::nullopt;
                double l = std::abs(p[1].x - p[0].x), w = std::abs(p[1].y - p[0].y), d = 0.0;
                if (p.size() == 2) {
                    pen.pos = pen.start = {std::min(p[0].x, p[1].x), std::min(p[0].y, p[1].y)};
                    pen.dir = {1.0, 0.0};
                } else {
                    const Vertex side = p[1] - p[0];
                    l = Norm(side);
                    if (!(l > 0.0))
                        throw std::invalid_argument("Rectangle(p0, p1, p2): p0 and p1 are one point; give two corners of a side");
                    const Vertex u = side * (1.0 / l), left{-u.y, u.x};
                    d = Dot(p[2] - p[1], left);
                    pen.pos = pen.start = p[0];
                    // p2 to the right: counterclockwise from p0 runs first along the other side at p0
                    pen.dir = d > 0.0 ? u : Vertex{u.y, -u.x};
                    w = std::abs(d);
                    if (!(d > 0.0))
                        std::swap(l, w);
                }
                const double tol = Tol(std::max({NormInf(p[0]), NormInf(p[1]), NormInf(p.back())}));
                if (!(l > tol) || !(w > tol))
                    throw std::invalid_argument(std::string(p.size() == 2 ? "Rectangle(p0, p1): the corners lie on one line along h or v"
                                                                          : "Rectangle(p0, p1, p2): p2 lies on the line through p0 and p1") +
                                                ", a side would have length 0; give " +
                                                (p.size() == 2 ? "opposite corners" : "p2 off that line"));
                const std::string name = opt.id ? *opt.id : FreshId("Rectangle");
                const std::vector<size_t> sides = RectangleSides(name, "Rectangle", l, w, opt);
                const auto corner = [&](size_t k) { return sketch::Ref{sides[k], sketch::Part::Start}; };
                if (p.size() == 2) {
                    Relate(sketch::Kind::Horizontal, {{sides[0]}});
                    for (size_t k = 0; k < 2; ++k)
                        if (at[k])
                            for (size_t j = 0; j < 4; ++j)
                                if (Coords(s.elements[sides[j]].Start()).IsClose(p[k], tol))
                                    Relate(sketch::Kind::Coincident, {corner(j), RefOf(*at[k])});
                } else {
                    if (at[0])
                        Relate(sketch::Kind::Coincident, {corner(0), RefOf(*at[0])});
                    if (at[1])
                        Relate(sketch::Kind::Coincident, {d > 0.0 ? sketch::Ref{sides[0], sketch::Part::End} : corner(3), RefOf(*at[1])});
                    if (at[2])
                        Relate(sketch::Kind::Coincident, {RefOf(*at[2]), {sides[d > 0.0 ? 2 : 1]}});
                }
                pen = saved;
            });
        }
        // a rectangle centred at center, or at the point at (tied to it), or at the pen, l along the drawing direction
        // and w across: its lines as Rectangle's from the corner behind and right of the centre, a construction diagonal
        // from the first corner to the third with the construction point RectangleC.k.center at its middle; the pen goes
        // to the centre
        BasicSketch &RectangleC(const std::optional<Vertex> &center, const std::optional<std::string> &at, double l, double w,
                                const Options &opt) {
            IsFinite("RectangleC", {l, w});
            if (center)
                IsFinite("RectangleC", {center->x, center->y});
            return Drawing(opt, [&] {
                Finish();
                const Vertex c = at ? Location(*at) : center.value_or(pen.pos), along = pen.dir, left{-pen.dir.y, pen.dir.x};
                pen.pos = pen.start = c - along * (0.5 * l) - left * (0.5 * w);
                const std::string name = opt.id ? *opt.id : FreshId("RectangleC");
                const std::vector<size_t> sides = RectangleSides(name, "RectangleC", l, w, opt);
                const Options aid{std::nullopt, {}, true};
                sketch::Scoped scope{idScope, std::exchange(idScope, name)};
                const size_t diagonal = AddElement(s.AddLine(s.elements[sides[0]].Start(), s.elements[sides[1]].End()), "Line", aid);
                Rename(diagonal, name + ".diagonal");
                const size_t middle = AddElement(s.AddPoint(s.NewPoint(c)), "Point", aid);
                Rename(middle, name + ".center");
                CentreTie(middle, diagonal);
                if (at)
                    Relate(sketch::Kind::Coincident, {{middle}, RefOf(*at)});
                pen.pos = pen.start = c;
                pen.dir = along;
                pen.point = pen.first = pen.last = std::nullopt;
            });
        }
        // turns the construction pen too
        BasicSketch &Direction(double dx, double dy) {
            IsFinite("Direction", {dx, dy});
            const double n = std::sqrt(dx * dx + dy * dy);
            if (!(n > 0.0))
                throw std::invalid_argument("Direction: zero vector");
            pen.dir = {dx / n, dy / n};
            if (parked)
                parked->dir = pen.dir;
            return *this;
        }
        BasicSketch &Turn(double angle) {
            IsFinite("Turn", {angle});
            pen.dir = Rotated(pen.dir, angle);
            if (parked)
                parked->dir = Rotated(parked->dir, angle);
            return *this;
        }
        // a line back to where the chain started, unless it is there; the flags as for that line
        BasicSketch &Close(const std::vector<sketch::Kind> &flags, const Options &opt, const std::optional<std::string> &idWire = std::nullopt) {
            CheckFlags(flags);
            return Drawing(opt, [&] {
                if (pen.last && pen.point != pen.first) {
                    const size_t e = AddElement(s.AddLine(s.elements[*pen.point].Point(), s.elements[*pen.first].Point()), "Line", opt);
                    Extend(e);
                    if (Norm(pen.start - pen.pos) > 0.0)
                        pen.dir = (pen.start - pen.pos) * (1.0 / Norm(pen.start - pen.pos));
                    pen.pos = pen.start;
                    pen.point = pen.first;
                    Flags(e, flags);
                }
                if (pen.chain.size() > 1)
                    Chained(pen.chain.back(), pen.chain.front());
                Finish(idWire);
            });
        }
        // a chain is a wire from its first element: Chain.k, or idWire, which replaces that id (the number is not given
        // again), for the calls that take groups of elements
        BasicSketch &Finish(const std::optional<std::string> &idWire = std::nullopt) {
            if (idWire)
                RenameChain(*idWire, "id_wire=");
            if (!normalPen)
                parked.reset();
            pen.point.reset();
            pen.first.reset();
            pen.last.reset();
            if (atomic > 0 && !normalPen)
                ended.push_back(std::move(pen.chain));
            pen.chain.clear();
            pen.wire.clear();
            return *this;
        }
        void Extend(size_t e) {
            if (pen.wire.empty()) {
                pen.wire = FreshId("Chain");
                wires[pen.wire];
                groups.push_back({"Chain", pen.wire});
                wireLog.push_back({pen.wire, std::string()});
            }
            if (!pen.chain.empty())
                Chained(pen.chain.back(), e);
            pen.chain.push_back(e);
            wires[pen.wire].push_back(elementIds[e]);
        }
        void RenameChain(const std::string &id, const std::string &call) {
            if (pen.chain.empty())
                throw std::invalid_argument(call + "'" + id + "': no chain is drawn to name");
            CheckUserId(id);
            CheckId(id);
            auto node = wires.extract(pen.wire);
            node.key() = id;
            wires.insert(std::move(node));
            RenameGroup(pen.wire, id);
            wireLog.push_back({id, pen.wire});
            pen.wire = id;
        }
        // the ids above id list it as one under them (add) or no longer
        void ListUnder(const std::string &id, bool add) {
            for (size_t dot = id.find('.'); dot != std::string::npos; dot = id.find('.', dot + 1)) {
                const std::string parent = id.substr(0, dot);
                if (add) {
                    idsUnder[parent].insert(id);
                    continue;
                }
                std::set<std::string> &under = idsUnder[parent];
                under.erase(id);
                if (under.empty())
                    idsUnder.erase(parent);
            }
        }
        void Rename(size_t e, const std::string &id) {
            if (elementIds[e] == id)
                return;
            CheckId(id);
            byId.erase(elementIds[e]);
            ListUnder(elementIds[e], false);
            ListUnder(id, true);
            elementIds[e] = id;
            byId[id] = {true, e};
        }
        void RenameGroup(const std::string &from, const std::string &to) {
            for (auto &[kind, id] : groups)
                if (id == from)
                    id = to;
        }
        void NameWire(const std::string &id, const std::vector<size_t> &elements, const std::string &call) {
            if (elements.empty())
                throw std::invalid_argument(call + "'" + id + "': no chain is drawn to name");
            CheckId(id);
            std::vector<std::string> &w = wires[id];
            for (size_t e : elements)
                w.push_back(elementIds[e]);
            wireLog.push_back({id, std::string()});
        }
        std::string FreshId(const std::string &prefix) {
            size_t &k = counters[prefix];
            while (true) {
                const std::string id = prefix + "." + std::to_string(++k);
                if (!byId.contains(id) && !wires.contains(id))
                    return id;
            }
        }
        std::string DidYouMean(const std::string &text) const {
            std::vector<std::string> ids;
            for (const auto &[id, at] : byId)
                ids.push_back(id);
            for (const auto &[id, w] : wires)
                ids.push_back(id);
            return planex2d::DidYouMean(text, ids);
        }
        bool IsWire(const std::string &id) const { return wires.contains(id); }
        // the cut elements kept as the spans of their pieces
        std::vector<std::string> Spans() const {
            std::vector<std::string> out;
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (IsSpan(e))
                    out.push_back(elementIds[e]);
            return out;
        }
        const std::map<std::string, std::vector<std::string>> &NamedWires() const { return wires; }
        // the ids with every wire id replaced by its elements
        virtual std::vector<std::string> Expanded(const std::vector<std::string> &ids) const {
            std::vector<std::string> out;
            for (const std::string &id : ids)
                if (const auto it = wires.find(id); it != wires.end())
                    out.insert(out.end(), it->second.begin(), it->second.end());
                else
                    out.push_back(id);
            return out;
        }

        // an element, or a wire with its elements; a line's or arc's ends go with it unless something else ends there
        BasicSketch &Remove(const std::string &id) {
            RemoveOne(id);
            return *this;
        }
        virtual void RemoveOne(const std::string &id) {
            if (const auto w = wires.find(id); w != wires.end()) {
                const std::vector<std::string> elements = w->second;
                wires.erase(w);
                // blends first: they are computed from the others
                for (const bool blends : {true, false})
                    for (auto e = elements.rbegin(); e != elements.rend(); ++e)
                        if (const auto at = byId.find(*e); at != byId.end() && IsBlend(at->second.index) == blends)
                            RemoveOne(*e);
                // the span of the pieces goes with them
                if (const auto at = byId.find(id); at != byId.end() && at->second.isElement && IsSpan(at->second.index))
                    RemoveOne(id);
                if (IsSlot(id))
                    RemoveOne(id + ".spine");
                if (IsPolygon(id))
                    RemoveOne(id + ".circle");
                if (IsCentredRectangle(id)) {
                    RemoveOne(id + ".center");
                    RemoveOne(id + ".diagonal");
                }
                return;
            }
            const auto it = byId.find(id);
            if (it == byId.end() || !it->second.isElement)
                throw std::invalid_argument("Remove('" + id + "'): no such element");
            const size_t k = it->second.index;
            if (const std::optional<size_t> blend = BlendOn(k))
                throw std::invalid_argument("Remove('" + id + "'): '" + elementIds[*blend] + "' is computed from it; Remove('" + elementIds[*blend] +
                                            "') first");
            const std::vector<std::string> ends = EndsToRemove(k, id);
            ElementGoes(k, id);
            Finish();
            std::erase_if(wires, [&](auto &w) {
                std::erase(w.second, id);
                return w.second.empty();
            });
            s.RemoveElement(k);
            EraseElementData(k);
            Reindex();
            for (const std::string &end : ends)
                if (Unused(byId.at(end).index))
                    RemoveOne(end);
            Reindex();
        }
        // the point elements at the ends and centre of element k (id), which go with it if nothing else uses them; raises
        // for the axes and the origin, and for a point something ends at, runs through or has its centre at
        std::vector<std::string> EndsToRemove(size_t k, const std::string &id) const {
            if (k < 3)
                throw std::invalid_argument("Remove('" + id + "'): the sketch's own axes and origin stay");
            const sketch::Element &e = s.elements[k];
            std::vector<std::string> ends;
            const std::vector<size_t> on = e.PointsAndCentre(s);
            for (size_t j = 3; j < s.elements.size(); ++j)
                if (s.elements[j].kind == sketch::ElementKind::Point && std::find(on.begin(), on.end(), s.elements[j].Point()) != on.end())
                    ends.push_back(elementIds[j]);
            if (e.kind == sketch::ElementKind::Point)
                for (size_t j = 0; j < s.elements.size(); ++j) {
                    const sketch::Element &other = s.elements[j];
                    const std::vector<size_t> through = other.PointsOn(s);
                    const bool centre = (other.kind == sketch::ElementKind::Arc || other.kind == sketch::ElementKind::Circle) && other.Center() == e.Point();
                    if (j != k && (centre || std::find(through.begin(), through.end(), e.Point()) != through.end()))
                        throw std::invalid_argument("Remove('" + id + "'): " + elementIds[j] +
                                                    (centre                                      ? " has its center there"
                                                     : other.kind == sketch::ElementKind::Spline ? " runs through it"
                                                                                                 : " ends there") +
                                                    ", remove it first");
                }
            return ends;
        }

        // the element ids by kind, in the order they were made
        std::vector<std::pair<std::string, std::vector<std::string>>> Elements() const {
            std::vector<std::pair<std::string, std::vector<std::string>>> out;
            for (const char *kind : {"Point", "Line", "Arc", "Circle", "Spline", "Curve", "Axis", "Pattern", "Offset"}) {
                std::vector<std::string> ids;
                for (size_t e = 0; e < s.elements.size(); ++e)
                    if (s.elements[e].KindName(s) == kind && !IsHidden(e))
                        ids.push_back(elementIds[e]);
                if (!ids.empty())
                    out.push_back({kind, ids});
            }
            return out;
        }
        // the kind of the element the id names now (no span, no id it had before a rename), none for others
        std::optional<std::string> KindById(const std::string &id) const {
            const auto at = byId.find(id);
            if (at == byId.end() || !at->second.isElement)
                return std::nullopt;
            const size_t e = at->second.index;
            if (elementIds[e] != id || IsHidden(e))
                return std::nullopt;
            return s.elements[e].KindName(s);
        }
        // the (kind, id) of what an id names in a selection: the element and the elements under it (Line.1.corner), in
        // the order of Elements(); none unless the id names a line, arc, circle, spline or point itself
        std::vector<std::pair<std::string, std::string>> Chosen(const std::string &id) const {
            constexpr std::array<const char *, 6> kinds = {"Point", "Line", "Arc", "Circle", "Spline", "Curve"};
            std::vector<std::pair<std::pair<size_t, size_t>, std::string>> hits;
            const auto take = [&](const std::string &x) {
                const auto at = byId.find(x);
                if (at == byId.end() || !at->second.isElement || x == "origin" || x == "px.origin")
                    return false;
                const size_t e = at->second.index;
                const auto rank = std::find_if(kinds.begin(), kinds.end(), [&](const char *k) { return k == s.elements[e].KindName(s); });
                if (elementIds[e] != x || IsHidden(e) || rank == kinds.end())
                    return false;
                hits.push_back({{(size_t)(rank - kinds.begin()), e}, x});
                return true;
            };
            if (!take(id))
                return {};
            if (const auto under = idsUnder.find(id); under != idsUnder.end())
                for (const std::string &x : under->second)
                    take(x);
            std::sort(hits.begin(), hits.end());
            std::vector<std::pair<std::string, std::string>> out;
            for (const auto &[at, x] : hits)
                out.push_back({kinds[at.first], x});
            return out;
        }
        // the id with each negative number in a part (Chain.-1, LinearPattern.1.-1.hole) the number it counts from the end
        // among those there now (Chain.4)
        std::string FromEnd(const std::string &text) const {
            if (text.find(".-") == std::string::npos)
                return text;
            std::string out;
            size_t at = 0;
            while (at <= text.size()) {
                const size_t dot = std::min(text.find('.', at), text.size());
                std::string part = text.substr(at, dot - at);
                if (at > 0 && part.size() > 1 && part[0] == '-' && part.find_first_not_of("0123456789", 1) == std::string::npos) {
                    std::vector<size_t> numbers;
                    const auto add = [&](const std::string &id) {
                        if (!id.starts_with(out + "."))
                            return;
                        const std::string next = id.substr(out.size() + 1, id.find('.', out.size() + 1) - out.size() - 1);
                        if (!next.empty() && next.find_first_not_of("0123456789") == std::string::npos)
                            numbers.push_back((size_t)std::stoull(next));
                    };
                    for (const auto &[id, element] : byId)
                        add(id);
                    for (const auto &[id, elements] : wires)
                        add(id);
                    std::sort(numbers.begin(), numbers.end());
                    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
                    const size_t k = (size_t)std::stoull(part.substr(1));
                    if (k == 0 || k > numbers.size())
                        throw std::invalid_argument("'" + text + "': there are " + std::to_string(numbers.size()) + " numbers under '" + out +
                                                    "' (-1 is the last)");
                    part = std::to_string(numbers[numbers.size() - k]);
                }
                out += (at > 0 ? "." : "") + part;
                at = dot + 1;
            }
            return out;
        }
        // the ids of the groups the calls made by the call (Chain, Rectangle, Offset, LinearPattern, Projection, ...),
        // in the order made
        std::vector<std::pair<std::string, std::vector<std::string>>> CallGroups() const {
            std::vector<std::pair<std::string, std::vector<std::string>>> out;
            for (const auto &[kind, id] : groups) {
                if (!wires.contains(id) && !byId.contains(id))
                    continue;
                auto it = std::find_if(out.begin(), out.end(), [&](const auto &g) { return g.first == kind; });
                if (it == out.end())
                    it = out.insert(out.end(), {kind, {}});
                if (std::find(it->second.begin(), it->second.end(), id) == it->second.end())
                    it->second.push_back(id);
            }
            return out;
        }
        // every point reference with where it is: point ids, the ends (.start, .end), .mid and .center of lines, arcs,
        // circles and splines, a spline's points (.p0, ...)
        std::vector<std::pair<std::string, Vertex>> Locations() {
            Flush();
            std::vector<std::pair<std::string, Vertex>> out;
            const auto add = [&](const std::string &ref) { out.push_back({ref, Location(ref)}); };
            const std::set<size_t> centres = CentrePoints();
            for (size_t e = 2; e < s.elements.size(); ++e) {
                const sketch::Element &el = s.elements[e];
                const std::string &id = elementIds[e];
                if (IsHidden(e))
                    continue;
                switch (el.kind) {
                case sketch::ElementKind::Point:
                    if (!centres.contains(e))
                        add(id);
                    break;
                case sketch::ElementKind::Circle:
                    add(id + ".center");
                    break;
                case sketch::ElementKind::Arc:
                    add(id + ".center");
                    [[fallthrough]];
                case sketch::ElementKind::Line:
                    for (const char *part : {".start", ".end", ".mid"})
                        add(id + part);
                    break;
                case sketch::ElementKind::Spline:
                    for (const char *part : {".start", ".end", ".mid"})
                        add(id + part);
                    // a curve's points are its ends
                    if (!el.IsFitted(s))
                        for (size_t k = 0; k < s.splines[el.SplineIndex()].points.size(); ++k)
                            add(id + ".p" + std::to_string(k));
                    break;
                case sketch::ElementKind::OffsetCurve:
                    for (const char *part : {".start", ".end", ".mid"})
                        add(id + part);
                    break;
                default:
                    break;
                }
            }
            return out;
        }
        // a call that needs the constraints (a flag, a dimension, auto_constraint=True, ...) raises in a plain sketch
        virtual void RequireConstraints(std::string_view call) const {
            throw std::invalid_argument(std::string(call) + ": a QuickSketch has no constraints; px.Sketch(...) has them");
        }
        // strict mode: the constraint sketch raises while the sketch can still move
        virtual void RequireFull(const std::string &) {}
        Vertex Location(const std::string &ref) {
            Flush();
            const sketch::Ref r = RefOf(ref);
            if (!r.IsPoint(s) && r.part != sketch::Part::Tangent)
                throw std::invalid_argument("'" + ref + "' is no point");
            return r.part == sketch::Part::Mid ? r.PointValue(s) : Coords(r.PointAt(s));
        }
        sketch::ElementValues ElementOf(const std::string &id) {
            Flush();
            const sketch::Element &e = s.elements[RefOf(id).element];
            const bool round = e.kind == sketch::ElementKind::Circle || e.kind == sketch::ElementKind::Arc, axis = e.kind == sketch::ElementKind::Axis;
            return {e, round ? s.values[e.Radius()] : 0.0, axis ? Degrees(s.values[e.AxisAngle()]) : 0.0, axis ? s.values[e.AxisDistance()] : 0.0};
        }
        Vertex Coords(size_t point) const { return s.Coords(point); }
        size_t ElementCount() const { return s.elements.size(); }

        struct Undo {
            typename Store::Mark mark;
            std::unordered_map<std::string, size_t> counters;
            size_t elements;
            // the pen without its chain, which only grows during a call: its length
            sketch::PenState pen;
            size_t chain, ended, wireLog;
            std::optional<sketch::PenState> parked;
            size_t groups;
        };

        // a call draws and constrains all or nothing
        template <class F>
        BasicSketch &Atomic(F f) {
            AtomicIf([&] {
                f();
                return true;
            });
            return *this;
        }
        // a call that keeps what it did only when f returns true; else everything is undone, as on an error
        template <class F>
        bool AtomicIf(F f) {
            CallStarts();
            const Undo u{s.Marked(), counters, elementIds.size(), {pen.pos, pen.dir, pen.start, pen.point, pen.first, pen.last, {}, pen.wire},
                         pen.chain.size(), ended.size(), wireLog.size(), parked, groups.size()};
            ++atomic;
            bool keep;
            try {
                keep = f();
                if (keep && atomic == 1)
                    CallEnds();
            } catch (...) {
                --atomic;
                Restore(u);
                throw;
            }
            if (!keep) {
                --atomic;
                Restore(u);
                return false;
            }
            if (--atomic == 0)
                ended.clear();
            return true;
        }
        // everything as it was when the call that made u started
        void Restore(const Undo &u) {
            s.Truncate(u.mark);
            counters = u.counters;
            elementIds.resize(u.elements);
            props.resize(u.elements);
            roles.resize(u.elements);
            CallUndone();
            std::vector<size_t> chain = std::move(pen.chain);
            pen = u.pen;
            parked = u.parked;
            while (wireLog.size() > u.wireLog) {
                const auto [id, before] = wireLog.back();
                wireLog.pop_back();
                if (before.empty())
                    wires.erase(id);
                else if (auto node = wires.extract(id); !node.empty()) {
                    node.key() = before;
                    wires.insert(std::move(node));
                    RenameGroup(id, before);
                }
            }
            groups.resize(u.groups);
            if (ended.size() > u.ended) {
                chain = std::move(ended[u.ended]);
                ended.resize(u.ended);
            }
            pen.chain = std::move(chain);
            pen.chain.resize(u.chain);
            if (const auto it = wires.find(pen.wire); it != wires.end())
                it->second.resize(u.chain);
            Reindex();
        }
        // a construction element is drawn by the construction pen: it starts on the normal pen's point and goes on from
        // the construction element before it; a normal element or a move ends that, the normal pen and its chain stay
        template <class F>
        BasicSketch &Drawing(const Options &opt, F f) {
            if (normalPen || !opt.construction)
                return Atomic([&] {
                    if (!normalPen)
                        parked.reset();
                    f();
                });
            return Atomic([&] {
                if (!parked)
                    parked = sketch::PenState{pen.pos, pen.dir, pen.pos, pen.point, pen.point, std::nullopt, {}, {}};
                normalPen = Swapped(std::move(*parked));
                struct Back {
                    BasicSketch &owner;
                    bool done = false;
                    void Restore() {
                        done = true;
                        owner.parked = owner.Swapped(std::move(*owner.normalPen));
                        owner.normalPen.reset();
                    }
                    ~Back() {
                        if (!done)
                            Restore();
                    }
                } back{*this};
                f();
                back.Restore();
                Sync();
            });
        }
        sketch::PenState Swapped(sketch::PenState in) { return std::exchange(pen, std::move(in)); }

        // the global origin projected into the plane, a fixed point made when it is first needed
        std::string GlobalOrigin() {
            if (!byId.contains("px.origin")) {
                AddElement(s.AddPoint(s.NewPoint(axes.ToHV(Vector3{}), true)), "Point",
                           {"px.origin", {}, true});
            }
            return "px.origin";
        }

        // the name an element carries: name= when drawn, .name = later, a projection's
        std::optional<std::string> NameOf(const std::string &id) const {
            const auto it = byId.find(id);
            if (it == byId.end() || !it->second.isElement || !props[it->second.index])
                return std::nullopt;
            return props[it->second.index]->name;
        }
        BasicSketch &AddProjection(const Projection &p, bool asConstruction, const std::optional<std::string> &name,
                                   const std::string &kind, const std::optional<std::string> &id, bool hold = true) {
            return Atomic([&] {
                Finish();
                const std::string scope = id ? *id : FreshId(kind);
                if (id)
                    CheckId(scope);
                sketch::Scoped guard{idScope, std::exchange(idScope, scope)};
                sketch::ProjectionMaker<BasicSketch>(*this, p, asConstruction, name, kind, scope, hold).Make();
            });
        }

        // an OCC edge given as a reference: projected as a construction element (as Project does), its id
        std::string ProjectedReference(const Projection &p, const std::string &call) {
            if (p.wire.Size() != 1)
                throw std::invalid_argument(call + ": an OCC shape given as a reference projects to " + std::to_string(p.wire.Size()) +
                                            " edges; give one edge, or Project the shape first and name its element");
            const size_t before = s.elements.size();
            AddProjection(p, true, std::nullopt, "Projection", std::nullopt);
            for (size_t e = before; e < s.elements.size(); ++e)
                if (s.elements[e].kind != sketch::ElementKind::Point)
                    return elementIds[e];
            throw std::invalid_argument(call + ": the OCC edge projects to a point; it gives no line to constrain to");
        }

        // the axes (H, V, the sketch's origin) to the point p of the plane; the drawing stays where it is, its
        // coordinates shift. Constraints to the axes follow them; without a solution then nothing changes
        virtual BasicSketch &SetOrigin(const Vertex &p) {
            Flush();
            axes.Translate(axes.XDirection() * p.x + axes.YDirection() * p.y);
            const size_t origin = s.elements[2].Point();
            for (size_t q = 0; q < s.ownCoordsOf.size(); ++q)
                if (q != origin) {
                    s.values[s.ownCoordsOf[q]] -= p.x;
                    s.values[s.ownCoordsOf[q] + 1] -= p.y;
                }
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (s.elements[e].kind == sketch::ElementKind::Axis)
                    s.values[s.elements[e].AxisDistance()] -= AxisShift(e, p);
            pen.pos = pen.pos - p;
            pen.start = pen.start - p;
            if (parked) {
                parked->pos = parked->pos - p;
                parked->start = parked->start - p;
            }
            return *this;
        }
        // how far the axis moves along its normal when the origin moves to p
        double AxisShift(size_t axis, const Vertex &p) const {
            const double t = s.values[s.elements[axis].AxisAngle()];
            return std::cos(t) * p.x + std::sin(t) * p.y;
        }

        // motions move the plane, what is drawn stays where it is in it: in the plane around c by angle degrees
        BasicSketch &Rotate(const Vertex &c, double angle) {
            axes.Rotate(Axis{axes.ToXYZ(c), axes.Normal()}, angle);
            return *this;
        }
        BasicSketch &Rotate(const Axis &axis, double angle) {
            axes.Rotate(axis, angle);
            return *this;
        }
        BasicSketch &Translate(const Vertex &d) {
            axes.Translate(axes.XDirection() * d.x + axes.YDirection() * d.y);
            return *this;
        }
        BasicSketch &Translate(const Vector3 &v) {
            axes.Translate(v);
            return *this;
        }
        // every length times f (coordinates, radii, length dimensions and what Fix holds; angles stay), the plane moved
        // so that c keeps its place
        BasicSketch &Scale(const Vertex &c, double f, bool scaleMaxh = false, bool scaleMinh = false) {
            ScaleInside(f, scaleMaxh, scaleMinh);
            return Translate(c * (1.0 - f));
        }
        BasicSketch &Scale(const Vector3 &p, double f, bool scaleMaxh = false, bool scaleMinh = false) {
            ScaleInside(f, scaleMaxh, scaleMinh);
            axes.Scale(p, f);
            return *this;
        }
        BBox Box() const {
            const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
            Wire w(axes);
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (IsDrawn(e))
                    AddEdges(e, w, vertexProps);
            return w.Box();
        }

        BasicSketch &NamedPoint(const std::string &name, const Vertex &p) {
            IsFinite("Vertex", {p.x, p.y});
            return Atomic([&] { AddElement(s.AddPoint(s.NewPoint(p)), "Point", {name, {}, true}); });
        }
        // the points with ids the user gave, where they are
        std::vector<std::pair<std::string, Vertex>> NamedPoints() {
            Flush();
            std::vector<std::pair<std::string, Vertex>> out;
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (s.elements[e].kind == sketch::ElementKind::Point && givenIds.contains(elementIds[e]) && elementIds[e] != "px.origin")
                    out.push_back({elementIds[e], Coords(s.elements[e].Point())});
            return out;
        }
        // the elements whose properties ids name: elements (id.start, id.end: the point element there), wire ids and
        // offsets; points only if they take the property (points: whether they do)
        std::vector<size_t> PropertyElements(const std::vector<std::string> &ids, bool points, const std::string &call) const {
            std::vector<size_t> out;
            for (const std::string &id : Expanded(ids)) {
                const sketch::Ref r = RefOf(id);
                size_t e = r.element;
                if (r.part == sketch::Part::Start || r.part == sketch::Part::End || r.part == sketch::Part::Center) {
                    const auto at = pointElementOf.find(r.PointAt(s));
                    if (at == pointElementOf.end())
                        throw std::invalid_argument(call + ": no point element at '" + id + "'");
                    e = at->second;
                } else if (r.part != sketch::Part::Whole)
                    throw std::invalid_argument(call + ": '" + id + "' is no element");
                const sketch::ElementKind k = s.elements[e].kind;
                if (e < 3 || k == sketch::ElementKind::Axis || k == sketch::ElementKind::Pattern || k == sketch::ElementKind::Offset)
                    throw std::invalid_argument(call + ": '" + id + "' has no properties to set");
                if ((points || k != sketch::ElementKind::Point) && std::find(out.begin(), out.end(), e) == out.end())
                    out.push_back(e);
            }
            return out;
        }
        // the point elements that are only an arc's or circle's centre (Center.k): listed with their arcs and circles
        // (id.center), they go with them
        std::set<size_t> CentrePoints() const {
            std::set<size_t> ends, centres;
            for (const sketch::Element &e : s.elements) {
                const std::vector<size_t> on = e.PointsOn(s);
                ends.insert(on.begin(), on.end());
                if (e.kind == sketch::ElementKind::Arc || e.kind == sketch::ElementKind::Circle)
                    centres.insert(e.Center());
            }
            std::set<size_t> out;
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (s.elements[e].kind == sketch::ElementKind::Point && centres.contains(s.elements[e].Point()) && !ends.contains(s.elements[e].Point()))
                    out.insert(e);
            return out;
        }
        // the point element a point reference names (a point's id, id.start, id.end, id.center), none for others
        std::optional<std::string> PointElementAt(const std::string &ref) const {
            const sketch::Ref r = RefOf(ref);
            if (r.part == sketch::Part::Whole)
                return s.elements[r.element].kind == sketch::ElementKind::Point ? std::optional(elementIds[r.element]) : std::nullopt;
            if (r.part != sketch::Part::Start && r.part != sketch::Part::End && r.part != sketch::Part::Center)
                return std::nullopt;
            const auto at = pointElementOf.find(r.PointAt(s));
            return at == pointElementOf.end() ? std::nullopt : std::optional(elementIds[at->second]);
        }
        // the point element old names gets id; old stays in byId for what was made with it (pattern copies, offsets)
        void RenamePoint(const std::string &old, const std::string &id) {
            const sketch::Ref r = RefOf(old);
            const size_t e = r.element;
            if (r.part != sketch::Part::Whole || s.elements[e].kind != sketch::ElementKind::Point)
                throw std::invalid_argument("id = '" + id + "': " + old + " is no point element");
            if (elementIds[e] == id)
                return;
            CheckUserId(id);
            CheckId(id);
            for (auto &[from, to] : renamed)
                if (to == elementIds[e])
                    to = id;
            renamed[elementIds[e]] = id;
            givenIds.insert(id);
            ListUnder(elementIds[e], false);
            ListUnder(id, true);
            elementIds[e] = id;
            byId[id] = {true, e};
        }
        // change on the properties of the elements ids name (points if they take them), as given when drawing
        template <class F>
        BasicSketch &ChangeProperties(const std::vector<std::string> &ids, bool points, const std::string &call, F change) {
            const std::vector<size_t> elements = PropertyElements(ids, points, call);
            if (elements.empty())
                throw std::invalid_argument(call + (ids.empty() ? ": selects nothing" : ": a point takes name, maxh and hpref"));
            for (size_t e : elements)
                props[e] = ChangedProperties(props[e], change);
            return *this;
        }
        // the elements ids name made construction (on) or drawn: Face(), the exports, plots and the bounding box follow;
        // constraints, ids, wire ids and the pens stay
        BasicSketch &SetConstruction(const std::vector<std::string> &ids, bool on, const std::string &call) {
            const std::vector<size_t> elements = PropertyElements(ids, true, call);
            if (elements.empty())
                throw std::invalid_argument(call + ": selects nothing");
            for (size_t e : elements)
                roles[e] = on ? sketch::Role::Construction : sketch::Role::Drawn;
            return *this;
        }
        // the properties of the elements ids name that take them (points: whether they do), each with whether it is
        // construction
        std::vector<std::pair<Properties, bool>> ElementProperties(const std::vector<std::string> &ids, bool points, const std::string &call) const {
            std::vector<std::pair<Properties, bool>> out;
            for (size_t e : PropertyElements(ids, points, call))
                out.push_back({props[e], !IsDrawn(e)});
            return out;
        }
        // an element's solved edges (a circle: its two halves, a spline: its pieces)
        Wire ElementWire(const std::string &id) {
            Flush();
            if (const std::vector<std::string> ids = Expanded({id}); ids.size() != 1 || ids[0] != id)
                return ElementsWire(ids);
            const sketch::Ref r = RefOf(id);
            const sketch::ElementKind k = s.elements[r.element].kind;
            if (r.part != sketch::Part::Whole || r.element < 3 || k == sketch::ElementKind::Point || k == sketch::ElementKind::Axis ||
                k == sketch::ElementKind::Pattern || k == sketch::ElementKind::Offset)
                throw std::invalid_argument("'" + id + "' is no line, arc, circle or spline");
            Wire w(axes);
            AddEdges(r.element, w, VertexProps());
            return w;
        }
        // an element's solved edges without the properties of its vertices, for the drawing helpers
        Wire ElementGeometry(const std::string &id) {
            Flush();
            Wire w(axes);
            AddEdges(RefOf(id).element, w, {});
            return w;
        }
        Wire ElementsWire(const std::vector<std::string> &ids) {
            Flush();
            const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
            Wire w(axes);
            for (const std::string &id : ids)
                AddEdges(RefOf(id).element, w, vertexProps);
            return w;
        }
        // the edges, drawn and construction, named name
        Wire NamedEdges(const std::string &name) {
            Flush();
            const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
            Wire w(axes);
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (props[e] && props[e]->name == name && s.elements[e].kind != sketch::ElementKind::Point && !IsHidden(e))
                    AddEdges(e, w, vertexProps);
            return w;
        }

        std::string OffsetCurveSource(const sketch::Element &e) const {
            const sketch::OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
            for (size_t k = 0; k < s.elements.size(); ++k)
                if (s.elements[k].kind == sketch::ElementKind::Spline && s.elements[k].SplineIndex() == c.spline && s.elements[k].PieceIndex() == c.piece)
                    return elementIds[k];
            return "";
        }

        // an infinite construction line through p along dir, free to move
        std::string AddAxisLine(const Vertex &p, const Vertex &along, const std::string &prefix, const std::optional<std::string> &id) {
            const Vertex u = along * (1.0 / Norm(along)), n{u.y, -u.x};
            AddElement(s.AddAxis(std::atan2(n.y, n.x), Dot(n, p), false), prefix, {id, {}, true});
            return elementIds.back();
        }
        // what a reference names: "point", "line", "axis" (an infinite line), "round" (circle or arc)
        std::string RefKind(const std::string &ref) const {
            const sketch::Ref r = RefOf(ref);
            return r.IsPoint(s) ? "point" : r.IsSegment(s) ? "line" : r.IsLine(s) ? "axis" : "round";
        }

        std::pair<Vertex, Vertex> LineOf(const std::string &axis, const std::string &call) const {
            const sketch::Ref ar = RefOf(axis);
            if (!ar.IsLine(s))
                throw std::invalid_argument(call + ": the axis '" + axis + "' is no line");
            return s.elements[ar.element].Carrier(s);
        }
        // the whole sketch mirrored at the line axis: its plane is mirrored, the coordinates, constraints and dimensions
        // in it stay
        BasicSketch &Mirror(const std::string &axis) {
            Flush();
            const auto [a0, u] = LineOf(axis, "Mirror");
            const Vector3 along = axes.XDirection() * u.x + axes.YDirection() * u.y;
            axes.MirrorKeepingCoordinates(AxesAlong(axes.ToXYZ(a0), Cross(along, axes.Normal())));
            return *this;
        }
        // the elements mirrored in place at the line axis: the points they reach (shared ones once, so what they share
        // with other elements moves too), an arc turning the other way, a spline's tangents; the mirrored points and
        // where they went
        std::vector<std::pair<size_t, Vertex>> MirrorElements(const std::vector<size_t> &elements, const std::string &axis) {
            const auto [a0, u] = LineOf(axis, "Mirror");
            const double turn = 2.0 * std::atan2(u.y, u.x);
            std::vector<size_t> points;
            std::set<size_t> splines;
            for (size_t e : elements) {
                sketch::Element &el = s.elements[e];
                switch (el.kind) {
                case sketch::ElementKind::Point:
                    points.push_back(el.Point());
                    break;
                case sketch::ElementKind::Circle:
                    points.push_back(el.Center());
                    if (el.OriginPoint() != sketch::none)
                        points.push_back(el.OriginPoint());
                    s.values[el.OriginAngle()] = turn - s.values[el.OriginAngle()];
                    el.ccw = !el.ccw;
                    break;
                case sketch::ElementKind::Line:
                    points.insert(points.end(), {el.Start(), el.End()});
                    break;
                case sketch::ElementKind::Arc:
                    points.insert(points.end(), {el.Center(), el.Start(), el.End()});
                    el.ccw = !el.ccw;
                    break;
                default:
                    if (splines.insert(el.SplineIndex()).second) {
                        sketch::SplineData &d = s.splines[el.SplineIndex()];
                        points.insert(points.end(), d.points.begin(), d.points.end());
                        for (size_t t : d.tangent)
                            s.values[t] = turn - s.values[t];
                        // a fitted spline's frame: its place mirrored, its turn as a tangent's, its shape turned over
                        if (d.fitted) {
                            points.push_back(d.fitted->origin);
                            s.values[d.fitted->angle] = turn - s.values[d.fitted->angle];
                            d.fitted->mirrored = !d.fitted->mirrored;
                        }
                    }
                }
            }
            std::set<size_t> slots;
            for (size_t q : points)
                slots.insert({s.coordsOf[q], s.ownCoordsOf[q]});
            for (size_t k : slots) {
                const Vertex m = planex2d::Mirrored(Vertex{s.values[k], s.values[k + 1]}, a0, u);
                s.values[k] = m.x;
                s.values[k + 1] = m.y;
            }
            std::vector<std::pair<size_t, Vertex>> out;
            for (size_t q : points)
                out.push_back({q, Coords(q)});
            return out;
        }
        BasicSketch &Mirror(const std::vector<std::string> &ids, const std::string &axis) {
            Flush();
            MirrorElements(Originals(ids, "Mirror"), axis);
            return *this;
        }
        // copies of the elements (with the points their lines and arcs end at, each copied once) mirrored at the
        // carrier of axis, under the call's scope (Mirrored.N or id) named after their sources (Mirrored.1.Line.1), with
        // their properties; tie (auto_constraint): every pair of points (and circle centres) symmetric about the axis and
        // every arc and circle copy of equal radius, so the copies follow the originals
        BasicSketch &Mirrored(const std::vector<std::string> &ids, const std::string &axis, bool tie, bool internal,
                              const std::optional<std::string> &id) {
            if (tie)
                RequireConstraints("Mirrored(auto_constraint=True)");
            if (internal)
                RequireConstraints("Mirrored(internal_constraints=True)");
            return Atomic([&] {
                const auto [a0, u] = LineOf(axis, "Mirrored");
                const std::string scope = id ? *id : FreshId("Mirrored");
                if (id)
                    CheckId(scope);
                const std::vector<size_t> originals = Originals(ids, "Mirrored");
                const sketch::Copies made = CopyElements(originals, [&](const Vertex &p) { return planex2d::Mirrored(p, a0, u); }, true, scope);
                if (tie)
                    MirrorTied(made, RefOf(axis));
                else if (internal)
                    ConstraintsCopied(scope, true);
                CopyWires(ids, made, scope);
                std::vector<size_t> edges;
                for (const auto &[o, c] : made.elements)
                    edges.push_back(c);
                if (!edges.empty())
                    NameWire(scope, edges, "Mirrored: ");
                groups.push_back({"Mirrored", scope});
                MirrorMade(scope, ids, axis, tie, internal);
            });
        }
        // the wires among ids copied too, under prefix: their copied elements in their order
        void CopyWires(const std::vector<std::string> &ids, const sketch::Copies &made, const std::string &prefix) {
            for (const std::string &id : ids) {
                const auto w = wires.find(id);
                if (w == wires.end())
                    continue;
                std::vector<size_t> copies;
                for (const std::string &e : w->second)
                    for (const auto &[o, c] : made.elements)
                        if (elementIds[o] == e)
                            copies.push_back(c);
                if (!copies.empty())
                    NameWire(prefix + "." + id, copies, "copy: ");
            }
        }

        // count instances of the elements (the original first): turned by step degrees about the point ref, or moved
        // step along the line ref (linear), with second also count2 times step2 along its line (a grid);
        // keep (the constraint sketch): tied to the original, see ConstraintSketch
        BasicSketch &AddPattern(const std::vector<std::string> &ids, const std::string &ref, size_t count, double step, bool linear,
                                bool keep, bool internal, const std::optional<std::string> &id, bool = false,
                                const std::optional<sketch::PatternDir2> &second = std::nullopt) {
            const std::string call = linear ? "LinearPattern" : "CircularPattern";
            CheckPattern(call, count, step, second);
            if (keep)
                RequireConstraints(call + "(auto_constraint=True)");
            if (internal)
                RequireConstraints(call + "(internal_constraints=True)");
            return Atomic([&] {
                const sketch::Ref r = PatternRef(call, ref, linear);
                const std::optional<sketch::Ref> r2 = second ? std::optional(PatternRef(call, second->ref, true)) : std::nullopt;
                const std::vector<size_t> originals = Originals(ids, call);
                const std::string scope = id ? *id : FreshId(call);
                if (id)
                    CheckId(scope);
                std::vector<size_t> edges;
                for (const auto &[k, j] : PatternSteps(count, second ? second->count : 0)) {
                    const std::string prefix = StepPrefix(scope, k, j, second.has_value());
                    const sketch::Copies made = PlaceCopy(originals, r, linear, step, k, prefix, r2, second ? second->step : 0.0, j);
                    if (internal)
                        ConstraintsCopied(prefix, false);
                    CopyWires(ids, made, prefix);
                    for (const auto &[o, c] : made.elements)
                        edges.push_back(c);
                }
                if (!edges.empty())
                    NameWire(scope, edges, call + ": ");
                groups.push_back({call, scope});
            });
        }
        static void CheckPattern(const std::string &call, size_t count, double step, const std::optional<sketch::PatternDir2> &second = std::nullopt) {
            if (count < (second ? 1u : 2u))
                throw std::invalid_argument(call + ": count is the number of instances with the original, at least " + (second ? "1" : "2"));
            IsFinite(call.c_str(), {step});
            if (second) {
                if (second->count < 1 || count * second->count < 2)
                    throw std::invalid_argument(call + ": count2 is the number of instances along dir2 with the original, at least 1, and "
                                                "count times count2 at least 2");
                IsFinite(call.c_str(), {second->step});
            }
        }
        // the steps (along dir, along dir2) of a pattern's copies, the original (0, 0) left out; count2 0: one direction
        static std::vector<std::pair<size_t, size_t>> PatternSteps(size_t count, size_t count2) {
            std::vector<std::pair<size_t, size_t>> out;
            for (size_t j = 0; j < std::max<size_t>(count2, 1); ++j)
                for (size_t k = 0; k < count; ++k)
                    if (k > 0 || j > 0)
                        out.push_back({k, j});
            return out;
        }
        // the ids of a copy: the pattern's, then its step, or its two steps in a pattern of two directions
        static std::string StepPrefix(const std::string &scope, size_t k, size_t j, bool two) {
            return scope + "." + std::to_string(k) + (two ? "." + std::to_string(j) : "");
        }
        // the line a linear pattern moves along, the point a circular one turns about
        sketch::Ref PatternRef(const std::string &call, const std::string &ref, bool linear) const {
            const sketch::Ref r = RefOf(ref);
            if (linear ? !r.IsLine(s) : !r.IsPoint(s))
                throw std::invalid_argument(call + ": '" + ref + (linear ? "' is no line" : "' is no point"));
            return r;
        }
        // the k-th copy of the originals, k steps on (a length along the line r, or degrees about the point r) and j steps
        // of step2 along the line r2 in a pattern of two directions, its ids under prefix
        sketch::Copies PlaceCopy(const std::vector<size_t> &originals, const sketch::Ref &r, bool linear, double step, size_t k, const std::string &prefix,
                                 const std::optional<sketch::Ref> &r2 = std::nullopt, double step2 = 0.0, size_t j = 0) {
            Vertex o, u;
            if (linear)
                u = s.elements[r.element].Carrier(s).second;
            else
                o = r.PointValue(s);
            const Vertex u2 = r2 ? s.elements[r2->element].Carrier(s).second * (step2 * (double)j) : Vertex{};
            return CopyElements(originals, [&](const Vertex &p) {
                return linear ? (r2 ? p + u * (step * (double)k) + u2 : p + u * (step * (double)k)) : Rotated(p, o, step * (double)k);
            }, false, prefix);
        }
        // the offset of the elements at distance d as new elements (constraints on: tied to the source, the distance the
        // dimension id.d; a QuickSketch makes the same elements without ties), shaped as the exact offset, round around
        // the corners. A closed chain (or a circle): one loop outside (d > 0, id.outer) or inside (d < 0, id.inner); an open
        // chain or one line or arc: both sides (id.left and id.right, left and right of the drawing direction) with the
        // caps id.startcap and id.endcap around its ends, or one side without caps (both_sides false: the left for
        // d > 0). both_sides true: both sides of a closed chain too; a point: the side it lies on, d the distance.
        // id.left.Line.3 is the offset of Line.3, id.left.Corner.k the corners along the chain. The pens stay
        BasicSketch &Offset(const std::vector<std::string> &ids, double d, const std::optional<Vertex> &point,
                                 const std::optional<bool> &bothSides, const OffsetForms &forms, const Options &opt,
                                 const std::optional<bool> &constructionGiven) {
            std::string shown;
            for (const std::string &id : ids)
                shown += (shown.empty() ? "'" : ", '") + id + "'";
            const std::string call = "Offset(" + (ids.size() == 1 ? shown : "[" + shown + "]") + ")";
            IsFinite(call.c_str(), {d});
            if (point)
                IsFinite(call.c_str(), {point->x, point->y});
            if (d == 0.0)
                throw std::invalid_argument(call + ": the distance d must not be 0");
            if (point && bothSides.value_or(false))
                throw std::invalid_argument(call + ": a point picks one side, both_sides=True asks for both");
            if (d < 0.0 && (point || bothSides.value_or(false)))
                throw std::invalid_argument(call + ": d is the distance to " + (point ? "the point's side" : "both sides") + ", it must be positive");
            return Atomic([&] { MakeOffset(ChainOf(ids, call), d, point, bothSides, opt, constructionGiven, call, "", std::nullopt, forms); });
        }
        // a slot (kind "Slot"), arc slot ("ArcSlot") or keyhole ("Keyhole", hole: the radius of its round start) of width
        // w: spine() draws its spine as a construction element, named id = scope.spine, the outline is the spine's offset
        // on both sides at w / 2 with round caps (scope.left, .right, .startcap, .endcap), the scope the wire of the
        // outline (call is the text of the errors). The pen goes on at the spine's end in its direction, as a new chain
        template <class F>
        BasicSketch &Slot(const std::string &kind, double w, std::optional<double> hole, const Options &opt, const std::string &call, F spine) {
            IsFinite(call.c_str(), {w, hole.value_or(0.0)});
            if (!(w > 0.0))
                throw std::invalid_argument(call + ": the width w = " + NumberText(w) + " must be positive");
            if (hole)
                if (const std::optional<std::string> wrong = HoleProblem(w, *hole, std::nullopt))
                    throw std::invalid_argument(call + ": " + *wrong);
            return Atomic([&] {
                const std::string name = opt.id ? *opt.id : FreshId(kind), id = name + ".spine";
                CheckId(name);
                const size_t chains = counters["Chain"];
                parked.reset();
                {
                    sketch::Scoped scope{idScope, std::exchange(idScope, name)};
                    spine();
                }
                if (!parked || !parked->last)
                    throw std::invalid_argument(call + ": the spine has no length; give it one");
                Rename(*parked->last, id);
                CheckSpine(id, w, hole, call);
                MakeOffset(ChainOf({id}, call), w / 2.0, std::nullopt, true, Options{name, opt.props, opt.construction}, std::nullopt, call, kind, hole);
                PenAtSpineEnd(chains);
            });
        }
        // a group one of the calls kinds made
        bool IsGroup(const std::string &id, std::initializer_list<std::string_view> kinds) const {
            for (const auto &[kind, group] : groups)
                if (group == id && std::find(kinds.begin(), kinds.end(), kind) != kinds.end())
                    return true;
            return false;
        }
        bool IsSlot(const std::string &id) const { return byId.contains(id + ".spine") && IsGroup(id, {"Slot", "ArcSlot", "Keyhole"}); }
        bool IsCentredRectangle(const std::string &id) const { return byId.contains(id + ".center") && IsGroup(id, {"RectangleC"}); }
        bool IsPolygon(const std::string &id) const { return byId.contains(id + ".circle") && IsGroup(id, {"Polygon", "Hexagon"}); }
        // a regular polygon (kind "Polygon") or hexagon ("Hexagon", flats: its width across flats) of n sides: circle() draws
        // its construction circle, named scope.circle; the corners are on it, the first along the pen's direction from the
        // centre, the sides scope.Line.1 to .Line.n counterclockwise from it; the pens stay
        template <class F>
        BasicSketch &Polygon(const std::string &kind, size_t n, std::optional<double> flats, const Options &opt, const std::string &call, F circle) {
            IsFinite(call.c_str(), {flats.value_or(1.0)});
            if (flats && !(*flats > 0.0))
                throw std::invalid_argument(call + ": the width across flats af = " + NumberText(*flats) + " must be positive");
            return Atomic([&] {
                const std::string name = opt.id ? *opt.id : FreshId(kind);
                CheckId(name);
                const Vertex along = pen.dir;
                parked.reset();
                {
                    sketch::Scoped scope{idScope, std::exchange(idScope, name)};
                    circle();
                }
                parked.reset();
                const size_t ring = elementIds.size() - 1;
                Rename(ring, name + ".circle");
                const std::vector<size_t> sides = PolygonSides(name, ring, n, along, Options{std::nullopt, opt.props, opt.construction});
                NameWire(name, sides, call + ": ");
                groups.push_back({kind, name});
                PolygonDimension(name, ring, sides, flats);
            });
        }
        // the width across flats (opposite sides 0 and 3, a hexagon's) or else the radius of the circle ring
        void PolygonDimension(const std::string &name, size_t ring, const std::vector<size_t> &sides, const std::optional<double> &flats) {
            if (flats)
                Dimension(sketch::Kind::Distance, {{sides[0]}, {sides[3]}}, *flats, name + ".af", false);
            else
                Dimension(sketch::Kind::Radius, {{ring}}, s.values[s.elements[ring].Radius()], name + ".r", false);
        }
        // the n corners on the circle ring and the sides between them, the sides equal; the sides' elements
        std::vector<size_t> PolygonSides(const std::string &name, size_t ring, size_t n, const Vertex &along, const Options &inner) {
            const Vertex centre = Coords(s.elements[ring].Center()), left{-along.y, along.x};
            const double r = s.values[s.elements[ring].Radius()];
            sketch::Scoped scope{idScope, std::exchange(idScope, name)};
            std::vector<size_t> corners, sides;
            for (size_t k = 0; k < n; ++k) {
                const double angle = 2.0 * std::numbers::pi * (double)k / (double)n;
                corners.push_back(AddElement(s.AddPoint(s.NewPoint(centre + (along * std::cos(angle) + left * std::sin(angle)) * r)), "Point", inner));
            }
            for (size_t k = 0; k < n; ++k)
                sides.push_back(AddElement(s.AddLine(s.elements[corners[k]].Point(), s.elements[corners[(k + 1) % n]].Point()), "Line", inner));
            PolygonTies(ring, corners, sides);
            return sides;
        }
        void PolygonTies(size_t ring, const std::vector<size_t> &corners, const std::vector<size_t> &sides) {
            for (size_t corner : corners)
                Relate(sketch::Kind::Coincident, {{corner}, {ring}});
            for (size_t k = 0; k + 1 < sides.size(); ++k)
                Relate(sketch::Kind::Equal, {{sides[k]}, {sides[k + 1]}});
        }
        sketch::OffsetChain ChainOf(const std::vector<std::string> &ids, const std::string &call) const {
            std::vector<size_t> given;
            for (const std::string &id : Expanded(ids)) {
                const sketch::Ref r = RefOf(id);
                const sketch::ElementKind k = s.elements[r.element].kind;
                if (r.part == sketch::Part::Whole && k == sketch::ElementKind::Point)
                    continue;
                if (r.part != sketch::Part::Whole || r.element < 3 ||
                    !(k == sketch::ElementKind::Line || k == sketch::ElementKind::Arc || k == sketch::ElementKind::Circle || k == sketch::ElementKind::Spline))
                    throw std::invalid_argument(call + ": '" + id + "' is no line, arc, circle or spline" +
                                                (k == sketch::ElementKind::OffsetCurve ? " (an offset of an offset curve comes later)" : ""));
                if (k == sketch::ElementKind::Spline && s.elements[r.element].IsPiece(s)) {
                    const planex2d::Curve<double, Vertex> f = s.elements[r.element].Curve(s);
                    if (s.elements[r.element].Range(s, f)[1] > f.params.back())
                        throw std::invalid_argument(call + ": '" + id + "' runs over the seam of its closed spline, which an offset cannot take yet");
                }
                if (std::find(given.begin(), given.end(), r.element) == given.end())
                    given.push_back(r.element);
            }
            if (given.empty())
                throw std::invalid_argument(call + ": no line, arc, circle or spline to offset");
            if (std::any_of(given.begin(), given.end(), [&](size_t e) { return s.elements[e].kind == sketch::ElementKind::Circle; })) {
                if (given.size() > 1)
                    throw std::invalid_argument(call + ": a circle is a loop of its own, offset it alone");
                return {given, {1}, true};
            }
            if (std::any_of(given.begin(), given.end(), [&](size_t e) { return LoopSpline(e); })) {
                if (given.size() > 1)
                    throw std::invalid_argument(call + ": a closed spline is a loop of its own, offset it alone");
                return {given, {1}, true};
            }
            const auto rootsOf = [&](size_t e) {
                const std::array<size_t, 2> ends = s.elements[e].Ends(s);
                return std::array<size_t, 2>{s.coordsOf[ends[0]], s.coordsOf[ends[1]]};
            };
            std::map<size_t, size_t> degree;
            for (size_t e : given)
                for (size_t root : rootsOf(e))
                    if (++degree[root] > 2)
                        throw std::invalid_argument(call + ": the elements branch at a point, an offset takes one chain");
            std::vector<char> used(given.size(), 0);
            used[0] = 1;
            // the element not in the chain yet that ends at root, and whether it starts there
            const auto touching = [&](size_t root) -> std::optional<std::pair<size_t, bool>> {
                for (size_t i = 0; i < given.size(); ++i) {
                    const std::array<size_t, 2> ends = rootsOf(given[i]);
                    if (!used[i] && (ends[0] == root || ends[1] == root)) {
                        used[i] = 1;
                        return std::pair{given[i], ends[0] == root};
                    }
                }
                return std::nullopt;
            };
            sketch::OffsetChain c{{given.front()}, {1}, false};
            size_t head = rootsOf(given.front())[0], tail = rootsOf(given.front())[1];
            while (const auto x = touching(tail)) {
                c.elements.push_back(x->first);
                c.forward.push_back(x->second);
                tail = rootsOf(x->first)[x->second ? 1 : 0];
            }
            while (const auto x = touching(head)) {
                c.elements.insert(c.elements.begin(), x->first);
                c.forward.insert(c.forward.begin(), !x->second);
                head = rootsOf(x->first)[x->second ? 1 : 0];
            }
            for (size_t i = 0; i < given.size(); ++i)
                if (!used[i])
                    throw std::invalid_argument(call + ": the elements are not one chain (" + elementIds[given[i]] + " does not join on)");
            c.closed = head == tail;
            return c;
        }
        void MakeOffset(const sketch::OffsetChain &source, double d, const std::optional<Vertex> &point, const std::optional<bool> &bothSides,
                        const Options &opt, const std::optional<bool> &constructionGiven, const std::string &call, const std::string &slot = "",
                        std::optional<double> hole = std::nullopt, const OffsetForms &forms = {}) {
            sketch::OffsetMaker<BasicSketch>(*this, source, d, point, bothSides, opt, constructionGiven, call, slot, hole, forms).Make();
        }
        // the source chain's edges, each running along the chain (none for a circle)
        EdgeList SourceEdges(const sketch::OffsetChain &source) const {
            EdgeList edges;
            for (size_t k = 0; k < source.elements.size(); ++k) {
                const sketch::Element &e = s.elements[source.elements[k]];
                const bool f = source.forward[k];
                const size_t i = e.kind == sketch::ElementKind::Line ? 0 : 2;
                if (e.kind == sketch::ElementKind::Circle)
                    continue;
                if (e.kind == sketch::ElementKind::Spline) {
                    for (planex2d::Spline &half : SourceSpline(e, f))
                        edges.push_back(std::move(half));
                    continue;
                }
                const Vertex a = Coords(e.p[f ? i : i + 1]), b = Coords(e.p[f ? i + 1 : i]);
                if (e.kind == sketch::ElementKind::Line)
                    edges.push_back(planex2d::Line(a, b));
                else
                    edges.push_back(planex2d::Arc(a, b, Coords(e.Center()), e.ccw == f));
            }
            return edges;
        }
        // the sides of the offset of the source by ad: of a closed chain outer (+1) and inner (-1), named; of an open chain
        // its band (0) or one side (+1 left, -1 right), the pieces telling their sides. A side with nothing left raises
        sketch::OffsetSides SidesOf(const sketch::OffsetChain &source, const EdgeList &edges, double ad,
                                                                             const std::vector<int> &towards, const OffsetForms &forms,
                                                                             const std::string &call) const {
            sketch::OffsetSides out;
            const sketch::Element &c = s.elements[source.elements.front()];
            const bool circle = c.kind == sketch::ElementKind::Circle, alone = circle || ClosedSpline(source);
            for (int t : towards) {
                if (source.closed) {
                    std::vector<OffsetLoop> loops = alone ? std::vector<OffsetLoop>{} : ClosedOffsetLoops(edges, t * ad, forms.corners, axes, call);
                    if (circle ? t < 0 && ad >= s.values[c.Radius()] : !alone && loops.empty())
                        throw std::invalid_argument(call + ": nothing is left inside at d = " + NumberText(ad));
                    out.push_back({t > 0 ? "outer" : "inner", std::move(loops)});
                    continue;
                }
                // one side has no caps
                std::vector<OffsetLoop> band = BandLoops(edges, ad, t == 0 ? forms.ends : OffsetEnd::Round, forms.corners, axes, call);
                if (t != 0) {
                    band = BandSide(band, t, source.elements.size(), call);
                    if (band.empty())
                        throw std::invalid_argument(call + ": nothing is left on the " + (t > 0 ? "left" : "right") + " at d = " + NumberText(ad));
                }
                out.push_back({"", std::move(band)});
            }
            return out;
        }
        // what is wrong with a keyhole of width w, hole radius r and (if known) spine length: r must exceed w / 2, and the
        // spine must reach where the sides meet the hole
        std::optional<std::string> HoleProblem(double w, double r, std::optional<double> length) const {
            if (!(r > w / 2.0))
                return "the hole's radius r = " + SizeText(r) + " must exceed half the width w / 2 = " + SizeText(w / 2.0) + "; make r larger or w smaller";
            const double reach = std::sqrt(r * r - w * w / 4.0);
            if (length && !(*length > reach))
                return "the spine's length " + SizeText(*length) + " must exceed " + SizeText(reach) + ", where the sides meet the hole of radius r = " +
                       SizeText(r) + "; make the spine longer";
            return std::nullopt;
        }
        void CheckSpine(const std::string &id, double w, std::optional<double> hole, const std::string &call) const {
            const sketch::Element &e = s.elements[byId.at(id).index];
            if (hole)
                if (const std::optional<std::string> wrong = HoleProblem(w, *hole, e.Length(s)))
                    throw std::invalid_argument(call + ": " + *wrong);
            if (e.kind == sketch::ElementKind::Arc && !(w < 2.0 * s.values[e.Radius()]))
                throw std::invalid_argument(call + ": the width w = " + NumberText(w) + " must be below 2 r = " + NumberText(2.0 * s.values[e.Radius()]) +
                                            " (the spine arc's radius r = " + NumberText(s.values[e.Radius()]) + "); make w smaller or r larger");
        }
        // the construction run that drew the spine ends: its place becomes the pen's, its chain wire is dropped
        void PenAtSpineEnd(size_t chains) {
            const sketch::PenState run = *parked;
            Finish();
            std::erase_if(groups, [&](const auto &g) { return g.second == run.wire; });
            wires.erase(run.wire);
            counters["Chain"] = chains;
            pen.pos = pen.start = run.pos;
            pen.dir = run.dir;
            pen.point = pen.first = run.point;
        }
        // the offset curve of chain element p.k at the distance of the offset element, between its points at (at the
        // chain's vertex k, then k + 1). tangent: that end is where the spline ends; else (a crossing) where it ends along
        // the spline is an unknown, started where the curve comes nearest the point
        sketch::OffsetCurveData CurveData(const sketch::OffsetChain &source, const OffsetPiece &p, size_t offset, std::array<size_t, 2> at,
                                      std::array<bool, 2> tangent) {
            const sketch::Element &e = s.elements[source.elements[p.k]];
            const bool along = source.forward[p.k];
            sketch::OffsetCurveData c{e.SplineIndex(), e.PieceIndex(), s.elements[offset].Distance(), (double)(along ? p.side : -p.side), {}, !along};
            const planex2d::Curve<double, Vertex> f = e.Curve(s);
            std::array<size_t, 2> t{sketch::none, sketch::none};
            for (size_t i = 0; i < 2; ++i)
                if (!tangent[i]) {
                    const std::array<double, 2> range = c.Range(s, f);
                    t[i] = s.NewValue(sketch::CurveEval{f, c.side, s.values[c.distance]}.Nearest(range[0], range[1], Coords(at[i])));
                }
            c.range = along ? sketch::SplinePiece{t[0], t[1], at[0], at[1]} : sketch::SplinePiece{t[1], t[0], at[1], at[0]};
            return c;
        }
        // a whole closed spline: a loop of its own
        bool LoopSpline(size_t e) const {
            const sketch::Element &el = s.elements[e];
            return el.kind == sketch::ElementKind::Spline && !el.IsPiece(s) && s.splines[el.SplineIndex()].closed;
        }
        bool ClosedSpline(const sketch::OffsetChain &source) const { return source.elements.size() == 1 && LoopSpline(source.elements[0]); }
        // a QuickSketch's offset curve of chain element p.k at the distance d between its points at (as CurveData takes
        // them): frozen into an ordinary spline
        void FrozenCurve(const sketch::OffsetChain &source, const OffsetPiece &p, double d, std::array<size_t, 2> at, std::array<bool, 2> tangent,
                         const Options &o, const std::string &call) {
            const sketch::Element &e = s.elements[source.elements[p.k]];
            const bool along = source.forward[p.k];
            const sketch::CurveEval g{e.Curve(s), (double)(along ? p.side : -p.side), d};
            const auto [from, to] = e.Range(s, g.f);
            std::array<double, 2> t{along ? from : to, along ? to : from};
            for (size_t i = 0; i < 2; ++i)
                if (!tangent[i])
                    t[i] = g.Nearest(from, to, Coords(at[i]));
            if (const std::optional<std::string> cusp = sketch::Cusp(g.f, g.side, std::min(t[0], t[1]), std::max(t[0], t[1]), d, elementIds[source.elements[p.k]]))
                throw std::invalid_argument(call + ": " + *cusp);
            AddElement(FitCurve(g, t[0], t[1], at, o), "Spline", o);
        }
        // the spline element FitParams gives, without its id; the points between its ends are made as point elements
        size_t FitCurve(const sketch::CurveEval &g, double ta, double tb, std::array<size_t, 2> at, const Options &o) {
            const bool closed = at[0] == at[1];
            const std::vector<double> t = g.FitParams(ta, tb, Coords(at[0]), Coords(at[1]), closed);
            const std::array<Vertex, 2> ends = g.EndDirections(ta, tb);
            std::vector<size_t> ids{at[0]};
            for (size_t i = 1; i + 1 < t.size(); ++i) {
                AddElement(s.AddPoint(s.NewPoint(g.At(t[i], 0))), "Point", {std::nullopt, {}, o.construction});
                ids.push_back(s.elements.back().Point());
            }
            if (!closed)
                ids.push_back(at[1]);
            const std::vector<double> angles = closed ? std::vector<double>{} : std::vector<double>{std::atan2(ends[0].y, ends[0].x), std::atan2(ends[1].y, ends[1].x)};
            return s.AddSpline(ids, closed ? std::vector<size_t>{} : std::vector<size_t>{0, ids.size() - 1}, angles, closed, SplineStyle::Averaged);
        }
        // an offset curve of a constraint sketch as the ordinary spline a QuickSketch offset makes
        void FreezeOffsetCurve(size_t e) {
            const sketch::OffsetRun run = s.elements[e].Run(s);
            const std::array<size_t, 2> ends = s.elements[e].Ends(s);
            const size_t spline = FitCurve(run.g, run.reversed ? run.to : run.from, run.reversed ? run.from : run.to, {ends[0], ends[1]},
                                           {std::nullopt, {}, !IsDrawn(e)});
            s.elements[e] = s.elements[spline];
            s.elements.pop_back();
        }
        // the elements that hold a step or a distance for the solver go, the ones after them move up
        void DropSolverElements() {
            std::vector<size_t> moved(s.elements.size()), gone;
            for (size_t e = 0; e < moved.size(); ++e) {
                moved[e] = e - gone.size();
                if (s.elements[e].kind == sketch::ElementKind::Pattern || s.elements[e].kind == sketch::ElementKind::Offset)
                    gone.push_back(e);
            }
            for (auto e = gone.rbegin(); e != gone.rend(); ++e) {
                s.RemoveElement(*e);
                EraseElementData(*e);
            }
            const auto move = [&](std::optional<size_t> &e) {
                if (e)
                    e = moved[*e];
            };
            const auto movePen = [&](sketch::PenState &p) {
                move(p.point);
                move(p.first);
                move(p.last);
                for (size_t &e : p.chain)
                    e = moved[e];
            };
            movePen(pen);
            for (std::optional<sketch::PenState> *held : {&parked, &normalPen})
                if (*held)
                    movePen(**held);
        }
        // what a plain sketch keeps of the elements of a constraint sketch
        void WithoutConstraints() {
            for (size_t e = 0; e < s.elements.size(); ++e)
                if (s.elements[e].kind == sketch::ElementKind::OffsetCurve)
                    FreezeOffsetCurve(e);
            // a curve keeps its shape, without the dimensions it refit from
            for (sketch::SplineData &d : s.splines)
                if (d.fitted)
                    d.fitted->formula.reset();
            DropSolverElements();
            Reindex();
        }
        // a closed curve that crosses itself: its arcs bound other than one region
        std::optional<std::string> Crossing(const std::vector<PrimitiveEdge> &arcs, const std::string &source) const {
            Wire w(axes);
            for (const PrimitiveEdge &a : arcs)
                std::visit([&](const auto &x) { w.Add(x); }, a);
            if (WireList{w}.Region().Faces().size() == 1)
                return std::nullopt;
            return "the offset of " + source + " would cross itself";
        }
        std::optional<std::string> CurveCrossing(const std::string &curve, const std::string &source) const {
            const size_t e = byId.at(curve).index;
            if (!s.elements[e].IsClosed(s))
                return std::nullopt;
            return Crossing(s.elements[e].OffsetCurveArcs(s), source);
        }
        // the closed ordinary spline element e, that a QuickSketch froze as the offset of source
        std::optional<std::string> FrozenCrossing(size_t e, const std::string &source) const {
            std::vector<PrimitiveEdge> arcs;
            for (const planex2d::Spline &half : SourceSpline(s.elements[e], true))
                arcs.insert(arcs.end(), half.Approximation().begin(), half.Approximation().end());
            return Crossing(arcs, source);
        }
        // a spline element as an edge running along the chain (against its own direction: reversed)
        std::vector<planex2d::Spline> SourceSpline(const sketch::Element &e, bool along) const {
            const planex2d::Curve<double, Vertex> f = e.Curve(s);
            std::vector<double> u;
            std::vector<Vertex> nodes;
            for (const auto &[t, v] : e.Nodes(s, f)) {
                u.push_back(t);
                nodes.push_back(v);
            }
            if (along)
                return planex2d::Spline::FromNodes(f, u, nodes);
            const double sum = f.First() + f.Last();
            std::reverse(u.begin(), u.end());
            std::reverse(nodes.begin(), nodes.end());
            for (double &t : u)
                t = sum - t;
            return planex2d::Spline::FromNodes(f.Reversed(), u, nodes);
        }
        // of the offset curve (id) of the spline source at the distance d, between where it ends now
        std::optional<std::string> CurveCusp(const std::string &curve, const std::string &source, double d) const {
            const sketch::OffsetCurveData &c = s.offsetCurves[s.elements[byId.at(curve).index].OffsetCurveIndex()];
            const planex2d::Curve<double, Vertex> f = s.splines[c.spline].Interpolated(s);
            const std::array<double, 2> range = c.Range(s, f);
            return sketch::Cusp(f, c.side, range[0], range[1], d, source);
        }
        // of any spline of the chain over all of it, on the sides the offset takes (towards as SidesOf takes it)
        std::optional<std::string> ChainCusp(const sketch::OffsetChain &source, const EdgeList &edges, double d, const std::vector<int> &towards) const {
            if (std::none_of(source.elements.begin(), source.elements.end(), [&](size_t e) { return s.elements[e].kind == sketch::ElementKind::Spline; }))
                return std::nullopt;
            const double turn = source.closed ? (ChainArea(edges) > 0.0 ? -1.0 : 1.0) : 1.0;
            for (int t : towards)
                for (int side : t == 0 ? std::vector<int>{1, -1} : std::vector<int>{t})
                    for (size_t k = 0; k < source.elements.size(); ++k) {
                        const sketch::Element &e = s.elements[source.elements[k]];
                        if (e.kind != sketch::ElementKind::Spline)
                            continue;
                        const planex2d::Curve<double, Vertex> f = e.Curve(s);
                        const auto [t0, t1] = e.Range(s, f);
                        if (std::optional<std::string> cusp = sketch::Cusp(f, side * turn * (source.forward[k] ? 1.0 : -1.0), t0, t1, d, elementIds[source.elements[k]]))
                            return cusp;
                    }
            return std::nullopt;
        }
        // Trim (trim): the piece of the element nearest near (default the pen; reverse: the next nearest) becomes a
        // construction element, the rest stays; Split: every piece stays, the one nearest near keeps the id (default the
        // first). The cuts are where the element meets the cutters (default every other line, arc and circle), each cut
        // point constrained onto its cutter so the cut follows later changes; pieces of a line stay on its line, pieces
        // of an arc or circle on its circle. The element keeps its id as the first kept piece, the others get new ids
        // and its properties. Constraints on an end that went to another piece follow it there; those that held the
        // element's old ends (a line's length or equal length, Fix, a symmetry of its ends) are removed with a warning.
        // Trim without cutters and with nothing to cut at makes all of it construction
        virtual BasicSketch &Cut(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse,
                                 bool trim, bool tied = true) {
            const std::string call = std::string(trim ? "Trim" : "Split") + "('" + id + "')";
            sketch::Undoable(*this, [&] {
                CutPieces(id, cutterIds, nearPoint, reverse, trim, tied, call);
            });
            return *this;
        }

        // the elements of a wire (a chain, an id_wire) or a group (a profile's id), in order; none for an element's id
        std::vector<std::string> ChainOf(const std::string &id) const {
            if (const auto it = byId.find(id); it != byId.end() && it->second.isElement)
                return {};
            if (const auto w = wires.find(id); w != wires.end())
                return w->second;
            std::vector<std::string> members;
            if (const auto under = idsUnder.find(id); under != idsUnder.end())
                for (const std::string &x : under->second)
                    if (const auto at = byId.find(x); at != byId.end() && at->second.isElement && elementIds[at->second.index] == x &&
                        s.elements[at->second.index].kind != sketch::ElementKind::Point)
                        members.push_back(x);
            return members;
        }
        // Trim of a chain or named wire: the run of it nearest near, between the cuts the cutters make on it nearest
        // that place, made construction across its elements: the element nearest near trimmed when the cutters meet it,
        // else made construction whole and the walk goes on along the chain each way, through the elements the cutters
        // do not meet (construction whole), up to one they meet, trimmed there on the run's side
        BasicSketch &TrimChain(const std::string &wire, const std::vector<std::string> &cutterIds,
                               const Vertex &nearPoint) {
            const std::string call = "Trim('" + wire + "')";
            sketch::Undoable(*this, [&] {
                const std::vector<std::string> ids = ChainOf(wire);
                const size_t n = ids.size();
                if (n == 1) {
                    Cut(ids[0], cutterIds, nearPoint, false, true);
                    return;
                }
                Wire cutters(axes);
                for (const std::string &c : cutterIds)
                    for (const Edge &e : ElementWire(c))
                        cutters.Add(e);
                // the elements the cutters meet other than at their ends
                std::vector<char> met(n, 0);
                std::vector<double> away(n, std::numeric_limits<double>::infinity());
                for (size_t k = 0; k < n; ++k) {
                    const Wire w = ElementWire(ids[k]);
                    for (const Edge &e : w)
                        away[k] = std::min(away[k], Distance(nearPoint, e));
                    const std::array<size_t, 2> ends = s.elements[RefOf(ids[k]).element].Ends(s);
                    for (const Vertex &x : Intersections(w, cutters)) {
                        bool atEnd = false;
                        for (size_t p : ends)
                            atEnd = atEnd || (p != sketch::none && x.IsClose(Coords(p), Tol(NormInf(x))));
                        met[k] = met[k] || !atEnd;
                    }
                }
                const size_t k = (size_t)(std::min_element(away.begin(), away.end()) - away.begin());
                if (met[k]) {
                    Cut(ids[k], cutterIds, nearPoint, false, true);
                    return;
                }
                // closed: the last element ends where the first one starts (two elements share both ends)
                size_t shared = 0;
                for (size_t p : s.elements[RefOf(ids.front()).element].Ends(s))
                    for (size_t q : s.elements[RefOf(ids.back()).element].Ends(s))
                        shared += p != sketch::none && q != sketch::none && Coords(p) == Coords(q);
                const bool closed = shared >= (n == 2 ? 2 : 1);
                std::vector<char> done(n, 0);
                done[k] = 1;
                std::vector<std::string> whole{ids[k]};
                size_t cut = 0;
                for (const int way : {1, -1}) {
                    for (size_t j = k, before = k;;) {
                        if (!closed && ((way > 0 && j + 1 == n) || (way < 0 && j == 0)))
                            break;
                        before = j;
                        j = (j + n + (size_t)(way > 0 ? 1 : n - 1)) % n;
                        if (done[j])
                            break;
                        done[j] = 1;
                        if (!met[j]) {
                            whole.push_back(ids[j]);
                            continue;
                        }
                        // trimmed on the run's side: the piece at its end shared with the element before it
                        const std::array<size_t, 2> ends = s.elements[RefOf(ids[j]).element].Ends(s);
                        const Wire previous = ElementWire(ids[before]);
                        const auto gap = [&](size_t p) {
                            double d = std::numeric_limits<double>::infinity();
                            for (const Edge &e : previous)
                                d = std::min(d, Distance(Coords(p), e));
                            return d;
                        };
                        Cut(ids[j], cutterIds, Coords(gap(ends[0]) <= gap(ends[1]) ? ends[0] : ends[1]), false, true);
                        ++cut;
                        break;
                    }
                }
                if (cut == 0)
                    throw std::invalid_argument(call + ": the cutters do not meet it");
                SetConstruction(whole, true, call);
            });
            return *this;
        }

        // Fillet (Chamfer): round (cut) the corners at the points at, each the end of exactly two drawn elements. The two
        // elements keep their ids, cut back to the touch (cut) points; the pieces cut off stay as construction elements,
        // named as a Trim names them (Line.1.trimmed.1), an element cut off whole becomes construction. The arc (line) is
        // Fillet.k (Chamfer.k), or the id given, one id per point; its properties are those of the two elements merged,
        // the given ones (one for all points or one per point) set over them. A corner that cannot be cut is skipped
        // with a warning, none cut raises and changes nothing. curvature: a curvature-continuous blend between the arc's
        // ends instead of the arc
        virtual BasicSketch &Fillet(const std::vector<Vertex> &at, double r, bool curvature, const std::vector<std::string> &ids,
                                    const std::vector<Properties> &given) {
            if (curvature)
                return CutCorners(at, CurvatureFilletRule{{r}}, ids, given, "Fillet");
            return CutCorners(at, FilletRule{r}, ids, given, "Fillet");
        }
        virtual BasicSketch &Chamfer(const std::vector<Vertex> &at, const ChamferRule &rule, const std::vector<std::string> &ids,
                                     const std::vector<Properties> &given) {
            return CutCorners(at, rule, ids, given, "Chamfer");
        }

        // Connect: a new element from an end of a to an end of b, both unchanged: a line (continuity Coincident), or a
        // blend matching their directions there (Tangent, a cubic) and their curvatures (Curvature, a quintic). a and b
        // name elements (their nearest ends; reverse: the other end of each) or ends ("Line.1.end"). Id Connect.k
        virtual BasicSketch &Connect(const std::string &a, const std::string &b, sketch::Kind continuity, bool reverse, const sketch::Options &opt) {
            const std::string call = "Connect('" + a + "', '" + b + "')";
            Flush();
            Finish();
            sketch::Undoable(*this, [&] {
                const std::array<std::pair<size_t, size_t>, 2> ends = ConnectedEnds(a, b, reverse, call);
                if (Coords(ends[0].second).IsClose(Coords(ends[1].second), Tol(s.Size())))
                    throw std::invalid_argument(call + ": the ends meet already, there is no gap to connect");
                if (continuity == sketch::Kind::Coincident) {
                    AddElement(s.AddLine(ends[0].second, ends[1].second), "Connect", opt);
                    return;
                }
                CheckBlendArms({ends[0].first, ends[1].first}, call);
                AddElement(s.AddBlend(ends[0].second, ends[1].second, {{ends[0].first, ends[1].first}, sketch::none, continuity == sketch::Kind::Curvature}),
                           "Connect", opt);
            });
            return *this;
        }

        // the solved lines, arcs and circles (each as two halves) as planex edges with their properties: the drawn
        // ones, or the construction ones
        Wire Edges(bool constructionOnes = false) {
            Flush();
            const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
            Wire w(axes);
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (!IsDrawn(e) == constructionOnes && !IsHidden(e))
                    AddEdges(e, w, vertexProps);
            return w;
        }
        // the faults of the drawn lines, arcs, circles and splines; gap: none for 1e-3 of their size
        sketch::Faults Diagnose(std::optional<double> gap) {
            Flush();
            std::vector<char> looked(s.elements.size(), 0);
            for (size_t e = 3; e < s.elements.size(); ++e) {
                const sketch::ElementKind kind = s.elements[e].kind;
                looked[e] = IsDrawn(e) && (kind == sketch::ElementKind::Line || kind == sketch::ElementKind::Arc || kind == sketch::ElementKind::Circle ||
                                           kind == sketch::ElementKind::Spline || kind == sketch::ElementKind::OffsetCurve);
            }
            const std::unordered_map<size_t, Properties> noVertexProps;
            return sketch::Diagnosis(s, std::move(looked), gap).Find([&](size_t e) {
                Wire w(axes);
                AddEdges(e, w, noVertexProps);
                return w.Edges();
            });
        }
        const std::string &ElementId(size_t e) const { return elementIds[e]; }
        // the drawn edges as chains in traversal order, ends within rounding joined. unify: cut at crossings and
        // T-junctions, an overlap kept once, pieces on one carrier merged where nothing else meets (as wire + wire); else
        // every element uncut, raising for a chain that crosses, touches or overlaps itself
        WireList Wires(bool unify, const std::string &call) {
            Flush();
            const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
            Wire c(axes);
            std::vector<size_t> source;
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (IsDrawn(e)) {
                    AddEdges(e, c, vertexProps);
                    source.resize(c.Size(), e);
                }
            if (unify) {
                Wire all(axes);
                for (const Wire &w : WiresOf(c.Edges(), axes, std::nullopt))
                    for (const Edge &e : w)
                        all.Add(e);
                all.MergeOnCarriers();
                return all.OrderedChains();
            }
            c.RemoveSmallGaps(c.Edges().JoinTol(std::nullopt));
            WireList out;
            for (const Loop &loop : c.Loops()) {
                Wire w(axes);
                std::vector<size_t> from;
                for (const std::pair<size_t, bool> &step : loop.edges) {
                    w.Add(c[step.first]);
                    from.push_back(source[step.first]);
                }
                if (const std::optional<std::pair<size_t, size_t>> hit = w.SelfContact()) {
                    const std::string &a = elementIds[from[hit->first]], &b = elementIds[from[hit->second]];
                    throw std::invalid_argument(call + ": " + (a == b ? "'" + a + "' crosses or touches itself" : "'" + a + "' and '" + b + "' cross, touch or overlap") +
                                                " in one chain; a wire never crosses itself: unify=True cuts the drawn edges into clean wires");
                }
                out.push_back(OrderChain(w.Edges(), axes, w.Edges().JoinTol(std::nullopt), call));
            }
            return out;
        }
        // the drawn edges as Edges(); a group with a point an odd number of element ends reach is an open wire, which
        // cancels nothing
        Wire Region() {
            Flush();
            std::vector<char> openGroup(s.elements.size(), 0);
            for (const std::vector<size_t> &g : Groups()) {
                std::unordered_map<size_t, size_t> uses;
                for (size_t e : g)
                    for (size_t p : s.elements[e].Ends(s))
                        if (p != sketch::none)
                            ++uses[s.coordsOf[p]];
                const bool open = std::any_of(uses.begin(), uses.end(), [](const auto &u) { return u.second % 2 == 1; });
                for (size_t e : g)
                    openGroup[e] = open;
            }
            const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
            Wire w(axes);
            std::vector<bool> open;
            for (size_t e = 3; e < s.elements.size(); ++e)
                if (IsDrawn(e)) {
                    // a curve crossing itself split there, as Wires(unify=True) does: InitialClean cuts edges against
                    // each other, not one against itself
                    Wire drawn(axes);
                    AddEdges(e, drawn, vertexProps);
                    for (const Edge &x : drawn)
                        AddUncrossed(w, x);
                    open.resize(w.Size(), openGroup[e]);
                }
            return InitialClean(w, open);
        }
        planex2d::Face Face() { return Region().SingleFace("Face()"); }
        planex2d::Shape Shape() { return planex2d::Shape(Region(), nullptr); }

        bool IsElement(const std::string &id) const {
            const auto it = byId.find(id);
            return it != byId.end() && it->second.isElement;
        }
        bool IsConstruction(const std::string &id) const { return !IsDrawn(RefOf(id).element); }
        const std::string &LastId() const { return elementIds.back(); }
        double Size() const { return s.Size(); }
        // the lines, arcs, circles and axes running through p, but except; the axes last, the drawing comes first
        std::vector<std::string> Through(const Vertex &p, const std::string &except) const {
            const double tol = Tol(std::max(s.Size(), NormInf(p)));
            std::vector<std::string> out, axesThrough;
            for (size_t k = 0; k < s.elements.size(); ++k) {
                const sketch::Element &e = s.elements[k];
                const auto at = [&](size_t point) { return Coords(point); };
                bool on = false;
                if (IsHidden(k))
                    continue;
                if (e.kind == sketch::ElementKind::Line) {
                    const Vertex a = at(e.Start()), d = at(e.End()) - a;
                    const double t = Dot(p - a, d) / Dot(d, d);
                    on = std::abs(Cross(d, p - a)) <= tol * Norm(d) && t > -tol && t < 1.0 + tol;
                } else if (e.kind == sketch::ElementKind::Circle || e.kind == sketch::ElementKind::Arc) {
                    on = std::abs(Norm(p - at(e.Center())) - s.values[e.Radius()]) <= tol;
                } else if (e.kind == sketch::ElementKind::Axis) {
                    const double t = s.values[e.AxisAngle()];
                    on = std::abs(std::cos(t) * p.x + std::sin(t) * p.y - s.values[e.AxisDistance()]) <= tol;
                }
                if (on && elementIds[k] != except)
                    (e.kind == sketch::ElementKind::Axis ? axesThrough : out).push_back(elementIds[k]);
            }
            out.insert(out.end(), axesThrough.begin(), axesThrough.end());
            return out;
        }

        BasicSketch(const BasicSketch &) = default;
        BasicSketch(BasicSketch &&) = default;
        BasicSketch &operator=(const BasicSketch &) = default;
        BasicSketch &operator=(BasicSketch &&) = default;
        virtual ~BasicSketch() = default;

    protected:
        // the hooks of the constraint sketch, which adds the constraints there; in the plain sketch they do nothing
        // a relation between elements, a flag or what a drawing call implies (params: positions along a spline)
        virtual void Relate(sketch::Kind, std::vector<sketch::Ref>, std::vector<size_t> = {}) {}
        // a relation the user's flag gives
        virtual void RelateGiven(sketch::Kind, std::vector<sketch::Ref>) {}
        // the chain goes on from element a to element b
        virtual void Chained(size_t, size_t) {}
        // dimension changes not applied yet go in first
        virtual void Flush() {}
        // a call (Atomic) starts; the outermost one ends, may raise; one was undone
        virtual void CallStarts() {}
        virtual void CallEnds() {}
        virtual void CallUndone() {}
        virtual void SplineAdded(size_t) {}
        // element k (id) goes: what refers to it goes first
        virtual void ElementGoes(size_t, const std::string &) {}
        virtual bool IsReferenced(size_t) const { return false; }
        // a cut element a (it was old) is the pieces now
        virtual void PiecesCut(size_t, const sketch::Element &, const std::vector<size_t> &, const std::string &, const std::string &) {}
        // the copies of a mirror tied to their originals (auto_constraint=True)
        virtual void MirrorTied(const sketch::Copies &, const sketch::Ref &) {}
        virtual void MirrorMade(const std::string &, const std::vector<std::string> &, const std::string &, bool, bool) {}
        virtual void ConstraintsCopied(const std::string &, bool) {}
        // an offset follows its source: its element with the distance, its ties, then the solve and the checks
        // a dimension (kind, references, value) with its id; inOffset: in an offset being made, solved with its ties
        virtual void Dimension(sketch::Kind, std::vector<sketch::Ref>, double, const std::string &, bool) {}
        virtual bool TiesOffsets() const { return false; }
        virtual size_t OffsetElement(double, const Options &, bool) { return 0; }
        virtual void TieOffset(sketch::OffsetTie, std::vector<sketch::Ref>, double, const std::string &) {}
        virtual void OffsetMade(const std::string &, const std::vector<std::string> &, const std::vector<std::string> &, const sketch::OffsetChain &,
                                const EdgeList &, double, const std::vector<int> &, const OffsetForms &,
                                const sketch::OffsetSides &,
                                const std::vector<std::pair<std::string, std::string>> &, const std::string &) {}

        Store s;
        AxisSystem axes;
        std::vector<std::string> elementIds;
        std::vector<Properties> props;
        std::vector<sketch::Role> roles;
        std::unordered_map<std::string, sketch::IdTarget> byId;
        std::unordered_map<std::string, size_t> counters;
        // the pattern whose copy is being placed: auto ids go under it (LinearPattern.1.Line.1), with its own counters
        std::string idScope;
        // the wires made or renamed, (id, id before: empty if new), for the undo of a call
        std::vector<std::pair<std::string, std::string>> wireLog;
        // the Atomic calls running and the chains they ended, for their undo
        size_t atomic = 0;
        std::vector<std::vector<size_t>> ended;
        std::map<std::string, std::vector<std::string>> wires;
        // per point (its own index, not merged) the first point element on it
        std::unordered_map<size_t, size_t> pointElementOf;
        // the ids the user gave (elements and constraints)
        std::set<std::string> givenIds;
        // the point elements' ids before point.id = ..., to the ids they have now
        std::map<std::string, std::string> renamed;
        // the groups the calls made, (the call, its id), in the order made; those removed are left out when listed
        std::vector<std::pair<std::string, std::string>> groups;
        // per id, the element ids that lie under it (Line.1.corner under Line.1)
        std::unordered_map<std::string, std::set<std::string>> idsUnder;
        sketch::PenState pen;
        // the construction pen while it is not the pen; while a construction element is drawn, the pen it stands in for
        std::optional<sketch::PenState> parked, normalPen;


        // every length times f: coordinates, radii, the elements' mesh sizes (if asked), the pens
        virtual void ScaleInside(double f, bool scaleMaxh, bool scaleMinh) {
            CheckScale(f);
            Flush();
            for (size_t u = 0; u < s.values.size(); ++u)
                if (!s.isAngle[u])
                    s.values[u] *= f;
            for (Properties &p : props)
                p = ScaledSizes(p, f, scaleMaxh, scaleMinh);
            for (sketch::SplineData &d : s.splines)
                if (d.fitted) {
                    d.fitted->scale *= f;
                    d.fitted->Rescale(f);
                }
            pen.pos = pen.pos * f;
            pen.start = pen.start * f;
            if (parked) {
                parked->pos = parked->pos * f;
                parked->start = parked->start * f;
            }
        }
        // a point element no line or arc ends at and no constraint refers to
        bool Unused(size_t point) const {
            const size_t p = s.elements[point].Point();
            for (size_t k = 0; k < s.elements.size(); ++k) {
                const std::vector<size_t> through = s.elements[k].PointsAndCentre(s);
                if (k != point && std::find(through.begin(), through.end(), p) != through.end())
                    return false;
            }
            return !IsReferenced(point);
        }
        // the origin angle of the copy of circle c under the map f
        template <class F>
        double OriginAngleCopied(const sketch::Element &c, const F &f) const {
            const double a = s.values[c.OriginAngle()];
            const Vertex centre = Coords(c.Center()), w = f(centre + Vertex{std::cos(a), std::sin(a)}) - f(centre);
            return std::atan2(w.y, w.x);
        }

        void CutPieces(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse,
                       bool trim, bool tied, const std::string &call) {
            Finish();
            const sketch::Ref ar = RefOf(id);
            const size_t a = ar.element;
            if (IsSpan(a))
                throw std::invalid_argument(call + ": it is cut into " + Joined(wires.at(elementIds[a])) + " already; cut one of them");
            const sketch::Element el = s.elements[a];
            const bool line = el.kind == sketch::ElementKind::Line, circle = el.kind == sketch::ElementKind::Circle;
            if (ar.part != sketch::Part::Whole || a < 3 || !(line || circle || el.kind == sketch::ElementKind::Arc || el.kind == sketch::ElementKind::Spline))
                throw std::invalid_argument(call + ": not a line, arc, circle or spline");
            std::vector<size_t> cutters;
            if (cutterIds.empty()) {
                for (size_t e = 3; e < s.elements.size(); ++e)
                    if (e != a && !IsHidden(e) && (s.elements[e].kind == sketch::ElementKind::Line || s.elements[e].kind == sketch::ElementKind::Arc ||
                                   s.elements[e].kind == sketch::ElementKind::Circle || s.elements[e].kind == sketch::ElementKind::Spline ||
                                   s.elements[e].kind == sketch::ElementKind::OffsetCurve))
                        cutters.push_back(e);
            } else
                for (const std::string &c : cutterIds) {
                    const sketch::Ref r = RefOf(c);
                    const sketch::ElementKind k = s.elements[r.element].kind;
                    if (r.part != sketch::Part::Whole || k == sketch::ElementKind::Point || k == sketch::ElementKind::Pattern || k == sketch::ElementKind::Offset ||
                        r.element == a)
                        throw std::invalid_argument(call + ": '" + c + "' cuts nothing");
                    cutters.push_back(r.element);
                }
            sketch::ElementCutter<BasicSketch>(*this, a, id, call).Cut(cutters, cutterIds.empty(), nearPoint.value_or(pen.pos), reverse, trim, tied);
        }


        void EraseElementData(size_t k) {
            elementIds.erase(elementIds.begin() + (std::ptrdiff_t)k);
            props.erase(props.begin() + (std::ptrdiff_t)k);
            roles.erase(roles.begin() + (std::ptrdiff_t)k);
        }
        bool IsSpan(size_t e) const { return roles[e] == sketch::Role::Span; }
        // a span or a helper: not drawn, listed or exported
        bool IsHidden(size_t e) const { return roles[e] == sketch::Role::Span || roles[e] == sketch::Role::Helper; }
        bool IsDrawn(size_t e) const { return roles[e] == sketch::Role::Drawn; }

        // the drawn lines, arcs and splines with an end at p: the element and whether it is its end
        std::vector<std::pair<size_t, bool>> EndsAt(const Vertex &p) const {
            std::vector<std::pair<size_t, bool>> out;
            for (size_t e = 3; e < s.elements.size(); ++e) {
                const sketch::ElementKind kind = s.elements[e].kind;
                if (!IsDrawn(e) || (kind != sketch::ElementKind::Line && kind != sketch::ElementKind::Arc && kind != sketch::ElementKind::Spline))
                    continue;
                const std::array<size_t, 2> ends = s.elements[e].Ends(s);
                for (size_t k = 0; k < 2; ++k)
                    if (Coords(ends[k]).IsClose(p, Tol(s.Size())))
                        out.push_back({e, k == 1});
            }
            return out;
        }
        // the end of element e (its end if atEnd) as the arm of a corner
        Arm ArmAt(const std::pair<size_t, bool> &end) const {
            Wire w(axes);
            AddEdges(end.first, w, VertexProps());
            return {w[end.second ? w.Size() - 1 : 0], end.second};
        }
        // set while an arm is cut, for the constraint sketch: the constraints of the element cut stay on its far end and
        // the corner
        bool cornering = false;
        // element e cut back to x, which lies on it; its end towards the corner (the end if atEnd) becomes a construction
        // piece. An element x is the far end of is cut off whole
        sketch::ArmCut CutBack(size_t e, const Arm &arm, const Vertex &x, const std::string &call) {
            const sketch::Element el = s.elements[e];
            const sketch::Part corner = arm.atEnd ? sketch::Part::End : sketch::Part::Start;
            if (arm.IsUsedUpBy(x)) {
                roles[e] = sketch::Role::Construction;
                return {el.Ends(s)[arm.atEnd ? 0 : 1], {e, arm.atEnd ? sketch::Part::Start : sketch::Part::End}, {e, corner}};
            }
            const size_t removed = arm.atEnd ? 1 : 0;
            sketch::CutMade made;
            cornering = true;
            const std::string id = elementIds[e];
            sketch::ElementCutter<BasicSketch> cutter(*this, e, id, call);
            if (el.kind == sketch::ElementKind::Spline) {
                const sketch::SplineSpan span = el.Span(s);
                const double t = span.Along(x);
                made = cutter.SplineCutAt(span, {sketch::CutPoint{t, span.At(t, 0), sketch::none}}, removed, true);
            } else
                made = cutter.CutAt({sketch::CutPoint{el.Param(s, x), x, sketch::none}}, removed, true, true);
            cornering = false;
            return {s.elements[made.cuts[0]].Point(), {made.cuts[0]}, {made.pieces[removed], corner}};
        }
        virtual void CornerMade(const FilletRule &, const sketch::CornerParts &) {}
        virtual void CornerMade(const ChamferRule &, const sketch::CornerParts &) {}
        // the blend takes the arc's place: its ends, id and properties
        virtual void CornerMade(const CurvatureFilletRule &, const sketch::CornerParts &c) {
            CheckBlendArms({c.arms[0], c.arms[1]}, "Fillet");
            const sketch::Element arc = s.elements[c.made];
            s.AddBlend(arc.Start(), arc.End(), {{c.arms[0], c.arms[1]}});
            s.elements[c.made] = s.elements.back();
            s.elements.pop_back();
            RemoveOne(elementIds[pointElementOf.at(arc.Center())]);
        }
        // the blends joining element a, now cut into pieces, join the piece that ends where they do
        void BlendsFollow(size_t a, const std::vector<size_t> &pieces) {
            for (sketch::SplineData &d : s.splines)
                if (d.blend)
                    for (size_t i = 0; i < 2; ++i)
                        if (d.blend->arms[i] == a)
                            for (size_t p : pieces)
                                for (size_t end : s.elements[p].Ends(s))
                                    if (s.coordsOf[end] == s.coordsOf[d.points[i]])
                                        d.blend->arms[i] = p;
        }
        bool IsBlend(size_t e) const { return s.elements[e].kind == sketch::ElementKind::Spline && s.splines[s.elements[e].SplineIndex()].blend; }
        // the blend (its element) computed from element e, if any
        std::optional<size_t> BlendOn(size_t e) const {
            for (size_t k = 3; k < s.elements.size(); ++k)
                if (s.elements[k].kind == sketch::ElementKind::Spline)
                    if (const std::optional<sketch::BlendShape> &b = s.splines[s.elements[k].SplineIndex()].blend; b && (b->arms[0] == e || b->arms[1] == e))
                        return k;
            return std::nullopt;
        }
        // the (element, end point) Connect joins for a and for b: the nearest pair of ends; reverse: the other end of each
        // element named whole
        std::array<std::pair<size_t, size_t>, 2> ConnectedEnds(const std::string &a, const std::string &b, bool reverse, const std::string &call) const {
            const auto ends = [&](const std::string &id) {
                const sketch::Ref r = RefOf(id);
                const sketch::Element &e = s.elements[r.element];
                const bool curve = e.kind == sketch::ElementKind::Line || e.kind == sketch::ElementKind::Arc || e.kind == sketch::ElementKind::Spline ||
                                   e.kind == sketch::ElementKind::OffsetCurve;
                if (r.element < 3 || !curve || IsHidden(r.element) || e.IsClosed(s))
                    throw std::invalid_argument(call + ": '" + id + "' is no line, arc or open spline");
                const std::array<size_t, 2> p = e.Ends(s);
                if (r.part == sketch::Part::Whole)
                    return std::pair{r.element, std::vector<size_t>{p[0], p[1]}};
                if (r.part == sketch::Part::Start || r.part == sketch::Part::End)
                    return std::pair{r.element, std::vector<size_t>{p[r.part == sketch::Part::Start ? 0 : 1]}};
                throw std::invalid_argument(call + ": '" + id + "' is no end; give an element or one of its ends, '" + elementIds[r.element] + ".end'");
            };
            const auto [ea, pa] = ends(a);
            const auto [eb, pb] = ends(b);
            if (ea == eb)
                throw std::invalid_argument(call + ": both ends are on one element");
            if (reverse && pa.size() == 1 && pb.size() == 1)
                throw std::invalid_argument(call + ": reverse=True takes the other end of an element given whole; the ends named are the ones joined");
            size_t i = 0, j = 0;
            double nearest = std::numeric_limits<double>::infinity();
            for (size_t x = 0; x < pa.size(); ++x)
                for (size_t y = 0; y < pb.size(); ++y)
                    if (const double d = Norm(Coords(pa[x]) - Coords(pb[y])); d < nearest) {
                        nearest = d;
                        i = x;
                        j = y;
                    }
            if (reverse) {
                i = pa.size() - 1 - i;
                j = pb.size() - 1 - j;
            }
            return {{{ea, pa[i]}, {eb, pb[j]}}};
        }
        // a blend joins lines, arcs and splines, not offset curves
        void CheckBlendArms(const std::array<size_t, 2> &arms, const std::string &call) const {
            for (size_t arm : arms)
                if (s.elements[arm].kind == sketch::ElementKind::OffsetCurve)
                    throw std::invalid_argument(call + ": '" + elementIds[arm] + "' is an offset curve; px.Curvature joins lines, arcs and splines");
        }

        // the new element between two elements of a corner joins the wires that hold both, between them
        void JoinWires(size_t a, size_t b, size_t made) {
            for (auto &[name, ids] : wires) {
                const auto ia = std::find(ids.begin(), ids.end(), elementIds[a]), ib = std::find(ids.begin(), ids.end(), elementIds[b]);
                if (ia == ids.end() || ib == ids.end())
                    continue;
                ids.insert(ia + 1 == ib ? ib : ib + 1 == ia ? ia : ids.end(), elementIds[made]);
            }
        }
        // why the corner at p cannot be cut, empty after cutting it
        template <class Rule>
        std::string CutCorner(const Vertex &p, const Rule &rule, const std::optional<std::string> &id, const Properties &given,
                              const std::string &call) {
            const std::string where = "the corner at " + PointText(p);
            std::vector<std::pair<size_t, bool>> ends = EndsAt(p);
            if (ends.empty())
                return OffVertex(p, {}, "");
            if (std::string why = CornerEndsProblem(where, ends.size()); !why.empty())
                return why;
            if (ends[0].first == ends[1].first)
                return where + " is where one edge closes on itself";
            if (rule.reverse)
                std::swap(ends[0], ends[1]);
            const Arm armOne = ArmAt(ends[0]), armTwo = ArmAt(ends[1]);
            CornerCut cut;
            if (std::string why = CornerCutOf(where, armOne, armTwo, rule, cut); !why.empty())
                return why;
            const Properties merged = Overridden(MergedWithoutPartition(props[ends[0].first], props[ends[1].first]), given);
            const sketch::ArmCut from = CutBack(ends[0].first, armOne, cut.first, call), to = CutBack(ends[1].first, armTwo, cut.second, call);
            const Options opt{id, merged, false};
            const size_t made = cut.center ? AddElement(s.AddArc(s.NewPoint(*cut.center), rule.Size(), from.point, to.point, cut.IsCounterclockwise()), Rule::kind, opt)
                                           : AddElement(s.AddLine(from.point, to.point), Rule::kind, opt);
            JoinWires(ends[0].first, ends[1].first, made);
            for (const auto &end : ends)
                if (!IsDrawn(end.first))
                    for (auto &[name, ids] : wires)
                        std::erase(ids, elementIds[end.first]);
            CornerMade(rule, {made, {ends[0].first, ends[1].first}, {from, to}});
            return {};
        }
        template <class Rule>
        BasicSketch &CutCorners(const std::vector<Vertex> &at, const Rule &rule, const std::vector<std::string> &ids,
                                const std::vector<Properties> &given, const std::string &call) {
            rule.Check(call);
            if (!ids.empty() && ids.size() != at.size())
                throw std::invalid_argument(call + ": " + std::to_string(ids.size()) + " ids for " + std::to_string(at.size()) +
                                            " corners; give one id per corner (a str for one corner, a list for several)");
            if (given.size() > 1 && given.size() != at.size())
                throw std::invalid_argument(call + ": " + std::to_string(given.size()) + " values of a property for " +
                                            std::to_string(at.size()) + " corners; give one value for all or one per corner");
            Flush();
            Finish();
            sketch::Undoable(*this, [&] {
                std::vector<std::string> failed;
                size_t done = 0;
                for (size_t k = 0; k < at.size(); ++k) {
                    if (std::any_of(at.begin(), at.begin() + (std::ptrdiff_t)k, [&](const Vertex &q) { return q.IsClose(at[k], Tol(s.Size())); }))
                        continue;
                    const std::optional<std::string> id = ids.empty() ? std::nullopt : std::optional<std::string>(ids[k]);
                    const Properties props = given.empty() ? nullptr : given[given.size() == 1 ? 0 : k];
                    if (std::string why = CutCorner(at[k], rule, id, props, call); why.empty())
                        ++done;
                    else
                        failed.push_back(std::move(why));
                }
                ReportCorners(call, done, failed);
            });
            return *this;
        }

        std::vector<size_t> Originals(const std::vector<std::string> &ids, const std::string &call) const {
            std::vector<size_t> out;
            for (const std::string &id : Expanded(ids)) {
                const sketch::Ref r = RefOf(id);
                const sketch::ElementKind k = s.elements[r.element].kind;
                if (r.part != sketch::Part::Whole || r.element < 3 || k == sketch::ElementKind::Axis || k == sketch::ElementKind::Pattern ||
                    k == sketch::ElementKind::Offset)
                    throw std::invalid_argument(call + ": '" + id + "' is no point, line, arc or circle of the drawing");
                if (k == sketch::ElementKind::OffsetCurve)
                    throw std::invalid_argument(call + ": '" + id + "' is an offset curve, it follows its source; copy its source and the "
                                                                    "offset's distance instead (offset the copy)");
                if (std::find(out.begin(), out.end(), r.element) == out.end())
                    out.push_back(r.element);
            }
            // a curvature fillet's hidden arc goes with its blend
            for (size_t k = 0, n = out.size(); k < n; ++k)
                if (IsBlend(out[k]))
                    if (const size_t h = s.splines[s.elements[out[k]].SplineIndex()].blend->helper;
                        h != sketch::none && std::find(out.begin(), out.end(), h) == out.end())
                        out.push_back(h);
            return out;
        }
        // copies of the elements with their properties, every point moved by f (each shared point copied once, so chains
        // stay connected), named after their sources under prefix (Mirrored.1.Line.1, LinearPattern.1.3.hole); flip: f
        // mirrors, arcs turn the other way
        template <class F>
        sketch::Copies CopyElements(const std::vector<size_t> &originals, F f, bool flip, const std::string &prefix) {
            std::unordered_map<size_t, size_t> pointElement;
            for (size_t e = 0; e < s.elements.size(); ++e)
                if (s.elements[e].kind == sketch::ElementKind::Point)
                    pointElement[s.elements[e].Point()] = e;
            sketch::Scoped scope{idScope, std::exchange(idScope, prefix)};
            const auto copyId = [&](size_t e) { return std::optional(prefix + "." + elementIds[e]); };
            sketch::Copies made;
            const auto copyPoint = [&](size_t point) {
                const size_t pe = pointElement.at(point);
                for (const auto &[o, c] : made.points)
                    if (o == pe)
                        return s.elements[c].Point();
                AddElement(s.AddPoint(s.NewPoint(f(Coords(point)))), "Point", {copyId(pe), props[pe], !IsDrawn(pe)});
                made.points.push_back({pe, elementIds.size() - 1});
                return s.elements.back().Point();
            };
            std::set<size_t> centres;
            std::unordered_map<size_t, size_t> copiedCentres;
            for (size_t e : originals)
                if (s.elements[e].kind == sketch::ElementKind::Arc || s.elements[e].kind == sketch::ElementKind::Circle)
                    centres.insert(s.elements[e].Center());
            for (size_t e : originals) {
                const sketch::Element el = s.elements[e];
                const Options opt{copyId(e), props[e], !IsDrawn(e)};
                if (el.kind == sketch::ElementKind::Point) {
                    if (!centres.contains(el.Point()))
                        copyPoint(el.Point());
                    continue;
                }
                if (el.kind == sketch::ElementKind::Spline) {
                    CopySpline(e, opt, f, flip, copyPoint, pointElement, made);
                    continue;
                }
                if (el.kind == sketch::ElementKind::Line) {
                    const size_t a = copyPoint(el.Start()), b = copyPoint(el.End());
                    AddElement(s.AddLine(a, b), "Line", opt);
                } else
                    CopyRound(e, opt, f, flip, copyPoint, pointElement, prefix, copiedCentres);
                made.elements.push_back({e, elementIds.size() - 1});
            }
            BlendCopiesJoinCopies(made, prefix);
            // a copied cut point lies on its copied spline at its copied position
            for (const sketch::Copies::Position &x : made.positions) {
                size_t point = sketch::none;
                for (const sketch::SplinePiece &piece : s.pieces)
                    for (const auto &[u, p] : {std::pair{piece.from, piece.fromPoint}, std::pair{piece.to, piece.toPoint}})
                        if (u == x.copy)
                            point = p;
                const size_t at = std::find_if(s.elements.begin(), s.elements.end(), [&](const sketch::Element &k) {
                                      return k.kind == sketch::ElementKind::Point && k.Point() == point;
                                  }) - s.elements.begin();
                Relate(sketch::Kind::Coincident, {{at}, {x.copyElement}}, {x.copy});
            }
            std::erase_if(givenIds, [&](const std::string &g) { return g.starts_with(prefix + "."); });
            return made;
        }
        // a copied blend joins the copies of the elements it joins; the copy of its helper arc is hidden as the arc is
        void BlendCopiesJoinCopies(const sketch::Copies &made, const std::string &prefix) {
            const auto copyOf = [&](size_t e) -> std::optional<size_t> {
                for (const auto &[o, c] : made.elements)
                    if (o == e)
                        return c;
                return std::nullopt;
            };
            for (const auto &[original, copy] : made.elements) {
                if (!IsBlend(copy))
                    continue;
                sketch::BlendShape &b = *s.splines[s.elements[copy].SplineIndex()].blend;
                for (size_t &arm : b.arms) {
                    const std::optional<size_t> c = copyOf(arm);
                    if (!c)
                        throw std::invalid_argument(prefix + ": '" + elementIds[original] + "' is computed from '" + elementIds[arm] +
                                                    "'; copy that with it");
                    arm = *c;
                }
                if (b.helper == sketch::none)
                    continue;
                b.helper = *copyOf(b.helper);
                roles[b.helper] = sketch::Role::Helper;
                roles[pointElementOf.at(s.elements[b.helper].Center())] = sketch::Role::Helper;
            }
        }
        // the copy of spline element e: pieces of one spline share one copy of it, the positions they end at copied once
        template <class F, class CopyPoint>
        void CopySpline(size_t e, const Options &opt, F f, bool flip, CopyPoint copyPoint, const std::unordered_map<size_t, size_t> &pointElement,
                        sketch::Copies &made) {
            const sketch::Element el = s.elements[e];
            const auto copied = std::find_if(made.splines.begin(), made.splines.end(),
                                             [&](const auto &x) { return s.elements[x.first].SplineIndex() == el.SplineIndex(); });
            const bool fresh = copied == made.splines.end();
            size_t copy = fresh ? 0 : copied->second;
            const sketch::SplinePiece *piece = el.Piece(s);
            if (fresh && el.IsFitted(s))
                copy = CopyFitted(e, opt, f, flip, copyPoint, pointElement, made);
            else if (fresh && IsBlend(e)) {
                // joining the originals' elements until CopyElements gives it their copies
                const sketch::SplineData d = s.splines[el.SplineIndex()];
                AddElement(s.AddBlend(copyPoint(d.points[0]), copyPoint(d.points[1]), *d.blend), "Spline", opt);
                copy = s.elements.size() - 1;
                made.splines.push_back({e, copy});
            } else if (fresh) {
                const sketch::SplineData d = s.splines[el.SplineIndex()];
                std::vector<size_t> points;
                for (size_t p : d.points)
                    points.push_back(copyPoint(p));
                std::vector<double> angles;
                for (size_t t = 0; t < d.tangent.size(); ++t) {
                    const Vertex p = Coords(d.points[d.tangentAt[t]]), u{std::cos(s.values[d.tangent[t]]), std::sin(s.values[d.tangent[t]])};
                    const Vertex w = f(p + u) - f(p);
                    angles.push_back(std::atan2(w.y, w.x));
                }
                AddElement(s.AddSpline(points, d.tangentAt, angles, d.closed, d.style), "Spline", opt);
                copy = s.elements.size() - 1;
                made.splines.push_back({e, copy});
            }
            size_t element = copy;
            if (piece) {
                const auto position = [&](size_t u, size_t point) {
                    if (u == sketch::none)
                        return std::pair{u, point};
                    made.placed.insert(pointElement.at(point));
                    const size_t at = copyPoint(point);
                    for (const sketch::Copies::Position &x : made.positions)
                        if (x.original == u)
                            return std::pair{x.copy, at};
                    made.positions.push_back({u, s.NewValue(s.values[u]), e, copy});
                    return std::pair{made.positions.back().copy, at};
                };
                const auto [from, fromPoint] = position(piece->from, piece->fromPoint);
                const auto [to, toPoint] = position(piece->to, piece->toPoint);
                const sketch::SplinePiece range{from, to, fromPoint, toPoint};
                if (fresh) {
                    s.pieces.push_back(range);
                    s.elements[copy].SetPieceIndex(s.pieces.size());
                } else {
                    AddElement(s.AddSplinePiece(s.elements[copy].SplineIndex(), range), "Spline", opt);
                    element = s.elements.size() - 1;
                }
            }
            made.elements.push_back({e, element});
        }
        // the copy of fitted spline element e: its source's shape (turned over by a mirror, flip), its frame's place and
        // turn mapped by f; its ends placed by its frame, so a copy ties neither of them
        template <class F, class CopyPoint>
        size_t CopyFitted(size_t e, const Options &opt, F f, bool flip, CopyPoint copyPoint, const std::unordered_map<size_t, size_t> &pointElement,
                          sketch::Copies &made) {
            const sketch::SplineData d = s.splines[s.elements[e].SplineIndex()];
            const sketch::FittedShape &source = *d.fitted;
            std::vector<size_t> ends;
            for (size_t p : d.points) {
                made.placed.insert(pointElement.at(p));
                ends.push_back(copyPoint(p));
            }
            const Vertex o = Coords(source.origin), w = f(o + Vertex{std::cos(s.values[source.angle]), std::sin(s.values[source.angle])}) - f(o);
            sketch::FittedShape copy{source.shape};
            copy.mirrored = source.mirrored != flip;
            copy.scale = source.scale;
            copy.copyOf = s.elements[e].SplineIndex();
            AddElement(s.AddFittedSpline(ends.front(), ends.back(), std::move(copy), f(o), std::atan2(w.y, w.x)), "Curve", opt);
            made.splines.push_back({e, s.elements.size() - 1});
            return s.elements.size() - 1;
        }
        // the copy of arc or circle e, its new centre named after the original's; centres: the copy of each centre point
        // copied so far (arcs and circles that share a centre share its copy)
        template <class F, class CopyPoint>
        void CopyRound(size_t e, const Options &opt, F f, bool flip, CopyPoint copyPoint, const std::unordered_map<size_t, size_t> &pointElement,
                       const std::string &prefix, std::unordered_map<size_t, size_t> &centres) {
            const sketch::Element el = s.elements[e];
            const size_t origin = el.kind == sketch::ElementKind::Circle && el.OriginPoint() != sketch::none ? copyPoint(el.OriginPoint()) : sketch::none;
            const auto shared = centres.find(el.Center());
            const size_t centre = shared != centres.end() ? shared->second : s.NewPoint(f(Coords(el.Center())));
            const size_t before = elementIds.size();
            const size_t round = el.kind == sketch::ElementKind::Arc
                                     ? AddElement(s.AddArc(centre, s.values[el.Radius()], copyPoint(el.Start()), copyPoint(el.End()), flip ? !el.ccw : el.ccw), "Arc", opt)
                                     : AddElement(s.AddCircle(centre, s.values[el.Radius()], OriginAngleCopied(el, f), flip ? !el.ccw : el.ccw, origin), "Circle", opt);
            if (shared != centres.end())
                return;
            centres[el.Center()] = centre;
            if (round > before && pointElement.contains(el.Center()))
                Rename(round - 1, prefix + "." + elementIds[pointElement.at(el.Center())]);
        }
        // the vertex properties of the point elements by the unknown their place starts at, the first one's where several
        // share it; the other points at the place of a point with properties (an edge's end there) get them too
        std::unordered_map<size_t, Properties> VertexProps() const {
            std::unordered_map<size_t, Properties> out;
            std::vector<std::pair<Vertex, Properties>> given;
            for (size_t k = 3; k < s.elements.size(); ++k)
                if (s.elements[k].kind == sketch::ElementKind::Point && props[k]) {
                    out.emplace(s.coordsOf[s.elements[k].Point()], props[k]);
                    given.push_back({Coords(s.elements[k].Point()), props[k]});
                }
            if (given.empty())
                return out;
            const double tol = Tol(s.Size());
            for (size_t k = 3; k < s.elements.size(); ++k) {
                if (s.elements[k].kind != sketch::ElementKind::Point || out.contains(s.coordsOf[s.elements[k].Point()]))
                    continue;
                const Vertex p = Coords(s.elements[k].Point());
                for (const auto &[q, give] : given)
                    if (p.IsClose(q, tol)) {
                        out.emplace(s.coordsOf[s.elements[k].Point()], give);
                        break;
                    }
            }
            return out;
        }
        // a spline element approximated from the whole spline's curve with its cut points as nodes, so the approximation
        // runs through them and neighbouring pieces share their cut exactly; with the whole's tolerance, the spans between
        // the whole's nodes are approximated as in the whole. A closed whole spline is one closed edge, a piece over its seam
        // one edge on the curve taken on past its end
        std::vector<planex2d::Spline> SplinePieces(const sketch::Element &el) const {
            const planex2d::Curve<double, Vertex> f = el.Curve(s);
            const std::vector<Vertex> q = el.SplinePoints(s);
            std::vector<Vertex> nodes;
            BBox all;
            for (size_t k = 0; k < f.params.size(); ++k) {
                nodes.push_back(q[k % q.size()]);
                all.Grow(nodes.back());
            }
            // a spline that closes only up to rounding closes exactly
            if (nodes.front() != nodes.back() && nodes.front().IsClose(nodes.back(), Tol(all.NormInf())))
                nodes.back() = nodes.front();
            const auto [from, to] = el.Range(s, f);
            const double begin = f.params.front(), end = f.params.back(), same = 1e-9 * end;
            const bool cutStart = from > begin + same, cutEnd = to < end - same;
            if (!cutStart && to <= end + same && !cutEnd)
                return planex2d::Spline::FromNodes(f, f.params, nodes);
            const std::array<Vertex, 2> ends = el.EndCoords(s);
            const double period = end - begin;
            // the points past the end once more, a period on (the first is the last)
            const auto part = [&](const planex2d::Curve<double, Vertex> &curve, double a, double b, const Vertex &pa, const Vertex &pb) {
                std::vector<double> u{a};
                std::vector<Vertex> p{pa};
                for (const double shift : {0.0, period})
                    for (size_t k = shift > 0.0 ? 1 : 0; k < f.params.size(); ++k)
                        if (const double t = f.params[k] + shift; t > a + same && t < b - same) {
                            u.push_back(t);
                            p.push_back(nodes[k]);
                        }
                u.push_back(b);
                p.push_back(pb);
                return planex2d::Spline::FromNodes(curve, u, p, config::state->spline_tol * Norm(all.Size())).front();
            };
            if (to <= end + same)
                return {part(f, cutStart ? from : begin, cutEnd ? to : end, cutStart ? ends[0] : nodes.front(), cutEnd ? ends[1] : nodes.back())};
            return {part(f.Segment(from, end).Joined(f.Segment(begin, to - period).Shifted(period)), from, to, ends[0], ends[1])};
        }
        void AddEdges(size_t e, Wire &w, const std::unordered_map<size_t, Properties> &vertexProps) const {
            const sketch::Element &el = s.elements[e];
            const auto at = [&](size_t point) -> Properties {
                const auto it = vertexProps.find(s.coordsOf[point]);
                return it == vertexProps.end() ? nullptr : it->second;
            };
            const std::array<size_t, 2> ends = el.Ends(s);
            const Ends vertices = ends[0] != sketch::none ? MakeEnds(at(ends[0]), at(ends[1])) : nullptr;
            const auto add = [&](auto edge) {
                edge.props = props[e];
                edge.ends = vertices;
                w.Add(std::move(edge));
            };
            if (el.kind == sketch::ElementKind::Line)
                add(planex2d::Line(Coords(el.Start()), Coords(el.End())));
            else if (el.kind == sketch::ElementKind::Arc)
                add(planex2d::Arc(Coords(el.Start()), Coords(el.End()), Coords(el.Center()), el.ccw));
            else if (el.kind == sketch::ElementKind::Circle) {
                add(planex2d::Circle(Coords(el.Center()), el.Origin(s), el.ccw));
            } else if (el.kind == sketch::ElementKind::Spline) {
                std::vector<planex2d::Spline> pieces = SplinePieces(el);
                // its vertex properties at its own ends only
                for (size_t k = 0; k < pieces.size(); ++k) {
                    pieces[k].props = props[e];
                    if (vertices)
                        pieces[k].ends = MakeEnds(k == 0 ? vertices->start : nullptr, k + 1 == pieces.size() ? vertices->end : nullptr);
                    w.Add(std::move(pieces[k]));
                }
            } else if (el.kind == sketch::ElementKind::OffsetCurve) {
                EdgeList parts = OffsetCurveEdges(e);
                for (size_t k = 0; k < parts.size(); ++k)
                    std::visit([&](auto &x) {
                        x.props = props[e];
                        if (vertices)
                            x.ends = MakeEnds(k == 0 ? vertices->start : nullptr, k + 1 == parts.size() ? vertices->end : nullptr);
                        w.Add(std::move(x));
                    }, parts[k]);
            }
        }
        // an offset curve's edges: one spline, its arcs within spline_tol of the exact curve and its B-spline fitted to them
        // (as the spline fit at export); if no B-spline fits them or no spline fit is loaded, the
        // arcs and lines themselves, with a warning
        EdgeList OffsetCurveEdges(size_t e) const {
            double atol = 0.0;
            const std::vector<PrimitiveEdge> arcs = s.elements[e].OffsetCurveArcs(s, &atol);
            const auto asArcs = [&](const char *why) {
                Warning() << elementIds[e] << ": " << why << ", it goes out as its " << arcs.size() << " arcs and lines";
                EdgeList out;
                for (const PrimitiveEdge &a : arcs)
                    out.push_back(std::visit([](const auto &x) -> Edge { return x; }, a));
                return out;
            };
            if (!config::state->spline_fitter)
                return asArcs("no spline fit without planex's OCC integration");
            // a closed curve as one closed spline
            std::optional<planex2d::Spline> fitted;
            for (size_t n = 2; n <= 64 && !fitted; n *= 2)
                fitted = planex2d::Spline::Fitted(arcs, planex2d::FitPoints(arcs, atol, n), atol, arcs.front().StartTangent(), arcs.back().EndTangent());
            if (!fitted)
                return asArcs("no B-spline fits it within spline_tol");
            return {std::move(*fitted)};
        }
        std::vector<std::vector<size_t>> Groups() const { return GroupsOf(false); }
        // the lines, arcs and circles (drawn ones, or all) by the groups they form at shared points, in the order drawn
        std::vector<std::vector<size_t>> GroupsOf(bool all) const {
            const size_t n = s.elements.size();
            DisjointSets groups(n);
            std::unordered_map<size_t, size_t> at;
            std::vector<size_t> drawn;
            for (size_t e = 3; e < n; ++e) {
                const sketch::Element &el = s.elements[e];
                if ((!IsDrawn(e) && !all) || IsHidden(e) || el.kind == sketch::ElementKind::Point || el.kind == sketch::ElementKind::Axis)
                    continue;
                drawn.push_back(e);
                if (el.kind == sketch::ElementKind::Circle)
                    continue;
                for (size_t p : el.Ends(s)) {
                    const auto [it, fresh] = at.emplace(s.coordsOf[p], e);
                    if (!fresh)
                        groups.Join(e, it->second);
                }
            }
            std::vector<std::vector<size_t>> out;
            std::unordered_map<size_t, size_t> index;
            for (size_t e : drawn) {
                const auto [it, fresh] = index.emplace(groups.Find(e), out.size());
                if (fresh)
                    out.emplace_back();
                out[it->second].push_back(e);
            }
            return out;
        }
        virtual void Reindex() {
            byId.clear();
            idsUnder.clear();
            pointElementOf.clear();
            for (size_t e = 0; e < elementIds.size(); ++e) {
                ListUnder(elementIds[e], true);
                byId[elementIds[e]] = {true, e};
                if (s.elements[e].kind == sketch::ElementKind::Point)
                    pointElementOf.emplace(s.elements[e].Point(), e);
            }
            for (const auto &[old, id] : renamed)
                if (const auto it = byId.find(id); it != byId.end()) {
                    const sketch::IdTarget at = it->second;
                    byId[old] = at;
                }
        }
        // an id not yet used, nor read as a point of an element; a user-given one checked by CheckUserId first. After a
        // wire's id a place word is a name (RectangleC.1.center): wires and elements share the ids, so it never names
        // an element's place
        void CheckId(const std::string &id) const {
            if (id.empty())
                throw std::invalid_argument("an id must not be empty");
            if (byId.contains(id) || wires.contains(id))
                throw std::invalid_argument("id '" + id + "' is taken");
            for (const std::string part : {".start", ".end", ".center", ".mid"})
                if (id.ends_with(part) && !wires.contains(id.substr(0, id.size() - part.size())))
                    throw std::invalid_argument("id '" + id + "' ends like a point of an element (" + part + ")");
            if (const size_t dot = id.rfind('.'); dot != std::string::npos && SplinePart(id.substr(dot + 1)))
                throw std::invalid_argument("id '" + id + "' ends like a point of a spline (.p0, .t0)");
        }
        std::string NewId(const std::string &prefix, const std::optional<std::string> &given) {
            if (given) {
                CheckId(*given);
                givenIds.insert(*given);
                return *given;
            }
            return FreshId(idScope.empty() ? prefix : idScope + "." + prefix);
        }
        // the element made last is e: with its id, properties and construction flag; an arc's or circle's centre gets a
        // point element (Center.k) if it has none, made before it, so the arc stays the last element made. Returns where e
        // is then
        size_t AddElement(size_t e, const std::string &prefix, const Options &opt) {
            const std::string id = NewId(prefix, opt.id);
            if (const sketch::ElementKind k = s.elements[e].kind;
                (k == sketch::ElementKind::Arc || k == sketch::ElementKind::Circle) && e + 1 == s.elements.size() && !pointElementOf.contains(s.elements[e].Center())) {
                s.AddPoint(s.elements[e].Center());
                std::swap(s.elements[e], s.elements[e + 1]);
                AddElement(e, "Center", {std::nullopt, nullptr, opt.construction});
                ++e;
            }
            elementIds.push_back(id);
            ListUnder(id, true);
            props.push_back(opt.props);
            roles.push_back(opt.construction ? sketch::Role::Construction : sketch::Role::Drawn);
            byId[id] = {true, e};
            if (s.elements[e].kind == sketch::ElementKind::Point)
                pointElementOf.emplace(s.elements[e].Point(), e);
            return e;
        }

        sketch::Ref RefOf(const std::string &text) const {
            if (const auto it = byId.find(text); it != byId.end()) {
                if (!it->second.isElement)
                    throw std::invalid_argument("'" + text + "' is a constraint, not an element");
                return {it->second.index};
            }
            const size_t dot = text.rfind('.');
            if (dot != std::string::npos) {
                const std::string part = text.substr(dot + 1);
                if (const std::optional<std::pair<sketch::Part, size_t>> k = SplinePart(part)) {
                    const auto it = byId.find(text.substr(0, dot));
                    if (it == byId.end() || !it->second.isElement || s.elements[it->second.index].kind != sketch::ElementKind::Spline)
                        throw std::invalid_argument("no element '" + text + "' (." + part + ": a point or tangent of a spline)");
                    if (s.elements[it->second.index].IsFitted(s))
                        throw std::invalid_argument("no element '" + text + "': a curve has no points of its own but its ends, '" + text.substr(0, dot) +
                                                    ".start' and '" + text.substr(0, dot) + ".end'");
                    const size_t n = s.splines[s.elements[it->second.index].SplineIndex()].points.size();
                    if (k->second >= n)
                        throw std::invalid_argument("no element '" + text + "': the spline has the points p0 to p" + std::to_string(n - 1));
                    return {it->second.index, k->first, k->second};
                }
                const std::optional<sketch::Part> p = part == "start"    ? std::optional(sketch::Part::Start)
                                                  : part == "end"    ? std::optional(sketch::Part::End)
                                                  : part == "center" ? std::optional(sketch::Part::Center)
                                                  : part == "mid"    ? std::optional(sketch::Part::Mid)
                                                                     : std::nullopt;
                if (const auto it = byId.find(text.substr(0, dot)); p && it != byId.end() && it->second.isElement) {
                    if (s.elements[it->second.index].kind == sketch::ElementKind::Circle && *p != sketch::Part::Center)
                        throw std::invalid_argument("no element '" + text + "': a circle has no start, end or mid point; its point is '" +
                                                    text.substr(0, dot) + ".center'");
                    const sketch::Ref r{it->second.index, *p};
                    if (!r.HasPart(s))
                        throw std::invalid_argument("no element '" + text + "': '" + text.substr(0, dot) + "' has no ." + part + " point");
                    return r;
                }
            }
            if (const auto w = wires.find(text); w != wires.end() && !w->second.empty())
                throw std::invalid_argument("'" + text + "' is a wire of " + std::to_string(w->second.size()) +
                                            " elements, not one element; name one of them, such as '" + w->second.front() + "'");
            if (const auto under = idsUnder.find(text); under != idsUnder.end()) {
                std::vector<std::string> members;
                for (const std::string &x : under->second)
                    if (const auto at = byId.find(x); at != byId.end() && at->second.isElement && elementIds[at->second.index] == x)
                        members.push_back(x);
                if (!members.empty())
                    throw std::invalid_argument("'" + text + "' is a group of " + std::to_string(members.size()) +
                                                " elements, not one element; name one of them, such as '" + members.front() + "'");
            }
            throw std::invalid_argument("no element '" + text + "'" + DidYouMean(text));
        }
        // "pK" / "tK": a spline's point K, its tangent line there
        static std::optional<std::pair<sketch::Part, size_t>> SplinePart(const std::string &part) {
            if (part.size() < 2 || (part[0] != 'p' && part[0] != 't'))
                return std::nullopt;
            size_t k = 0;
            const auto [end, error] = std::from_chars(part.data() + 1, part.data() + part.size(), k);
            if (error != std::errc() || end != part.data() + part.size())
                return std::nullopt;
            return std::pair{part[0] == 'p' ? sketch::Part::Point : sketch::Part::Tangent, k};
        }
        std::string RefText(const sketch::Ref &r) const {
            const std::string &id = elementIds[r.element];
            if (r.element < 2)
                return "px." + id;
            if (id == "px.origin" && r.part == sketch::Part::Whole)
                return id;
            return "\"" + RefName(r) + "\"";
        }
        // an element's id, with the part it names
        // the id an element shows under: a curvature fillet's hidden arc that of its blend
        const std::string &ShownId(size_t e) const {
            if (roles[e] == sketch::Role::Helper)
                for (size_t k = 3; k < s.elements.size(); ++k)
                    if (s.elements[k].kind == sketch::ElementKind::Spline) {
                        const std::optional<sketch::BlendShape> &b = s.splines[s.elements[k].SplineIndex()].blend;
                        if (b && b->helper == e)
                            return elementIds[k];
                    }
            return elementIds[e];
        }
        std::string RefName(const sketch::Ref &r) const {
            const std::string &id = ShownId(r.element);
            if (r.part == sketch::Part::Point || r.part == sketch::Part::Tangent)
                return id + (r.part == sketch::Part::Point ? ".p" : ".t") + std::to_string(r.index);
            constexpr const char *parts[] = {"", ".start", ".end", ".center", ".mid"};
            return id + parts[(size_t)r.part];
        }
        void CheckFlags(const std::vector<sketch::Kind> &flags) const {
            if (!flags.empty())
                RequireConstraints("px." + std::string(sketch::KindName(flags.front())));
        }
        // what the flags of a drawing call say about the new element e (and the one before it in the chain)
        void Flags(size_t e, const std::vector<sketch::Kind> &flags) {
            for (sketch::Kind k : flags) {
                switch (k) {
                case sketch::Kind::Horizontal:
                case sketch::Kind::Vertical:
                case sketch::Kind::Fix:
                    RelateGiven(k, {{e}});
                    break;
                case sketch::Kind::Tangent:
                case sketch::Kind::Perpendicular:
                case sketch::Kind::Parallel:
                case sketch::Kind::Equal:
                    if (!pen.last)
                        throw std::invalid_argument(elementIds[e] + ": px." + sketch::KindName(k) +
                                                    " relates it to the element before it, this chain has none");
                    RelateGiven(k, {{*pen.last}, {e}});
                    break;
                default:
                    throw std::invalid_argument(elementIds[e] + ": px." + std::string(sketch::KindName(k)) + " is no drawing flag");
                }
            }
        }

        // the pen follows its point when constraints move it
        void Sync() {
            if (pen.point)
                pen.pos = Coords(s.elements[*pen.point].Point());
            if (pen.first)
                pen.start = Coords(s.elements[*pen.first].Point());
            if (parked && parked->point)
                parked->pos = Coords(s.elements[*parked->point].Point());
            if (parked && parked->first)
                parked->start = Coords(s.elements[*parked->first].Point());
        }
        bool ValidMove(const Vertex &end) const {
            IsFinite("move", {pen.pos.x, pen.pos.y, end.x, end.y});
            if (Norm(end - pen.pos) > Tol(std::max(NormInf(pen.pos), NormInf(end))))
                return true;
            Warning() << "zero length move to (" << end.x << ", " << end.y << ") skipped";
            return false;
        }
        void CentreTie(size_t middle, size_t diagonal) { Relate(sketch::Kind::Coincident, {{middle}, {diagonal, sketch::Part::Mid}}); }
        void RectangleTies(const std::vector<size_t> &sides) {
            for (size_t k = 0; k + 1 < sides.size(); ++k)
                Relate(sketch::Kind::Perpendicular, {{sides[k]}, {sides[k + 1]}});
        }
        // the four lines of a rectangle from the pen, l along the drawing direction and w to its left, counterclockwise,
        // as the wire name under that scope with their right angles; the pen stays at the start corner
        std::vector<size_t> RectangleSides(const std::string &name, const std::string &group, double l, double w, const Options &opt) {
            if (opt.id)
                CheckId(name);
            const Options inner{std::nullopt, opt.props, opt.construction};
            const Vertex along = pen.dir, left{-pen.dir.y, pen.dir.x};
            const Vertex p0 = pen.pos, p1 = p0 + pen.dir * l, p2 = p1 + left * w, p3 = p0 + left * w;
            sketch::Scoped scope{idScope, std::exchange(idScope, name)};
            const size_t corner = PenPoint(inner);
            std::vector<size_t> sides;
            for (const Vertex &p : {p1, p2, p3}) {
                const size_t drawn = elementIds.size();
                LineTo(p, {}, inner, false);
                if (elementIds.size() == drawn)
                    throw std::invalid_argument(group + "(l, w): a side within rounding of 0");
                sides.push_back(*pen.last);
            }
            sides.push_back(AddElement(s.AddLine(s.elements[*pen.point].Point(), s.elements[corner].Point()), "Line", inner));
            pen.pos = p0;
            pen.point = corner;
            pen.dir = along;
            pen.last = sides.back();
            RectangleTies(sides);
            NameWire(name, sides, group + ": ");
            groups.push_back({group, name});
            return sides;
        }
        // the circle e of a full turn: the pen's point home on it, tangent to the element before it
        void TurnTies(size_t home, size_t e, bool onCircle, const std::optional<size_t> &before) {
            if (onCircle)
                Relate(sketch::Kind::Coincident, {{home}, {e}});
            if (before)
                Relate(sketch::Kind::Tangent, {{*before}, {e}});
        }
        size_t PenPoint(const Options &opt) {
            if (!pen.point) {
                pen.point = pen.first = AddElement(s.AddPoint(s.NewPoint(pen.pos)), "Point", {std::nullopt, {}, opt.construction});
                pen.start = pen.pos;
                // a construction run starting where the pen has no point yet: the pen takes it too
                if (normalPen && !normalPen->point && normalPen->pos == pen.pos) {
                    normalPen->point = normalPen->first = pen.point;
                    normalPen->start = pen.pos;
                }
            }
            return *pen.point;
        }
        // a chain that comes back to where it started ends in its first point
        size_t EndPoint(const Vertex &end, const Options &opt) {
            if (pen.first && pen.last && end.IsClose(pen.start, Tol(std::max({NormInf(pen.pos), NormInf(pen.start), NormInf(end)}))))
                return *pen.first;
            return AddElement(s.AddPoint(s.NewPoint(end)), "Point", {std::nullopt, {}, opt.construction});
        }
        BasicSketch &ArcThrough(const Vertex &center, const Vertex &end, bool ccw, const Vertex &heading,
                                     const std::vector<sketch::Kind> &flags, const Options &opt) {
            return Drawing(opt, [&] {
                if (!ValidMove(end))
                    return;
                const size_t a = PenPoint(opt), b = EndPoint(end, opt);
                const size_t e = AddElement(s.AddArc(s.NewPoint(center), Norm(end - center), s.elements[a].Point(), s.elements[b].Point(), ccw),
                                            "Arc", opt);
                Extend(e);
                pen.pos = end;
                pen.point = b;
                pen.dir = heading;
                Flags(e, flags);
                pen.last = e;
                Sync();
            });
        }
    };

    using Sketch = BasicSketch<sketch::Geometry>;

}
