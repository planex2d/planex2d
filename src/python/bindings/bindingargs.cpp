// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

void AddShapes(const py::handle &h, ShapeList &out, bool compounds) {
    if (IsA<Shape>(h))
        out.push_back(h.cast<Shape>());
    else if (std::optional<Face> copy; IsA<Face>(h))
        out.emplace_back(AsFace(h, copy));
    else if (IsA<Wire>(h))
        out.push_back(WireShape(h.cast<const Wire &>()));
    else if (py::hasattr(h, "parts")) {
        if (!compounds)
            throw py::type_error(NoCompoundBooleans);
        for (const py::handle &p : h.attr("parts"))
            AddShapes(p, out);
    } else
        throw py::type_error(compounds ? "expected a Face, Shape, Wire or Compound" : "expected a Face, Shape or Wire");
}

std::optional<Vector3> XYZArg(const py::handle &h, const std::string &call) {
    if (h.is_none())
        return std::nullopt;
    if (std::optional<Vector3> p = occHooks->point(h))
        return p;
    if (std::optional<Vector3> d = occHooks->direction(h))
        return d;
    try {
        const std::array<double, 3> a = h.cast<std::array<double, 3>>();
        return Vector3{a[0], a[1], a[2]};
    } catch (const py::cast_error &) {
        throw py::type_error(call + ": expected a 3d point or direction, e.g. (x, y, z) or occ.X");
    }
}

py::object InPlane(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    std::optional<Vector3> p = occHooks->point(h);
    if (!p && (py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h)) && py::len(h) == 3 && IsNumber(h[py::int_(0)]) &&
             IsNumber(h[py::int_(1)]) && IsNumber(h[py::int_(2)]))
        p = Vector3{h[py::int_(0)].cast<double>(), h[py::int_(1)].cast<double>(), h[py::int_(2)].cast<double>()};
    if (!p)
        return py::reinterpret_borrow<py::object>(h);
    if (!axes.IsOn(*p))
        throw py::value_error(call + ": the point " + py::repr(h).cast<std::string>() + " is " +
                              py::repr(py::float_(std::abs(axes.Height(*p)))).cast<std::string>() +
                              " from the plane; take it into the plane with plane.ToHV(point)");
    return Tuple2(axes.ToHV(*p));
}

SplineStyle StyleArg(const py::handle &h, const std::string &call) {
    if (h.is_none())
        return config::state->spline_style;
    const std::string v = py::isinstance<py::str>(h) ? h.cast<std::string>() : "";
    if (v == "averaged")
        return SplineStyle::Averaged;
    if (v == "clamped")
        return SplineStyle::Clamped;
    if (v == "quintic")
        return SplineStyle::Quintic;
    if (v == "px" || v == "occ")
        throw py::value_error(call + ": style '" + v + "' is now '" + (v == "px" ? "averaged" : "clamped") + "'");
    throw py::value_error(call + ": style " + py::repr(h).cast<std::string>() + "; it is 'quintic', 'averaged' or 'clamped'");
}

std::map<size_t, TangentSetting<double>> TangentSettingsArg(const std::optional<std::map<size_t, double>> &tensions, const std::optional<std::map<size_t, double>> &radii,
                                                            const std::string &call) {
    std::map<size_t, TangentSetting<double>> out;
    for (const auto &[i, t] : tensions.value_or(std::map<size_t, double>{}))
        out[i].tension = t;
    for (const auto &[i, r] : radii.value_or(std::map<size_t, double>{})) {
        if (r == 0.0 || std::isnan(r))
            throw py::value_error(call + ": the curvature radius at point " + std::to_string(i) + " is " + py::repr(py::float_(r)).cast<std::string>() +
                                  "; it is nonzero, math.inf for straight");
        out[i].curvature = std::isinf(r) ? 0.0 : 1.0 / r;
    }
    return out;
}

