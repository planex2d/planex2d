// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <set>
#include "binding.hpp"
#include "binding_curve.hpp"
#include "curves/interpolate.hpp"
#include "sketch/sketch.hpp"

inline sketch::Kind KindNamed(const std::string &name) {
    for (size_t k = 0; k <= (size_t)sketch::Kind::OnCircle; ++k)
        if (name == sketch::KindName(static_cast<sketch::Kind>(k)))
            return static_cast<sketch::Kind>(k);
    throw py::value_error("no constraint kind " + name);
}

// an element index, or (element, "start" | "end" | "center" | "mid")
inline sketch::Ref RefOf(const py::handle &h) {
    if (!py::isinstance<py::tuple>(h))
        return {h.cast<size_t>()};
    const py::tuple t = h.cast<py::tuple>();
    const std::string part = t[1].cast<std::string>();
    const sketch::Part p = part == "start" ? sketch::Part::Start : part == "end" ? sketch::Part::End : part == "center" ? sketch::Part::Center
                       : part == "mid" ? sketch::Part::Mid : throw py::value_error("no point " + part);
    return {t[0].cast<size_t>(), p};
}

// px.H and px.V: the axes of whichever sketch they are used in; px.origin: the global origin, projected
enum class SketchAxis { H, V, Origin };

// a drawing call's numbers and points, apart from its flags, which come last
struct CallArgs {
    std::vector<py::object> values;
    std::vector<sketch::Kind> flags;
    // px.Auto among the flags: the relations the call finds as constraints
    std::optional<AutoConstraint> autoFlag;
};

inline CallArgs Split(const py::tuple &args, const std::string &call) {
    CallArgs out;
    for (const py::handle &a : args) {
        if (IsA<sketch::Kind>(a))
            out.flags.push_back(a.cast<sketch::Kind>());
        else if (IsAuto(a)) {
            if (out.autoFlag)
                throw py::type_error(call + ": px.Auto is given twice");
            out.autoFlag = AutoRelationsOf(a.cast<const AutoValue &>());
        } else if (!out.flags.empty() || out.autoFlag)
            throw py::type_error(call + ": the flags (px.Horizontal, ..., px.Auto) come after the other arguments");
        else
            out.values.push_back(py::reinterpret_borrow<py::object>(a));
    }
    return out;
}

// a drawing call with px.Auto among its flags finds the relations of what it draws as constraints, whatever the
// sketch's auto_constraint, for that call only
template <class S>
class AutoScope {
public:
    AutoScope(S &s, const std::optional<AutoConstraint> &flag, const std::string &call) : s(s) {
        if (!flag)
            return;
        if constexpr (requires { s.autoConstraint; }) {
            saved = s.autoConstraint;
            s.autoConstraint = *flag;
            active = true;
        } else
            throw py::value_error(call + "(px.Auto): a QuickSketch has no constraints; px.Sketch(...) has them");
    }
    ~AutoScope() {
        if constexpr (requires { s.autoConstraint; })
            if (active)
                s.autoConstraint = saved;
    }
    AutoScope(const AutoScope &) = delete;
    AutoScope &operator=(const AutoScope &) = delete;

private:
    S &s;
    std::optional<AutoConstraint> saved;
    bool active = false;
};

// an id, px.H / px.V / px.origin, or a selected element or point (a one-item selection too)
inline bool IsRef(const py::handle &h) {
    return py::isinstance<py::str>(h) || IsA<SketchAxis>(h) || py::hasattr(h, "_ref_in");
}
inline std::string RefText(const py::handle &h, const std::string &call) {
    if (py::isinstance<py::str>(h))
        return h.cast<std::string>();
    if (IsA<SketchAxis>(h))
        return h.cast<SketchAxis>() == SketchAxis::H ? "H" : h.cast<SketchAxis>() == SketchAxis::V ? "V" : "px.origin";
    throw py::type_error(call + ": " + py::repr(h).cast<std::string>() + " is no id");
}
// an id, or a selected element or point reference of this sketch: its id there
// a constraint sketch (px.Sketch), not the plain one (px.QuickSketch)
template <class S>
constexpr bool HasConstraints = requires(S &s) { s.autoConstraint; };

// the one resolver of the ids a sketch call takes: a local id ('Line.2'), the sketch's global one ('Sketch.1.Line.2'),
// px.H / px.V / px.origin, or a selected element or point of this sketch, as its local id; another sketch's id raises
template <class S>
inline std::string LocalId(S &s, const std::string &text, const std::string &call) {
    if (const std::string &own = s.AssignedId(); !own.empty() && text.starts_with(own + "."))
        return s.FromEnd(text.substr(own.size() + 1));
    const std::string kind = s.IdKind() + ".";
    if (text.starts_with(kind)) {
        const size_t digits = text.find_first_not_of("0123456789", kind.size());
        if (digits != std::string::npos && digits > kind.size() && text[digits] == '.')
            throw py::value_error(call + ": '" + text + "' belongs to " + text.substr(0, digits) + ", this is " + s.Id());
    }
    return s.FromEnd(text);
}
template <class S>
inline std::string RefIn(S &s, const py::handle &h, const std::string &call) {
    if (py::hasattr(h, "_ref_in"))
        return h.attr("_ref_in")(py::cast(&s, py::return_value_policy::reference)).template cast<std::string>();
    if (py::isinstance<py::str>(h))
        return LocalId(s, h.cast<std::string>(), call);
    return RefText(h, call);
}
// one id or several: a list of them or a selection
template <class S>
inline std::vector<std::string> RefsIn(S &s, const py::handle &h, const std::string &call) {
    std::vector<std::string> out;
    if (py::hasattr(h, "_items")) {
        for (const py::handle &x : h.attr("_items"))
            out.push_back(RefIn(s, x, call));
    } else if (IsRef(h))
        out.push_back(RefIn(s, h, call));
    else
        for (const py::handle &x : h)
            for (std::string &id : RefsIn(s, x, call))
                out.push_back(std::move(id));
    return out;
}
// what a call was given, with every id and selected element or point in it as the sketch's local id
template <class S>
inline py::object Resolved(S &s, const py::handle &h, const std::string &call) {
    if (py::isinstance<py::str>(h) || py::hasattr(h, "_ref_in"))
        return py::str(RefIn(s, h, call));
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) {
        py::list out;
        for (const py::handle &x : h)
            out.append(Resolved(s, x, call));
        return py::isinstance<py::tuple>(h) ? py::object(py::tuple(out)) : py::object(out);
    }
    return py::reinterpret_borrow<py::object>(h);
}
// px.origin is made in a sketch when it is first used there
template <class S>
inline void Prepare(S &s, const py::handle &h) {
    if (IsA<SketchAxis>(h) && h.cast<SketchAxis>() == SketchAxis::Origin)
        s.GlobalOrigin();
    else if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h) || py::isinstance<py::args>(h))
        for (const py::handle &x : h)
            Prepare(s, x);
    else if (py::isinstance<py::dict>(h))
        for (const auto &[k, v] : h.cast<py::dict>())
            Prepare(s, v);
}

// sk.pen: where the pen is and the direction it draws in, when read
struct SketchPen {
    Vertex position, direction;
};
// sk.pen where a point is taken is its position, where a direction is taken (direction) its direction
inline py::object Unpen(const py::handle &h, bool direction) {
    if (IsA<SketchPen>(h)) {
        const SketchPen &p = h.cast<const SketchPen &>();
        return Tuple2(direction ? p.direction : p.position);
    }
    if (py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h)) {
        py::list out;
        for (const py::handle &x : h)
            out.append(Unpen(x, false));
        return py::isinstance<py::tuple>(h) ? py::object(py::tuple(out)) : py::object(out);
    }
    return py::reinterpret_borrow<py::object>(h);
}
// every (x, y, z) point in h as the plane's (h, v); the top level is no point where h is a direction
inline py::object InPlaneAll(const AxisSystem &axes, const py::handle &h, bool direction, const std::string &call) {
    if (!direction) {
        const py::object p = InPlane(axes, h, call);
        if (!p.is(h))
            return p;
    }
    if (!(py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h)))
        return py::reinterpret_borrow<py::object>(h);
    py::list out;
    for (const py::handle &x : h)
        out.append(InPlaneAll(axes, x, false, call));
    return py::isinstance<py::tuple>(h) ? py::object(py::tuple(out)) : py::object(out);
}
inline py::tuple UnpenArgs(const AxisSystem &axes, const py::args &args, const std::string &call) {
    py::list out;
    for (const py::handle &x : args)
        out.append(InPlaneAll(axes, Unpen(x, false), false, call));
    return py::tuple(out);
}
// the keywords with sk.pen as a point, a direction (tangent=, dir=) or a line along it (parallel=, perpendicular=)
template <class S>
inline py::dict UnpenKeywords(const S &s, const py::kwargs &kw, const std::string &call) {
    py::dict out;
    for (const auto &[k, v] : kw) {
        const std::string key = k.cast<std::string>();
        if ((key == "parallel" || key == "perpendicular") && IsA<SketchPen>(v)) {
            const SketchPen &p = v.cast<const SketchPen &>();
            Wire along(s.Axes());
            along.Add(Line(p.position, p.position + p.direction));
            out[k] = py::cast(along);
        } else {
            const bool direction = key == "tangent" || key == "dir";
            const py::object v1 = Unpen(v, direction);
            const bool point = direction || key == "center" || key == "through" || key == "concentric" || key == "distance";
            out[k] = point ? InPlaneAll(s.Axes(), v1, direction, call) : v1;
        }
    }
    return out;
}

inline Vertex PointArg(const py::handle &h, const std::string &call) {
    try {
        return h.cast<Vertex>();
    } catch (const py::cast_error &) {
    }
    try {
        const auto a = h.cast<std::array<double, 2>>();
        IsFinite(call.c_str(), {a[0], a[1]});
    } catch (const py::cast_error &) {
    }
    throw py::type_error(call + ": " + py::repr(h).cast<std::string>() + " is no point (h, v)");
}
inline Vertex PointArg(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    return PointArg(InPlane(axes, h, call), call);
}
// a point, or two numbers as a tuple or list (which PointArg then checks for finiteness)
inline bool IsPointLike(const py::handle &h) {
    if (IsPoint(h))
        return true;
    if (!(py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h)) || py::len(h) != 2)
        return false;
    const py::sequence q = py::reinterpret_borrow<py::sequence>(h);
    return IsNumber(q[0]) && IsNumber(q[1]);
}
inline bool Numbers(const CallArgs &a, size_t n) {
    if (a.values.size() != n)
        return false;
    for (const py::object &v : a.values)
        if (!IsNumber(v))
            return false;
    return true;
}

// a list or tuple of planex or OCC objects, not the numbers of a point
inline bool IsObjectList(const py::handle &h) {
    return (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) && py::len(h) > 0 && !IsNumber(h.cast<py::sequence>()[0]) &&
           !IsA<Vertex>(h.cast<py::sequence>()[0]);
}

inline std::optional<Vertex> StartArg(const py::handle &h, const std::string &call) {
    const auto shown = [&] { return py::repr(h).cast<std::string>(); };
    if (py::hasattr(h, "_hv"))
        return PointArg(h, call);
    if (py::isinstance<py::str>(h))
        throw py::type_error(call + "(" + shown() + "): a string is no start point and no plane");
    if (IsNumber(h))
        throw py::type_error(call + "(" + shown() + ", ...): the start point is one argument, (h, v)");
    const bool sequence = py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h) || py::isinstance<py::array>(h);
    if (IsObjectList(h))
        return std::nullopt;
    if (occ->point(h) || (sequence && py::len(h) == 3))
        return PointArg(AxisSystem(), h, call);
    if (sequence || IsA<Vertex>(h))
        return PointArg(h, call);
    return std::nullopt;
}

template <class S>
inline void GiveId(S &s, const std::optional<std::string> &id) {
    if (id)
        CheckUserId(*id);
    id ? s.SetId(*id) : (void)s.Id();
}

// copy=True: the one sketch or geometry to copy; only id= goes with it (and auto_constraint= for a Sketch), the rest is a
// new sketch's, not a copy's
inline py::object SketchToCopy(const std::string &call, const py::tuple &args, const py::object &autoConstraint,
                               const std::optional<bool> &errorOnBranchChange, const std::string &mode, const py::object &dir) {
    if (args.size() != 1)
        throw py::type_error(call + "(sketch, copy=True): got " + std::to_string(args.size()) + " arguments; copy=True takes the one sketch or geometry to copy, " +
                             call + "(sketch, copy=True)");
    const bool sketch = call == "Sketch";
    if (!dir.is_none() || (AsksAutoConstraint(autoConstraint) && !sketch) || errorOnBranchChange || mode != "normal")
        throw py::type_error(call + (sketch ? "(sketch, copy=True): only id= and auto_constraint= go with copy=True; dir=, "
                                              "error_on_branch_change= and mode= belong to a new sketch, leave them out"
                                            : "(sketch, copy=True): only id= goes with copy=True; dir=, auto_constraint=, "
                                              "error_on_branch_change= and mode= belong to a new sketch, leave them out"));
    return py::reinterpret_borrow<py::object>(args[0]);
}

// a projection into the sketch's plane, with whether it stays where it is: the edges of an OCC shape do (as Project
// has them), those of a planex object move like drawn ones
struct CopiedGeometry {
    Projection projection;
    bool hold;
};

// the edges and points of a planex or OCC object, or of a list of them; none for a Plane or Axes
inline void GeometryOf(const py::handle &h, const AxisSystem &axes, std::vector<CopiedGeometry> &out) {
    if (IsA<Wire>(h))
        out.push_back({{h.cast<const Wire &>(), {}}, false});
    else if (std::optional<Face> copy; IsA<Face>(h))
        out.push_back({{AsFace(h, copy).Edges(), {}}, false});
    else if (IsA<Shape>(h))
        out.push_back({{h.cast<const Shape &>().Edges(), {}}, false});
    else if (std::optional<Projection> p = occ->projected(h, axes, true))
        out.push_back({std::move(*p), true});
    else if (py::hasattr(h, "parts"))
        for (const py::handle &part : h.attr("parts"))
            GeometryOf(part, axes, out);
    else if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h))
        for (const py::handle &item : h)
            GeometryOf(item, axes, out);
}

// QuickSketch(objects, copy=True): an empty sketch on their plane with their edges drawn in
inline Sketch QuickSketchOfObjects(const py::object &objects, const std::string &call) {
    Sketch s(AxesArg(objects, call));
    std::vector<CopiedGeometry> geometry;
    GeometryOf(objects, s.Axes(), geometry);
    if (geometry.empty())
        Warning() << call << "(" << py::repr(objects).cast<std::string>() << ", copy=True): nothing to copy, an empty sketch";
    for (const CopiedGeometry &g : geometry)
        s.AddProjection(g.projection, false, std::nullopt, "Projection", std::nullopt, g.hold);
    return s;
}
// the one Wire, Face or Shape sk.Add takes, as a wire on its plane; a list or compound may overlap itself: glued first
inline Wire OneGeometry(const py::object &geometry, const std::string &call) {
    if (IsA<Wire>(geometry))
        return geometry.cast<const Wire &>();
    if (std::optional<Face> copy; IsA<Face>(geometry))
        return AsFace(geometry, copy).Edges();
    if (IsA<Shape>(geometry))
        return geometry.cast<const Shape &>().Edges();
    if (py::isinstance<py::list>(geometry) || py::isinstance<py::tuple>(geometry) || py::hasattr(geometry, "parts"))
        throw py::type_error(call + ": one Wire, Face or Shape; the edges of a list or compound may overlap, glue them into one "
                                    "shape first: sk." + call + "(px.Glue(parts))");
    if (occ->isShape(geometry))
        throw py::type_error(call + ": an OCC shape is projected in: sk.Project(shape, construction=False)");
    throw py::type_error(call + ": a Wire, Face or Shape, not " + py::str(py::type::of(geometry).attr("__name__")).cast<std::string>());
}
// w with its coordinates in the axes of a plane it lies on (its own axes may be placed or turned in it, or face the other
// way); raises when it lies on another plane
inline Wire InPlaneOf(Wire w, const AxisSystem &axes, const std::string &call) {
    const AxisSystem &a = w.Axes();
    if (a.IsClose(axes))
        return w;
    const Vector3 &n = axes.Normal();
    const double angle = config::state->axes_angle;
    const bool same = AngleBetween(a.Normal(), n) <= angle, flipped = AngleBetween(a.Normal(), n * -1.0) <= angle;
    if (!(same || flipped) || std::abs(Dot(a.Origin() - axes.Origin(), n)) > config::state->axes_distance)
        throw py::value_error(call + ": the geometry lies on another plane than the sketch");
    const Vertex o = axes.ToHV(a.Origin()), x = axes.ToHV(a.Origin() + a.XDirection()) - o;
    if (flipped)
        w.Mirror(Vertex{0.0, 0.0}, Vertex{1.0, 0.0});
    w.Rotate(Vertex{0.0, 0.0}, Degrees(std::atan2(x.y, x.x)));
    w.Translate(o);
    w.SetAxes(axes);
    return w;
}
// a QuickSketch of a constraint sketch (set where px.Sketch is bound): none if h is no constraint sketch
inline py::object (*quickOfConstraintSketch)(const py::handle &h) = nullptr;

template <class S>
inline S NewSketch(const std::string &call, const py::tuple &args, const py::object &axes, const py::object &autoConstraint, const std::optional<bool> &errorOnBranchChange,
                   const std::string &mode, const std::optional<std::string> &id, const py::object &dir) {
    const std::optional<Vertex> start = args.empty() ? std::nullopt : StartArg(args[0], call);
    if (!args.empty() && !start && !axes.is_none())
        throw py::type_error(call + ": the plane is given twice, as the first argument and as plane=");
    const CallArgs rest = Split(args.empty() ? py::tuple() : py::tuple(args[py::slice(1, (py::ssize_t)args.size(), 1)]), call);
    if (!rest.values.empty() || (!start && !rest.flags.empty()))
        throw py::type_error(call + "((h, v), flags..., dir=(dh, dv)) starts at (h, v), " + call + "(plane) draws on the plane");
    if (rest.autoFlag)
        throw py::type_error(call + "(px.Auto): a new sketch draws nothing yet; auto_constraint= sets it for the sketch, px.Auto goes "
                                    "with a drawing call");
    if (!dir.is_none() && !start)
        throw py::type_error(call + "(dir=...): dir= goes with a start point, " + call + "((h, v), dir=(dh, dv))");
    S s(AxesArg(start || args.empty() ? axes : py::object(args[0]), call));
    if constexpr (requires { s.autoConstraint; }) {
        s.autoConstraint = AutoConstraintOf(autoConstraint, call);
        s.SetMode(mode);
        if (errorOnBranchChange)
            s.errorOnBranchChange = *errorOnBranchChange;
    } else if (AsksAutoConstraint(autoConstraint) || mode == "strict")
        throw py::value_error(call + (AsksAutoConstraint(autoConstraint) ? "(auto_constraint=True)" : "(mode='strict')") +
                              ": a QuickSketch has no constraints; px.Sketch(...) has them");
    GiveId(s, id);
    if (start) {
        s.MoveTo(*start, rest.flags);
        if (!dir.is_none()) {
            const Vertex d = PointArg(dir, call + "(dir=)");
            if (!(Norm(d) > 0.0))
                throw py::value_error(call + "(dir=): the direction has no length");
            s.Direction(d.x, d.y);
        }
    }
    return s;
}

template <class S>
inline std::vector<std::string> CutterIds(S &s, const py::object &b, const std::string &call) {
    return b.is_none() ? std::vector<std::string>{} : RefsIn(s, b, call);
}

template <class T>
inline const char *KwType() {
    if constexpr (std::is_same_v<T, bool>)
        return "True or False";
    else if constexpr (std::is_same_v<T, int>)
        return "an int";
    else if constexpr (std::is_same_v<T, double>)
        return "a number";
    else if constexpr (std::is_same_v<T, std::string>)
        return "a string";
    else
        return "a list of numbers";
}
template <class T>
inline std::optional<T> KwOpt(const py::dict &kw, const char *key) {
    if (!kw.contains(key) || kw[key].is_none())
        return std::nullopt;
    try {
        return kw[key].cast<T>();
    } catch (const py::cast_error &) {
        throw py::type_error(std::string(key) + "=" + py::repr(kw[key]).cast<std::string>() + ": " + key + " is " + KwType<T>());
    }
}

// an id the user gives, checked against the id rule (automatic and derived ids are made by the sketch)
inline std::optional<std::string> UserId(const std::optional<std::string> &id) {
    if (id)
        CheckUserId(*id);
    return id;
}

