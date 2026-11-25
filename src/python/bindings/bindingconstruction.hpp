// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "bindingbase.hpp"
#include "bindingviews.hpp"
#include "bindingargs.hpp"

template <class S>
inline void CheckPlane(const S &s, const Wire &w, const std::string &call) {
    if (!s.Axes().IsClose(w.Axes()))
        throw py::value_error(call + ": the target lies in another plane");
}

inline bool IsTarget(const py::handle &t) {
    return py::isinstance<py::str>(t) || IsA<EdgeView>(t) || IsA<Wire>(t) || IsA<Face>(t) || IsA<Shape>(t) ||
           occHooks->isShape(t);
}
inline bool IsPoint(const py::handle &t) {
    if (IsTarget(t))
        return false;
    try {
        t.cast<Vertex>();
        return true;
    } catch (const py::cast_error &) {
        return false;
    }
}

template <class S>
inline EdgeList TargetEdges(S &s, const py::handle &t, const std::string &call) {
    EdgeList edges;
    if (py::isinstance<py::str>(t)) {
        std::string name = t.cast<std::string>();
        edges = s.NamedEdges(name);
        if (edges.empty())
            throw py::value_error(call + ": no edge named '" + name + "'");
    } else if (IsA<EdgeView>(t)) {
        const EdgeView &r = t.cast<const EdgeView &>();
        CheckPlane(s, *r.wire, call);
        edges.push_back(r.Get());
    } else if (IsA<Wire>(t) || IsA<Face>(t) || IsA<Shape>(t)) {
        std::optional<Face> copy;
        const Wire &w = IsA<Wire>(t) ? t.cast<const Wire &>()
                        : IsA<Face>(t) ? AsFace(t, copy).Edges() : t.cast<const Shape &>().Edges();
        CheckPlane(s, w, call);
        edges.assign(w.begin(), w.end());
    } else if (std::optional<Projection> p = occHooks->projected(t, s.Axes(), false)) {
        edges.assign(p->wire.begin(), p->wire.end());
        if (edges.empty())
            throw py::value_error(call + ": the shape projects to points only");
    } else {
        throw py::type_error(call + ": a target is an id, an Edge, a Wire, a Face, a Shape or an OCC shape, or (target, point)");
    }
    if (edges.empty())
        throw py::value_error(call + ": the target has no edges");
    return edges;
}

template <class S>
inline sketch::Target TargetArg(S &s, const py::handle &t, const std::string &call) {
    sketch::Target target;
    py::handle what = t;
    if ((py::isinstance<py::tuple>(t) || py::isinstance<py::list>(t)) && py::len(t) == 2) {
        py::sequence pair = py::reinterpret_borrow<py::sequence>(t);
        if (IsTarget(pair[0]) && IsPoint(pair[1])) {
            target.point = pair[1].cast<Vertex>();
            what = pair[0];
        }
    }
    target.edges = TargetEdges(s, what, call);
    if (py::isinstance<py::str>(what))
        target.name = what.cast<std::string>();
    target.key = target.name ? "name " + *target.name : "object " + std::to_string(reinterpret_cast<std::uintptr_t>(what.ptr()));
    return target;
}

template <class S>
inline sketch::Target EndArg(S &s, const py::handle &t, const std::string &call) {
    sketch::Target target;
    if (IsPoint(t)) {
        target.at = t.cast<Vertex>();
        return target;
    }
    if (py::isinstance<py::str>(t)) {
        const std::string name = t.cast<std::string>();
        if (s.NamedEdges(name).empty()) {
            target.at = s.FindPoint(name);
            if (!target.at)
                throw py::value_error(call + ": no edge or point named '" + name + "'");
            return target;
        }
    }
    return TargetArg(s, t, call);
}

template <class S>
inline std::vector<sketch::Target> TangentArg(S &s, const py::handle &t, const std::string &call) {
    if (t.is_none())
        return {};
    if ((py::isinstance<py::tuple>(t) || py::isinstance<py::list>(t)) && py::len(t) == 2) {
        py::sequence pair = py::reinterpret_borrow<py::sequence>(t);
        if (!(IsTarget(pair[0]) && IsPoint(pair[1])))
            return {TargetArg(s, pair[0], call), TargetArg(s, pair[1], call)};
    }
    return {TargetArg(s, t, call)};
}

