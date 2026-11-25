// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

template <class S>
inline Wire OneSketchWire(S &s, bool unify, const std::string &call) {
    WireList wires = s.Wires(unify, call);
    if (wires.empty())
        throw py::value_error(call + ": nothing is drawn yet");
    if (wires.size() > 1)
        throw py::value_error(call + ": the drawn edges form " + std::to_string(wires.size()) +
                              " wires; sk.Wires() returns them as a list");
    return std::move(wires.front());
}

inline std::pair<std::vector<size_t>, std::vector<Vertex>> TangentsOf(const std::map<size_t, Vertex> &tangents) {
    std::pair<std::vector<size_t>, std::vector<Vertex>> out;
    for (const auto &[k, t] : tangents) {
        out.first.push_back(k);
        out.second.push_back(t);
    }
    return out;
}

inline bool PointField(const std::string &field) { return field == "name" || field == "maxh" || field == "hpref"; }
inline std::string PropertyCall(const std::vector<std::string> &ids, const std::string &field) {
    return (ids.size() == 1 ? ids.front() : std::string("the elements")) + "." + field;
}
template <class S>
inline void SetSketchProperty(S &s, const std::vector<std::string> &ids, const std::string &field, const py::object &value) {
    const std::string call = PropertyCall(ids, field);
    const auto shown = [&] { return call + " = " + py::repr(value).cast<std::string>(); };
    if (field == "construction") {
        if (!py::isinstance<py::bool_>(value))
            throw py::type_error(shown() + ": construction is True or False");
        s.SetConstruction(ids, value.cast<bool>(), call);
        return;
    }
    bool found = false;
    EdgeFields([&](const char *name, auto, auto set) {
        if (found || field != name)
            return;
        found = true;
        using V = SetterValueOf<decltype(set)>;
        V v;
        try {
            v = value.cast<V>();
        } catch (const py::cast_error &) {
            throw py::type_error(shown() + ": not a value of " + field);
        }
        ShapeProperties check;
        set(check, v);
        s.ChangeProperties(ids, PointField(field), call, [&](ShapeProperties &p) { set(p, v); });
    });
    if (!found)
        throw py::attribute_error(call + ": a sketch element has no property '" + field + "'");
}
template <class S>
inline py::list SketchProperties(S &s, const std::vector<std::string> &ids, const std::string &field) {
    const std::string call = PropertyCall(ids, field);
    py::list out;
    if (field == "construction") {
        for (const auto &p : s.ElementProperties(ids, true, call))
            out.append(py::bool_(p.second));
        return out;
    }
    bool found = false;
    EdgeFields([&](const char *name, auto get, auto) {
        if (found || field != name)
            return;
        found = true;
        for (const auto &p : s.ElementProperties(ids, PointField(field), call))
            out.append(py::cast(get(PropsOrDefault(p.first))));
    });
    if (!found)
        throw py::attribute_error(call + ": a sketch element has no property '" + field + "'");
    return out;
}

template <class S>
inline Thing NamedInSketch(S &s, const std::optional<std::string> &given) {
    if (!given) {
        const planex2d::Shape shape = s.Shape();
        return {s.Axes(), shape.Faces(), s.Edges(), {}};
    }
    const std::string name = LocalId(s, *given, "Measure");
    if (s.Book().IsWire(name))
        return {s.Axes(), {}, s.ElementWire(name), {}};
    if (s.HasLocation(name))
        return {s.Axes(), {}, Wire(s.Axes()), s.Location(name)};
    if (s.Book().IsElement(name))
        return {s.Axes(), {}, s.ElementWire(name), {}};
    Wire named = s.NamedEdges(name);
    if (named.Edges().empty())
        throw py::value_error("Measure('" + *given + "'): no element, point or edge name '" + name + "'" + s.Book().DidYouMean(name));
    return {s.Axes(), {}, std::move(named), {}};
}