// id= and the properties (name=, maxh=, ...) of a drawing call; every other keyword must be one of its helpers
inline sketch::Options OptionsOf(const py::dict &kw) {
    if (kw.empty())
        return {};
    return {UserId(KwOpt<std::string>(kw, "id")),
            Props(KwOpt<std::string>(kw, "name"), KwOpt<double>(kw, "maxh"), KwOpt<std::vector<double>>(kw, "col"),
                  KwOpt<double>(kw, "minh"), KwOpt<double>(kw, "hpref"), KwOpt<int>(kw, "layer"),
                  KwOpt<std::vector<double>>(kw, "partition")),
            KwOpt<bool>(kw, "construction").value_or(false)};
}
// a sketch has no near=: a point goes with the argument it decides
inline const char *NEAR_LINE = "a point goes with the argument it decides: (target, point) as from or to, "
                               "tangent=(target, point), distance=(d, point); reverse=True takes the other solution";
inline const char *NEAR_CIRCLE = "a point goes with the argument it decides: tangent=(target, point); reverse=True takes "
                                 "the other solution";
inline void NoMoreKeywords(const py::kwargs &kw, const std::string &call, const std::string &instead) {
    for (const auto &[k, v] : kw) {
        const std::string key = k.cast<std::string>();
        if (key == "near")
            throw py::type_error(call + "(near=...): a Sketch has no near=; " + instead);
        throw py::type_error(call + "() got an unexpected keyword argument '" + key + "'");
    }
}

// a, or (a, point) with the point ((h, v) or a point id) picking a piece of a
template <class S>
inline std::pair<std::string, std::optional<Vertex>> PieceArg(S &s, const py::object &a, const std::string &call) {
    if (!IsRef(a) && (py::isinstance<py::tuple>(a) || py::isinstance<py::list>(a)) && py::len(a) == 2 && IsRef(a[py::int_(0)])) {
        const py::object p = a[py::int_(1)];
        return {RefIn(s, a[py::int_(0)], call), IsRef(p) ? s.Location(RefIn(s, p, call)) : PointArg(s.Axes(), p, call)};
    }
    return {RefIn(s, a, call), std::nullopt};
}

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
inline const std::vector<std::string> OptionKeys = {"id", "name", "maxh", "col", "minh", "hpref", "layer", "partition", "construction"};
template <class S>
inline py::dict HelpersOf(S &s, const py::dict &kw, const std::string &call, const std::vector<const char *> &helpers) {
    py::dict out;
    for (const auto &[k, v] : kw) {
        const std::string key = k.cast<std::string>();
        if (std::find(OptionKeys.begin(), OptionKeys.end(), key) != OptionKeys.end())
            continue;
        if (key == "near")
            throw py::type_error(call + "(near=...): a Sketch has no near=; " + (call == "Circle" ? NEAR_CIRCLE : NEAR_LINE));
        if ((call == "Line" || call == "Arc") && (key == "from_" || key == "to" || key == "start" || key == "end"))
            throw py::type_error(call + "(" + key + "=...): the ends are positional: " + call + "(start, end, ...), or " + call +
                                 "(to, ...) from the pen");
        if (key == "congruent")
            throw py::type_error(call + "(congruent=...): congruent= is now coincident=, as the constraint px.Coincident: " +
                                 call + "(..., coincident=\"edge\")");
        if (call == "Arc" && key == "ccw")
            throw py::type_error("Arc(ccw=...): ccw is gone; an arc runs counterclockwise between two given ends, along the "
                                 "pen's direction from the pen, and reverse=True takes the other solution");
        if (call == "Arc" && key == "r")
            throw py::type_error("Arc(r=...): the radius is positional: Arc(r, sweep), Arc(to, r), Arc(start, end, r)");
        if (std::find_if(helpers.begin(), helpers.end(), [&](const char *h) { return key == h; }) == helpers.end())
            throw py::type_error(call + "() got an unexpected keyword argument '" + key + "'");
        if (!v.is_none() && !(py::isinstance<py::bool_>(v) && !v.cast<bool>()))
            out[k] = Resolved(s, v, call);
    }
    return out;
}
inline const std::vector<const char *> LineHelpers{"until", "parallel", "perpendicular", "angle", "coincident", "tangent", "distance",
                                                   "through", "reverse"};
inline const std::vector<const char *> ArcHelpers{"sweep", "center", "concentric", "coincident", "tangent", "through", "until", "reverse"};
inline const std::vector<const char *> CircleHelpers{"center", "concentric", "coincident", "through", "tangent", "reverse"};

// the sketch as the drawing helpers look things up in it, by element id: lines, arcs, circles and splines as their edges,
// the axes as lines reaching far beyond the drawing, points as named points
template <class S>
struct HelperView {
    S &s;
    double reach = 0.0;

    const AxisSystem &Axes() const { return s.Axes(); }
    Vertex Position() const { return s.Position(); }
    EdgeList NamedEdges(const std::string &id) {
        if (!s.IsElement(id))
            return {};
        const auto [e, r, angle, d] = s.ElementOf(id);
        if (e.kind == sketch::ElementKind::Axis) {
            if (reach == 0.0)
                reach = 10.0 * std::max(1.0, s.Size());
            const double t = Radians(angle);
            const Vertex n{std::cos(t), std::sin(t)}, u{-n.y, n.x};
            return {Line(n * d - u * reach, n * d + u * reach)};
        }
        if (e.kind == sketch::ElementKind::Point || e.kind == sketch::ElementKind::Pattern || e.kind == sketch::ElementKind::Offset)
            return {};
        const Wire w = s.ElementGeometry(id);
        return EdgeList(w.begin(), w.end());
    }
    // of several the one nearest near, of equally near ones the first
    Edge NamedEdge(const std::string &id, const Vertex &nearPoint) {
        std::optional<Edge> best;
        double bestDistance = 0.0;
        for (const Edge &e : NamedEdges(id)) {
            const double d = Distance(nearPoint, e);
            const double tol = Tol(std::max(NormInf(nearPoint), std::visit([](const auto &x) { return x.LengthScale(); }, e)));
            if (!best || d < bestDistance - tol) {
                best = e;
                bestDistance = d;
            }
        }
        if (!best)
            throw std::invalid_argument("no edge named '" + id + "'");
        return *best;
    }
    std::optional<Vertex> FindPoint(const std::string &id) {
        if (!s.IsElement(id))
            return std::nullopt;
        const sketch::Element &e = s.ElementOf(id).element;
        if (e.kind != sketch::ElementKind::Point)
            return std::nullopt;
        return s.Coords(e.Point());
    }
    Vertex PointNamed(const std::string &id, const std::string &call) {
        const std::optional<Vertex> p = FindPoint(id);
        if (!p)
            throw std::invalid_argument(call + ": no point named '" + id + "'");
        return *p;
    }
    // what until= meets: the drawn lines, arcs, circles and splines, then the construction ones
    EdgeList EveryEdge() {
        EdgeList drawn, construction;
        for (const auto &[kind, ids] : s.Elements())
            for (const std::string &id : kind == "Axis" ? std::vector<std::string>() : ids)
                for (const Edge &e : NamedEdges(id))
                    (s.IsConstruction(id) ? construction : drawn).push_back(e);
        drawn.insert(drawn.end(), construction.begin(), construction.end());
        return drawn;
    }
};

// a helper's value as HelperView reads it: ids of elements stay, points of elements become their coordinates, px.H /
// px.V their ids
template <class S>
inline py::object ForHelpers(S &s, const py::handle &h) {
    if (IsA<SketchAxis>(h) && h.cast<SketchAxis>() == SketchAxis::Origin)
        return Tuple2(s.Location(s.GlobalOrigin()));
    if (IsA<SketchAxis>(h))
        return py::str(h.cast<SketchAxis>() == SketchAxis::H ? "H" : "V");
    if (py::isinstance<py::str>(h)) {
        const std::string t = h.cast<std::string>();
        return s.IsElement(t) ? py::str(t) : py::object(Tuple2(s.Location(t)));
    }
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) {
        py::list out;
        for (const py::handle &x : h)
            out.append(ForHelpers(s, x));
        return py::isinstance<py::tuple>(h) ? py::object(py::tuple(out)) : py::object(out);
    }
    return py::reinterpret_borrow<py::object>(h);
}

// a helper's target as an id: an id, px.H / px.V, or the target of (target, point); none for coordinates
inline std::optional<std::string> TargetId(const py::handle &h) {
    if (IsA<SketchAxis>(h))
        return RefText(h, "");
    if (py::isinstance<py::str>(h))
        return h.cast<std::string>();
    if (py::isinstance<py::tuple>(h) && py::len(h) == 2 && (py::isinstance<py::str>(h[py::int_(0)]) || IsA<SketchAxis>(h[py::int_(0)])))
        return TargetId(h[py::int_(0)]);
    return std::nullopt;
}

// an end of id that lands on an end point of the element t it was given only lies on t: say how to share the point
template <class S>
inline void WarnLandedOnEnd(S &s, const std::string &call, const std::string &id, const std::string &t, const std::string &end,
                            const char *role) {
    if (!s.IsElement(t))
        return;
    // the placement finds a meeting at a tangent point to about sqrt(eps) only
    const double close = 1e-6 * std::max(1.0, s.Size());
    const Vertex p = s.Location(id + end);
    for (const char *part : {".start", ".end"})
        try {
            if (Norm(s.Location(t + part) - p) <= close)
                Warning() << call << "(\"" << t << "\"): its " << end.substr(1) << " lands on " << t << part << " but only lies on "
                          << t << "; \"" << t << part << "\" as " << role << " makes them one point";
        } catch (const std::invalid_argument &) {
        }
}

// the ids tangent= names, as the turtle Sketch reads it (TangentArg): a pair is two targets unless its second item is
// a point, then it is a target with its hint; a longer list is a target each
template <class S>
inline std::vector<std::string> TangentTargets(S &s, const py::object &tangent) {
    const bool sequence = py::isinstance<py::list>(tangent) || py::isinstance<py::tuple>(tangent);
    std::vector<py::object> items{tangent};
    if (sequence && py::len(tangent) == 2) {
        const py::sequence mirrored = ForHelpers(s, tangent);
        if (!(IsTarget(mirrored[0]) && IsPoint(mirrored[1])))
            items = {tangent[py::int_(0)], tangent[py::int_(1)]};
    } else if (py::isinstance<py::list>(tangent)) {
        items.clear();
        for (const py::handle &x : tangent)
            items.push_back(py::reinterpret_borrow<py::object>(x));
    }
    std::vector<std::string> targets;
    for (const py::object &x : items)
        if (const auto t = TargetId(x))
            targets.push_back(*t);
    return targets;
}

// what the helpers said, as constraints on the element just drawn
template <class S>
inline void AutoConstraints(S &s, const std::string &call, const py::dict &h, const std::string &id) {
    const auto add = [&](const std::vector<std::string> &refs, sketch::Kind k) { s.ConstrainImplied(refs, k); };
    const auto target = [&](const char *key) { return h.contains(key) ? TargetId(h[key]) : std::nullopt; };
    const bool ends = call != "Circle" && s.ElementOf(id).element.kind != sketch::ElementKind::Circle;
    if (const auto t = target("from_"); t && ends) {
        add({id + ".start", *t}, sketch::Kind::Coincident);
        WarnLandedOnEnd(s, call, id, *t, ".start", "from");
    }
    if (const auto t = target("to"); t && ends) {
        add({id + ".end", *t}, sketch::Kind::Coincident);
        WarnLandedOnEnd(s, call, id, *t, ".end", "to");
    }
    if (h.contains("until")) {
        const std::vector<std::string> on = s.Through(s.Location(id + ".end"), id);
        if (!on.empty())
            add({id + ".end", on.front()}, sketch::Kind::Coincident);
    }
    if (const auto t = target("parallel"))
        add({*t, id}, sketch::Kind::Parallel);
    if (const auto t = target("perpendicular"))
        add({*t, id}, sketch::Kind::Perpendicular);
    if (const auto t = target("coincident"))
        add({id, *t}, sketch::Kind::Coincident);
    if (const auto t = target("through"))
        add({*t, id}, sketch::Kind::Coincident);
    if (const auto t = target("center"))
        add({id + ".center", *t}, sketch::Kind::Coincident);
    if (const auto t = target("concentric"))
        add({id, *t}, sketch::Kind::Concentric);
    if (h.contains("tangent"))
        for (const std::string &t : TangentTargets(s, h["tangent"])) {
            add({id, t}, sketch::Kind::Tangent);
            if (ends)
                for (const char *end : {".start", ".end"}) {
                    const std::vector<std::string> on = s.Through(s.Location(id + end), id);
                    if (std::find(on.begin(), on.end(), t) != on.end())
                        add({id + end, t}, sketch::Kind::Coincident);
                }
        }
}

// the magnitudes a helped call works with: the pen, what it draws and every edge and point it refers to
struct Magnitude {
    double m = 1.0;
    void Add(const Vertex &p) { m = std::max(m, NormInf(p)); }
    void Add(const Edge &e) { m = std::max(m, std::visit([](const auto &x) { return x.LengthScale(); }, e)); }
    void Add(const sketch::Target &t) {
        for (const Edge &e : t.edges)
            Add(e);
        Add(t.point);
        Add(t.at);
    }
    void Add(const std::vector<sketch::Target> &targets) {
        for (const sketch::Target &t : targets)
            Add(t);
    }
    template <class T>
    void Add(const std::optional<T> &x) {
        if (x)
            Add(*x);
    }
};

// draws what the helpers place for call, as one new element of this sketch
// the sketch's id errors name no call: the call goes in front, as the call's other errors have it
inline bool IdError(std::string_view m) {
    return m.starts_with("no element '") || m.starts_with("id '") || m.starts_with("an id must") || m.starts_with("no dimension '") ||
           m.starts_with("'");
}
template <class F, class C>
inline void NamingTheCall(F &&f, C &&callText) {
    try {
        f();
    } catch (const std::invalid_argument &e) {
        if (IdError(e.what()))
            throw std::invalid_argument(callText() + ": " + e.what());
        throw;
    }
}

template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <class T>
inline std::string ArgText(const T &x) {
    if constexpr (IsOptional<T>::value)
        return x ? ArgText(*x) : "";
    else if constexpr (std::is_same_v<T, py::args>) {
        std::string out;
        for (const py::handle &h : x)
            out += (out.empty() ? "" : ", ") + std::string(py::repr(h));
        return out;
    } else if constexpr (std::is_base_of_v<py::handle, T>)
        return x.is_none() ? "" : std::string(py::repr(x));
    else if constexpr (std::is_same_v<T, std::string>)
        return std::string(py::repr(py::str(x)));
    else
        return "";
}

// a method whose id errors show the call with its first argument: "Vertex('a'): id 'a' is taken"
template <class R, class S, class First, class... More>
inline auto NamingIds(const char *call, R (*f)(S, First, More...)) {
    return [call, f](S s, First first, More... more) -> R {
        try {
            return f(s, first, more...);
        } catch (const std::invalid_argument &e) {
            if (IdError(e.what()))
                throw std::invalid_argument(std::string(call) + "(" + ArgText(first) + "): " + e.what());
            throw;
        }
    };
}

inline const char *LINE_FORMS = "Line(l), Line(dx, dy), Line(to) or Line(start, end), then flags; the helpers place them";
inline const char *ARC_FORMS = "Arc(r, sweep), Arc(to), Arc(to, r), Arc(start, end) or Arc(start, end, r), then flags; the helpers "
                               "place them";

// a drawing call's positional values: its ends (a point, a point id, an element, (element, point), a selected element or
// point), then its sizes
template <class S>
inline void EndsAndSizes(S &s, const CallArgs &a, const std::string &call, const char *forms, std::vector<py::object> &ends,
                         std::vector<double> &sizes) {
    for (const py::object &v : a.values) {
        if (IsNumber(v)) {
            sizes.push_back(v.cast<double>());
            continue;
        }
        if (!sizes.empty())
            throw py::type_error(call + ": the ends come first, then the sizes; " + forms);
        ends.push_back(IsRef(v) && !IsA<SketchAxis>(v) ? py::object(py::str(RefIn(s, v, call))) : v);
    }
    if (ends.size() > 2)
        throw py::type_error(call + ": at most two ends, from and to; " + forms);
}

// a helped call as its errors show it: the ends, the numbers, then the helpers in their order
inline std::string HelpedCall(const std::string &call, const std::vector<py::object> &ends, const std::vector<double> &numbers,
                              const py::dict &helpers) {
    std::vector<py::object> values(ends.begin(), ends.end());
    for (double x : numbers)
        values.push_back(py::float_(x));
    std::vector<std::pair<const char *, py::handle>> args;
    for (const py::object &v : values)
        args.push_back({nullptr, v});
    for (const char *key : call == "Line" ? LineHelpers : call == "Arc" ? ArcHelpers : CircleHelpers)
        if (helpers.contains(key))
            args.push_back({key, helpers[key]});
    return CallText(call, args);
}

// a helped Line, Arc or Circle placed from its references: the helpers resolved in the sketch, the ends read from the
// positionals (from, to), the pen as the turtle Sketch had it
template <class S>
struct HelpedPlacement {
    S &s;
    const std::string &text;
    const std::vector<double> &numbers;
    const std::vector<sketch::Kind> &flags;
    const sketch::Options &opt;
    HelperView<S> view;
    py::dict kw;
    bool endOption, isFrom, reverse;
    py::object from, to;
    Vertex pos;
    sketch::Pen pen;

    HelpedPlacement(S &sketch, const std::string &call, const std::string &callText, const std::vector<py::object> &ends,
                    const std::vector<double> &nums, const py::dict &helpers, const std::vector<sketch::Kind> &fl,
                    const sketch::Options &o)
        : s(sketch), text(callText), numbers(nums), flags(fl), opt(o), view{sketch} {
        for (const auto &[k, v] : helpers)
            kw[k] = k.cast<std::string>() == "until" && (IsA<Until>(v) || py::isinstance<py::str>(v)) ? py::reinterpret_borrow<py::object>(v)
                                                                                                             : ForHelpers(s, v);
        endOption = IsGiven("until") || (call == "Arc" && IsGiven("sweep"));
        isFrom = ends.size() == 2 || (ends.size() == 1 && endOption);
        from = isFrom ? ForHelpers(s, ends[0]) : py::object(py::none());
        to = ends.size() == 2 ? ForHelpers(s, ends[1]) : ends.size() == 1 && !endOption ? ForHelpers(s, ends[0]) : py::object(py::none());
        reverse = kw.contains("reverse") && py::cast<bool>(kw["reverse"]);
        pos = s.Position();
        const Vertex heading = s.Heading();
        // normalised as the turtle Sketch's Direction did: the helpers place bit for bit as they did through it
        const double n = std::hypot(heading.x, heading.y);
        pen = {pos, {heading.x / n, heading.y / n}, Tol(NormInf(pos))};
    }
    bool IsGiven(const char *key) const { return kw.contains(key) && !kw[key].is_none(); }
    py::object Arg(const char *key) const { return kw.contains(key) ? py::object(kw[key]) : py::object(py::none()); }
    std::optional<double> Number(size_t k) const { return k < numbers.size() ? std::optional(numbers[k]) : std::nullopt; }
    // what until= meets: the target it names, else everything
    EdgeList Every(Until until, const std::optional<sketch::Target> &target) {
        return target ? target->edges : until == Until::No ? EdgeList() : view.EveryEdge();
    }