template <class S>
inline Until UntilArg(S &s, const py::handle &t, std::optional<sketch::Target> &target, const std::string &call) {
    if (t.is_none())
        return Until::No;
    if (IsA<Until>(t))
        return t.cast<Until>();
    if (py::isinstance<py::str>(t) && (t.cast<std::string>() == "first" || t.cast<std::string>() == "last") &&
        s.NamedEdges(t.cast<std::string>()).empty())
        throw py::value_error(call + ": until='" + t.cast<std::string>() + "' names no element; until=px." + t.cast<std::string>() +
                              " goes until the " + t.cast<std::string>() + " element ahead");
    if (!IsTarget(t) && !(py::isinstance<py::tuple>(t) || py::isinstance<py::list>(t)))
        throw py::type_error(call + ": until is px.first, px.last or a target (an id, an Edge, a Wire, a Face, a Shape, an "
                             "OCC shape)");
    target = TargetArg(s, t, call);
    return Until::First;
}

template <class S>
inline Edge ReferenceEdge(S &s, const py::handle &t, const Vertex &nearPoint, bool nearGiven, const std::string &call) {
    if ((py::isinstance<py::tuple>(t) || py::isinstance<py::list>(t)) && py::len(t) == 2)
        throw py::type_error(call + ": a reference edge is an id, an Edge, a Wire, a Face, a Shape or an OCC shape; "
                             "a target with a point is for the positional ends and tangent, near= picks among edges of one name");
    if (py::isinstance<py::str>(t)) {
        std::string name = t.cast<std::string>();
        EdgeList named = s.NamedEdges(name);
        size_t n = named.size();
        if (n == 0)
            throw py::value_error(call + ": no edge named '" + name + "'");
        if (n > 1 && !nearGiven && !named.IsOnOneCarrier())
            Warning() << call << ": " << n << " edges named '" << name << "', using the one nearest (" << nearPoint.x
                      << ", " << nearPoint.y << ")";
        return s.NamedEdge(name, nearPoint);
    }
    EdgeList edges = TargetEdges(s, t, call);
    if (edges.size() != 1)
        throw py::value_error(call + ": a reference is a single edge");
    return edges.front();
}

template <class S>
inline std::optional<Vertex> PointOrIdArg(S &s, const py::handle &t, const std::string &call) {
    if (t.is_none())
        return std::nullopt;
    if (py::isinstance<py::str>(t))
        return s.PointNamed(t.cast<std::string>(), call);
    return InPlane(s.Axes(), t, call).template cast<Vertex>();
}

template <class S>
inline std::optional<Vertex> CenterArg(S &wp, const py::object &center, const py::object &concentric, const Vertex &at,
                                       bool nearGiven, const std::string &call) {
    if (!center.is_none() && !concentric.is_none())
        throw py::value_error(call + ": give center or concentric, not both");
    if (concentric.is_none())
        return PointOrIdArg(wp, center, call);
    if (py::isinstance<py::str>(concentric))
        if (std::optional<Vertex> p = wp.FindPoint(concentric.cast<std::string>()))
            return p;
    if (IsTarget(concentric)) {
        Edge e = ReferenceEdge(wp, concentric, at, nearGiven, call);
        const std::optional<CircleCarrier> circle = CarrierOf(e);
        if (!circle)
            throw py::value_error(call + ": concentric needs an arc, a circle or a point");
        return circle->center;
    }
    return concentric.cast<Vertex>();
}

template <class S>
inline sketch::LineRefs LineRefsOf(S &wp, const std::string &call, std::optional<double> l, const py::object &until, const py::object &from,
                           const py::object &to, const py::object &parallel, const py::object &perpendicular, const py::object &angle,
                           const py::object &congruent, const py::object &tangent, const py::object &distance,
                           const py::object &through, const py::object &nearPoint, bool reverse) {
    sketch::LineRefs r;
    r.call = call;
    r.reverse = reverse;
    r.until = UntilArg(wp, until, r.untilTarget, r.call);
    r.nearPoint = PointOrIdArg(wp, nearPoint, r.call);
    r.through = PointOrIdArg(wp, through, r.call);
    const Vertex at = r.nearPoint.value_or(wp.Position());
    r.length = l;
    if (!from.is_none())
        r.from = EndArg(wp, from, r.call);
    if (!to.is_none())
        r.to = EndArg(wp, to, r.call);
    r.tangent = TangentArg(wp, tangent, r.call);
    if (!parallel.is_none())
        r.parallel = ReferenceEdge(wp, parallel, at, r.nearPoint.has_value(), r.call);
    if (!perpendicular.is_none())
        r.perpendicular = ReferenceEdge(wp, perpendicular, at, r.nearPoint.has_value(), r.call);
    if (!congruent.is_none())
        r.congruent = ReferenceEdge(wp, congruent, at, r.nearPoint.has_value(), r.call);
    if (py::isinstance<py::tuple>(angle) || py::isinstance<py::list>(angle)) {
        py::sequence a = angle;
        if (a.size() != 2)
            throw py::value_error(r.call + ": angle is degrees or (degrees, reference)");
        r.angle = a[0].cast<double>();
        r.angleRef = ReferenceEdge(wp, a[1], at, r.nearPoint.has_value(), r.call);
    } else if (!angle.is_none()) {
        r.angle = angle.cast<double>();
    }
    if (py::isinstance<py::tuple>(distance) || py::isinstance<py::list>(distance)) {
        py::sequence d = distance;
        const std::optional<Vertex> p = d.size() == 2 ? PointOrIdArg(wp, d[1], r.call) : std::nullopt;
        if (!p)
            throw py::value_error(r.call + ": distance is a length or (length, point)");
        r.SetDistanceTowards(d[0].cast<double>(), *p);
    } else if (!distance.is_none())
        r.SetDistanceFrom(distance.cast<double>(), at);
    return r;
}