AxisSystem SharedAxes(const py::iterable &items, const std::string &call, const std::string &whole, const std::string &parts) {
    std::optional<AxisSystem> axes;
    for (const py::handle &item : items) {
        const AxisSystem a = AxesArg(item, call);
        if (axes && !axes->IsClose(a))
            throw py::value_error(call + ": the " + parts + " of the " + whole + " lie on different planes; " + py::repr(item).cast<std::string>() +
                                  " is off the plane of the first");
        axes = a;
    }
    if (!axes)
        throw py::value_error(call + ": the " + whole + " is empty");
    return *axes;
}

AxisSystem AxesArg(const py::handle &h, const std::string &call) {
    if (h.is_none())
        return AxisSystem();
    if (IsA<AxisSystem>(h))
        return h.cast<AxisSystem>();
    if (std::optional<AxisSystem> a = occHooks->axes(h, call))
        return *a;
    if (IsA<Shape>(h))
        return h.cast<const Shape &>().Axes();
    if (std::optional<Face> copy; IsA<Face>(h))
        return AsFace(h, copy).Axes();
    if (IsA<Wire>(h))
        return h.cast<const Wire &>().Axes();
    if (py::hasattr(h, "_axes"))
        return h.attr("_axes").cast<AxisSystem>();
    if (py::hasattr(h, "parts"))
        return SharedAxes(h.attr("parts"), call, "compound", "parts");
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h))
        return SharedAxes(py::reinterpret_borrow<py::iterable>(h), call, "list", "items");
    throw py::type_error(call + ": plane is a Plane, netgen Axes, a Sketch, Shape, Face, Wire or Compound, a planar OCC shape, or a list of these");
}

std::string CallText(const std::string &function, const std::vector<std::pair<const char *, py::handle>> &args) {
    std::string text = function + "(";
    bool first = true;
    for (const auto &[name, value] : args) {
        if (value.is_none())
            continue;
        bool plain = py::isinstance<py::str>(value) || py::isinstance<py::int_>(value) || py::isinstance<py::float_>(value) ||
                     py::isinstance<py::tuple>(value) || py::isinstance<py::list>(value);
        const std::string shown = IsA<Until>(value) ? std::string(value.cast<Until>() == Until::Last ? "px.last" : "px.first")
                                  : plain ? py::repr(value).cast<std::string>()
                                          : "<" + py::type::of(value).attr("__name__").cast<std::string>() + ">";
        text += (first ? "" : ", ") + (name ? std::string(name) + "=" : std::string()) + shown;
        first = false;
    }
    return text + ")";
}

void CollectEdges(const py::handle &h, const std::string &call, EdgeList &out, std::optional<AxisSystem> &axes) {
    auto plane = [&](const AxisSystem &a) {
        if (axes && !axes->IsClose(a))
            throw py::value_error(call + ": the edges lie on different planes; move them onto one plane first");
        axes = a;
    };
    if (IsA<planex2d::Line>(h))
        out.push_back(h.cast<planex2d::Line>());
    else if (IsA<planex2d::Arc>(h))
        out.push_back(h.cast<planex2d::Arc>());
    else if (IsA<planex2d::Spline>(h))
        out.push_back(h.cast<planex2d::Spline>());
    else if (IsA<EdgeView>(h)) {
        const EdgeView &v = h.cast<const EdgeView &>();
        out.push_back(v.Get());
        plane(v.wire->Axes());
    } else if (IsA<Wire>(h)) {
        const Wire &w = h.cast<const Wire &>();
        if (w.Empty())
            return;
        plane(w.Axes());
        for (const Edge &e : w)
            out.push_back(e);
    } else
        throw py::type_error(call + ": takes edges (px.Line, px.Arc, px.Spline, edge views) and wires, not " +
                             py::str(py::type::handle_of(h).attr("__name__")).cast<std::string>());
}