    void PlaceCircle() {
        const sketch::CircleRefs r = CircleRefsOf(view, Number(0), Arg("center"), Arg("concentric"), Arg("coincident"), Arg("through"),
                                          Arg("tangent"), py::none(), reverse);
        const CircleCarrier c = r.Circle(pen);
        // the radius of the first half of the turtle Sketch's circle
        const planex2d::Arc half({c.center.x + c.radius, c.center.y}, {c.center.x - c.radius, c.center.y}, c.center, true);
        s.Circle(half.Center(), half.Radius(), flags, opt);
    }
    void PlaceLine() {
        const sketch::LineRefs r = LineRefsOf(view, text, Number(0), Arg("until"), from, to, Arg("parallel"), Arg("perpendicular"),
                                      Arg("angle"), Arg("coincident"), Arg("tangent"), Arg("distance"), Arg("through"), py::none(),
                                      reverse);
        const sketch::ConstructedLine h = sketch::LineConstruction(r, pen, s.Axes(), Every(r.until, r.untilTarget)).Drawn();
        Magnitude size;
        size.Add(pos);
        for (const Vertex &p : {h.start, h.end})
            size.Add(p);
        size.Add(r.from);
        size.Add(r.to);
        size.Add(r.tangent);
        for (const std::optional<Edge> *e : {&r.parallel, &r.perpendicular, &r.congruent, &r.angleRef})
            size.Add(*e);
        size.Add(r.through);
        if (!h.start.IsClose(pos, Tol(size.m)))
            s.MoveTo(h.start, {});
        s.Line(h.end, flags, opt);
    }
    void PlaceArc() {
        const std::optional<double> sweep = Number(1) ? Number(1) : kw.contains("sweep") ? std::optional(py::cast<double>(kw["sweep"])) : std::nullopt;
        const sketch::ArcRefs r = ArcRefsOf(view, text, Number(0), sweep, Arg("center"), Arg("concentric"), Arg("coincident"),
                                    Arg("tangent"), Arg("through"), Arg("until"), from, to, py::none(), reverse);
        const sketch::ConstructedArc h = sketch::ArcConstruction(r, pen, s.Axes(), Every(r.until, r.untilTarget)).Drawn();
        if (h.full) {
            if (h.move)
                s.MoveTo(h.start, {});
            s.FullTurn(h.center, h.ccw, flags, opt);
            return;
        }
        const planex2d::Arc a(h.start, h.end, h.center, h.ccw);
        Magnitude size;
        size.Add(pos);
        for (const Vertex &p : {a.Start(), a.End(), a.Center()})
            size.Add(p);
        size.Add(r.center);
        size.Add(r.congruent);
        size.Add(r.from);
        size.Add(r.to);
        size.Add(r.tangent);
        size.Add(r.through);
        if (!a.Start().IsClose(pos, Tol(size.m)))
            s.MoveTo(a.Start(), {});
        const Vertex c = a.Center(), p = a.Start();
        s.Arc(a.End(), a.CCW() ? Vertex{c.y - p.y, p.x - c.x} : Vertex{p.y - c.y, c.x - p.x}, flags, opt);
    }
};

// ends: the positional ends (two: from and to; one: to, or from when until= or sweep= gives the end); numbers: a
// Line's length, an Arc's radius (and angle), a Circle's radius
template <class S>
inline void DrawHelped(S &s, const std::string &call, const std::vector<py::object> &ends, const std::vector<double> &numbers,
                       const py::dict &helpers, const std::vector<sketch::Kind> &flags, const sketch::Options &opt) {
    const std::string text = HelpedCall(call, ends, numbers, helpers);
    NamingTheCall([&] { s.Drawing(opt, [&] {
        HelpedPlacement<S> placement(s, call, text, ends, numbers, helpers, flags, opt);
        const std::string before = s.LastId();
        if (call == "Circle")
            placement.PlaceCircle();
        else if (call == "Line")
            placement.PlaceLine();
        else
            placement.PlaceArc();
        if constexpr (requires { s.autoConstraint; })
            if (s.autoConstraint && s.autoConstraint->named_helpers && s.LastId() != before) {
                py::dict implied;
                for (const auto &[k, v] : helpers)
                    implied[k] = v;
                if (placement.isFrom)
                    implied["from_"] = ends[0];
                if (!ends.empty() && !(ends.size() == 1 && placement.endOption))
                    implied["to"] = ends.back();
                AutoConstraints(s, call, implied, s.LastId());
            }
    }); }, [&] { return text; });
}

inline std::pair<std::vector<size_t>, std::vector<Vertex>> TangentsOf(const std::map<size_t, Vertex> &tangents) {
    std::pair<std::vector<size_t>, std::vector<Vertex>> out;
    for (const auto &[k, t] : tangents) {
        out.first.push_back(k);
        out.second.push_back(t);
    }
    return out;
}

// a sketch element's property as the selections set it: the fields of edges (points: name, maxh, hpref), construction
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

// what Measure measures: an element by its id (a point: where it is), a point (id.end, ...), else the edges named
// name; nothing: the region and every drawn edge
template <class S>
inline Thing NamedInSketch(S &s, const std::optional<std::string> &given) {
    if (!given) {
        const planex2d::Shape shape = s.Shape();
        return {s.Axes(), shape.Faces(), s.Edges(), {}};
    }
    const std::string name = LocalId(s, *given, "Measure");
    if (s.IsWire(name))
        return {s.Axes(), {}, s.ElementWire(name), {}};
    if (s.IsElement(name)) {
        try {
            return {s.Axes(), {}, s.ElementWire(name), {}};
        } catch (const std::invalid_argument &) {
            return {s.Axes(), {}, Wire(s.Axes()), s.Location(name)};
        }
    }
    try {
        return {s.Axes(), {}, Wire(s.Axes()), s.Location(name)};
    } catch (const std::invalid_argument &) {
    }
    Wire named = s.NamedEdges(name);
    if (named.Edges().empty())
        throw py::value_error("Measure('" + *given + "'): no element, point or edge name '" + name + "'" + s.DidYouMean(name));
    return {s.Axes(), {}, std::move(named), {}};
}

// a spline's points, whether it is closed, its given tangents (unit, by point index) and points along it
// (a curve's points: its ends)
template <class S>
inline void SplineGeometry(const S &s, const sketch::Element &e, py::dict &out) {
    const auto &g = s.Geometry();
    const Curve<double, Vertex> f = e.Curve(g);
    py::list points, path;
    if (e.IsFitted(g))
        for (size_t p : g.splines[e.SplineIndex()].points)
            points.append(Tuple2(g.Coords(p)));
    else
        for (const Vertex &p : e.SplinePoints(g))
            points.append(Tuple2(p));
    for (const Vertex &p : e.SplinePath(g))
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
    // a curvature fillet's hidden arc, which its dimension r measures
    if (const std::optional<sketch::BlendShape> &b = g.splines[e.SplineIndex()].blend; b && b->helper != sketch::none) {
        const sketch::Element &arc = g.elements[b->helper];
        py::dict fillet;
        fillet["center"] = Tuple2(g.Coords(arc.Center()));
        fillet["r"] = g.values[arc.Radius()];
        out["fillet"] = fillet;
    }
}