template <class S>
inline sketch::ArcRefs ArcRefsOf(S &wp, const std::string &call, std::optional<double> r, std::optional<double> ang,
                         const py::object &center, const py::object &concentric, const py::object &congruent,
                         const py::object &tangent, const py::object &through, const py::object &until, const py::object &from,
                         const py::object &to, const py::object &nearPoint, bool reverse) {
    sketch::ArcRefs a;
    a.call = call;
    a.reverse = reverse;
    a.until = UntilArg(wp, until, a.untilTarget, a.call);
    a.radius = r;
    a.sweep = ang;
    a.nearPoint = PointOrIdArg(wp, nearPoint, a.call);
    a.through = PointOrIdArg(wp, through, a.call);
    const Vertex at = a.nearPoint.value_or(wp.Position());
    a.center = CenterArg(wp, center, concentric, at, a.nearPoint.has_value(), a.call);
    if (!congruent.is_none())
        a.congruent = ReferenceEdge(wp, congruent, at, a.nearPoint.has_value(), a.call);
    if (!tangent.is_none() && IsPoint(tangent)) {
        a.start = tangent.cast<Vertex>();
        if (!(Norm(*a.start) > 0.0))
            throw py::value_error(a.call + ": the direction tangent=(dh, dv) has no length");
    } else
        a.tangent = TangentArg(wp, tangent, a.call);
    if (!from.is_none())
        a.from = EndArg(wp, from, a.call);
    if (!to.is_none())
        a.to = EndArg(wp, to, a.call);
    return a;
}

template <class S>
inline sketch::CircleRefs CircleRefsOf(S &wp, std::optional<double> r, const py::object &center, const py::object &concentric,
                               const py::object &congruent, const py::object &through, const py::object &tangent,
                               const py::object &nearPoint, bool reverse) {
    sketch::CircleRefs refs;
    refs.call = CallText("Circle", {{"r", r ? py::object(py::float_(*r)) : py::object(py::none())}, {"center", center},
                                 {"concentric", concentric}, {"coincident", congruent}, {"through", through},
                                 {"tangent", tangent}, {"near", nearPoint},
                                 {"reverse", reverse ? py::object(py::bool_(true)) : py::object(py::none())}});
    refs.radius = r;
    refs.reverse = reverse;
    refs.nearPoint = PointOrIdArg(wp, nearPoint, refs.call);
    const Vertex at = refs.nearPoint.value_or(wp.Position());
    refs.center = CenterArg(wp, center, concentric, at, refs.nearPoint.has_value(), refs.call);
    if (!congruent.is_none())
        refs.congruent = ReferenceEdge(wp, congruent, at, refs.nearPoint.has_value(), refs.call);
    refs.through = PointOrIdArg(wp, through, refs.call);
    refs.tangent = TangentArg(wp, tangent, refs.call);
    return refs;
}

template <class S>
inline sketch::SymRef SymRefOf(S &s, const py::handle &t, const Vertex &nearPoint, bool nearGiven,
                               const std::string &call) {
    if (py::isinstance<py::str>(t))
        if (std::optional<Vertex> p = s.FindPoint(t.cast<std::string>()))
            return *p;
    if (std::optional<std::vector<double>> p = AsPoint(t); p && p->size() == 2)
        return Vertex{(*p)[0], (*p)[1]};
    Edge e = ReferenceEdge(s, t, nearPoint, nearGiven, call);
    if (const Line *l = std::get_if<Line>(&e))
        return *l;
    if (const std::optional<CircleCarrier> c = CarrierOf(e))
        return *c;
    throw py::value_error(call + ": a spline has no symmetry line");
}