EdgeList EdgesArg(const py::object &edges, const std::string &call, std::optional<AxisSystem> &axes) {
    EdgeList out;
    if (IsEdgeLike(edges)) {
        CollectEdges(edges, call, out, axes);
        return out;
    }
    if (!py::isinstance<py::iterable>(edges) || py::isinstance<py::str>(edges))
        throw py::type_error(call + ": takes a list of edges (px.Line, px.Arc, px.Spline, edge views) and wires, not " +
                             py::str(py::type::handle_of(edges).attr("__name__")).cast<std::string>());
    for (const py::handle &h : edges)
        CollectEdges(h, call, out, axes);
    return out;
}

// start and end equally far from the center within tol (default: the rounding the Arc constructor recenters within)
void CheckArcRadii(const std::string &call, const Vertex &start, const Vertex &end, const Vertex &center, OptDouble tol) {
    if (tol && !(std::isfinite(*tol) && *tol >= 0.0))
        throw py::value_error(call + "(tol): tol must be at least 0 and finite");
    const double rs = Norm(start - center), re = Norm(end - center);
    const double t = tol ? *tol : Tol(NormInf(center) + std::max(rs, re));
    if (std::abs(re - rs) > t) {
        std::ostringstream text;
        text << std::setprecision(12) << call << ": the start is " << rs << " and the end " << re
             << " from the center, they differ by more than tol = " << t;
        throw py::value_error(text.str());
    }
}

AutoConstraint AutoConstraintOptions(const py::dict &d, AutoConstraint base, const std::string &key) {
    std::string names;
    for (const auto &[name, on] : autoConstraintKeys)
        names += (names.empty() ? "" : ", ") + std::string(name);
    for (const auto &[k, v] : d) {
        const std::string name = py::str(k);
        const auto it = std::find_if(std::begin(autoConstraintKeys), std::end(autoConstraintKeys),
                                     [&](const auto &entry) { return name == entry.first; });
        if (it == std::end(autoConstraintKeys))
            throw py::type_error(key + ": no relation '" + name + "' (" + names + ")");
        if (!py::isinstance<py::bool_>(v))
            throw py::type_error(key + ": '" + name + "' is True or False, not " + py::repr(v).cast<std::string>());
        base.*(it->second) = v.cast<bool>();
    }
    return base;
}

std::optional<Continuity> PropagationOf(const py::handle &h, const std::string &call) {
    if (IsA<MinimalValue>(h))
        return std::nullopt;
    if (py::isinstance<sketch::Kind>(h))
        switch (h.cast<sketch::Kind>()) {
            case sketch::Kind::Coincident:
                return Continuity::Point;
            case sketch::Kind::Tangent:
                return Continuity::Tangent;
            case sketch::Kind::Curvature:
                return Continuity::Curvature;
            default:
                break;
        }
    throw py::value_error(call + "(propagate=" + py::repr(h).cast<std::string>() +
                          "): px.Minimal, px.Coincident, px.Tangent or px.Curvature");
}

std::optional<Thing> AsThing(const py::handle &h) {
    if (std::optional<Face> copy; IsA<Face>(h))
        return ThingOf(AsFace(h, copy));
    if (IsA<Shape>(h))
        return ThingOf(h.cast<const Shape &>());
    if (IsA<Wire>(h))
        return ThingOf(h.cast<const Wire &>());
    if (IsA<EdgeView>(h))
        return ThingOf(h.cast<const EdgeView &>().AsWire());
    if (IsA<FaceView>(h))
        return ThingOf(h.cast<const FaceView &>().Copy());
    if (IsA<VertexView>(h))
        return ThingOf(h.cast<const VertexView &>());
    return std::nullopt;
}

std::optional<std::vector<double>> AsPoint(const py::handle &h) {
    if (!py::isinstance<py::sequence>(h) || py::isinstance<py::str>(h) || IsA<VertexView>(h))
        return std::nullopt;
    if (py::hasattr(h, "_xyz"))
        return h.attr("_xyz")().cast<std::vector<double>>();
    try {
        std::vector<double> p = h.cast<std::vector<double>>();
        for (double x : p)
            IsFinite("point", {x});
        if (p.size() == 2 || p.size() == 3)
            return p;
    } catch (const py::cast_error &) {
    }
    return std::nullopt;
}