// an offset curve as a spline without points of its own: its path, ends, style and the spline it is the offset of
template <class S>
inline void OffsetCurveGeometry(const S &s, const sketch::Element &e, py::dict &out) {
    const auto &g = s.Geometry();
    py::list path;
    for (const Vertex &p : e.OffsetCurvePath(g))
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

// an element's solved geometry: a point where it is; a line its ends, length and angle (degrees, from h); a circle its
// centre and radius; an arc also its ends, its turn (degrees, positive counterclockwise) and direction; an axis its
// normal's angle and distance from the origin; a spline its points, given tangents, ends and points along it
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
        SplineGeometry(s, e, out);
        break;
    case sketch::ElementKind::OffsetCurve:
        OffsetCurveGeometry(s, e, out);
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

// the names a curve's formula in the sketch s may use, as _expressions._ParseCurve takes them: of a constraint sketch
// its dimensions (the driven ones with why), elements, points and other ids; a plain sketch has none
template <class S>
inline py::dict CurveNames(S &s) {
    py::dict out;
    if constexpr (HasConstraints<S>) {
        s.Flush();
        py::dict dimensions, driven, elements, others;
        const auto names = s.ExpressionNames();
        for (const auto &[name, turn] : names.dimensions)
            dimensions[py::str(name)] = turn ? "angle" : "length";
        for (const auto &[name, kind] : names.others)
            others[py::str(name)] = kind;
        for (const auto &[name, why] : s.DrivenDimensions())
            driven[py::str(name)] = why;
        for (const auto &[kind, ids] : s.Elements())
            for (const std::string &e : ids)
                elements[py::str(e)] = kind;
        out["dimensions"] = dimensions;
        out["driven"] = driven;
        out["elements"] = elements;
        out["others"] = others;
        out["ispoint"] = py::cpp_function([&s](const std::string &r) {
            try {
                return s.RefKind(r) == "point";
            } catch (const std::invalid_argument &) {
                return false;
            }
        });
    } else
        out["numbers_only"] = "a QuickSketch has no dimensions";
    return out;
}

// px.Find among the sketches: one check per sketch class (BindDrawing adds it)
inline std::vector<bool (*)(py::handle, const std::string &)> sketchOwnsId;

// px.Move's px.Sketch, set by BindConstraintSketch; a build without the constraints has none
inline py::object (*constraintSketchAt)(const py::tuple &start, const py::object &axes, const py::object &autoConstraint,
                                        const std::optional<bool> &errorOnBranchChange, const std::string &mode,
                                        const std::optional<std::string> &id, const py::object &dir) = nullptr;

// what both sketches have: the drawing, the elements, the exports
// a drawing call's arguments with the pen and the names resolved: the positionals and flags, the options, the helpers
struct DrawArgs {
    CallArgs a;
    sketch::Options opt;
    py::dict helpers;
};
template <class S>
DrawArgs DrawArgsOf(S &s, const py::args &args, const py::kwargs &kw, const std::string &call, const std::vector<const char *> &helpers) {
    const py::tuple given(Resolved(s, UnpenArgs(s.Axes(), args, call), call));
    const py::dict keywords = UnpenKeywords(s, kw, call);
    Prepare(s, given);
    Prepare(s, keywords);
    return {Split(given, call), OptionsOf(keywords), HelpersOf(s, keywords, call, helpers)};
}

template <class S>
S &DrawLine(S &s, const py::args &args, const py::kwargs &kw) {
    const DrawArgs d = DrawArgsOf(s, args, kw, "Line", LineHelpers);
    const AutoScope<S> scope(s, d.a.autoFlag, "Line");
    const CallArgs &a = d.a;
    const sketch::Options &opt = d.opt;
    const py::dict &helpers = d.helpers;
    if (helpers.empty()) {
        if (Numbers(a, 1)) {
            s.Line(a.values[0].cast<double>(), a.flags, opt);
            return s;
        }
        if (Numbers(a, 2)) {
            s.Line(a.values[0].cast<double>(), a.values[1].cast<double>(), a.flags, opt);
            return s;
        }
        if (a.values.size() == 1 && IsPointLike(a.values[0])) {
            s.Line(PointArg(s.Axes(), a.values[0], "Line"), a.flags, opt);
            return s;
        }
    }
    std::vector<py::object> ends;
    std::vector<double> sizes;
    EndsAndSizes(s, a, "Line", LINE_FORMS, ends, sizes);
    if (sizes.size() > 1 || (!sizes.empty() && !ends.empty()) || (ends.empty() && sizes.empty() && helpers.empty()))
        throw py::type_error(std::string("Line: ") + LINE_FORMS);
    DrawHelped(s, "Line", ends, sizes, helpers, a.flags, opt);
    return s;
}

// Arc(r, tangent=(a, b)) is Arc(a, b, r, tangent=(a, b)): the targets as the ends
inline std::vector<py::object> FilletEnds(const std::vector<py::object> &ends, const std::vector<double> &sizes, const py::dict &helpers) {
    if (!ends.empty() || sizes.size() != 1 || !helpers.contains("tangent"))
        return ends;
    const py::handle t = helpers["tangent"];
    if (!(py::isinstance<py::tuple>(t) || py::isinstance<py::list>(t)) || py::len(t) != 2 || IsPoint(t))
        return ends;
    const py::sequence pair = py::reinterpret_borrow<py::sequence>(t);
    if (IsTarget(pair[0]) && IsPoint(pair[1]))
        return ends;
    return {pair[0], pair[1]};
}

template <class S>
S &DrawArc(S &s, const py::args &args, const py::kwargs &kw) {
    const DrawArgs d = DrawArgsOf(s, args, kw, "Arc", ArcHelpers);
    const AutoScope<S> scope(s, d.a.autoFlag, "Arc");
    const CallArgs &a = d.a;
    const sketch::Options &opt = d.opt;
    const py::dict &helpers = d.helpers;
    std::vector<py::object> ends;
    std::vector<double> sizes;
    EndsAndSizes(s, a, "Arc", ARC_FORMS, ends, sizes);
    if (sizes.size() > (ends.empty() ? 2u : 1u))
        throw py::type_error(std::string("Arc: ") + ARC_FORMS);
    const bool circle = helpers.contains("center") || helpers.contains("concentric") || helpers.contains("coincident");
    if (helpers.contains("sweep") && sizes.size() == 2)
        throw py::type_error("Arc() got multiple values for argument 'sweep'");
    if (helpers.contains("sweep") && !circle && ends.empty())
        throw py::type_error("Arc(r, sweep=...): along the pen the sweep is positional, Arc(r, sweep); sweep= ends an arc on "
                             "a circle given by center=, concentric= or coincident=");
    if (sizes.size() == 2 && circle)
        throw py::type_error("Arc(r, sweep, ...): on a circle given by center=, concentric= or coincident= the sweep is "
                             "sweep=: Arc(r, center=(0, 0), sweep=90)");
    if (helpers.empty()) {
        if (ends.empty() && sizes.size() == 2) {
            s.Arc(sizes[0], sizes[1], a.flags, opt);
            return s;
        }
        if (ends.size() == 1 && sizes.empty() && IsPointLike(ends[0])) {
            s.Arc(PointArg(s.Axes(), ends[0], "Arc"), s.Heading(), a.flags, opt);
            return s;
        }
        if (ends.empty())
            throw py::type_error(std::string("Arc: ") + ARC_FORMS);
    }
    if (helpers.size() == 1 && helpers.contains("tangent") && IsPointLike(helpers["tangent"]) && ends.size() == 1 && sizes.empty() &&
        IsPointLike(ends[0])) {
        s.Arc(PointArg(s.Axes(), ends[0], "Arc"), PointArg(helpers["tangent"], "Arc(tangent=)"), a.flags, opt);
        return s;
    }
    DrawHelped(s, "Arc", FilletEnds(ends, sizes, helpers), sizes, helpers, a.flags, opt);
    return s;
}

template <class S>
S &DrawCircle(S &s, const py::args &args, const py::kwargs &kw) {
    const py::dict keywords = UnpenKeywords(s, kw, "Circle");
    Prepare(s, keywords);
    const CallArgs a = Split(py::tuple(Resolved(s, UnpenArgs(s.Axes(), args, "Circle"), "Circle")), "Circle");
    const AutoScope<S> scope(s, a.autoFlag, "Circle");
    const sketch::Options opt = OptionsOf(keywords);
    const py::dict helpers = HelpersOf(s, keywords, "Circle", CircleHelpers);
    if (!helpers.empty()) {
        if (!a.values.empty() && !Numbers(a, 1))
            throw py::type_error("Circle(r, helpers...): only a radius goes with the helpers");
        DrawHelped(s, "Circle", {}, a.values.empty() ? std::vector<double>() : std::vector<double>{a.values[0].cast<double>()},
                   helpers, a.flags, opt);
        return s;
    }
    if (Numbers(a, 1)) {
        s.Circle(s.Position(), a.values[0].cast<double>(), a.flags, opt);
        return s;
    }
    // the center as one point; Circle(h, v, r) is gone
    if (a.values.size() == 2 && IsPointLike(a.values[0]) && IsNumber(a.values[1])) {
        s.Circle(PointArg(s.Axes(), a.values[0], "Circle"), a.values[1].cast<double>(), a.flags, opt);
        return s;
    }
    if (Numbers(a, 3))
        throw py::type_error("Circle(h, v, r): the center is one point now, Circle((h, v), r)");
    throw py::type_error("Circle(r) or Circle((h, v), r), then flags; or the helpers");

}

// a Slot, ArcSlot or Keyhole: the spine is what Line (Arc) draws from the arguments before the last numbers, the width w
// (a Keyhole: then the hole's radius r last)
template <class S>
S &DrawSlot(S &s, const std::string &kind, const py::args &args, const py::kwargs &kw) {
    std::vector<py::object> given;
    for (const py::handle &h : args)
        given.push_back(py::reinterpret_borrow<py::object>(h));
    const char *forms = kind == "Slot"      ? "Slot(l, w), Slot(dh, dv, w), Slot(to, w), Slot(start, end, w), then flags; Line's helpers place the spine"
                        : kind == "Keyhole" ? "Keyhole(l, w, r), Keyhole(dh, dv, w, r), Keyhole(to, w, r), Keyhole(start, end, w, r), then flags; Line's helpers "
                                              "place the spine"
                                            : "ArcSlot(r, sweep, w), ArcSlot(to, w), ArcSlot(to, r, w), ArcSlot(start, end, r, w), then flags; Arc's "
                                              "helpers place the spine";
    const auto lastNumber = [&](const char *what) {
        const auto at = std::find_if(given.rbegin(), given.rend(), [](const py::object &h) { return !IsA<sketch::Kind>(h); });
        if (at == given.rend() || !IsNumber(*at))
            throw py::type_error(kind + ": " + what + " is the last number, before the flags; " + forms);
        const double v = at->template cast<double>();
        given.erase(std::next(at).base());
        return v;
    };
    const std::optional<double> hole = kind == "Keyhole" ? std::optional<double>(lastNumber("the hole's radius r")) : std::nullopt;
    const double w = lastNumber(hole ? "the width w, then r," : "the width w");
    std::vector<std::pair<const char *, py::handle>> shown;
    for (const py::object &h : given)
        shown.push_back({nullptr, h});
    const py::float_ widthText(w), holeText(hole.value_or(0.0));
    shown.push_back({nullptr, widthText});
    if (hole)
        shown.push_back({nullptr, holeText});
    const std::string call = CallText(kind, shown);
    py::kwargs spineKw;
    for (const auto &[k, v] : kw)
        if (std::find(OptionKeys.begin(), OptionKeys.end(), k.cast<std::string>()) == OptionKeys.end())
            spineKw[k] = v;
    spineKw["construction"] = true;
    const py::args spineArgs(py::tuple(py::cast(given)));
    NamingTheCall([&] {
        s.Slot(kind, w, hole, OptionsOf(kw), call, [&] {
            if (kind == "ArcSlot")
                DrawArc(s, spineArgs, spineKw);
            else
                DrawLine(s, spineArgs, spineKw);
        });
    }, [&] { return call; });
    return s;
}

// a Polygon or Hexagon: the circle is what Circle draws from the arguments before the last number, the number of sides n
// (a Hexagon: the width across flats af, the circle's radius af / sqrt(3))
template <class S>
S &DrawPolygon(S &s, const std::string &kind, const py::args &args, const py::kwargs &kw) {
    const bool hexagon = kind == "Hexagon";
    std::vector<py::object> given;
    for (const py::handle &h : args)
        given.push_back(py::reinterpret_borrow<py::object>(h));
    std::vector<std::pair<const char *, py::handle>> shown;
    for (const py::object &h : given)
        shown.push_back({nullptr, h});
    const std::string call = CallText(kind, shown);
    const auto at = std::find_if(given.rbegin(), given.rend(), [](const py::object &h) { return !IsA<sketch::Kind>(h); });
    const char *form = hexagon ? "Hexagon(af), Hexagon(h, v, af), then flags; Circle's helpers place the circle, the width af across flats is the last number"
                               : "Polygon(r, n), Polygon(h, v, r, n), then flags; Circle's helpers place the circle, the number of sides n (an int) is the last number";
    if (at == given.rend() || !(hexagon ? IsNumber(*at) : py::isinstance<py::int_>(*at) && !py::isinstance<py::bool_>(*at)))
        throw py::type_error(call + ": " + form);
    std::optional<double> flats;
    size_t n = 6;
    if (hexagon) {
        flats = at->template cast<double>();
        *at = py::float_(*flats / std::sqrt(3.0));
    } else {
        const long long count = at->template cast<long long>();
        if (count < 3)
            throw py::value_error(call + ": a polygon has n >= 3 sides, n = " + std::to_string(count) + "; make n larger");
        n = (size_t)count;
        given.erase(std::next(at).base());
    }
    py::kwargs circleKw;
    for (const auto &[k, v] : kw)
        if (std::find(OptionKeys.begin(), OptionKeys.end(), k.cast<std::string>()) == OptionKeys.end())
            circleKw[k] = v;
    circleKw["construction"] = true;
    const py::args circleArgs(py::tuple(py::cast(given)));
    NamingTheCall([&] {
        s.Polygon(kind, n, flats, OptionsOf(kw), call, [&] {
            try {
                DrawCircle(s, circleArgs, circleKw);
            } catch (const py::type_error &e) {
                throw py::type_error(call + ": the circle takes the forms of Circle: " + e.what());
            }
        });
    }, [&] { return call; });
    return s;
}

template <class S>
S &AddSymmetryLine(S &s, const py::object &a, const py::object &b, bool reverse, const std::optional<std::string> &id,
                   const py::kwargs &kw) {
    NoMoreKeywords(kw, "SymmetryLine", "reverse=True takes the other bisector");
    UserId(id);
    Prepare(s, py::make_tuple(a, b));
    NamingTheCall([&] { s.Atomic([&] {
        const bool refs = IsRef(a) && IsRef(b);
        std::string ta = refs ? RefIn(s, a, "SymmetryLine") : "", tb = refs ? RefIn(s, b, "SymmetryLine") : "";
        const std::string ka = refs ? s.RefKind(ta) : "", kb = refs ? s.RefKind(tb) : "";
        std::string axis;
        if (ka == "round" && kb == "round") {
            // the bisector of the centres whatever the radii drawn: they are made equal
            const Vertex p = s.Location(ta + ".center"), q = s.Location(tb + ".center");
            if (!(Norm(q - p) > Tol(std::max(NormInf(p), NormInf(q)))))
                throw py::value_error("SymmetryLine('" + ta + "', '" + tb + "'): both lie on the same circle");
            const Vertex d = (q - p) * (1.0 / Norm(q - p));
            axis = s.AddAxisLine((p + q) * 0.5, Vertex{-d.y, d.x}, "SymmetryLine", id);
        } else {
            HelperView view{s};
            const py::object ra = ForHelpers(s, a), rb = ForHelpers(s, b);
            const std::string call = CallText("SymmetryLine", {{"a", ra}, {"b", rb}});
            const sketch::SymRef sa = SymRefOf(view, ra, s.Position(), false, call), sb = SymRefOf(view, rb, s.Position(), false, call);
            const Line line = sketch::SymmetryAxis(sa, sb, reverse, call);
            axis = s.AddAxisLine(line.Start(), line.End() - line.Start(), "SymmetryLine", id);
        }
        if (!refs)
            return;
        const bool lines = (ka == "line" || ka == "axis") && (kb == "line" || kb == "axis");
        if (!lines && !(ka == "point" && kb == "point") && !(ka == "round" && kb == "round"))
            throw py::value_error("SymmetryLine: a and b are two points, two lines or two circles");
        if constexpr (HasConstraints<S>) {
            if (lines) {
                if (ka == "axis")
                    std::swap(ta, tb);
                s.Constrain({ta, tb, axis}, sketch::Kind::SymmetricCarriers, std::nullopt, std::nullopt);
            } else if (ka == "point")
                s.Constrain({ta, tb, axis}, sketch::Kind::Symmetric, std::nullopt, std::nullopt);
            else {
                s.Constrain({ta + ".center", tb + ".center", axis}, sketch::Kind::Symmetric, std::nullopt, std::nullopt);
                s.Constrain({ta, tb}, sketch::Kind::Equal, std::nullopt, std::nullopt);
                s.TieLastConstraintTo(axis);
            }
        }
    }); }, [&] { return CallText("SymmetryLine", {{"a", a}, {"b", b}}); });
    return s;
}

// for the plots: every element with its geometry, state and whether it is construction
template <class S>
py::list DrawingOf(S &s) {
    py::list out;
    std::map<std::string, std::string> states;
    if constexpr (HasConstraints<S>)
        for (const auto &[id, state] : s.States())
            states[id] = state;
    for (const auto &[kind, ids] : s.Elements())
        for (const std::string &id : ids) {
            const auto [e, radius, angle, d] = s.ElementOf(id);
            py::dict g;
            g["id"] = id;
            // a curve draws as the spline it is
            g["kind"] = kind == "Curve" ? "Spline" : kind;
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
                SplineGeometry(s, e, g);
            else if (e.kind == sketch::ElementKind::OffsetCurve)
                OffsetCurveGeometry(s, e, g);
            out.append(g);
        }
    return out;
}

// the points of the corners at: a point id ("Line.1.end", "A"), a sketch point or a selection of them, a point (h, v), or a
// list of these
template <class S>
inline void AddSketchCorners(S &s, const py::handle &at, const std::string &call, std::vector<Vertex> &out) {
    if (py::hasattr(at, "_items")) {
        for (const py::handle &x : at.attr("_items"))
            AddSketchCorners(s, x, call, out);
    } else if (IsRef(at)) {
        out.push_back(s.Location(RefIn(s, at, call)));
    } else if (IsPointLike(at)) {
        out.push_back(PointArg(s.Axes(), at, call));
    } else if (py::isinstance<py::iterable>(at)) {
        for (const py::handle &x : at)
            AddSketchCorners(s, x, call, out);
    } else {
        throw py::type_error(call + ": at is a point id, a sketch point, a point (h, v) or a list of them, not " +
                             py::str(py::type::handle_of(at).attr("__name__")).cast<std::string>());
    }
}
template <class S>
inline std::vector<Vertex> SketchCornerPoints(S &s, const py::handle &at, const std::string &call) {
    Prepare(s, at);
    std::vector<Vertex> corners;
    AddSketchCorners(s, at, call, corners);
    if (corners.empty())
        throw py::value_error(call + ": no corner given; at is a point id, a point (h, v) or a list of them");
    return corners;
}
// id=: a str for one corner, a list of str for several
inline std::vector<std::string> CornerIds(const py::object &id, const std::string &call) {
    std::vector<std::string> out;
    if (id.is_none())
        return out;
    if (py::isinstance<py::str>(id))
        out.push_back(id.cast<std::string>());
    else if (py::isinstance<py::list>(id) || py::isinstance<py::tuple>(id))
        for (const py::handle &x : id) {
            if (!py::isinstance<py::str>(x))
                throw py::type_error(call + "(id=...): an id is a str, not " + py::repr(x).cast<std::string>());
            out.push_back(x.cast<std::string>());
        }
    else
        throw py::type_error(call + "(id=...): id is a str for one corner or a list of str for several");
    for (const std::string &one : out)
        CheckUserId(one);
    return out;
}

// the properties name=, maxh=, minh=, hpref=, layer=, col= of the new elements: each one value for all corners or a
// list of one per corner (col: a list of colours); none, one Properties for all, or one per corner
inline std::vector<Properties> CornerProps(const py::kwargs &kw, const std::string &call) {
    const char *keys[] = {"name", "maxh", "minh", "hpref", "layer", "col"};
    size_t n = 0;
    py::dict each;
    for (const auto &[k, v] : kw) {
        const std::string key = k.cast<std::string>();
        if (std::find(std::begin(keys), std::end(keys), key) == std::end(keys))
            throw py::type_error(call + "() got an unexpected keyword argument '" + key + "'; the new element takes id=, name=, "
                                 "maxh=, minh=, hpref=, layer=, col=");
        const bool list = (py::isinstance<py::list>(v) || py::isinstance<py::tuple>(v)) &&
                          (key != "col" || (py::len(v) > 0 && !py::isinstance<py::float_>(v[py::int_(0)]) && !py::isinstance<py::int_>(v[py::int_(0)])));
        if (!list) {
            each[k] = v;
            continue;
        }
        if (n > 1 && py::len(v) != n)
            throw py::value_error(call + "(" + key + "=...): " + std::to_string(py::len(v)) + " values, another property has " +
                                  std::to_string(n) + "; give one value for all corners or one per corner");
        n = std::max(n, (size_t)py::len(v));
    }
    std::vector<Properties> out;
    for (size_t c = 0; c < std::max(n, kw.empty() ? (size_t)0 : (size_t)1); ++c) {
        py::dict one;
        for (const auto &[k, v] : kw)
            one[k] = each.contains(k) ? py::reinterpret_borrow<py::object>(v) : py::object(py::reinterpret_borrow<py::sequence>(v)[c]);
        out.push_back(Props(KwOpt<std::string>(one, "name"), KwOpt<double>(one, "maxh"), KwOpt<std::vector<double>>(one, "col"),
                            KwOpt<double>(one, "minh"), KwOpt<double>(one, "hpref"), KwOpt<int>(one, "layer"),
                            std::optional<std::vector<double>>()));
    }
    return out;
}

inline const char *SketchCornerDoc(bool chamfer, const std::string &sk, bool constraints) {
    const std::string kind = chamfer ? "Chamfer" : "Fillet";
    const std::string at = "Id | Point | list[Id | Point]";
    const std::string sizes = chamfer ? "d: float, /, d2: float | None = None, angle: float | None = None, reverse: bool = False, *, "
                                        "id: str | list[str] | None = None, **options"
                                      : "r: float, continuity: Constraint = px.Tangent, /, *, id: str | list[str] | None = None, **options";
    std::string text = kind + "(at: " + at + ", " + sizes + ") -> " + sk + "\n\n" +
                       (chamfer ? "Cut corners with a straight line, in place." : "Round corners with an arc tangent to both elements, in place.") +
                       "\n\n"
                       "A corner is a point that is the end of exactly two drawn elements (construction elements do not count) whose "
                       "directions differ. The new " + std::string(chamfer ? "line" : "arc") + " is the element '" + kind + ".k' (k counts per "
                       "sketch, in the order the corners are given) with the properties of the two elements merged, the name of the first "
                       "winning (the first element is the one drawn first), and the properties given in options set over them. The two elements keep their ids, cut back to the " +
                       (chamfer ? "cut" : "touch") + " points. What is cut off stays as a construction element named as Trim names it "
                       "('Line.1.trimmed.1', 'Line.2.trimmed.1'; the sharp corner is the end the two share, 'Line.1.trimmed.1.end'). An "
                       "element the cut reaches the far end of is used up: it becomes construction as a whole, and the new element ends "
                       "at its far end. Lines and arcs are cut exactly; a spline is cut where the " + (chamfer ? "line" : "arc") +
                       " meets its exact curve. Remove('" + kind + ".1') takes the " + (chamfer ? "line" : "arc") +
                       " away; the elements stay cut.\n\n"
                       "A corner that cannot be cut (a tangent joint, a point on no vertex, more or fewer than two ends, a size that "
                       "does not fit) is skipped with a PlanexWarning that names why and the largest size that fits; if none can be "
                       "cut the call raises and changes nothing.\n\n" +
                       (constraints ? std::string(chamfer ? "The line is held by its ends on the two elements and the dimensions 'Chamfer.k.d' (from the "
                                                              "sharp corner along the first element, as the straight distance) and 'Chamfer.k.d2' (the second), or "
                                                              "'Chamfer.k.angle' if angle was given (the first element must be a line), and 'Chamfer.k.reverse' "
                                                              "(0 or 1: which element takes d); the dimensions are changed like every sketch value: "
                                                              "sk['Chamfer.1.d'] = 2 or sk['Chamfer.1.reverse'] = True, then Update(). A change of "
                                                              "reverse moves d and d2 (angle) to the other elements."
                                                            : "The arc is held by its tangency to the two elements and the dimension 'Fillet.k.r', which drives it: "
                                                              "sk['Fillet.1.r'] = 2, then Update() moves the touch points. With px.Curvature that arc "
                                                              "stays hidden (not drawn, listed or exported) and sets where the blend starts and ends; "
                                                              "the blend follows the two elements and r, and constraints on it act through them.") + "\n\n"
                                    : std::string()) +
                       (chamfer ? std::string()
                                : std::string("With px.Curvature a curvature-continuous blend takes the arc's place between the same touch points: a "
                                              "spline 'Fillet.k' (a quintic) whose curvature runs from the first element's to the second's "
                                              "without a jump. It bends tighter than the arc inside (its smallest radius about 0.67 r to 0.71 r "
                                              "for corners turning 60 to 120 degrees) and passes closer to the corner (0.33 r instead of the "
                                              "arc's 0.41 r at a right angle).\n\n")) +
                       "Args:\n"
                       "    at: the corners: a point id ('Line.1.end', 'A'), a sketch point or a selection of points, a point (h, v) "
                       "on a corner, or a list of these; there is no default.\n";
    if (chamfer)
        text += "    d: the length on the first element, a length > 0; also on the second if d2 and angle are None.\n"
                "    d2: the length on the second element, a length > 0.\n"
                "    angle: with d, the angle in degrees between the first element and the new line, between 0 and 180. d2 and "
                "angle exclude each other.\n"
                "    reverse: True makes the second element the first.\n";
    else
        text += "    r: the radius, a length > 0.\n"
                "    continuity: px.Tangent the arc, px.Curvature the curvature-continuous blend.\n";
    text += "    id: the id of the new element for one corner (a str), or one id per corner (a list); default '" + kind + ".k'.\n"
            "    options: the new element's properties name=, maxh=, minh=, hpref=, layer=, col=, each one value for all "
            "corners or a list of one per corner (col=[(1, 0, 0), (0, 0, 1)]); they replace the merged ones.\n\n"
            "Returns:\n    this sketch, so calls chain.\n\n"
            "Raises:\n"
            "    TypeError: at is none of the forms above, id is neither a str nor a list of str, or an option is no property.\n"
            "    ValueError: a size is not positive and finite" + std::string(chamfer ? ", the angle is not between 0 and 180 or d2 and "
            "angle are both given" : ", continuity is neither px.Tangent nor px.Curvature") + ", no corner is given, an id is no point, or no corner can be cut (the message names each "
            "corner and why); an id is taken, or the number of ids or of a property's values differs from the number of corners.\n\n"
            "Examples:\n";
    if (chamfer)
        text += "    sk.Chamfer(\"Line.2.end\", 1)\n    sk.Chamfer([(4, 0), (4, 4)], 1, d2=2, id=[\"a\", \"b\"])";
    else
        text += "    sk.Fillet(\"Line.2.end\", 0.5)\n    sk.Fillet([(4, 0), (4, 4)], 0.5, px.Curvature, name=[\"r1\", \"r2\"])";
    return Doc(text);
}

inline const char *ConnectDoc(const std::string &sk, bool constraints) {
    std::string text = "Connect(a: Id, b: Id, continuity: Constraint = px.Tangent, /, reverse: bool = False, *, id: str | None = None, "
                       "**options) -> " + sk + "\n\n"
                       "Bridge the gap between an end of a and an end of b with a new element; a and b stay as they are.\n\n"
                       "a and b are lines, arcs or open splines: their nearest pair of ends is joined (reverse=True: the other end of "
                       "each), or name the ends ('Line.1.end', 'Arc.2.start'). With px.Coincident the new element is the line between the "
                       "ends; with px.Tangent a spline (a cubic) leaving a's end along a and arriving along b; with px.Curvature a "
                       "spline (a quintic) that also takes up the curvature of a and b there, so the curvature runs on without a jump "
                       "(for smooth ducts and cam tracks). Its id is 'Connect.k'.\n\n" +
                       std::string(constraints ? "The spline is computed from a and b at their ends: it follows them when they move, and "
                                                 "constraints on it act through them. a and b cannot be removed while it exists.\n\n"
                                               : "") +
                       "Args:\n"
                       "    a: the element (or end) the new element leaves from.\n"
                       "    b: the element (or end) it arrives at.\n"
                       "    continuity: px.Coincident, px.Tangent or px.Curvature.\n"
                       "    reverse: True joins the other end of each element given whole.\n"
                       "    id: the new element's id, default 'Connect.k'.\n"
                       "    options: its properties name=, maxh=, minh=, hpref=, layer=, col=, construction=.\n\n"
                       "Returns:\n    this sketch, so calls chain.\n\n"
                       "Raises:\n"
                       "    TypeError: a or b is no id, or an unknown keyword.\n"
                       "    ValueError: a or b is no line, arc or open spline (or an offset curve, which px.Tangent and px.Curvature "
                       "do not join), names no end, both are one element, the ends meet already, reverse=True with two named ends, or "
                       "another continuity.\n\n"
                       "Examples:\n"
                       "    sk.Connect(\"inlet\", \"outlet\", px.Curvature)\n"
                       "    sk.Connect(\"Line.1.end\", \"Arc.1.start\")";
    return Doc(text);
}

template <class S>
inline void BindDrawing(py::class_<S> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string sk = NameOf(sketch);
    const std::string ties = HasConstraints<S> ? "True" : "False";
    const std::string exportSignature = ExportSignature();
    const std::string exportArgs = ExportArgs("None takes px.config.remove_free_edges, False for a wire export");
    sketchOwnsId.push_back(+[](py::handle h, const std::string &id) { return IsA<S>(h) && h.cast<S &>().AssignedId() == id; });
    BindMotion<S, false>(sketch);
    BindCopy<S>(sketch);
    sketch
        .def_property_readonly("origin", [](const S &) { return std::string("origin"); }, "origin: str\n\nThe id of the sketch's origin.")
        .def_property_readonly("pen", [](const S &s) { return SketchPen{s.Position(), s.Heading()}; },
                               "pen: SketchPen\n\nThe pen as it is now, a snapshot (sk.pen.position, sk.pen.direction); a drawing call takes "
                               "it where a point (its position) or a direction (its direction) is asked for.")
        .def_property_readonly("chain_start", [](const S &s) { return Tuple2(s.StartPosition()); },
                               "chain_start: tuple[float, float]\n\nWhere the chain being drawn started, (h, v): Close() goes back there.")
        .def("Move", NamingIds("Move", +[](S &s, const py::args &raw, const py::kwargs &rawKw) -> S & {
            const py::tuple args = UnpenArgs(s.Axes(), raw, "Move");
            const py::dict kw = UnpenKeywords(s, rawKw, "Move");
            Prepare(s, args);
            const CallArgs a = Split(args, "Move");
            const AutoScope<S> scope(s, a.autoFlag, "Move");
            HelpersOf(s, kw, "Move", {"dir"});
            std::optional<Vertex> dir;
            if (kw.contains("dir") && !kw["dir"].is_none()) {
                dir = PointArg(kw["dir"], "Move(dir=)");
                if (!(Norm(*dir) > 0.0))
                    throw py::value_error("Move(dir=): the direction has no length");
            }
            if (Numbers(a, 2))
                s.MoveTo(s.Position() + Vertex{a.values[0].cast<double>(), a.values[1].cast<double>()}, a.flags);
            else if (Numbers(a, 1))
                s.MoveTo(s.Position() + s.Heading() * a.values[0].cast<double>(), a.flags);
            else if (a.values.size() == 1 && IsRef(a.values[0]))
                s.MoveTo(RefIn(s, a.values[0], "Move"), a.flags);
            else if (a.values.size() == 1)
                s.MoveTo(PointArg(s.Axes(), a.values[0], "Move"), a.flags);
            else
                throw py::type_error("Move((h, v)), Move(id), Move(point), Move(dh, dv) or Move(l), then flags, dir=(dh, dv)");
            if (dir)
                s.Direction(dir->x, dir->y);
            return s;
        }), ref,
             Doc("Move(point: Point, /, *flags: Constraint, dir: tuple[float, float] | None = None) -> " + sk + "\n"
                 "Move(dh: float, dv: float, /, *flags: Constraint, dir: tuple[float, float] | None = None) -> " + sk + "\n"
                 "Move(l: float, /, *flags: Constraint, dir: tuple[float, float] | None = None) -> " + sk + "\n\n"
                 "Move the pen without drawing; a new chain starts there.\n\n"
                 "Args:\n"
                 "    point: where the pen goes: (h, v), a point id ('Line.1.end'), a selected point or sk.pen.\n"
                 "    dh: the move along h from the pen; with dv.\n"
                 "    dv: the move along v from the pen.\n"
                 "    l: the move along the pen's direction.\n"
                 "    flags: px.Coincident (the start shares the point it moves to), px.Fix (the start is fixed).\n"
                 "    dir: the drawing direction (dh, dv) from there, or sk.pen (its direction); default: it stays.\n\n"
                 "Raises:\n"
                 "    TypeError: another form of arguments, or an unknown keyword.\n"
                 "    ValueError: dir has no length.\n\n"
                 "Examples:\n"
                 "    sk.Move((10, 0)).Line(5)\n"
                 "    sk.Move(\"Line.1.end\", px.Coincident, dir=(0, 1))"))
        .def("Line", &DrawLine<S>, ref,
             Doc("Line(l: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Line(dh: float, dv: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Line(to: Point | Id, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Line(start: Point | Id, end: Point | Id, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Line(*ends: Point | Id | float | Constraint, until: Id | Shape | SketchUntil | None = None, "
                 "parallel: Id | Shape | None = None, perpendicular: Id | Shape | None = None, "
                 "angle: float | tuple[float, Id] | None = None, coincident: Id | None = None, "
                 "tangent: Id | Shape | tuple[Id, Id] | None = None, distance: float | tuple[float, Point] | None = None, "
                 "through: Point | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw a line from the pen or between two ends.\n\n"
                 "The positional arguments are read in this order: start, end, then a length, then the flags. The helpers "
                 "(the keywords from until to reverse) place the line from references: one of parallel, perpendicular, "
                 "angle, coincident, tangent or through fixes its carrier (else the pen's direction), and l, to or until "
                 "its end. A target is an id, px.H, px.V, an edge, Wire, Face, Shape or OCC shape (projected). Of "
                 "several solutions the one on the edges before their extensions, then the shortest line, then the one "
                 "nearest the pen is taken. With auto_constraint the helpers also constrain the line.\n\n"
                 "Args:\n"
                 "    l: the length along the pen's direction, or with a helper the length along its carrier.\n"
                 "    dh: the line's extent along h from the pen; with dv.\n"
                 "    dv: the line's extent along v from the pen.\n"
                 "    to: the end: a point (h, v), a point id ('A', 'Line.1.end'), sk.pen, an element or another "
                 "target (where the carrier meets it; (target, point) takes the intersection nearest the point). An "
                 "element as an end needs a helper that fixes the carrier.\n"
                 "    start: the start of the two-end form, read as to.\n"
                 "    end: the end of the two-end form, read as to.\n"
                 "    ends: start and end, or a single start with until=, or a length l, then the flags.\n"
                 "    flags: px.Horizontal, px.Vertical, px.Fix (of the line); px.Tangent, px.Perpendicular, "
                 "px.Parallel, px.Equal (with the element before it).\n"
                 "    until: the end: px.first / px.last until the first / last line, arc, circle or spline ahead (the "
                 "start not counted), or a target; with an element as to, px.first / px.last picks the first or last "
                 "intersection with it ahead. With until= a single positional is the from: "
                 "sk.Line(\"c1\", tangent=\"c2\", until=\"wall\").\n"
                 "    parallel: a line or target the line is parallel to; sk.pen takes the pen's direction.\n"
                 "    perpendicular: a line or target the line is perpendicular to; sk.pen takes the pen's direction.\n"
                 "    angle: degrees from the h axis, or (degrees, reference) from the line reference.\n"
                 "    coincident: a line whose carrier the line lies on.\n"
                 "    tangent: one target the line touches, or two circles (the line goes from the first to the second); a "
                 "spline is touched on its approximation, within spline_tol (its edge's Tangent is the exact curve's).\n"
                 "    distance: with parallel, the offset from the reference: d > 0 on the side of it the pen is on, "
                 "d < 0 the other side (pen on the reference: d > 0 to its left); (d, point): d on the side of the "
                 "point. Needs a length, an end target or until=; sk.Offset gives an offset copy.\n"
                 "    through: a point: with a direction helper the carrier passes it and the line starts there, else "
                 "the line goes from the start through it.\n"
                 "    reverse: take the other solution; with none, run a forward line the other way.\n"
                 "    options: id= (the line's id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, "
                 "partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: a wrong form of arguments, an unknown keyword, a keyword for an end (the ends are positional), "
                 "near=.\n"
                 "    ValueError: the line cannot be placed (the message names what is missing); nothing is drawn.\n\n"
                 "Examples:\n"
                 "    sk.Line(5).Line((5, 3)).Line(\"A\", \"B\", through=(2, 1))\n"
                 "    sk.Line(\"wall\", angle=30)"))
        .def("Arc", &DrawArc<S>, ref,
             Doc("Arc(r: float, sweep: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Arc(to: Point | Id, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Arc(to: Point | Id, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Arc(start: Point | Id, end: Point | Id, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Arc(*ends: Point | Id | float | Constraint, sweep: float | None = None, center: Point | None = None, "
                 "concentric: Id | None = None, coincident: Id | None = None, "
                 "tangent: tuple[float, float] | Id | Shape | tuple[Id, Id] | None = None, through: Point | None = None, "
                 "until: Id | Shape | SketchUntil | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw an arc from the pen or between two ends.\n\n"
                 "The positional arguments are read in this order: start, end, then the sizes (r, sweep), then the flags. "
                 "The circle of the arc: center= or concentric= with radius r (default through the start), coincident= "
                 "(an arc's circle), else continuing the pen's direction with radius r (r > 0 turns left). On a given "
                 "circle the start is the given start (an element: where it meets the circle nearest the pen, or (element, "
                 "point)), else the pen projected onto it; the arc runs counterclockwise between two given ends, along "
                 "the pen's direction from the pen, with sweep= by its sign. Its end: a positional to, sweep= or until=.\n\n"
                 "Args:\n"
                 "    r: the radius; from the pen a negative r starts backwards, its center on the other side; with two "
                 "ends the arc runs counterclockwise the short way, -r clockwise.\n"
                 "    sweep: the turn in degrees from the pen, positive counterclockwise; a full turn (also sweep=360 with "
                 "center=) draws one circle element (Circle.k, or id=) through the pen, its origin the pen's point, and "
                 "the pen stays; in a px.Sketch it touches the element before it; as sweep= the turn "
                 "on a circle given by center=, concentric= or coincident=.\n"
                 "    to: the end, as for Line: a point (h, v), a point id, sk.pen, an element (until=px.first, the "
                 "default, or px.last picks the intersection along the arc) or (element, point). Arc(to) alone leaves "
                 "the pen along its direction. An element as an end needs the circle.\n"
                 "    start: the start of the two-end form, read as to.\n"
                 "    end: the end of the two-end form, read as to.\n"
                 "    ends: start and end, or a single start with sweep= or until=, then the sizes, then the flags.\n"
                 "    flags: as for Line.\n"
                 "    center: the center, a point or a point id.\n"
                 "    concentric: a point id or an arc whose center the arc shares.\n"
                 "    coincident: an arc whose circle the arc lies on.\n"
                 "    tangent: (dh, dv), the direction at the start; sk.pen, the pen's direction; a "
                 "target: the arc leaves the pen along its direction and ends touching it; (a, b) with r: the arc of "
                 "radius r touching a and b, from a to b, by default in the corner they face. A target given as (target, "
                 "point): of a line the side the point is on, of a circle a point inside puts the arc inside. A spline is "
                 "touched on its approximation, within spline_tol.\n"
                 "    through: a third point the arc passes.\n"
                 "    until: px.first / px.last until the first / last line, arc, circle or spline along the arc (the "
                 "start not counted), or a target. With sweep= or until= a single positional is the from: "
                 "sk.Arc(\"tangent\", sweep=90, coincident=\"c\").\n"
                 "    reverse: the other way round; with two ends and r the long way.\n"
                 "    options: id= (the arc's id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, "
                 "partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: a wrong form of arguments, an unknown keyword, a keyword for an end (the ends are positional), r= or ccw=, near=.\n"
                 "    ValueError: the arc cannot be placed (the message names the form it needs); nothing is drawn.\n\n"
                 "Examples:\n"
                 "    sk.Arc(5, 90).Arc((3, 1), tangent=(0, 1)).Arc((0, 0), (2, 0), 5)\n"
                 "    sk.Arc(\"lower\", \"upper\", coincident=\"boss\")"))
        .def("Slot", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawSlot(s, "Slot", args, kw); }, ref,
             Doc("Slot(l: float, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Slot(dh: float, dv: float, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Slot(to: Point | Id, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Slot(start: Point | Id, end: Point | Id, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Slot(*ends: Point | Id | float | Constraint, until: Id | Shape | SketchUntil | None = None, "
                 "parallel: Id | Shape | None = None, perpendicular: Id | Shape | None = None, "
                 "angle: float | tuple[float, Id] | None = None, coincident: Id | None = None, "
                 "tangent: Id | Shape | tuple[Id, Id] | None = None, distance: float | tuple[float, Point] | None = None, "
                 "through: Point | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw a straight slot: a line and its offset on both sides, closed by round caps.\n\n"
                 "Every form of Line, with the width w as the last number before the flags. The line is the slot's "
                 "spine, a construction element; the outline is its offset at w / 2 on both sides, the "
                 "caps half circles around the spine's ends. The pen goes on at the spine's end in its direction "
                 "(a new chain). Slot.k is the wire of the outline, with Slot.k.spine, Slot.k.left and Slot.k.right "
                 "(left and right of the spine's direction), Slot.k.startcap and Slot.k.endcap. A Sketch keeps the "
                 "slot a slot when the spine moves, and has the dimension Slot.k.w (sk[\"Slot.1.w\"] = 6; sk.Update()); "
                 "with auto_constraint the spine gets what Line gets. Remove(\"Slot.1\") removes the whole slot.\n\n"
                 "Args:\n"
                 "    w: the width of the slot, across the spine; positive.\n"
                 "    l: as for Line, which draws the spine.\n"
                 "    dh: as for Line, which draws the spine.\n"
                 "    dv: as for Line, which draws the spine.\n"
                 "    to: as for Line, which draws the spine.\n"
                 "    start: as for Line, which draws the spine.\n"
                 "    end: as for Line, which draws the spine.\n"
                 "    ends: as for Line, which draws the spine.\n"
                 "    flags: as for Line, which draws the spine.\n"
                 "    until: as for Line, which draws the spine.\n"
                 "    parallel: as for Line, which draws the spine.\n"
                 "    perpendicular: as for Line, which draws the spine.\n"
                 "    angle: as for Line, which draws the spine.\n"
                 "    coincident: as for Line, which draws the spine.\n"
                 "    tangent: as for Line, which draws the spine.\n"
                 "    distance: as for Line, which draws the spine.\n"
                 "    through: as for Line, which draws the spine.\n"
                 "    reverse: as for Line, which draws the spine.\n"
                 "    options: id= replaces Slot.k (id='groove' gives groove.spine, groove.left, ...), and the outline's "
                 "properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: no width as the last number, another form of arguments, an unknown keyword.\n"
                 "    ValueError: w is not positive; the spine has no length or cannot be placed; id is taken.\n\n"
                 "Examples:\n"
                 "    sk.Slot(10, 4)\n"
                 "    sk.Slot((0, 0), (0, 10), 2, id=\"groove\")"))
        .def("ArcSlot", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawSlot(s, "ArcSlot", args, kw); }, ref,
             Doc("ArcSlot(r: float, sweep: float, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "ArcSlot(to: Point | Id, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "ArcSlot(to: Point | Id, r: float, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "ArcSlot(start: Point | Id, end: Point | Id, r: float, w: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "ArcSlot(*ends: Point | Id | float | Constraint, sweep: float | None = None, center: Point | None = None, "
                 "concentric: Id | None = None, coincident: Id | None = None, "
                 "tangent: tuple[float, float] | Id | Shape | tuple[Id, Id] | None = None, through: Point | None = None, "
                 "until: Id | Shape | SketchUntil | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw an arc slot: an arc and its offset on both sides, closed by round caps.\n\n"
                 "Every form of Arc, with the width w as the last number before the flags. The arc is the slot's spine, "
                 "a construction element; the outline is its offset at w / 2 on both sides, the caps half circles "
                 "around the spine's ends. The pen goes on at the spine's end in its direction (a new chain). The ids "
                 "are those of Slot: ArcSlot.k is the wire, with ArcSlot.k.spine, .left, .right, .startcap and "
                 ".endcap; a Sketch has the dimension ArcSlot.k.w and keeps the slot a slot when the spine moves. "
                 "Remove(\"ArcSlot.1\") removes the whole slot.\n\n"
                 "Args:\n"
                 "    w: the width of the slot, across the spine; positive and below the diameter 2 r of the spine arc.\n"
                 "    r: as for Arc, which draws the spine.\n"
                 "    sweep: as for Arc, which draws the spine.\n"
                 "    to: as for Arc, which draws the spine.\n"
                 "    start: as for Arc, which draws the spine.\n"
                 "    end: as for Arc, which draws the spine.\n"
                 "    ends: as for Arc, which draws the spine.\n"
                 "    flags: as for Arc, which draws the spine.\n"
                 "    center: as for Arc, which draws the spine.\n"
                 "    concentric: as for Arc, which draws the spine.\n"
                 "    coincident: as for Arc, which draws the spine.\n"
                 "    tangent: as for Arc, which draws the spine.\n"
                 "    through: as for Arc, which draws the spine.\n"
                 "    until: as for Arc, which draws the spine.\n"
                 "    reverse: as for Arc, which draws the spine.\n"
                 "    options: id= replaces ArcSlot.k, and the outline's properties name=, maxh=, minh=, hpref=, layer=, "
                 "col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: no width as the last number, another form of arguments, an unknown keyword.\n"
                 "    ValueError: w is not positive or not below 2 r; the spine has no length or cannot be placed; id is "
                 "taken.\n\n"
                 "Examples:\n"
                 "    sk.ArcSlot(10, 90, 2)\n"
                 "    sk.ArcSlot((10, 0), (0, 10), 10, 2, id=\"track\")"))
        .def("Keyhole", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawSlot(s, "Keyhole", args, kw); }, ref,
             Doc("Keyhole(l: float, w: float, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Keyhole(dh: float, dv: float, w: float, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Keyhole(to: Point | Id, w: float, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Keyhole(start: Point | Id, end: Point | Id, w: float, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Keyhole(*ends: Point | Id | float | Constraint, until: Id | Shape | SketchUntil | None = None, "
                 "parallel: Id | Shape | None = None, perpendicular: Id | Shape | None = None, "
                 "angle: float | tuple[float, Id] | None = None, coincident: Id | None = None, "
                 "tangent: Id | Shape | tuple[Id, Id] | None = None, distance: float | tuple[float, Point] | None = None, "
                 "through: Point | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw a keyhole: a straight slot that starts in a round hole.\n\n"
                 "Every form of Slot, with the hole's radius r as the last number. The spine is a construction line; "
                 "the hole is centered at its start. The outline is one loop: the two sides run from the hole's circle to "
                 "the round end cap, and the hole is the arc of its circle between the sides, the long way round. The pen "
                 "goes on at the spine's end in its direction (a new chain). Keyhole.k is the wire of the outline, with "
                 "Keyhole.k.spine, Keyhole.k.left and Keyhole.k.right (left and right of the spine's direction), "
                 "Keyhole.k.endcap and Keyhole.k.hole. A Sketch keeps the keyhole a keyhole when the spine moves, and has "
                 "the dimensions Keyhole.k.w and Keyhole.k.r (sk[\"Keyhole.1.r\"] = 4; sk.Update()); with auto_constraint "
                 "the spine gets what Line gets. Remove(\"Keyhole.1\") removes the whole keyhole.\n\n"
                 "Args:\n"
                 "    r: the radius of the hole; larger than w / 2.\n"
                 "    w: the width of the slot, across the spine; positive.\n"
                 "    l: as for Line, which draws the spine.\n"
                 "    dh: as for Line, which draws the spine.\n"
                 "    dv: as for Line, which draws the spine.\n"
                 "    to: as for Line, which draws the spine.\n"
                 "    start: as for Line, which draws the spine.\n"
                 "    end: as for Line, which draws the spine.\n"
                 "    ends: as for Line, which draws the spine.\n"
                 "    flags: as for Line, which draws the spine.\n"
                 "    until: as for Line, which draws the spine.\n"
                 "    parallel: as for Line, which draws the spine.\n"
                 "    perpendicular: as for Line, which draws the spine.\n"
                 "    angle: as for Line, which draws the spine.\n"
                 "    coincident: as for Line, which draws the spine.\n"
                 "    tangent: as for Line, which draws the spine.\n"
                 "    distance: as for Line, which draws the spine.\n"
                 "    through: as for Line, which draws the spine.\n"
                 "    reverse: as for Line, which draws the spine.\n"
                 "    options: id= replaces Keyhole.k (id='hook' gives hook.spine, hook.left, ...), and the outline's "
                 "properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: no width and radius as the last numbers, another form of arguments, an unknown keyword.\n"
                 "    ValueError: w is not positive; r is not larger than w / 2; the spine is not longer than "
                 "sqrt(r^2 - (w / 2)^2) or cannot be placed; id is taken.\n\n"
                 "Examples:\n"
                 "    sk.Keyhole(10, 2, 3)\n"
                 "    sk.Keyhole((0, 0), (0, 10), 2, 3, id=\"hook\")"))
        .def("Polygon", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawPolygon(s, "Polygon", args, kw); }, ref,
             Doc("Polygon(r: float, n: int, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Polygon(h: float, v: float, r: float, n: int, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Polygon(n: int, /, *flags: Constraint, center: Point | None = None, concentric: Id | None = None, "
                 "coincident: Id | None = None, through: Point | None = None, "
                 "tangent: Id | Shape | tuple[Id, Id] | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw a regular polygon of n equal sides, its corners on a circle.\n\n"
                 "Every form of Circle, with the number of sides n as the last number. The circle is a construction "
                 "element, Polygon.k.circle, of radius r around the pen's position, around (h, v) or placed by the "
                 "helpers; the first corner is on it along the pen's drawing direction from its center, the others "
                 "follow counterclockwise. Polygon.k.Line.1 to .Line.n run from the first corner, and Polygon.k is their "
                 "wire (sk('Polygon.1'), Offset, Remove, the patterns). The pen and its direction stay. A Sketch keeps "
                 "the polygon regular (the corners on the circle, equal sides) and has the dimension Polygon.k.r "
                 "(sk[\"Polygon.1.r\"] = 3; sk.Update()); with auto_constraint the circle gets what Circle gets. "
                 "Remove(\"Polygon.1\") removes the whole polygon.\n\n"
                 "Args:\n"
                 "    n: the number of sides, an int of at least 3.\n"
                 "    r: the radius of the circle the corners are on.\n"
                 "    h: the center's h.\n"
                 "    v: the center's v.\n"
                 "    flags: as for Circle, which draws the circle.\n"
                 "    center: as for Circle.\n"
                 "    concentric: as for Circle.\n"
                 "    coincident: as for Circle.\n"
                 "    through: as for Circle.\n"
                 "    tangent: as for Circle.\n"
                 "    reverse: as for Circle.\n"
                 "    options: id= replaces Polygon.k (id='nut' gives nut.circle, nut.Line.1, ...), and the sides' "
                 "properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: n is no int or not the last number, another form of arguments, an unknown keyword.\n"
                 "    ValueError: n is below 3; the circle cannot be placed; id is taken.\n\n"
                 "Examples:\n"
                 "    sk.Polygon(5, 6)\n"
                 "    sk.Polygon(0, 0, 5, 8, id=\"gear\")"))
        .def("Hexagon", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawPolygon(s, "Hexagon", args, kw); }, ref,
             Doc("Hexagon(af: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Hexagon(h: float, v: float, af: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Hexagon(af: float, /, *flags: Constraint, center: Point | None = None, concentric: Id | None = None, "
                 "coincident: Id | None = None, through: Point | None = None, "
                 "tangent: Id | Shape | tuple[Id, Id] | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw a regular hexagon of width af across its flats, as for a nut.\n\n"
                 "A Polygon of six sides whose circle has the radius af / sqrt(3): the first corner is along the pen's "
                 "drawing direction from the center, so two flats run parallel to it. Hexagon.k.Line.1 to .Line.6 run "
                 "from the first corner counterclockwise, Hexagon.k.circle is the construction circle, and Hexagon.k is "
                 "the wire of the lines. The pen and its direction stay. A Sketch keeps the hexagon regular and has the "
                 "dimension Hexagon.k.af, the distance between two opposite flats (sk[\"Hexagon.1.af\"] = 17; "
                 "sk.Update()). Remove(\"Hexagon.1\") removes the whole hexagon.\n\n"
                 "Args:\n"
                 "    af: the width across flats; positive.\n"
                 "    h: the center's h.\n"
                 "    v: the center's v.\n"
                 "    flags: as for Circle, which draws the circle.\n"
                 "    center: as for Circle.\n"
                 "    concentric: as for Circle.\n"
                 "    coincident: as for Circle.\n"
                 "    through: as for Circle.\n"
                 "    tangent: as for Circle.\n"
                 "    reverse: as for Circle.\n"
                 "    options: id= replaces Hexagon.k (id='nut' gives nut.circle, nut.Line.1, ...), and the sides' "
                 "properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: af is no number or not the last number, another form of arguments, an unknown keyword.\n"
                 "    ValueError: af is not positive; the circle cannot be placed; id is taken.\n\n"
                 "Examples:\n"
                 "    sk.Hexagon(13)\n"
                 "    sk.Hexagon(10, 5, 13, id=\"nut\")"))
        .def("Circle", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawCircle(s, args, kw); }, ref,
             Doc("Circle(r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Circle(center: Point, r: float, /, *flags: Constraint, **options) -> " + sk + "\n"
                 "Circle(*r: float | Constraint, center: Point | None = None, concentric: Id | None = None, "
                 "coincident: Id | None = None, through: Point | None = None, "
                 "tangent: Id | Shape | tuple[Id, Id] | None = None, reverse: bool = False, **options) -> " + sk + "\n\n"
                 "Draw a circle around the pen's position, around a center (h, v), or placed by the helpers.\n\n"
                 "With helpers the center is center= or concentric=, else the pen's position; the radius is r, "
                 "coincident=, through= or tangent=. The circle is one circle edge starting at the point r from the center "
                 "along +h.\n\n"
                 "Args:\n"
                 "    r: the radius; in the helper form at most one radius, then the flags.\n"
                 "    flags: px.Fix.\n"
                 "    center: the center, a point (h, v) or (x, y, z) on the plane; center= also a point id.\n"
                 "    concentric: a point id or an arc whose center is the circle's.\n"
                 "    coincident: an arc or circle whose radius the circle takes.\n"
                 "    through: a point or point id on the circle.\n"
                 "    tangent: a target x the circle touches: of a line where the foot from the center lies, of an arc "
                 "where the line of the centers meets it; on the edge first, then nearest the target's point, else the "
                 "smaller radius. (a, b) with r: the circle touching both, as Arc(r, tangent=(a, b)). (target, point): "
                 "the solution near the point. A spline is touched on its approximation, within spline_tol.\n"
                 "    reverse: take the next solution.\n"
                 "    options: id= (the circle's id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, "
                 "partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: a wrong form of arguments, an unknown keyword, near=.\n"
                 "    ValueError: the circle cannot be placed.\n\n"
                 "Examples:\n"
                 "    sk.Circle((10, 0), 5)\n"
                 "    sk.Circle(2, center=\"Line.1.end\", id=\"hole\")"))
        .def("Spline", [](S &s, const py::args &args, const py::kwargs &kw) -> S & {
            const CallArgs a = Split(args, "Spline");
            const AutoScope<S> scope(s, a.autoFlag, "Spline");
            HelpersOf(s, kw, "Spline", {"periodic", "tol", "tangents", "start_from_localpos", "style"});
            if (a.values.size() != 1)
                throw py::type_error("Spline(points), then flags; periodic=, tangents=, start_from_localpos=, style=");
            std::vector<Vertex> points;
            for (const py::handle &p : a.values[0])
                points.push_back(PointArg(s.Axes(), p, "Spline"));
            std::map<size_t, Vertex> tangents;
            if (kw.contains("tangents") && !kw["tangents"].is_none())
                for (const auto &[k, t] : kw["tangents"].cast<py::dict>())
                    tangents[k.cast<size_t>()] = PointArg(s.Axes(), t, "Spline");
            s.Spline(points, KwOpt<bool>(kw, "periodic").value_or(false), tangents, KwOpt<bool>(kw, "start_from_localpos").value_or(true),
                     a.flags, OptionsOf(kw), StyleArg(kw.contains("style") ? py::object(kw["style"]) : py::none(), "Spline"));
            return s;
        }, ref,
             Doc("Spline(points: list[tuple[float, float]], /, *flags: Constraint, periodic: bool = False, "
                 "tangents: dict[int, tuple[float, float]] | None = None, start_from_localpos: bool = True, "
                 "style: str | None = None, tol: float | None = None, **options) -> " + sk + "\n\n"
                 "Draw a spline from the pen's position through points; its points are point elements.\n\n"
                 "Args:\n"
                 "    points: the points (h, v) it passes, after the start.\n"
                 "    flags: px.Fix; px.Tangent (with the element before it).\n"
                 "    periodic: closed back to its start.\n"
                 "    tangents: {index: (dh, dv)}, directions at some of the points, index 0 the start; they become "
                 "angle unknowns.\n"
                 "    start_from_localpos: False starts at the first point instead of the pen's position.\n"
                 "    style: 'averaged' (planex's: closed without tangents the C2 periodic cubic) or 'clamped' (OCC's "
                 "Geom2dAPI_Interpolate, netgen's splines; a sketch keeps tangent directions only, so two points with "
                 "one tangent of another length than 1 differ from netgen's); default config.spline_style.\n"
                 "    tol: ignored.\n"
                 "    options: id= (the spline's id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, "
                 "partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: not one list of points, a point that is no (h, v), or an unknown keyword.\n\n"
                 "Examples:\n"
                 "    sk.Spline([(5, 2), (10, 0)])\n"
                 "    sk.Spline([(5, 2), (10, 0), (5, -2)], periodic=True, tangents={0: (0, 1)})"))
        .def("Curve", [](S &s, const py::args &args, const py::kwargs &kw) -> S & {
            const CallArgs a = Split(args, "Curve");
            const AutoScope<S> scope(s, a.autoFlag, "Curve");
            HelpersOf(s, kw, "Curve", {});
            if (a.values.size() != 2)
                throw py::type_error("Curve(point, range), then flags: the point (h, v) as two expressions in t or a callable t -> (h, v), "
                                     "the range (t0, t1)");
            const std::string text = CurveText("Curve", a.values[0], a.values[1]);
            if (IsCurveCallable(a.values[0])) {
                s.Curve(CallableShape(a.values[0], a.values[1], text), std::nullopt, a.flags, OptionsOf(kw));
                return s;
            }
            sketch::CurveFormula f = ParsedCurve(a.values[0], a.values[1], text, CurveNames(s));
            if constexpr (HasConstraints<S>)
                s.CurveDimensions(f);
            const Curve<double, Vertex> shape = f.Shape(f.used);
            s.Curve(shape, std::move(f), a.flags, OptionsOf(kw));
            return s;
        }, ref,
             Doc("Curve(point: tuple[str, str] | Callable[[float], Point], range: tuple[float | str, float | str], /, *flags: Constraint, "
                 "**options) -> " + sk + "\n\n"
                 "Draw a parametric curve, the spline fitted to it to about 1e-12 of its size, where its formula puts it.\n\n"
                 "The point is (h, v) as two expressions in the parameter t: numbers, units, pi, + - * / ^, sqrt, abs, min, max, "
                 "sin, cos, tan, asin, acos, atan2 (angles in degrees, t too where it is an angle), exp, log (natural), sinh, cosh, tanh, asinh, acosh, "
                 "atanh" +
                 (HasConstraints<S> ? std::string(", the ids of driving dimensions (the curve refits when they change)") : std::string()) +
                 ", and h in v or v in h (the other coordinate at the same t). Or a callable t -> (h, v), a tuple, list or "
                 "array, called once per t while the curve is drawn (then fitted by a cubic to 1e-10 of its size). The curve "
                 "goes on from the pen if the pen is at its start, else it starts on its own; the pen goes to its end. Its end "
                 "at its start closes it (smooth there where the formula is periodic). " +
                 (HasConstraints<S> ? std::string("It moves and turns as one (3 degrees of freedom), fixed by default (a Fix you may "
                                                  "remove); its references are the curve, .start and .end. ")
                                    : std::string("Its references are the curve, .start and .end. ")) +
                 "Copies, mirrors, patterns, offsets and trims take the fitted spline. h, v and t are no ids.\n\n"
                 "Args:\n"
                 "    point: (h, v), two expressions in t, (\"t\", \"0.1*t^2\") for v = 0.1 h^2; or a callable t -> (h, v).\n"
                 "    range: (t0, t1), numbers" +
                 (HasConstraints<S> ? std::string(" or expressions of driving dimensions") : std::string()) + "; a callable's numbers.\n"
                 "    flags: px.Tangent (with the element before it), px.Fix.\n"
                 "    options: id= (the curve's id, default Curve.N), and its properties name=, maxh=, minh=, hpref=, layer=, "
                 "col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: a point that is no two expressions or callable, a range that is no two numbers or expressions, "
                 "a callable that gives no point, an unknown keyword.\n"
                 "    ValueError: a name the formula cannot use (" +
                 (HasConstraints<S> ? std::string("a driven or reference dimension, a measure of the solved geometry, ")
                                    : std::string("any dimension, ")) +
                 "an unknown id), h and v using each other or a coordinate itself, a formula out of its domain in the range, an "
                 "empty range, a curve of no length.\n\n"
                 "Examples:\n"
                 "    sk.Curve((\"t\", \"0.1*t^2\"), (0, 10))\n"
                 "    sk.Curve((\"10*(1 - cos(t))/2\", \"sqrt(h)\"), (0, 180), id=\"airfoil\")"))
        .def("Rectangle", [](S &s, double l, double w, const py::kwargs &kw) -> S & {
            if (kw.contains("id_wire"))
                throw py::type_error("Rectangle(id_wire=...): a rectangle is the wire of its four lines under its own id (Rectangle.1, "
                                     "lines Rectangle.1.Line.1 to .Line.4); name it with id=, Rectangle(l, w, id='plate')");
            HelpersOf(s, kw, "Rectangle", {});
            s.Rectangle(l, w, OptionsOf(kw));
            return s;
        }, py::arg("l"), py::arg("w"), ref,
             Doc("Rectangle(l: float, w: float, **options) -> " + sk + "\n"
                 "Rectangle(p0: Point, p1: Point, /, **options) -> " + sk + "\n"
                 "Rectangle(p0: Point, p1: Point, p2: Point, /, **options) -> " + sk + "\n\n"
                 "Draw a rectangle from the pen, l along the drawing direction and w to its left, or through its corners.\n\n"
                 "Its four lines are Rectangle.k.Line.1 to .Line.4 from the start corner along the drawing direction "
                 "(counterclockwise), and Rectangle.k is their wire (sk('Rectangle.1'), Offset('Rectangle.1', 2), Remove, "
                 "the patterns). It is not part of a chain; its three right angles are Rectangle.k.Perpendicular.1 to .3; "
                 "the pen stays at the start corner.\n\n"
                 "Through corners: two opposite corners p0, p1 (in any order) give a rectangle with its sides along h and v, "
                 "starting at the corner of the smallest h and v (in a px.Sketch its first side is also Horizontal). Three "
                 "points: p0 to p1 is a side and the opposite side runs through p2, which sets the width; it starts at p0 "
                 "and runs counterclockwise (with p2 to the right of p0 -> p1, Line.1 leaves p0 along its other side and "
                 "Line.4 runs p1 -> p0). A corner is a point (h, v) or a selected point such as sk('A'), which it is tied "
                 "to (p2: the opposite side passes through it). The pen stays where it was.\n\n"
                 "Args:\n"
                 "    l: the length along the drawing direction.\n"
                 "    w: the width to the left of it.\n"
                 "    p0: a corner.\n"
                 "    p1: the opposite corner (two points), or the corner after p0 (three points).\n"
                 "    p2: a point on the side opposite p0 -> p1.\n"
                 "    options: id= replaces Rectangle.k (id='plate' gives plate.Line.1 to plate.Line.4 and the wire "
                 "'plate'); the lines' properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    ValueError: id is taken or no valid id, a side within rounding of 0, a selection of more than one point.\n"
                 "    TypeError: id_wire= (the id names the wire), an unknown keyword, a corner that is no point or an id "
                 "string, or four points.\n\n"
                 "Examples:\n"
                 "    sk.Rectangle(40, 20, id='plate', maxh=2)\n"
                 "    sk.Rectangle((0, 0), (40, 20))"))
        .def("Rectangle", [](S &s, const py::args &args, const py::kwargs &kw) -> S & {
            if (kw.contains("id_wire"))
                throw py::type_error("Rectangle(id_wire=...): name the rectangle with id=");
            HelpersOf(s, kw, "Rectangle", {});
            if (args.size() != 2 && args.size() != 3)
                throw py::type_error("Rectangle takes l and w, two opposite corners or three points (p0 to p1 a side, p2 on the "
                                     "opposite side), not " + std::to_string(args.size()) + " arguments");
            std::vector<Vertex> corners;
            std::vector<std::optional<std::string>> at;
            for (const py::handle c : args) {
                if (py::isinstance<py::str>(c))
                    throw py::type_error("Rectangle('" + c.cast<std::string>() + "', ...): a corner is a point (h, v) or a selected "
                                         "point such as sk('A'), not an id string");
                if (py::hasattr(c, "_items") || py::hasattr(c, "_ref_in")) {
                    const std::vector<std::string> ids = RefsIn(s, c, "Rectangle");
                    if (ids.size() != 1)
                        throw py::value_error("Rectangle: a corner is one point; the selection holds " + std::to_string(ids.size()));
                    corners.push_back(s.Location(ids[0]));
                    at.push_back(ids[0]);
                } else {
                    corners.push_back(PointArg(s.Axes(), c, "Rectangle"));
                    at.push_back(std::nullopt);
                }
            }
            s.Rectangle(corners, at, OptionsOf(kw));
            return s;
        }, ref, "")
        .def("RectangleC", [](S &s, const py::args &args, const py::kwargs &kw) -> S & {
            if (kw.contains("id_wire"))
                throw py::type_error("RectangleC(id_wire=...): a rectangle is the wire of its four lines under its own id (RectangleC.1, "
                                     "lines RectangleC.1.Line.1 to .Line.4); name it with id=, RectangleC(l, w, id='plate')");
            HelpersOf(s, kw, "RectangleC", {});
            const auto size = [&](size_t k) {
                if (!IsNumber(args[k]))
                    throw py::type_error("RectangleC: l and w are numbers; RectangleC(l, w) or RectangleC(center, l, w)");
                return args[k].cast<double>();
            };
            if (args.size() == 2) {
                s.RectangleC(std::nullopt, std::nullopt, size(0), size(1), OptionsOf(kw));
                return s;
            }
            if (args.size() != 3)
                throw py::type_error("RectangleC takes l and w, or the center first: RectangleC(l, w), RectangleC(center, l, w)");
            const py::handle c = args[0];
            if (py::isinstance<py::str>(c))
                throw py::type_error("RectangleC('" + c.cast<std::string>() + "', ...): the center is a point (h, v) or a selected point "
                                     "such as sk('A'), not an id string");
            if (py::hasattr(c, "_items") || py::hasattr(c, "_ref_in")) {
                const std::vector<std::string> ids = RefsIn(s, c, "RectangleC");
                if (ids.size() != 1)
                    throw py::value_error("RectangleC: the center is one point; the selection holds " + std::to_string(ids.size()));
                s.RectangleC(std::nullopt, ids[0], size(1), size(2), OptionsOf(kw));
            } else
                s.RectangleC(PointArg(s.Axes(), c, "RectangleC"), std::nullopt, size(1), size(2), OptionsOf(kw));
            return s;
        }, ref,
             Doc("RectangleC(l: float, w: float, /, **options) -> " + sk + "\n"
                 "RectangleC(center: Point, l: float, w: float, /, **options) -> " + sk + "\n\n"
                 "Draw a rectangle centered at the pen or at center, l along the drawing direction and w across it.\n\n"
                 "Its four lines are RectangleC.k.Line.1 to .Line.4 from the corner behind and to the right of the center, along "
                 "the drawing direction (counterclockwise), and RectangleC.k is their wire. The construction line "
                 "RectangleC.k.diagonal runs from the first corner to the third, the construction point RectangleC.k.center sits "
                 "at its middle; a selected point as center ties the center to it. It is not part of a chain; the pen goes to "
                 "the center and keeps its direction.\n\n"
                 "Args:\n"
                 "    center: the center: a point (h, v) or a selected point such as sk('A') (tied to it); default the pen.\n"
                 "    l: the length along the drawing direction.\n"
                 "    w: the width across it.\n"
                 "    options: id= replaces RectangleC.k (id='plate' gives plate.Line.1 to plate.Line.4, plate.center and the wire "
                 "'plate'); the lines' properties name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=.\n\n"
                 "Raises:\n"
                 "    ValueError: a side within rounding of 0, a selection of more than one point, id is taken or no valid id.\n"
                 "    TypeError: l or w is no number, center is an id string or no point, id_wire= or an unknown keyword.\n\n"
                 "Examples:\n"
                 "    sk.RectangleC(40, 20)\n"
                 "    sk.RectangleC((10, 5), 40, 20, id='plate')"))
        .def("Direction", [](S &s, const py::args &args) -> S & {
            if (args.size() == 2 && IsNumber(args[0]) && IsNumber(args[1]))
                s.Direction(args[0].cast<double>(), args[1].cast<double>());
            else if (args.size() == 1) {
                const Vertex d = PointArg(args[0], "Direction");
                s.Direction(d.x, d.y);
            } else
                throw py::type_error("Direction((h, v)) or Direction(dh, dv)");
            return s;
        }, ref,
             Doc("Direction(d: tuple[float, float], /) -> " + sk + "\n"
                 "Direction(dh: float, dv: float, /) -> " + sk + "\n\n"
                 "Set the pen's direction.\n\n"
                 "Args:\n"
                 "    d: the direction (dh, dv).\n"
                 "    dh: the direction's h; with dv.\n"
                 "    dv: the direction's v.\n\n"
                 "Raises:\n"
                 "    TypeError: another form of arguments.\n\n"
                 "Examples:\n"
                 "    sk.Direction((0, 1)).Line(5)\n"
                 "    sk.Direction(1, 1)"))
        .def("Turn", &S::Turn, py::arg("angle"), ref,
             Doc("Turn(angle: float) -> " + sk + "\n\n"
                 "Turn the pen's direction.\n\n"
                 "Args:\n"
                 "    angle: the turn in degrees, positive counterclockwise.\n\n"
                 "Examples:\n"
                 "    sk.Line(5).Turn(90).Line(5)"))
        .def("Close", [](S &s, const py::args &args, const py::kwargs &kw) -> S & {
            const CallArgs a = Split(args, "Close");
            const AutoScope<S> scope(s, a.autoFlag, "Close");
            if (!a.values.empty())
                throw py::type_error("Close takes only flags");
            HelpersOf(s, kw, "Close", {"id_wire"});
            s.Close(a.flags, OptionsOf(kw), KwOpt<std::string>(kw, "id_wire"));
            return s;
        }, ref,
             Doc("Close(*flags: Constraint, id_wire: str | None = None, **options) -> " + sk + "\n\n"
                 "Draw a line back to where the chain started; the chain ends.\n\n"
                 "Args:\n"
                 "    flags: as for Line.\n"
                 "    id_wire: replaces the chain's wire id Chain.k (for the patterns, RigidGroup, Remove, Edge, Measure).\n"
                 "    options: id= (the closing line's id), and its properties name=, maxh=, minh=, hpref=, layer=, col=, "
                 "partition=, construction=.\n\n"
                 "Raises:\n"
                 "    TypeError: a positional argument other than a flag, or an unknown keyword.\n\n"
                 "Examples:\n"
                 "    sk.Line(10).Turn(90).Line(5).Close()\n"
                 "    sk.Close(id_wire=\"outline\")"))
        .def("Finish", &S::Finish, py::arg("id_wire") = py::none(), ref,
             Doc("Finish(id_wire: str | None = None) -> " + sk + "\n\n"
                 "End the chain without closing it; the next one starts where the pen is.\n\n"
                 "Args:\n"
                 "    id_wire: replaces the chain's wire id Chain.k (for the patterns, RigidGroup, Remove, Edge, Measure).\n\n"
                 "Raises:\n"
                 "    ValueError: id_wire is taken or invalid, or no chain is drawn.\n\n"
                 "Examples:\n"
                 "    sk.Line(10).Finish()\n"
                 "    sk.Line(10).Turn(90).Line(5).Finish(id_wire=\"rail\")"))
        .def_property_readonly("wires", [](const S &s) {
            py::dict out;
            for (const auto &[id, elements] : s.NamedWires())
                out[py::str(id)] = elements;
            return out;
        }, "wires: dict[str, list[str]]\n\nThe wires, {wire id: [element ids]}: every pen chain (Chain.1, Chain.2, ..., or its id_wire), every "
           "Rectangle and every Offset.")
        .def("Vertex", NamingIds("Vertex", +[](S &s, const std::string &id, const py::object &point) -> S & {
            CheckUserId(id);
            s.NamedPoint(id, point.is_none() ? s.Position() : PointArg(s.Axes(), Unpen(point, false), "Vertex"));
            return s;
        }), py::arg("id"), py::arg("point") = py::none(), ref,
           Doc("Vertex(id: str, point: tuple[float, float] | None = None) -> " + sk + "\n\n"
               "Add a construction point; it is constrained like any point.\n\n"
               "Args:\n"
               "    id: the point's id.\n"
               "    point: where it is, (h, v) or sk.pen; default the pen's position.\n\n"
               "Raises:\n"
               "    ValueError: id is taken or no valid id.\n"
               "    TypeError: point is no (h, v).\n\n"
               "Examples:\n"
               "    sk.Vertex(\"A\", (3, 4))\n"
               "    sk.Line(5).Vertex(\"tip\")"))
        .def_property_readonly("points", [](S &s) {
            py::dict out;
            for (const auto &[id, p] : s.NamedPoints())
                out[py::str(id)] = Tuple2(p);
            return out;
        }, "points: dict[str, tuple[float, float]]\n\nThe points with ids given to them, {id: (h, v)}.")
        .def("_SetProperty", &SetSketchProperty<S>, py::arg("ids"), py::arg("field"), py::arg("value"),
             "test hook of the selections' properties: field (name, maxh, minh, hpref, layer, col, partition, construction) of "
             "the elements ids name set to value; points take name, maxh, hpref and are left out of the others")
        .def("_PointElement", &S::PointElementAt, py::arg("ref"),
             "test hook of the selections: the point element a point reference names (id.start, id.end, id.center), or None")
        .def("_KindById", &S::KindById, py::arg("id"),
             "for the selections: the kind (Point, Line, Arc, ...) of the element the id names, or None")
        .def("_Chosen", &S::Chosen, py::arg("id"),
             "for the selections: [(kind, id)] of the element the id names and the elements under it, or []")
        .def("_RenamePoint", &S::RenamePoint, py::arg("old"), py::arg("id"), "for the Python layer: point.id = id")
        .def("_FromEnd", &S::FromEnd, py::arg("id"), "for the selections: Chain.-1 as the id it counts to")
        .def("_Spans", &S::Spans, "for the selections: the cut elements kept as the spans of their pieces (Line.3 of Line.3.1, ...)")
        .def("_Properties", &SketchProperties<S>, py::arg("ids"), py::arg("field"),
             "test hook of the selections' properties: the values of field of the elements ids name that take it")
        .def("Measure", NamingIds("Measure", +[](S &s, const py::object &name, const py::object &to, bool project) {
            const std::optional<std::string> id = name.is_none() ? std::nullopt : std::optional(RefIn(s, name, "Measure"));
            const py::object target = py::isinstance<py::str>(to) ? py::str(RefIn(s, to, "Measure")) : to;
            return MeasureWith(NamedInSketch(s, id), target, project,
                               [&](const std::string &n) { return NamedInSketch(s, n); });
        }), py::arg("name") = py::none(), py::arg("to") = py::none(), py::arg("project") = false,
           "Measure(name: Id | None = None, to: Point | Point3 | Shape | Id | None = None, project: bool = False) -> dict\n\n"
           "Measure an element, a point or named edges as Shape.Measure does, alone or against a target.\n\n"
           "Args:\n"
           "    name: an element's id, a point ('Line.1.end') or a name the edges carry (drawn and construction); "
           "default the region of Face()/Shape() and all drawn edges.\n"
           "    to: an id, a name, or anything Distance takes (also OCC shapes).\n"
           "    project: measure to the target projected into the sketch's plane.\n\n"
           "Returns:\n"
           "    Without to: type, faces, area, edges, length, bounding_box, and where it has mass center, inertia, Ih, "
           "Iv, Ihv, I1, I2, principal_angle (a circle or arc also radius and angle). With to: distance, du, dv, dx, dy, "
           "dz, points (the nearest pair) and, for two lines, angle.\n\n"
           "Raises:\n"
           "    ValueError: no element, point or edge has the name.\n\n"
           "Examples:\n"
           "    sk.Measure(\"Line.1\")[\"length\"]\n"
           "    sk.Measure(\"Circle.1\", to=\"Line.2\")[\"distance\"]")
        .def("SymmetryLine", &AddSymmetryLine<S>, py::arg("a"), py::arg("b"), py::pos_only(), py::kw_only(), py::arg("reverse") = false,
             py::arg("id") = py::none(), ref,
             Doc("SymmetryLine(a: Point | Id, b: Point | Id, /, *, reverse: bool = False, id: str | None = None) -> " + sk + "\n\n"
                 "Add the infinite construction line that mirrors a onto b.\n\n"
                 "Crossing lines: the bisector mapping a onto b best; parallel lines: the midline; points: their "
                 "perpendicular bisector; circles and arcs: that of the centers, whatever radii drawn. The line is free to "
                 "move and kept there by symmetry constraints: on the lines' carriers (never equal lengths), the points, "
                 "or the centers of the circles or arcs (with equal radii, never their ends).\n\n"
                 "Args:\n"
                 "    a: a line, point, circle or arc.\n"
                 "    b: one of the same kind.\n"
                 "    reverse: of two crossing lines the other bisector.\n"
                 "    id: the line's id, default SymmetryLine.N.\n\n"
                 "Raises:\n"
                 "    ValueError: a and b are not two points, two lines or two circles, or two circles or arcs share "
                 "their center.\n"
                 "    TypeError: an unknown keyword.\n\n"
                 "Examples:\n"
                 "    sk.SymmetryLine(\"Line.1\", \"Line.3\")\n"
                 "    sk.SymmetryLine(\"Circle.1\", \"Circle.2\", id=\"mid\")"))
        .def("Mirror", NamingIds("Mirror", +[](S &s, const py::object &axis) -> S & {
            Prepare(s, axis);
            s.Mirror(RefIn(s, axis, "Mirror"));
            return s;
        }), py::arg("axis"), ref,
             Doc("Mirror(axis: Id) -> " + sk + "\n"
                 "Mirror(ids: Ids, axis: Id) -> " + sk + "\n\n"
                 "Mirror the whole sketch, or some of its elements in place, at the line axis.\n\n"
                 "The whole sketch: its plane is mirrored, so in 3d it is its mirror image (the plane's normal turns "
                 "over); its coordinates, constraints and dimensions stay. Elements: they move with the points they reach "
                 "(what they share with other elements moves too); their constraints stay and the sketch solves again "
                 "(Mirrored adds mirrored copies instead).\n\n"
                 "Args:\n"
                 "    axis: px.H, px.V, a line or a symmetry line.\n"
                 "    ids: the elements: one id, a list or a selection.\n\n"
                 "Raises:\n"
                 "    ValueError: the constraints do not allow the mirrored place; nothing changes.\n\n"
                 "Examples:\n"
                 "    sk.Mirror(px.V)\n"
                 "    sk.Mirror(\"slot\", px.V)"))
        .def("Mirror", NamingIds("Mirror", +[](S &s, const py::object &ids, const py::object &axis) -> S & {
            Prepare(s, axis);
            const std::vector<std::string> list = RefsIn(s, ids, "Mirror");
            s.Mirror(list, RefIn(s, axis, "Mirror"));
            return s;
        }), py::arg("ids"), py::arg("axis"), ref, "")
        .def("Mirrored", [](const S &s, const py::object &axis) {
            S r = CopyOf(s, "Mirror");
            Prepare(r, axis);
            return r.Mirror(RefIn(r, axis, "Mirrored")), r;
        }, py::arg("axis"),
             Doc("Mirrored(axis: Id) -> " + sk + "\n"
                 "Mirrored(ids: Ids, axis: Id, auto_constraint: bool = " + ties + ", internal_constraints: bool = " + ties + ", "
                 "id: str | None = None) -> " + sk + "\n\n"
                 "A copy of the whole sketch mirrored at the line axis, or mirrored copies of elements added to this sketch.\n\n"
                 "Mirrored(axis) returns a new sketch, as Mirror(axis). Mirrored(ids, axis) adds copies of the elements, "
                 "with the points their lines and arcs end at and their properties, under 'Mirrored.N' (or id), named "
                 "after their sources ('Mirrored.1.Line.1'); 'Mirrored.N' is the wire of the copies. The originals stay; "
                 "it returns this sketch.\n\n"
                 "Args:\n"
                 "    axis: px.H, px.V, a line or a symmetry line.\n"
                 "    ids: the elements: one id, a list or a selection.\n"
                 "    auto_constraint: keep the copies symmetric to the originals about the axis, listed as one constraint "
                 "'Mirrored.N'; default True in a Sketch, False in a QuickSketch.\n"
                 "    internal_constraints: show the constraints among the originals on the copies ('Mirrored.1.<id>', "
                 "derived), or with auto_constraint=False give the copies those constraints; default True in a Sketch, False in a QuickSketch.\n"
                 "    id: replaces Mirrored.N.\n\n"
                 "Examples:\n"
                 "    right = sk.Mirrored(px.V)\n"
                 "    sk.Mirrored(\"Chain.1\", px.V)"))
        .def("Mirrored", NamingIds("Mirrored", +[](S &s, const py::object &ids, const py::object &axis, const py::object &tie,
                                                   const py::object &internal, const std::optional<std::string> &id) -> S & {
            Prepare(s, axis);
            const std::vector<std::string> list = RefsIn(s, ids, "Mirrored");
            s.Mirrored(list, RefIn(s, axis, "Mirrored"), TieArg(tie, "Mirrored", "auto_constraint"), TieArg(internal, "Mirrored", "internal_constraints"),
                       UserId(id));
            return s;
        }), py::arg("ids"), py::arg("axis"), py::arg("auto_constraint") = py::bool_(HasConstraints<S>),
           py::arg("internal_constraints") = py::bool_(HasConstraints<S>),
           py::arg("id") = py::none(), ref, "")
        .def("CircularPattern", NamingIds("CircularPattern", +[](S &s, const py::object &ids, const py::object &center, size_t count,
                                   const py::object &angleArg, const py::object &keep, const py::object &internal,
                                   const std::optional<std::string> &id) -> S & {
            Prepare(s, center);
            const std::vector<std::string> list = RefsIn(s, ids, "CircularPattern");
            const std::optional<double> angle = NumberOrAuto(angleArg, "CircularPattern", "angle");
            s.AddPattern(list, RefIn(s, center, "CircularPattern"), count, angle.value_or(360.0 / double(count)), false,
                         TieArg(keep, "CircularPattern", "auto_constraint"), TieArg(internal, "CircularPattern", "internal_constraints"),
                         UserId(id), !angle.has_value());
            return s;
        }), py::arg("ids"), py::arg("center"), py::arg("count"), py::arg("angle") = PlainAuto(),
             py::arg("auto_constraint") = py::bool_(HasConstraints<S>), py::arg("internal_constraints") = py::bool_(HasConstraints<S>),
             py::arg("id") = py::none(), ref,
             Doc("CircularPattern(ids: Ids, center: Id, count: int, angle: float | Auto = px.Auto, "
                 "auto_constraint: bool = " + ties + ", internal_constraints: bool = " + ties + ", id: str | None = None) -> " + sk + "\n\n"
                 "Add count instances of the elements turned about a point, the original the first.\n\n"
                 "The pattern is 'CircularPattern.N' (or id), the copies named after their sources by their step "
                 "('CircularPattern.1.3.bolt').\n\n"
                 "Args:\n"
                 "    ids: the elements: one id, a list or a selection.\n"
                 "    center: the point id they turn about.\n"
                 "    count: the number of instances, the original included.\n"
                 "    angle: the step in degrees; px.Auto: 360 / count, and the pattern stays spread over the full turn "
                 "when count changes.\n"
                 "    auto_constraint: tie the copies to the originals through the pattern, its step the dimension "
                 "'CircularPattern.N.angle' and its number of instances 'CircularPattern.N.count' (set like a dimension: "
                 "copies are added or the last ones removed), each copy listed as one constraint 'CircularPattern.N.k'; "
                 "default True in a Sketch, False in a QuickSketch.\n"
                 "    internal_constraints: show the constraints among the originals on the copies "
                 "('CircularPattern.1.3.<id>', derived), or with auto_constraint=False give the copies those constraints; "
                 "default True in a Sketch, False in a QuickSketch.\n"
                 "    id: replaces CircularPattern.N.\n\n"
                 "Examples:\n"
                 "    sk.CircularPattern(\"bolt\", \"Circle.1.center\", 6)\n"
                 "    sk.CircularPattern([\"Line.1\", \"Line.2\"], px.origin, 4, angle=30, id=\"ribs\")"))
        .def("LinearPattern", NamingIds("LinearPattern", +[](S &s, const py::object &ids, const py::object &direction, size_t count, double spacing,
                                 const py::object &direction2, const std::optional<size_t> &count2, const std::optional<double> &spacing2,
                                 const py::object &keep, const py::object &internal, const std::optional<std::string> &id) -> S & {
            const std::vector<std::string> list = RefsIn(s, ids, "LinearPattern");
            std::optional<sketch::PatternDir2> second;
            if (!direction2.is_none() || count2 || spacing2) {
                if (direction2.is_none() || !count2 || !spacing2)
                    throw py::type_error("LinearPattern: dir2=, count2= and spacing2= go together, a second direction takes all three");
                second = sketch::PatternDir2{RefIn(s, direction2, "LinearPattern"), *count2, *spacing2};
            }
            s.AddPattern(list, RefIn(s, direction, "LinearPattern"), count, spacing, true, TieArg(keep, "LinearPattern", "auto_constraint"),
                         TieArg(internal, "LinearPattern", "internal_constraints"), UserId(id), false, second);
            return s;
        }), py::arg("ids"), py::arg("dir"), py::arg("count"), py::arg("spacing"), py::arg("dir2") = py::none(), py::arg("count2") = py::none(),
             py::arg("spacing2") = py::none(), py::arg("auto_constraint") = py::bool_(HasConstraints<S>),
             py::arg("internal_constraints") = py::bool_(HasConstraints<S>),
             py::arg("id") = py::none(), ref,
             Doc("LinearPattern(ids: Ids, dir: Id, count: int, spacing: float, dir2: Id | None = None, count2: int | None = None, "
                 "spacing2: float | None = None, auto_constraint: bool = " + ties + ", internal_constraints: bool = " + ties + ", "
                 "id: str | None = None) -> " + sk + "\n\n"
                 "Add count instances of the elements in a row, the original the first; with dir2 a grid of count by count2.\n\n"
                 "The pattern is 'LinearPattern.N' (or id), the copies named after their sources by their step "
                 "('LinearPattern.1.3.hole'), in a grid by their two steps ('LinearPattern.1.2.1.hole', "
                 "'LinearPattern.1.2.0.hole').\n\n"
                 "Args:\n"
                 "    ids: the elements: one id, a list or a selection.\n"
                 "    dir: px.H, px.V or a line (its direction start to end).\n"
                 "    count: the number of instances, the original included.\n"
                 "    spacing: how much further along dir each instance is.\n"
                 "    dir2: a second direction, as dir: the rows of a grid.\n"
                 "    count2: the number of instances along dir2, the original included.\n"
                 "    spacing2: how much further along dir2 each instance is.\n"
                 "    auto_constraint: tie the copies to the originals through the pattern, its spacing the dimension "
                 "'LinearPattern.N.spacing' (and 'LinearPattern.N.spacing2') and its number of instances "
                 "'LinearPattern.N.count' (and 'LinearPattern.N.count2'), set like a dimension: copies are added or the "
                 "last ones removed, a grid stays complete; each copy listed as one constraint 'LinearPattern.N.k' "
                 "('LinearPattern.N.k.j' in a grid); default True in a Sketch, False in a QuickSketch.\n"
                 "    internal_constraints: show the constraints among the originals on the copies "
                 "('LinearPattern.1.3.<id>', derived), or with auto_constraint=False give the copies those constraints; "
                 "default True in a Sketch, False in a QuickSketch.\n"
                 "    id: replaces LinearPattern.N.\n\n"
                 "Raises:\n"
                 "    TypeError: dir2, count2 and spacing2 are not given together.\n"
                 "    ValueError: count (with count2: count times count2) is below 2; dir or dir2 is no line.\n\n"
                 "Examples:\n"
                 "    sk.LinearPattern(\"hole\", px.H, 5, 10)\n"
                 "    sk.LinearPattern(\"hole\", px.H, 4, 10, dir2=px.V, count2=3, spacing2=8)"))
        .def("Offset", NamingIds("Offset", +[](S &s, const py::object &source, const py::object &d,
                                               const py::object &sides, OffsetForm ends, OffsetForm corners, const py::kwargs &kw) -> S & {
            HelpersOf(s, kw, "Offset", {});
            const std::optional<bool> bothSides = IsPlainAuto(sides, "Offset", "both_sides") ? std::nullopt
                                                                                               : std::optional(TieArg(sides, "Offset", "both_sides"));
            const std::vector<std::string> ids = RefsIn(s, source, "Offset");
            std::optional<Vertex> point;
            double distance;
            if (IsNumber(d))
                distance = d.cast<double>();
            else if ((py::isinstance<py::tuple>(d) || py::isinstance<py::list>(d)) && py::len(d) == 2 && IsNumber(d[py::int_(0)])) {
                distance = d[py::int_(0)].cast<double>();
                const py::object p = d[py::int_(1)];
                point = IsRef(p) ? s.Location(RefIn(s, p, "Offset")) : PointArg(s.Axes(), p, "Offset");
            } else
                throw py::type_error("Offset(source, d): d is a distance, or (d, point) with the point on the side wanted");
            s.Offset(ids, distance, point, bothSides, {EndsOf(ends), CornersOf(corners)}, OptionsOf(kw), KwOpt<bool>(kw, "construction"));
            return s;
        }), py::arg("source"), py::pos_only(), py::arg("d"), py::arg("both_sides") = PlainAuto(), py::arg("ends") = OffsetForm::Round,
             py::arg("corners") = OffsetForm::Round, ref,
             Doc("Offset(source: Ids, /, d: float | tuple[float, Point], both_sides: bool | Auto = px.Auto, ends: OffsetForm = px.Round, "
                 "corners: OffsetForm = px.Round, **options) -> " + sk + "\n\n"
                 "Add new elements at distance d from source that follow it: the exact offset, its corners and ends as "
                 "asked, sharp where the sides cross.\n\n"
                 "The offset of a spline is a curve at exactly d (kind 'Spline'). With the constraints on, the offset "
                 "follows the source and '<id>.d'; a change of d moves the geometry as little as it can, a free source too: "
                 "fix or dimension the source to keep it. A "
                 "QuickSketch makes the same elements without ties, a spline's offset as an ordinary spline within "
                 "config.spline_tol. The pens stay (a Rectangle after it follows the pen's direction; dir= sets it). A "
                 "source cannot be removed while the offset exists (sk('Line.2').construction = True keeps it); the "
                 "offset's parts cannot be removed, trimmed, split, fixed or copied alone.\n\n"
                 "Args:\n"
                 "    source: an id, a wire id or a list of ids forming one chain of lines, arcs and splines; or one "
                 "circle or closed spline.\n"
                 "    d: the distance. A closed chain gets one loop, outside for d > 0, inside for d < 0; an open chain "
                 "both sides with caps ('<id>.startcap', '<id>.endcap'). (d, point): the side the point ((h, v) "
                 "or a point id) lies on, d > 0.\n"
                 "    both_sides: True both sides (a closed chain: both loops); False one side (an open chain: the left "
                 "for d > 0, the right for d < 0, no caps); px.Auto: one side for a closed chain, both for an open one.\n"
                 "    ends: the caps of an open chain's two sides: a half circle (px.Round), the line across (px.Flat) or "
                 "flat d beyond the end (px.Square, the sides made longer).\n"
                 "    corners: around the outside of a corner an arc of radius d (px.Round), the two offsets extended "
                 "along their end tangents until they meet (px.Sharp, without a limit: lines made longer, a curve's offset "
                 "continued by a straight line '<id>.left.Arc.2.after' or '<id>.left.Arc.3.before'), or the line between "
                 "their ends (px.Chamfer); a round or chamfered corner is '<id>.left.Line.3.corner' (around the end of "
                 "Line.3).\n"
                 "    options: id= the offset's id, default 'Offset.N'. One side: '<id>.Line.3' is the offset of Line.3, "
                 "'<id>.Line.3.corner' the round corner around the end of Line.3. Both sides: the side word first, "
                 "'<id>.left.Line.3', '<id>.right.Line.3.corner', and '<id>.left', '<id>.right' the sides (a loop counts "
                 "counterclockwise: left is its inside). '<id>' is all of it; '<id>.d' its distance, a dimension. "
                 "name=, maxh=, minh=, hpref=, layer=, col=, partition=, construction=: the elements' properties, by "
                 "default those of the source.\n\n"
                 "Raises:\n"
                 "    ValueError: d = 0; ends is px.Sharp or px.Chamfer, corners px.Flat or px.Square; a source that is "
                 "not one chain; d at or beyond the radius of a spline's "
                 "tightest bend towards the side (a cusp). Later, a change of d or of the source that needs another "
                 "shape (an arc's offset vanishing or coming back, a corner appearing, a cusp, sides crossing) raises "
                 "and changes nothing: Remove('<id>'), then Offset again.\n"
                 "    TypeError: d is neither a distance nor (d, point), or an unknown keyword.\n\n"
                 "Examples:\n"
                 "    sk.Offset('plate', -2, id='wall')\n"
                 "    sk.Offset('s', (1.5, (10, 40)), id='bead')\n"
                 "    sk.Offset('track', 0.4, ends=px.Flat, corners=px.Sharp)"))
        .def("Fillet", [](S &s, const py::object &at, double r, sketch::Kind continuity, const py::object &id, const py::kwargs &kw) -> S & {
            const bool curvature = CurvatureOf(continuity, "Fillet");
            NamingTheCall([&] { s.Fillet(SketchCornerPoints(s, at, "Fillet"), r, curvature, CornerIds(id, "Fillet"), CornerProps(kw, "Fillet")); },
                          [&] { return CallText("Fillet", {{"at", at}, {"r", py::float_(r)}}); });
            return s;
        }, py::arg("at"), py::arg("r"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), py::kw_only(), py::arg("id") = py::none(), ref,
           SketchCornerDoc(false, sk, HasConstraints<S>))
        .def("Connect", [](S &s, const py::object &a, const py::object &b, sketch::Kind continuity, bool reverse, const py::kwargs &kw) -> S & {
            if (continuity != sketch::Kind::Coincident && continuity != sketch::Kind::Tangent && continuity != sketch::Kind::Curvature)
                throw py::value_error(std::string("Connect: continuity is px.Coincident, px.Tangent or px.Curvature, not px.") + sketch::KindName(continuity));
            HelpersOf(s, kw, "Connect", {});
            const std::string ida = RefIn(s, a, "Connect"), idb = RefIn(s, b, "Connect");
            NamingTheCall([&] { s.Connect(ida, idb, continuity, reverse, OptionsOf(kw)); },
                          [&] { return CallText("Connect", {{"a", a}, {"b", b}}); });
            return s;
        }, py::arg("a"), py::arg("b"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), py::arg("reverse") = false, ref, ConnectDoc(sk, HasConstraints<S>))
        .def("Chamfer", [](S &s, const py::object &at, double d, OptDouble d2, OptDouble angle, bool reverse, const py::object &id,
                           const py::kwargs &kw) -> S & {
            NamingTheCall([&] {
                s.Chamfer(SketchCornerPoints(s, at, "Chamfer"), ChamferRule{d, d2, angle, reverse}, CornerIds(id, "Chamfer"),
                          CornerProps(kw, "Chamfer"));
            },
                          [&] { return CallText("Chamfer", {{"at", at}, {"d", py::float_(d)}}); });
            return s;
        }, py::arg("at"), py::arg("d"), py::pos_only(), py::arg("d2") = py::none(), py::arg("angle") = py::none(),
             py::arg("reverse") = false, py::kw_only(), py::arg("id") = py::none(), ref, SketchCornerDoc(true, sk, HasConstraints<S>))
        .def("Trim", [](S &s, const py::object &a, const py::object &b, bool reverse, const py::kwargs &kw) -> S & {
            NoMoreKeywords(kw, "Trim", "the point goes with the element: Trim((a, point), b) trims the piece of a nearest it");
            Prepare(s, py::make_tuple(a, b));
            NamingTheCall([&] {
                const auto [id, at] = PieceArg(s, a, "Trim");
                if (!s.ChainOf(id).empty()) {
                    if (reverse)
                        throw py::value_error("Trim: reverse= picks among one element's pieces, not along a chain or wire; give "
                                              "the point of the run to trim: Trim(('" + id + "', point), b)");
                    s.TrimChain(id, CutterIds(s, b, "Trim"), at.value_or(s.Position()));
                } else
                    s.Cut(id, CutterIds(s, b, "Trim"), at, reverse, true);
            }, [&] { return CallText("Trim", {{"a", a}, {"b", b}}); });
            return s;
        }, py::arg("a"), py::arg("b") = py::none(), py::pos_only(), py::kw_only(), py::arg("reverse") = false, ref,
             Doc("Trim(a: Id | tuple[Id, Point], b: Ids | None = None, /, *, reverse: bool = False) -> " + sk + "\n\n"
                 "Cut a where it meets b and make one piece of it a construction element ('Line.3.trimmed.1').\n\n"
                 "The cut points stay on b. One piece left keeps a's id; a cut out of the middle leaves 'Line.3.1', "
                 "'Line.3.2', and 'Line.3' is the wire of the pieces, which what referred to Line.3 resolves through. "
                 "Without b and with nothing to cut at, all of a becomes construction.\n\n"
                 "Args:\n"
                 "    a: the element, or a chain or wire id (Chain.-1, an id_wire, a profile's id: the run of it between "
                 "the cuts nearest the point becomes construction, across its elements); (id, point) picks the piece "
                 "nearest the point ((h, v) or a point id), else the piece nearest the pen is trimmed.\n"
                 "    b: what cuts it: an id, a list or a selection; default every other line, arc and circle, construction "
                 "ones included (RectangleC's diagonal cuts a line through the rectangle's center).\n"
                 "    reverse: trim the next nearest piece.\n\n"
                 "Raises:\n"
                 "    TypeError: a keyword other than reverse (near=: the point goes with a).\n"
                 "    ValueError: the cutters do not meet a; reverse with a chain or wire.\n\n"
                 "Examples:\n"
                 "    sk.Trim(\"Line.3\", \"Line.5\")\n"
                 "    sk.Trim((\"Line.3\", (4, 0)), \"Line.5\", reverse=True)"))
        .def("Split", [](S &s, const py::object &a, const py::object &b, const py::object &tie, const py::kwargs &kw) -> S & {
            const bool tied = TieArg(tie, "Split", "auto_constraint");
            NoMoreKeywords(kw, "Split", "every piece is numbered ('Line.3.1', 'Line.3.2', ...), none keeps a's id: give a alone");
            Prepare(s, py::make_tuple(a, b));
            NamingTheCall([&] {
                const auto [id, at] = PieceArg(s, a, "Split");
                if (at)
                    throw py::value_error("Split((a, point), b): every piece is numbered ('Line.3.1', 'Line.3.2', ...), none keeps "
                                          "a's id; give a alone");
                s.Cut(id, CutterIds(s, b, "Split"), std::nullopt, false, false, tied);
            }, [&] { return CallText("Split", {{"a", a}, {"b", b}}); });
            return s;
        }, py::arg("a"), py::arg("b") = py::none(), py::pos_only(), py::kw_only(), py::arg("auto_constraint") = py::bool_(true), ref,
             Doc("Split(a: Id, b: Ids | None = None, /, *, auto_constraint: bool = True) -> " + sk + "\n\n"
                 "Cut a into pieces where it meets b.\n\n"
                 "The pieces are 'Line.3.1', 'Line.3.2', ... along it (a circle's from the piece at 0 degrees, "
                 "counterclockwise), and 'Line.3' is the wire of the pieces, which what referred to Line.3 (its ends, "
                 "its length, a tangency) resolves through. The cut points stay on b.\n\n"
                 "Args:\n"
                 "    a: the element.\n"
                 "    b: what cuts it: an id, a list or a selection; default every other line, arc and circle.\n"
                 "    auto_constraint: keep the pieces on one line or circle; False lets them move apart (an arc's pieces "
                 "get their own centers and radii).\n\n"
                 "Raises:\n"
                 "    ValueError: a given as (a, point): every piece is numbered, none keeps a's id.\n"
                 "    TypeError: a keyword other than auto_constraint.\n\n"
                 "Examples:\n"
                 "    sk.Split(\"Line.3\", \"Line.5\")\n"
                 "    sk.Split(\"Circle.1\", [\"Line.1\", \"Line.2\"], auto_constraint=False)"))
        .def("SetOrigin", NamingIds("SetOrigin", +[](S &s, const py::object &point) -> S & {
            Prepare(s, point);
            s.SetOrigin(IsRef(point) ? s.Location(RefIn(s, point, "SetOrigin")) : PointArg(s.Axes(), point, "SetOrigin"));
            return s;
        }), py::arg("point"), py::pos_only(), ref,
             Doc("SetOrigin(point: Point, /) -> " + sk + "\n\n"
                 "Move the axes (H, V, the sketch's origin) to a point; the drawing stays where it is, its coordinates "
                 "shift, and constraints to the axes follow them.\n\n"
                 "Args:\n"
                 "    point: the new origin, (h, v) or a point id.\n\n"
                 "Examples:\n"
                 "    sk.SetOrigin((10, 5))\n"
                 "    sk.SetOrigin(\"Circle.1.center\")"))
        .def_property_readonly("_axes", &S::Axes, "the plane, for plane= of other objects")
        .def("Location", NamingIds("Location", +[](S &s, const py::object &id) {
            Prepare(s, id);
            return Tuple2(s.Location(RefIn(s, id, "Location")));
        }), py::arg("id"), py::pos_only(),
           "Location(id: Id, /) -> tuple[float, float]\n\n"
           "Where a point is, solved, as (h, v).\n\n"
           "Args:\n"
           "    id: a point id, or a point of an element: 'Line.1.end', '.start', '.mid', '.center', a spline's '.p2'.\n\n"
           "Raises:\n"
           "    ValueError: id names no point.\n\n"
           "Examples:\n"
           "    sk.Location(\"Line.1.end\")\n"
           "    sk.Location(\"A\")")
        .def_property_readonly("locations", [](S &s) {
            py::dict out;
            for (const auto &[ref, p] : s.Locations())
                out[py::str(ref)] = Tuple2(p);
            return out;
        }, "locations: dict[str, tuple[float, float]]\n\nEvery point reference with where it is, solved, {ref: (h, v)}.")
        .def("Geometry", NamingIds("Geometry", +[](S &s, const py::object &r) { return GeometryOf(s, r); }), py::arg("id"),
             "Geometry(id: Id) -> dict\n\n"
             "An element's solved geometry.\n\n"
             "Args:\n"
             "    id: the element.\n\n"
             "Returns:\n"
             "    kind, and: a point its point; a line start, end, length, angle (degrees from h); a circle center, r; "
             "an arc center, r, start, end, ccw, sweep (degrees, positive counterclockwise); an axis normal_angle, d; a "
             "spline points, closed, tangents, path, start, end, projected, style (an offset curve also source).\n\n"
             "Examples:\n"
             "    sk.Geometry(\"Line.1\")[\"length\"]\n"
             "    sk.Geometry(\"Arc.1\")[\"sweep\"]")
        .def("Remove", +[](S &s, const py::object &id) -> S & { return s.Remove(RefIn(s, id, "Remove")); }, py::arg("id"), ref,
             Doc("Remove(id: Id) -> " + sk + "\n\n"
                 "Remove a constraint, an element with its constraints (a line's ends too, unless used), or the elements "
                 "of a wire id; an expression that uses a dimension removed goes too, with a warning.\n\n"
                 "Args:\n"
                 "    id: the constraint, element or wire.\n\n"
                 "Examples:\n"
                 "    sk.Remove(\"Line.2\")\n"
                 "    sk.Remove(\"Chain.1\")"))
        .def_property_readonly("_elements", [](const S &s) {
            py::dict out;
            for (const auto &[kind, ids] : s.Elements())
                if (kind != "Pattern" && kind != "Offset")
                    out[py::str(kind)] = ids;
            for (const auto &[kind, ids] : s.CallGroups())
                out[py::str(kind)] = ids;
            return out;
        }, "for sk.elements: the element ids by kind, then the groups by the call that made them")
        .def("__repr__", [](S &s) {
            const std::string elements = std::to_string(s.ElementCount()) + " elements";
            if constexpr (HasConstraints<S>)
                return "Sketch(" + elements + ", " + std::to_string(s.ConstraintCount()) + " constraints, " + s.Status() + ", " +
                       std::to_string(s.Dof()) + " degrees of freedom)";
            else
                return "QuickSketch(" + elements + ")";
        })
        .def("Face", [](S &s) {
            s.RequireFull("Face()");
            return s.Face();
        },
             "Face() -> Face\n\n"
             "The region the drawn elements bound.\n\n"
             "Construction elements are left out, crossings cut, stretches drawn twice by closed wires cancel, an open "
             "wire's stretch on another edge is left out, holes by nesting.\n\n"
             "Raises:\n"
             "    ValueError: the region has several parts (Shape() keeps them all); in mode='strict' while the sketch "
             "can still move.\n\n"
             "Examples:\n"
             "    face = sk.Face()")
        .def("Shape", [](S &s) {
            s.RequireFull("Shape()");
            return s.Shape();
        },
           "Shape() -> Shape\n\n"
           "The regions the drawn elements bound, as Face() but all of them.\n\n"
           "Raises:\n"
           "    ValueError: in mode='strict' while the sketch can still move.\n\n"
           "Examples:\n"
           "    shape = sk.Shape()")
        .def("Diagnose", [](S &s, const py::object &gapArg) {
            const std::optional<double> gap = NumberOrAuto(gapArg, "Diagnose", "gap");
            if (gap && !(*gap >= 0.0))
                throw py::value_error("Diagnose(gap=" + py::repr(gapArg).cast<std::string>() + "): the gap is a length, 0 or more");
            const sketch::Faults found = s.Diagnose(gap);
            const auto listed = [&](const std::vector<sketch::Fault> &faults) {
                py::list out;
                for (const sketch::Fault &f : faults) {
                    py::tuple ids(f.elements.size());
                    for (size_t k = 0; k < f.elements.size(); ++k)
                        ids[k] = s.ElementId(f.elements[k]);
                    py::dict d;
                    d["ids"] = ids;
                    d["at"] = Tuple2(f.at);
                    out.append(d);
                }
                return out;
            };
            py::dict out;
            out["open_ends"] = listed(found.openEnds);
            out["missing_coincident"] = listed(found.missingCoincident);
            out["crossings"] = listed(found.crossings);
            out["overlaps"] = listed(found.overlaps);
            out["degenerate"] = listed(found.degenerate);
            return out;
        }, py::arg("gap") = PlainAuto(),
           "Diagnose(gap: float | Auto = px.Auto) -> dict\n\n"
           "The faults of the drawn geometry, as a dict of five lists, each entry {'ids': (id, ...), 'at': (h, v)}.\n\n"
           "Only the drawn lines, arcs, circles and splines are looked at; construction elements, points, the origin and "
           "the axes are not. The keys, always all five: 'open_ends', an end of a line, arc or open spline no other "
           "element's end meets (at the end); 'missing_coincident', ends of two elements closer than gap but not one "
           "point (at the middle between them); 'crossings', two elements meeting where not both end, an end on "
           "another element included (at the point); 'overlaps', two elements sharing a stretch (at its middle); "
           "'degenerate', a line shorter than the rounding, an arc or circle of about no radius or an arc of about no "
           "length, a spline with a point twice in a row (at its middle; left out of the other checks). sk.Plot("
           "show_diagnostics=True) draws them.\n\n"
           "Args:\n"
           "    gap: the distance below which two ends are a missing coincident; px.Auto: 1e-3 of the larger side of "
           "the drawn elements' bounding box. Ends closer than the rounding are one point.\n\n"
           "Raises:\n"
           "    TypeError: gap is None or not a number.\n"
           "    ValueError: gap is negative.\n\n"
           "Examples:\n"
           "    sk.Diagnose()[\"open_ends\"]\n"
           "    sk.Diagnose(gap=0.01)")
        .def("Add", [](S &s, const py::object &geometry) -> S & {
            Wire w = OneGeometry(geometry, "Add");
            w = InPlaneOf(std::move(w), s.Axes(), "Add");
            s.Atomic([&] {
                const size_t first = s.ElementCount();
                s.AddProjection({w, {}}, false, std::nullopt, "Projection", std::nullopt, false);
                if constexpr (HasConstraints<S>)
                    s.RelateAdded(first);
            });
            return s;
        }, py::arg("geometry"), py::pos_only(), ref,
             Doc("Add(geometry: Wire | Face | Shape, /) -> " + sk + "\n\n"
                 "Add a wire, or the boundary of a face or shape (holes included), as elements that move like drawn ones.\n\n"
                 "The geometry lies on the sketch's plane; its coordinates are taken into the sketch's axes. The elements "
                 "are named from the edges (their names and properties kept), as with copy=True. In a Sketch with "
                 "auto_constraint on, the relations of the added elements to each other and to the rest become constraints "
                 "where they hold to rounding, as copy=True finds them.\n\n"
                 "Args:\n"
                 "    geometry: one Wire, Face or Shape.\n\n"
                 "Raises:\n"
                 "    TypeError: a list, tuple or Compound (their edges may overlap: glue them into one shape first, "
                 "sk.Add(px.Glue(parts))); an OCC shape (sk.Project(shape, construction=False) adds it); any other type.\n"
                 "    ValueError: the geometry lies on another plane.\n\n"
                 "Examples:\n"
                 "    sk.Add(face)\n"
                 "    sk.Add(px.Glue(px.FromDXF(\"part.dxf\")))"))
        .def("Wire", NamingIds("Wire", +[](S &s, const py::object &element, bool unify) -> Wire {
            if (element.is_none()) {
                s.RequireFull("Wire()");
                return OneSketchWire(s, unify, unify ? "Wire()" : "Wire(unify=False)");
            }
            Prepare(s, element);
            s.RequireFull("Wire()");
            const std::string local = RefIn(s, element, "Wire"), call = "Wire('" + local + "')";
            const Wire w = s.ElementWire(local);
            Wire chain = OrderChain(w.Edges(), w.Axes(), w.Edges().JoinTol(std::nullopt), call, "sk.Wires() gives the chains");
            chain.CheckChain(call);
            return chain;
        }), py::arg("element") = py::none(), py::pos_only(), py::kw_only(), py::arg("unify") = true,
           "Wire(element: Id | None = None, /, *, unify: bool = True) -> Wire\n\n"
           "The drawn edges as one Wire, its ends within rounding joined; construction elements take no part. With "
           "element, that element's solved edges (a line or arc one edge, a circle one circle edge, a spline its "
           "pieces) or a wire id's elements in one chain.\n\n"
           "Args:\n"
           "    element: an element, or a wire id (Chain.1, an id_wire); None the whole sketch.\n"
           "    unify: for the whole sketch: True cuts the drawn edges at crossings and T-junctions, keeps an "
           "overlapping stretch once and merges the pieces on one carrier where nothing else meets (as wire + wire); "
           "False keeps every drawn element uncut.\n\n"
           "Raises:\n"
           "    ValueError: nothing is drawn, the edges form several wires (sk.Wires() returns them as a list), or, "
           "with unify=False or an element, the chain crosses, touches or overlaps itself; in mode='strict' while "
           "the sketch can still move.\n\n"
           "Examples:\n"
           "    sk.Wire()\n"
           "    sk.Wire(\"Chain.1\")")
        .def("Wires", [](S &s, bool unify) {
            s.RequireFull("Wires()");
            return s.Wires(unify, unify ? "Wires()" : "Wires(unify=False)");
        }, py::arg("unify") = true,
           "Wires(unify: bool = True) -> list[Wire]\n\n"
           "The drawn edges as a list of Wire, each one chain in traversal order; ends within rounding are joined, a "
           "larger gap stays open; construction elements take no part.\n\n"
           "Args:\n"
           "    unify: True (as px.Wires) cuts the drawn edges at crossings and T-junctions, keeps an overlapping "
           "stretch once and merges the pieces on one carrier where nothing else meets: an outline with a rib ending "
           "on it is one closed wire and the rib its own. False keeps every drawn element uncut (an overlap stays "
           "twice) and joins them at their ends.\n\n"
           "Raises:\n"
           "    ValueError: with unify=False a chain crosses, touches or overlaps itself (the message names the two "
           "elements); in mode='strict' while the sketch can still move.\n\n"
           "Examples:\n"
           "    sk.Wires()\n"
           "    sk.Wires(unify=False)")
        .def("Construction", [](S &s) {
            s.RequireFull("Construction()");
            return s.Edges(true);
        },
           "Construction() -> Wire\n\n"
           "The construction edges as a wire.\n\n"
           "Raises:\n"
           "    ValueError: in mode='strict' while the sketch can still move.\n\n"
           "Examples:\n"
           "    helpers = sk.Construction()")
        .def("_Drawing", &DrawingOf<S>, "for the plots: every element with its geometry, state ('full', 'under', 'over') and whether it is construction")
        .def("_Location", NamingIds("_Location", +[](S &s, const py::object &r) {
            Prepare(s, r);
            return Tuple2(s.Location(RefIn(s, r, "_Location")));
        }),
             py::arg("ref"), "for tests: where a point is")
        .def("_Name", &S::NameOf, py::arg("id"), "the name an element carries, or None")
        .def("_LastId", &S::LastId, "for px.WorkPlane: the id of the element made last")
        .def("_Wire", &S::ElementsWire, py::arg("ids"), "for px.WorkPlane: the solved edges of the elements, in their order")
        .def("_Geometry", NamingIds("_Geometry", +[](S &s, const py::object &r) { return GeometryOf(s, r); }), py::arg("id"),
             "for tests: as Geometry");
}
