// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

inline sketch::Kind KindNamed(const std::string &name) {
    for (size_t k = 0; k <= (size_t)sketch::Kind::OnCircle; ++k)
        if (name == sketch::KindName(static_cast<sketch::Kind>(k)))
            return static_cast<sketch::Kind>(k);
    throw py::value_error("no constraint kind " + name);
}

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

struct CallArgs {
    std::vector<py::object> values;
    std::vector<sketch::Kind> flags;
    std::optional<AutoConstraint> autoFlag;
};

CallArgs Split(const py::tuple &args, const std::string &call);

template <class S>
constexpr bool HasConstraints = requires(S &s) { s.autoConstraint; };

template <class S>
class AutoScope {
public:
    AutoScope(S &s, const std::optional<AutoConstraint> &flag, const std::string &call) : s(s) {
        if (!flag)
            return;
        if constexpr (HasConstraints<S>) {
            saved = s.autoConstraint;
            s.autoConstraint = *flag;
            active = true;
        } else
            throw py::value_error(call + "(px.Auto): a QuickSketch has no constraints; px.Sketch(...) has them");
    }
    ~AutoScope() {
        if constexpr (HasConstraints<S>)
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

template <class F>
inline py::object Mapped(const py::handle &h, F f) {
    py::list out;
    for (const py::handle &x : h)
        out.append(f(x));
    return py::isinstance<py::tuple>(h) ? py::object(py::tuple(out)) : py::object(out);
}

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
template <class S>
inline std::string LocalId(S &s, const std::string &text, const std::string &call) {
    if (const std::string &own = s.AssignedId(); !own.empty() && text.starts_with(own + "."))
        return s.Book().FromEnd(text.substr(own.size() + 1));
    const std::string kind = s.IdKind() + ".";
    if (text.starts_with(kind)) {
        const size_t digits = text.find_first_not_of("0123456789", kind.size());
        if (digits != std::string::npos && digits > kind.size() && text[digits] == '.')
            throw py::value_error(call + ": '" + text + "' belongs to " + text.substr(0, digits) + ", this is " + s.Id());
    }
    return s.Book().FromEnd(text);
}
template <class S>
inline std::string RefIn(S &s, const py::handle &h, const std::string &call) {
    if (py::hasattr(h, "_ref_in"))
        return h.attr("_ref_in")(py::cast(&s, py::return_value_policy::reference)).template cast<std::string>();
    if (py::isinstance<py::str>(h))
        return LocalId(s, h.cast<std::string>(), call);
    return RefText(h, call);
}
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
template <class S>
inline py::object Resolved(S &s, const py::handle &h, const std::string &call) {
    if (py::isinstance<py::str>(h) || py::hasattr(h, "_ref_in"))
        return py::str(RefIn(s, h, call));
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h))
        return Mapped(h, [&](const py::handle &x) { return Resolved(s, x, call); });
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

struct SketchPen {
    Vertex position, direction;
};
// sk.pen where a point is taken is its position, where a direction is taken (direction) its direction
inline py::object Unpen(const py::handle &h, bool direction) {
    if (IsA<SketchPen>(h)) {
        const SketchPen &p = h.cast<const SketchPen &>();
        return Tuple2(direction ? p.direction : p.position);
    }
    if (py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h))
        return Mapped(h, [&](const py::handle &x) { return Unpen(x, false); });
    return py::reinterpret_borrow<py::object>(h);
}
inline py::object InPlaneAll(const AxisSystem &axes, const py::handle &h, bool direction, const std::string &call) {
    if (!direction) {
        const py::object p = InPlane(axes, h, call);
        if (!p.is(h))
            return p;
    }
    if (!(py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h)))
        return py::reinterpret_borrow<py::object>(h);
    return Mapped(h, [&](const py::handle &x) { return InPlaneAll(axes, x, false, call); });
}
inline py::tuple UnpenArgs(const AxisSystem &axes, const py::args &args, const std::string &call) {
    py::list out;
    for (const py::handle &x : args)
        out.append(InPlaneAll(axes, Unpen(x, false), false, call));
    return py::tuple(out);
}
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

Vertex PointArg(const py::handle &h, const std::string &call);

inline Vertex PointArg(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    return PointArg(InPlane(axes, h, call), call);
}
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

inline bool IsObjectList(const py::handle &h) {
    return (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) && py::len(h) > 0 && !IsNumber(h.cast<py::sequence>()[0]) &&
           !IsA<Vertex>(h.cast<py::sequence>()[0]);
}

std::optional<Vertex> StartArg(const py::handle &h, const std::string &call);

template <class S>
inline void GiveId(S &s, const std::optional<std::string> &id) {
    if (id)
        CheckUserId(*id);
    id ? s.SetId(*id) : (void)s.Id();
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

inline std::optional<std::string> UserId(const std::optional<std::string> &id) {
    if (id)
        CheckUserId(*id);
    return id;
}

inline Properties PropsOf(const py::dict &kw) {
    return Props(KwOpt<std::string>(kw, "name"), KwOpt<double>(kw, "maxh"), KwOpt<std::vector<double>>(kw, "col"), KwOpt<double>(kw, "minh"),
                 KwOpt<double>(kw, "hpref"), KwOpt<int>(kw, "layer"), KwOpt<std::vector<double>>(kw, "partition"));
}

inline sketch::Options OptionsOf(const py::dict &kw) {
    if (kw.empty())
        return {};
    return {UserId(KwOpt<std::string>(kw, "id")), PropsOf(kw), KwOpt<bool>(kw, "construction").value_or(false)};
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

template <class S>
inline std::pair<std::string, std::optional<Vertex>> PieceArg(S &s, const py::object &a, const std::string &call) {
    if (!IsRef(a) && (py::isinstance<py::tuple>(a) || py::isinstance<py::list>(a)) && py::len(a) == 2 && IsRef(a[py::int_(0)])) {
        const py::object p = a[py::int_(1)];
        return {RefIn(s, a[py::int_(0)], call), IsRef(p) ? s.Location(RefIn(s, p, call)) : PointArg(s.Axes(), p, call)};
    }
    return {RefIn(s, a, call), std::nullopt};
}

inline const std::vector<std::string> OptionKeys ={"id", "name", "maxh", "col", "minh", "hpref", "layer", "partition", "construction"};
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

std::vector<std::string> CornerIds(const py::object &id, const std::string &call);

std::vector<Properties> CornerProps(const py::kwargs &kw, const std::string &call);