std::optional<PointRows> PointBatch(const py::handle &h, const std::string &call) {
    bool nested = false;
    if ((py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) && py::len(h) > 0) {
        const py::object first = py::reinterpret_borrow<py::sequence>(h)[0];
        nested = py::isinstance<py::list>(first) || py::isinstance<py::tuple>(first) || py::isinstance<py::array>(first);
    }
    if (!nested && !py::isinstance<py::array>(h))
        return std::nullopt;
    PointRows a = PointRows::ensure(h);
    if (!a)
        throw py::value_error(call + ": many points are an (n, 2) or (n, 3) array of numbers");
    if (a.ndim() == 1 && (a.shape(0) == 2 || a.shape(0) == 3))
        return std::nullopt;
    if (a.ndim() != 2 || (a.shape(1) != 2 && a.shape(1) != 3))
        throw py::value_error(call + ": a point is (h, v) or (x, y, z), many points an (n, 2) or (n, 3) array, not shape " + ShapeText(a));
    CheckRowsFinite(a, call);
    return a;
}

PointRows PlaneRows(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    const PointRows a = PointRows::ensure(h);
    if (!a || a.ndim() != 2 || a.shape(1) != 3)
        return PlaneRows(h, call);
    CheckRowsFinite(a, call);
    PointRows out(std::vector<py::ssize_t>{a.shape(0), 2});
    for (py::ssize_t i = 0; i < a.shape(0); ++i) {
        const Vertex p = HVArg(axes, py::make_tuple(a.data()[3 * i], a.data()[3 * i + 1], a.data()[3 * i + 2]), call);
        out.mutable_data()[2 * i] = p.x;
        out.mutable_data()[2 * i + 1] = p.y;
    }
    return out;
}

Wire CutterIn(const AxisSystem &axes, const py::handle &h) {
    std::optional<Wire> w;
    if (IsA<Wire>(h))
        w = h.cast<Wire>();
    else if (IsA<EdgeView>(h))
        w = h.cast<const EdgeView &>().AsWire();
    else if (std::optional<Face> copy; IsA<Face>(h))
        w = AsFace(h, copy).Edges();
    else if (IsA<Shape>(h))
        w = h.cast<const Shape &>().Edges();
    else if (py::hasattr(h, "_axes")) {
        w = Wire(h.attr("_axes").cast<AxisSystem>());
        for (const py::handle &part : h.attr("Wires")())
            for (const Edge &e : part.cast<const Wire &>())
                w->Add(e);
    }
    else if (std::optional<Projection> p = occHooks->projected(h, axes, false))
        return p->wire;
    else
        throw py::type_error("a cutter is a Wire, Edge, Face, Shape, Sketch or an OCC shape");
    if (w->Axes().IsClose(axes))
        return *w;
    return occHooks->across(Planar{Wire(w->Axes()), *w}, w->Axes(), axes);
}

TrimRule RuleOf(const AxisSystem &axes, const py::object &keepClosest, const py::object &removeClosest, bool largest, bool smallest, bool left, bool right) {
    int given = !keepClosest.is_none() + !removeClosest.is_none() + largest + smallest + left + right;
    if (given != 1)
        throw py::value_error("Trim(...): give exactly one of keep_closest_to, remove_closest_to, keep_largest, keep_smallest, "
                              "keep_left and keep_right");
    auto point = [&](const py::object &o) {
        std::optional<std::vector<double>> p = AsPoint(o);
        if (!p)
            throw py::type_error("Trim(...): a point of two plane coordinates or three in 3d");
        return p->size() == 2 ? Vertex{(*p)[0], (*p)[1]} : axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
    };
    if (!keepClosest.is_none())
        return {TrimRule::KeepClosest, point(keepClosest)};
    if (!removeClosest.is_none())
        return {TrimRule::RemoveClosest, point(removeClosest)};
    return {largest ? TrimRule::KeepLargest : smallest ? TrimRule::KeepSmallest : left ? TrimRule::KeepLeft : TrimRule::KeepRight, {}};
}
