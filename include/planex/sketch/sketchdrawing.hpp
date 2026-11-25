// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::MoveTo(const Vertex &p, const std::vector<sketch::Kind> &flags) {
        CheckFlags(flags);
        IsFinite("Move", {p.x, p.y});
        return Atomic([&] {
            Finish();
            pen.MoveTo(p);
            for (sketch::Kind k : flags) {
                if (!sketch::IsFix(k))
                    throw std::invalid_argument("Move((h, v)): px." + std::string(sketch::KindName(k)) +
                                                " is no flag here (a coincidence needs a point id: Move(id, px.Coincident))");
                RelateGiven(k, {{PenPoint({})}});
            }
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::MoveTo(const std::string &ref, const std::vector<sketch::Kind> &flags) {
        CheckFlags(flags);
        return Atomic([&] {
            Finish();
            const sketch::Ref r = RefOf(ref);
            if (!r.IsPoint(s))
                throw std::invalid_argument("Move('" + ref + "'): not a point");
            pen.MoveTo(r.PointValue(s));
            for (sketch::Kind k : flags) {
                if (!sketch::IsFix(k) && k != sketch::Kind::Coincident)
                    throw std::invalid_argument("Move('" + ref + "'): px." + std::string(sketch::KindName(k)) + " is no flag here");
                RelateGiven(k, sketch::IsFix(k) ? std::vector<sketch::Ref>{{PenPoint({})}} : std::vector<sketch::Ref>{{PenPoint({})}, r});
            }
            Sync();
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Line(const Vertex &end, const std::vector<sketch::Kind> &flags, const Options &opt) {
        CheckFlags(flags);
        return Drawing(opt, [&] { LineTo(end, flags, opt); });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Line(double l, const std::vector<sketch::Kind> &flags, const Options &opt) {
        CheckFlags(flags);
        return Drawing(opt, [&] { LineTo(pen.pos + pen.dir * l, flags, opt); });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Line(double dx, double dy, const std::vector<sketch::Kind> &flags, const Options &opt) {
        CheckFlags(flags);
        return Drawing(opt, [&] { LineTo(pen.pos + Vertex{dx, dy}, flags, opt); });
    }

    template <class Store>
    void BasicSketch<Store>::LineTo(const Vertex &end, const std::vector<sketch::Kind> &flags, const Options &opt, bool inChain) {
        if (!ValidMove(end))
            return;
        const size_t a = PenPoint(opt), b = EndPoint(end, opt);
        const size_t e = AddElement(s.AddLine(s.elements[a].Point(), s.elements[b].Point()), "Line", opt);
        if (inChain)
            AddToChain(e);
        pen.HeadAlong(end - pen.pos);
        pen.pos = end;
        pen.point = b;
        ElementDrawn(e, flags);
    }

    // ang in degrees, positive to the left of the drawing direction; a negative r puts the centre on the other side
    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Arc(double r, double ang, const std::vector<sketch::Kind> &flags, const Options &opt) {
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

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::FullTurn(const Vertex &center, bool ccw, const std::vector<sketch::Kind> &flags, const Options &opt) {
        return Drawing(opt, [&] {
            const size_t home = PenPoint(opt);
            const Vertex from = pen.pos - center;
            const size_t e = AddElement(s.AddCircle(s.NewPoint(center), Norm(from), std::atan2(from.y, from.x), ccw, s.elements[home].Point()),
                                        "Circle", opt);
            AddToChain(e);
            const bool fix = std::find(flags.begin(), flags.end(), sketch::Kind::Fix) != flags.end();
            if (fix)
                RelateGiven(sketch::Kind::Fix, {{home}});
            TurnTies(home, e, !fix, std::find(flags.begin(), flags.end(), sketch::Kind::Tangent) == flags.end() ? pen.last : std::nullopt);
            ElementDrawn(e, flags);
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Arc(const Vertex &end, const Vertex &t, const std::vector<sketch::Kind> &flags, const Options &opt) {
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

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Spline(const std::vector<Vertex> &points, bool closed, const std::map<size_t, Vertex> &tangents, bool fromPen,
                                                   const std::vector<sketch::Kind> &flags, const Options &opt, SplineStyle style,
                                                   const std::map<size_t, TangentSetting<double>> &settings) {
        CheckFlags(flags);
        for (const Vertex &p : points)
            IsFinite("Spline", {p.x, p.y});
        for (const auto &[k, t] : tangents)
            IsFinite("Spline", {t.x, t.y});
        for (const auto &[k, setting] : settings) {
            if (!tangents.count(k))
                throw std::invalid_argument("Spline: a " + std::string(setting.curvature ? "curvature radius" : "tension") + " at point " +
                                            std::to_string(k) + ", which has no tangent; give its tangent too");
            if (!(setting.tension > 0.0) || !std::isfinite(setting.tension))
                throw std::invalid_argument("Spline: the tension at point " + std::to_string(k) + " is " + std::to_string(setting.tension) +
                                            "; it is positive and finite");
        }
        return Drawing(opt, [&] {
            std::vector<Vertex> rest = points;
            if (!fromPen && !rest.empty()) {
                Finish();
                pen.MoveTo(rest.front());
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
            const size_t e = AddElement(s.AddSpline(ids, at, angles, closed, style, settings), "Spline", opt);
            SplineAdded(e);
            for (const auto &[k, setting] : settings)
                if (setting.curvature)
                    Dimension(sketch::Kind::CurvatureRadius, {{e, sketch::Part::Point, k}},
                              *setting.curvature == 0.0 ? std::numeric_limits<double>::infinity() : 1.0 / *setting.curvature, "", false);
            AddToChain(e);
            if (closed)
                pen.pos = Coords(s.elements[*pen.point].Point());
            const Vertex t = s.elements[e].TangentAtFraction(s, closed ? 0.0 : 1.0);
            pen.HeadAlong(t);
            ElementDrawn(e, flags);
            if (closed)
                Finish();
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::BSpline(const std::vector<Vertex> &poles, const std::optional<std::vector<double>> &knots, size_t degree, bool periodic,
                                                    const std::vector<sketch::Kind> &flags, const Options &opt) {
        CheckFlags(flags);
        for (const Vertex &p : poles)
            IsFinite("BSpline", {p.x, p.y});
        if (poles.size() < degree + 1 || degree < 1)
            throw std::invalid_argument("BSpline: " + std::to_string(poles.size()) + " poles of degree " + std::to_string(degree) +
                                        "; it needs a degree of 1 at least and degree + 1 poles");
        return Drawing(opt, [&] {
            if (!poles.front().IsClose(pen.pos, Tol(std::max(NormInf(pen.pos), NormInf(poles.front()))))) {
                Finish();
                pen.MoveTo(poles.front());
            }
            std::vector<size_t> ids{s.elements[PenPoint(opt)].Point()};
            for (size_t k = 1; k < poles.size(); ++k) {
                const size_t e = k + 1 == poles.size() && !periodic ? EndPoint(poles[k], opt)
                                                                     : AddElement(s.AddPoint(s.NewPoint(poles[k])), "Point", {std::nullopt, {}, true});
                ids.push_back(s.elements[e].Point());
                pen.pos = poles[k];
                if (!periodic)
                    pen.point = e;
            }
            const std::vector<double> k = knots ? *knots : UniformKnots(poles.size(), degree, periodic);
            const size_t e = AddElement(s.AddPoledSpline(ids, k, degree, periodic), "BSpline", opt);
            AddLegs(e);
            SplineAdded(e);
            AddToChain(e);
            if (periodic)
                pen.pos = poles.front();
            const Vertex t = s.elements[e].TangentAtFraction(s, periodic ? 0.0 : 1.0);
            pen.HeadAlong(t);
            ElementDrawn(e, flags);
            if (periodic)
                Finish();
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Curve(const planex2d::Curve<double, Vertex> &shape, std::optional<sketch::CurveFormula> formula,
                                                  const std::vector<sketch::Kind> &flags, const Options &opt) {
        CheckFlags(flags);
        return Drawing(opt, [&] {
            const Vertex start = shape.poles.front(), end = shape.poles.back();
            if (!start.IsClose(pen.pos, Tol(std::max(NormInf(pen.pos), NormInf(start))))) {
                Finish();
                pen.MoveTo(start);
            }
            const size_t a = PenPoint(opt), b = shape.closed ? a : EndPoint(end, opt);
            sketch::FittedShape f{shape};
            f.formula = std::move(formula);
            const size_t e = AddElement(s.AddFittedSpline(s.elements[a].Point(), s.elements[b].Point(), std::move(f), {0.0, 0.0}, 0.0), "Curve", opt);
            AddToChain(e);
            pen.point = b;
            pen.pos = Coords(s.elements[b].Point());
            const Vertex t = shape.At(shape.closed ? shape.params.front() : shape.params.back(), 1);
            pen.HeadAlong(t);
            Flags(e, flags);
            if (std::none_of(flags.begin(), flags.end(), sketch::IsFix))
                RelateGiven(sketch::Kind::Fix, {{e}});
            pen.last = e;
            Sync();
            if (shape.closed)
                Finish();
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Circle(const Vertex &center, double r, const std::vector<sketch::Kind> &flags, const Options &opt) {
        CheckFlags(flags);
        IsFinite("Circle", {center.x, center.y, r});
        if (!(r > 0.0))
            throw std::invalid_argument("Circle: the radius must be positive");
        return Drawing(opt, [&] {
            Finish();
            Flags(AddElement(s.AddCircle(s.NewPoint(center), r), "Circle", opt), flags);
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Rectangle(double l, double w, const Options &opt) {
        IsFinite("Rectangle", {l, w});
        return Drawing(opt, [&] { RectangleSides(opt.id ? *opt.id : book.FreshId("Rectangle"), "Rectangle", l, w, opt); });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Rectangle(const std::vector<Vertex> &p, const std::vector<std::optional<std::string>> &at, const Options &opt) {
        for (const Vertex &q : p)
            IsFinite("Rectangle", {q.x, q.y});
        return Drawing(opt, [&] {
            const sketch::PenState saved = pen;
            pen.Lift();
            double l = std::abs(p[1].x - p[0].x), w = std::abs(p[1].y - p[0].y), d = 0.0;
            if (p.size() == 2) {
                pen.MoveTo({std::min(p[0].x, p[1].x), std::min(p[0].y, p[1].y)});
                pen.dir = {1.0, 0.0};
            } else {
                const Vertex side = p[1] - p[0];
                l = Norm(side);
                if (!(l > 0.0))
                    throw std::invalid_argument("Rectangle(p0, p1, p2): p0 and p1 are one point; give two corners of a side");
                const Vertex u = side * (1.0 / l), left{-u.y, u.x};
                d = Dot(p[2] - p[1], left);
                pen.MoveTo(p[0]);
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
            const std::string name = opt.id ? *opt.id : book.FreshId("Rectangle");
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

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::RectangleC(const std::optional<Vertex> &center, const std::optional<std::string> &at, double l, double w,
                                                       const Options &opt) {
        IsFinite("RectangleC", {l, w});
        if (center)
            IsFinite("RectangleC", {center->x, center->y});
        return Drawing(opt, [&] {
            Finish();
            const Vertex c = at ? Location(*at) : center.value_or(pen.pos), along = pen.dir, left{-pen.dir.y, pen.dir.x};
            pen.MoveTo(c - along * (0.5 * l) - left * (0.5 * w));
            const std::string name = opt.id ? *opt.id : book.FreshId("RectangleC");
            const std::vector<size_t> sides = RectangleSides(name, "RectangleC", l, w, opt);
            const Options aid{std::nullopt, {}, true};
            sketch::Scoped scope{book.idScope, std::exchange(book.idScope, name)};
            const size_t diagonal = AddElement(s.AddLine(s.elements[sides[0]].Start(), s.elements[sides[1]].End()), "Line", aid);
            book.Rename(diagonal, name + ".diagonal");
            const size_t middle = AddElement(s.AddPoint(s.NewPoint(c)), "Point", aid);
            book.Rename(middle, name + ".center");
            CentreTie(middle, diagonal);
            if (at)
                Relate(sketch::Kind::Coincident, {{middle}, RefOf(*at)});
            pen.MoveTo(c);
            pen.dir = along;
            pen.Lift();
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Direction(double dx, double dy) {
        IsFinite("Direction", {dx, dy});
        const double n = std::sqrt(dx * dx + dy * dy);
        if (!(n > 0.0))
            throw std::invalid_argument("Direction: zero vector");
        pen.dir = {dx / n, dy / n};
        if (parked)
            parked->dir = pen.dir;
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Turn(double angle) {
        IsFinite("Turn", {angle});
        pen.dir = Rotated(pen.dir, angle);
        if (parked)
            parked->dir = Rotated(parked->dir, angle);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Close(const std::vector<sketch::Kind> &flags, const Options &opt, const std::optional<std::string> &idWire) {
        CheckFlags(flags);
        return Drawing(opt, [&] {
            if (pen.last && pen.point != pen.first) {
                const size_t e = AddElement(s.AddLine(s.elements[*pen.point].Point(), s.elements[*pen.first].Point()), "Line", opt);
                AddToChain(e);
                if (Norm(pen.start - pen.pos) > 0.0)
                    pen.HeadAlong(pen.start - pen.pos);
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
    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Finish(const std::optional<std::string> &idWire) {
        if (idWire)
            RenameChain(*idWire, "id_wire=");
        if (!normalPen)
            parked.reset();
        pen.Lift();
        if (atomic > 0 && !normalPen)
            ended.push_back(std::move(pen.chain));
        pen.chain.clear();
        pen.wire.clear();
        return *this;
    }

    template <class Store>
    void BasicSketch<Store>::AddToChain(size_t e) {
        if (pen.wire.empty()) {
            pen.wire = book.FreshId("Chain");
            book.wires[pen.wire];
            book.groups.push_back({"Chain", pen.wire});
            book.wireLog.push_back({pen.wire, std::string()});
        }
        if (!pen.chain.empty())
            Chained(pen.chain.back(), e);
        pen.chain.push_back(e);
        book.wires[pen.wire].push_back(book.elementIds[e]);
    }

    template <class Store>
    void BasicSketch<Store>::RenameChain(const std::string &id, const std::string &call) {
        if (pen.chain.empty())
            throw std::invalid_argument(call + "'" + id + "': no chain is drawn to name");
        book.RenameWire(pen.wire, id);
        pen.wire = id;
    }

    template <class Store>
    std::string BasicSketch<Store>::GlobalOrigin() {
        if (!book.byId.contains("px.origin")) {
            AddElement(s.AddPoint(s.NewPoint(axes.ToHV(Vector3{}), true)), "Point",
                       {"px.origin", {}, true});
        }
        return "px.origin";
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::AddProjection(const Projection &p, bool asConstruction, const std::optional<std::string> &name, const std::string &kind,
                                                          const std::optional<std::string> &id, bool hold) {
        return Atomic([&] {
            Finish();
            const std::string scope = id ? *id : book.FreshId(kind);
            if (id)
                book.CheckId(scope);
            sketch::Scoped guard{book.idScope, std::exchange(book.idScope, scope)};
            sketch::ProjectionMaker<BasicSketch>(*this, p, asConstruction, name, kind, scope, hold).Make();
        });
    }

    template <class Store>
    std::string BasicSketch<Store>::ProjectedReference(const Projection &p, const std::string &call) {
        if (p.wire.Size() != 1)
            throw std::invalid_argument(call + ": an OCC shape given as a reference projects to " + std::to_string(p.wire.Size()) +
                                        " edges; give one edge, or Project the shape first and name its element");
        const size_t before = s.elements.size();
        AddProjection(p, true, std::nullopt, "Projection", std::nullopt);
        for (size_t e = before; e < s.elements.size(); ++e)
            if (s.elements[e].kind != sketch::ElementKind::Point)
                return book.elementIds[e];
        throw std::invalid_argument(call + ": the OCC edge projects to a point; it gives no line to constrain to");
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::NamedPoint(const std::string &name, const Vertex &p) {
        IsFinite("Vertex", {p.x, p.y});
        return Atomic([&] { AddElement(s.AddPoint(s.NewPoint(p)), "Point", {name, {}, true}); });
    }

    template <class Store>
    std::string BasicSketch<Store>::AddAxisLine(const Vertex &p, const Vertex &along, const std::string &prefix, const std::optional<std::string> &id) {
        const Vertex u = along * (1.0 / Norm(along)), n{u.y, -u.x};
        AddElement(s.AddAxis(std::atan2(n.y, n.x), Dot(n, p), false), prefix, {id, {}, true});
        return book.elementIds.back();
    }

    template <class Store>
    template <class F>
    BasicSketch<Store> &BasicSketch<Store>::Slot(const std::string &kind, double w, std::optional<double> hole, const Options &opt, const std::string &call, F spine) {
        IsFinite(call.c_str(), {w, hole.value_or(0.0)});
        if (!(w > 0.0))
            throw std::invalid_argument(call + ": the width w = " + NumberText(w) + " must be positive");
        if (hole)
            if (const std::optional<std::string> wrong = HoleProblem(w, *hole, std::nullopt))
                throw std::invalid_argument(call + ": " + *wrong);
        return Atomic([&] {
            const std::string name = opt.id ? *opt.id : book.FreshId(kind), id = name + ".spine";
            book.CheckId(name);
            const size_t chains = book.counters["Chain"];
            parked.reset();
            {
                sketch::Scoped scope{book.idScope, std::exchange(book.idScope, name)};
                spine();
            }
            if (!parked || !parked->last)
                throw std::invalid_argument(call + ": the spine has no length; give it one");
            book.Rename(*parked->last, id);
            CheckSpine(id, w, hole, call);
            MakeOffset(ChainOf({id}, call), w / 2.0, std::nullopt, true, Options{name, opt.props, opt.construction}, std::nullopt, call, kind, hole);
            PenAtSpineEnd(chains);
        });
    }

    template <class Store>
    template <class F>
    BasicSketch<Store> &BasicSketch<Store>::Polygon(const std::string &kind, size_t n, std::optional<double> flats, const Options &opt, const std::string &call,
                                                    F circle) {
        IsFinite(call.c_str(), {flats.value_or(1.0)});
        if (flats && !(*flats > 0.0))
            throw std::invalid_argument(call + ": the width across flats af = " + NumberText(*flats) + " must be positive");
        return Atomic([&] {
            const std::string name = opt.id ? *opt.id : book.FreshId(kind);
            book.CheckId(name);
            const Vertex along = pen.dir;
            parked.reset();
            {
                sketch::Scoped scope{book.idScope, std::exchange(book.idScope, name)};
                circle();
            }
            parked.reset();
            const size_t ring = book.elementIds.size() - 1;
            book.Rename(ring, name + ".circle");
            const std::vector<size_t> sides = PolygonSides(name, ring, n, along, Options{std::nullopt, opt.props, opt.construction});
            book.NameWire(name, sides, call + ": ");
            book.groups.push_back({kind, name});
            PolygonDimension(name, ring, sides, flats);
        });
    }

    template <class Store>
    void BasicSketch<Store>::PolygonDimension(const std::string &name, size_t ring, const std::vector<size_t> &sides, const std::optional<double> &flats) {
        if (flats)
            Dimension(sketch::Kind::Distance, {{sides[0]}, {sides[3]}}, *flats, name + ".af", false);
        else
            Dimension(sketch::Kind::Radius, {{ring}}, s.values[s.elements[ring].Radius()], name + ".r", false);
    }

    template <class Store>
    std::vector<size_t> BasicSketch<Store>::PolygonSides(const std::string &name, size_t ring, size_t n, const Vertex &along, const Options &inner) {
        const Vertex centre = Coords(s.elements[ring].Center()), left{-along.y, along.x};
        const double r = s.values[s.elements[ring].Radius()];
        sketch::Scoped scope{book.idScope, std::exchange(book.idScope, name)};
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

    template <class Store>
    void BasicSketch<Store>::PolygonTies(size_t ring, const std::vector<size_t> &corners, const std::vector<size_t> &sides) {
        for (size_t corner : corners)
            Relate(sketch::Kind::Coincident, {{corner}, {ring}});
        for (size_t k = 0; k + 1 < sides.size(); ++k)
            Relate(sketch::Kind::Equal, {{sides[k]}, {sides[k + 1]}});
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::HoleProblem(double w, double r, std::optional<double> length) const {
        if (!(r > w / 2.0))
            return "the hole's radius r = " + SizeText(r) + " must exceed half the width w / 2 = " + SizeText(w / 2.0) + "; make r larger or w smaller";
        const double reach = std::sqrt(r * r - w * w / 4.0);
        if (length && !(*length > reach))
            return "the spine's length " + SizeText(*length) + " must exceed " + SizeText(reach) + ", where the sides meet the hole of radius r = " +
                   SizeText(r) + "; make the spine longer";
        return std::nullopt;
    }

    template <class Store>
    void BasicSketch<Store>::CheckSpine(const std::string &id, double w, std::optional<double> hole, const std::string &call) const {
        const sketch::Element &e = s.elements[book.byId.at(id).index];
        if (hole)
            if (const std::optional<std::string> wrong = HoleProblem(w, *hole, e.Length(s)))
                throw std::invalid_argument(call + ": " + *wrong);
        if (e.kind == sketch::ElementKind::Arc && !(w < 2.0 * s.values[e.Radius()]))
            throw std::invalid_argument(call + ": the width w = " + NumberText(w) + " must be below 2 r = " + NumberText(2.0 * s.values[e.Radius()]) +
                                        " (the spine arc's radius r = " + NumberText(s.values[e.Radius()]) + "); make w smaller or r larger");
    }

    template <class Store>
    void BasicSketch<Store>::PenAtSpineEnd(size_t chains) {
        const sketch::PenState run = *parked;
        Finish();
        std::erase_if(book.groups, [&](const auto &g) { return g.second == run.wire; });
        book.wires.erase(run.wire);
        book.counters["Chain"] = chains;
        pen.MoveTo(run.pos);
        pen.dir = run.dir;
        pen.point = pen.first = run.point;
    }

    template <class Store>
    void BasicSketch<Store>::AddLegs(size_t e) {
        const sketch::SplineData &d = s.splines[s.elements[e].SplineIndex()];
        const std::vector<size_t> poles = d.points;
        const bool closed = d.closed;
        for (size_t i = 0; i + 1 < poles.size() + (closed ? 1 : 0); ++i)
            AddElement(s.AddLine(poles[i], poles[(i + 1) % poles.size()]), "Line", {book.elementIds[e] + ".leg." + std::to_string(i), {}, true});
    }

    // the element made last is e: with its id, properties and construction flag; an arc's or circle's centre gets a
    // point element (Center.k) if it has none, made before it, so the arc stays the last element made. Returns where e
    // is then
    template <class Store>
    size_t BasicSketch<Store>::AddElement(size_t e, const std::string &prefix, const Options &opt) {
        const std::string id = book.NewId(prefix, opt.id);
        if (s.elements[e].IsRound() && e + 1 == s.elements.size() && !pointElementOf.contains(s.elements[e].Center())) {
            s.AddPoint(s.elements[e].Center());
            std::swap(s.elements[e], s.elements[e + 1]);
            AddElement(e, "Center", {std::nullopt, nullptr, opt.construction});
            ++e;
        }
        book.elementIds.push_back(id);
        book.ListUnder(id, true);
        props.push_back(opt.props);
        roles.push_back(opt.construction ? sketch::Role::Construction : sketch::Role::Drawn);
        book.byId[id] = {true, e};
        if (s.elements[e].kind == sketch::ElementKind::Point)
            pointElementOf.emplace(s.elements[e].Point(), e);
        return e;
    }

    template <class Store>
    void BasicSketch<Store>::CheckFlags(const std::vector<sketch::Kind> &flags) const {
        if (!flags.empty())
            RequireConstraints("px." + std::string(sketch::KindName(flags.front())));
    }

    template <class Store>
    void BasicSketch<Store>::Flags(size_t e, const std::vector<sketch::Kind> &flags) {
        for (sketch::Kind k : flags) {
            switch (k) {
            case sketch::Kind::Horizontal:
            case sketch::Kind::Vertical:
            case sketch::Kind::Fix:
            case sketch::Kind::FixH:
            case sketch::Kind::FixV:
                RelateGiven(k, {{e}});
                break;
            case sketch::Kind::Tangent:
            case sketch::Kind::Perpendicular:
            case sketch::Kind::Parallel:
            case sketch::Kind::Equal:
                if (!pen.last)
                    throw std::invalid_argument(book.elementIds[e] + ": px." + sketch::KindName(k) +
                                                " relates it to the element before it, this chain has none");
                RelateGiven(k, {{*pen.last}, {e}});
                break;
            default:
                throw std::invalid_argument(book.elementIds[e] + ": px." + std::string(sketch::KindName(k)) + " is no drawing flag");
            }
        }
    }

    template <class Store>
    void BasicSketch<Store>::ElementDrawn(size_t e, const std::vector<sketch::Kind> &flags) {
        Flags(e, flags);
        pen.last = e;
        Sync();
    }

    template <class Store>
    void BasicSketch<Store>::Sync() {
        pen.Sync(s);
        if (parked)
            parked->Sync(s);
    }

    template <class Store>
    bool BasicSketch<Store>::ValidMove(const Vertex &end) const {
        IsFinite("move", {pen.pos.x, pen.pos.y, end.x, end.y});
        if (Norm(end - pen.pos) > Tol(std::max(NormInf(pen.pos), NormInf(end))))
            return true;
        Warning() << "zero length move to (" << end.x << ", " << end.y << ") skipped";
        return false;
    }

    template <class Store>
    void BasicSketch<Store>::RectangleTies(const std::vector<size_t> &sides) {
        for (size_t k = 0; k + 1 < sides.size(); ++k)
            Relate(sketch::Kind::Perpendicular, {{sides[k]}, {sides[k + 1]}});
    }

    template <class Store>
    std::vector<size_t> BasicSketch<Store>::RectangleSides(const std::string &name, const std::string &group, double l, double w, const Options &opt) {
        if (opt.id)
            book.CheckId(name);
        const Options inner{std::nullopt, opt.props, opt.construction};
        const Vertex along = pen.dir, left{-pen.dir.y, pen.dir.x};
        const Vertex p0 = pen.pos, p1 = p0 + pen.dir * l, p2 = p1 + left * w, p3 = p0 + left * w;
        sketch::Scoped scope{book.idScope, std::exchange(book.idScope, name)};
        const size_t corner = PenPoint(inner);
        std::vector<size_t> sides;
        for (const Vertex &p : {p1, p2, p3}) {
            const size_t drawn = book.elementIds.size();
            LineTo(p, {}, inner, false);
            if (book.elementIds.size() == drawn)
                throw std::invalid_argument(group + "(l, w): a side within rounding of 0");
            sides.push_back(*pen.last);
        }
        sides.push_back(AddElement(s.AddLine(s.elements[*pen.point].Point(), s.elements[corner].Point()), "Line", inner));
        pen.pos = p0;
        pen.point = corner;
        pen.dir = along;
        pen.last = sides.back();
        RectangleTies(sides);
        book.NameWire(name, sides, group + ": ");
        book.groups.push_back({group, name});
        return sides;
    }

    template <class Store>
    void BasicSketch<Store>::TurnTies(size_t home, size_t e, bool onCircle, const std::optional<size_t> &before) {
        if (onCircle)
            Relate(sketch::Kind::Coincident, {{home}, {e}});
        if (before)
            Relate(sketch::Kind::Tangent, {{*before}, {e}});
    }

    template <class Store>
    size_t BasicSketch<Store>::PenPoint(const Options &opt) {
        if (!pen.point) {
            pen.point = pen.first = AddElement(s.AddPoint(s.NewPoint(pen.pos)), "Point", {std::nullopt, {}, opt.construction});
            pen.start = pen.pos;
            if (normalPen && !normalPen->point && normalPen->pos == pen.pos) {
                normalPen->point = normalPen->first = pen.point;
                normalPen->start = pen.pos;
            }
        }
        return *pen.point;
    }

    template <class Store>
    size_t BasicSketch<Store>::EndPoint(const Vertex &end, const Options &opt) {
        if (pen.first && pen.last && end.IsClose(pen.start, Tol(std::max({NormInf(pen.pos), NormInf(pen.start), NormInf(end)}))))
            return *pen.first;
        return AddElement(s.AddPoint(s.NewPoint(end)), "Point", {std::nullopt, {}, opt.construction});
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::ArcThrough(const Vertex &center, const Vertex &end, bool ccw, const Vertex &heading, const std::vector<sketch::Kind> &flags,
                                                       const Options &opt) {
        return Drawing(opt, [&] {
            if (!ValidMove(end))
                return;
            const size_t a = PenPoint(opt), b = EndPoint(end, opt);
            const size_t e = AddElement(s.AddArc(s.NewPoint(center), Norm(end - center), s.elements[a].Point(), s.elements[b].Point(), ccw),
                                        "Arc", opt);
            AddToChain(e);
            pen.pos = end;
            pen.point = b;
            pen.dir = heading;
            ElementDrawn(e, flags);
        });
    }

    // the bisector of the centres whatever the radii drawn: they are made equal
    template <class Store>
    std::string BasicSketch<Store>::AddCentreBisector(const std::string &a, const std::string &b, const std::optional<std::string> &id) {
        const Vertex p = Location(a + ".center"), q = Location(b + ".center");
        if (!(Norm(q - p) > Tol(std::max(NormInf(p), NormInf(q)))))
            throw std::invalid_argument("SymmetryLine('" + a + "', '" + b + "'): both lie on the same circle");
        const Vertex d = (q - p) * (1.0 / Norm(q - p));
        return AddAxisLine((p + q) * 0.5, Vertex{-d.y, d.x}, "SymmetryLine", id);
    }

}