template <class S>
inline py::object ElementOfEdge(S &s, const std::optional<std::string> &id, size_t i) {
    if (!id)
        return py::none();
    const std::string name = LocalId(s, *id, "Measure");
    if (!s.Book().IsWire(name) && !s.Book().IsElement(name))
        return py::none();
    const std::optional<std::string> e = s.ElementOfEdge(name, i);
    return e ? py::object(py::str(*e)) : py::object(py::none());
}

template <class S>
inline void SplineGeometry(const S &s, const sketch::Element &e, double tol, py::dict &out) {
    const auto &g = s.Geometry();
    const Curve<double, Vertex> f = e.Curve(g);
    py::list points, path;
    if (e.IsFitted(g))
        for (size_t p : g.splines[e.SplineIndex()].points)
            points.append(Tuple2(g.Coords(p)));
    else
        for (const Vertex &p : e.SplinePoints(g))
            points.append(Tuple2(p));
    for (const Vertex &p : e.SplinePath(g, tol))
        path.append(Tuple2(p));
    py::dict tangents;
    for (size_t k : f.tangentAt)
        tangents[py::int_(k)] = Tuple2(f.At(f.params[k], 1));
    const std::array<Vertex, 2> ends = e.EndCoords(g);
    out["points"] = points;
    out["closed"] = f.closed && !e.IsPiece(g);
    out["tangents"] = tangents;
    out["path"] = path;
    out["start"] = Tuple2(ends[0]);
    out["end"] = Tuple2(ends[1]);
    out["projected"] = e.IsHeld(g);
    out["style"] = StyleName(e.Style(g));
    if (const std::optional<sketch::BlendShape> &b = g.splines[e.SplineIndex()].blend; b && b->helper != sketch::none) {
        const sketch::Element &arc = g.elements[b->helper];
        py::dict fillet;
        fillet["center"] = Tuple2(g.Coords(arc.Center()));
        fillet["r"] = g.values[arc.Radius()];
        out["fillet"] = fillet;
    }
}

template <class S>
inline void OffsetCurveGeometry(const S &s, const sketch::Element &e, double tol, py::dict &out) {
    const auto &g = s.Geometry();
    py::list path;
    for (const Vertex &p : e.OffsetCurvePath(g, tol))
        path.append(Tuple2(p));
    const std::array<Vertex, 2> ends = e.EndCoords(g);
    out["points"] = py::list();
    out["closed"] = e.IsClosed(g);
    out["tangents"] = py::dict();
    out["path"] = path;
    out["start"] = Tuple2(ends[0]);
    out["end"] = Tuple2(ends[1]);
    out["projected"] = false;
    out["style"] = StyleName(e.Style(g));
    out["source"] = s.OffsetCurveSource(e);
}

template <class S>
inline py::dict ElementGeometry(S &s, const std::string &id) {
    const auto [e, radius, angle, d] = s.ElementOf(id);
    py::dict out;
    out["kind"] = sketch::ElementKindName(e.kind);
    switch (e.kind) {
    case sketch::ElementKind::Point:
        out["point"] = Tuple2(s.Coords(e.Point()));
        break;
    case sketch::ElementKind::Line: {
        const Vertex a = s.Coords(e.Start()), b = s.Coords(e.End());
        out["start"] = Tuple2(a);
        out["end"] = Tuple2(b);
        out["length"] = Norm(b - a);
        out["angle"] = Degrees(std::atan2(b.y - a.y, b.x - a.x));
        break;
    }
    case sketch::ElementKind::Circle:
        out["center"] = Tuple2(s.Coords(e.Center()));
        out["r"] = radius;
        break;
    case sketch::ElementKind::Arc: {
        const Vertex c = s.Coords(e.Center()), a = s.Coords(e.Start()), b = s.Coords(e.End());
        out["center"] = Tuple2(c);
        out["r"] = radius;
        out["start"] = Tuple2(a);
        out["end"] = Tuple2(b);
        out["ccw"] = e.ccw;
        const double turn = 360.0, from = Degrees(std::atan2(a.y - c.y, a.x - c.x)),
                     to = Degrees(std::atan2(b.y - c.y, b.x - c.x));
        const double ccwTurn = std::fmod(std::fmod(to - from, turn) + turn, turn);
        out["sweep"] = e.ccw ? ccwTurn : ccwTurn - turn;
        break;
    }
    case sketch::ElementKind::Axis:
        out["normal_angle"] = angle;
        out["d"] = d;
        break;
    case sketch::ElementKind::Spline:
        SplineGeometry(s, e, s.Geometry().DrawingTol(), out);
        break;
    case sketch::ElementKind::OffsetCurve:
        OffsetCurveGeometry(s, e, s.Geometry().DrawingTol(), out);
        break;
    case sketch::ElementKind::Pattern:
    case sketch::ElementKind::Offset:
        break;
    }
    return out;
}
template <class S>
inline py::dict GeometryOf(S &s, const py::object &r) {
    Prepare(s, r);
    return ElementGeometry(s, RefIn(s, r, "Geometry"));
}

template <class S>
inline py::dict ExpressionNamesOf(S &s) {
    py::dict dimensions, elements, others, out;
    const auto names = s.ExpressionNames();
    for (const auto &[name, turn] : names.dimensions)
        dimensions[py::str(name)] = turn ? "angle" : "length";
    for (const auto &[name, kind] : names.others)
        others[py::str(name)] = kind;
    for (const auto &[kind, ids] : s.Elements())
        for (const std::string &e : ids)
            elements[py::str(e)] = kind;
    out["dimensions"] = dimensions;
    out["elements"] = elements;
    out["others"] = others;
    out["ispoint"] = py::cpp_function([&s](const std::string &r) { return s.IsPoint(r); });
    return out;
}

template <class S>
inline py::dict CurveNames(S &s) {
    py::dict out;
    if constexpr (HasConstraints<S>) {
        s.Flush();
        out = ExpressionNamesOf(s);
        py::dict driven;
        for (const auto &[name, why] : s.DrivenDimensions())
            driven[py::str(name)] = why;
        out["driven"] = driven;
    } else
        out["numbers_only"] = "a QuickSketch has no dimensions";
    return out;
}

template <class S>
py::list DrawingOf(S &s) {
    py::list out;
    std::map<std::string, std::string> states;
    if constexpr (HasConstraints<S>)
        for (const auto &[id, state] : s.States())
            states[id] = state;
    const double tol = s.Geometry().DrawingTol();
    for (const auto &[kind, ids] : s.Elements())
        for (const std::string &id : ids) {
            const auto [e, radius, angle, d] = s.ElementOf(id);
            py::dict g;
            g["id"] = id;
            // a curve and a b-spline draw as the splines they are
            g["kind"] = kind == "Curve" || kind == "BSpline" ? "Spline" : kind;
            g["state"] = states.contains(id) ? states[id] : std::string("under");
            g["construction"] = s.IsConstruction(id);
            g["projected"] = e.IsHeld(s.Geometry());
            if (e.kind == sketch::ElementKind::Point)
                g["point"] = Tuple2(s.Coords(e.Point()));
            else if (e.kind == sketch::ElementKind::Line) {
                g["start"] = Tuple2(s.Coords(e.Start()));
                g["end"] = Tuple2(s.Coords(e.End()));
            } else if (e.kind == sketch::ElementKind::Circle || e.kind == sketch::ElementKind::Arc) {
                g["center"] = Tuple2(s.Coords(e.Center()));
                g["r"] = radius;
                if (e.kind == sketch::ElementKind::Arc) {
                    g["start"] = Tuple2(s.Coords(e.Start()));
                    g["end"] = Tuple2(s.Coords(e.End()));
                    g["ccw"] = e.ccw;
                }
            } else if (e.kind == sketch::ElementKind::Axis) {
                g["normal_angle"] = angle;
                g["d"] = d;
            } else if (e.kind == sketch::ElementKind::Spline)
                SplineGeometry(s, e, tol, g);
            else if (e.kind == sketch::ElementKind::OffsetCurve)
                OffsetCurveGeometry(s, e, tol, g);
            out.append(g);
        }
    return out;
}
