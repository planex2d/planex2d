// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

inline Shape WireShape(const Wire &w) { return Shape(w.Axes(), {}, {w}); }

inline constexpr const char *NoCompoundBooleans = "a Compound has no booleans, merge it first with px.Unify(comp)";

void AddShapes(const py::handle &h, ShapeList &out, bool compounds = true);

inline ShapeList ShapesArg(const py::iterable &parts, bool compounds = true) {
    if (!compounds && py::hasattr(parts, "parts"))
        throw py::type_error(NoCompoundBooleans);
    ShapeList list;
    for (const py::handle &h : parts)
        AddShapes(h, list, compounds);
    return list;
}

inline ShapeList Operands(Shape a, const py::handle &b) {
    ShapeList list{std::move(a)};
    AddShapes(b, list, false);
    return list;
}

inline Shape Combined(ShapeList parts, Shape (*f)(const ShapeList &, bool), bool multithreaded) {
    Identifications ids = parts.MergedIds();
    py::gil_scoped_release release;
    Shape r = f(parts, multithreaded);
    r.Ids() = std::move(ids);
    return Fresh(std::move(r));
}

enum class OffsetForm { Round, Flat, Square, Sharp, Chamfer };
inline const char *OffsetFormName(OffsetForm f) {
    constexpr const char *names[] = {"Round", "Flat", "Square", "Sharp", "Chamfer"};
    return names[(size_t)f];
}
inline OffsetEnd EndsOf(OffsetForm f) {
    if (f == OffsetForm::Sharp || f == OffsetForm::Chamfer)
        throw py::value_error(std::string("Offset: ends= is px.Round, px.Flat or px.Square, not px.") + OffsetFormName(f) +
                              "; px.Sharp and px.Chamfer are corners=");
    return f == OffsetForm::Round ? OffsetEnd::Round : f == OffsetForm::Flat ? OffsetEnd::Flat : OffsetEnd::Square;
}
inline OffsetCorner CornersOf(OffsetForm f) {
    if (f == OffsetForm::Flat || f == OffsetForm::Square)
        throw py::value_error(std::string("Offset: corners= is px.Round, px.Sharp or px.Chamfer, not px.") + OffsetFormName(f) +
                              "; px.Flat and px.Square are ends=");
    return f == OffsetForm::Round ? OffsetCorner::Round : f == OffsetForm::Sharp ? OffsetCorner::Sharp : OffsetCorner::Chamfer;
}

inline Shape OffsetFacesOf(Shape s, double d, OffsetForm corners) {
    const OffsetCorner form = CornersOf(corners);
    py::gil_scoped_release release;
    return OffsetFaces(s, d, form);
}

template <class A, class B>
inline py::tuple CutEdges(A &a, B &b) {
    CutPieces pieces;
    CutRecord rec;
    CutResult r = Cut(a, b, pieces, rec);
    EdgeList out = std::move(pieces.a);
    for (Edge &e : pieces.b)
        out.push_back(std::move(e));
    return py::make_tuple(r, out);
}

inline py::array_t<double> VertexArray(const std::vector<Vertex> &v) {
    py::array_t<double> a({(py::ssize_t)v.size(), (py::ssize_t)2});
    auto r = a.mutable_unchecked<2>();
    for (size_t i = 0; i < v.size(); ++i) {
        r((py::ssize_t)i, 0) = v[i].x;
        r((py::ssize_t)i, 1) = v[i].y;
    }
    return a;
}

inline void CheckSampleTol(const char *call, double tol) {
    if (!(tol > 0.0) || !std::isfinite(tol))
        throw py::value_error(std::string(call) + "(tol): tol must be positive and finite");
}

inline void AddArrays(const std::vector<std::vector<Vertex>> &loops, py::list &out) {
    for (const std::vector<Vertex> &loop : loops)
        out.append(VertexArray(loop));
}

std::optional<Vector3> XYZArg(const py::handle &h, const std::string &call);

inline bool IsNumber(const py::handle &h) {
    return (py::isinstance<py::float_>(h) || py::isinstance<py::int_>(h)) && !py::isinstance<py::bool_>(h);
}

inline bool IsOneXYZ(const py::handle &h) {
    if (occHooks->point(h) || occHooks->direction(h))
        return true;
    if (!py::isinstance<py::sequence>(h) || py::isinstance<py::str>(h) || py::len(h) != 3)
        return false;
    for (const py::handle x : h)
        if (py::isinstance<py::sequence>(x) || occHooks->point(x) || occHooks->direction(x))
            return false;
    return true;
}
inline py::object ToHVOf(const AxisSystem &axes, const py::object &p) {
    if (IsOneXYZ(p))
        return Tuple2(axes.ToHV(*XYZArg(p, "ToHV")));
    py::list out;
    for (const py::handle q : p)
        out.append(Tuple2(axes.ToHV(*XYZArg(q, "ToHV"))));
    return out;
}

py::object InPlane(const AxisSystem &axes, const py::handle &h, const std::string &call);

inline Vertex HVArg(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    try {
        return InPlane(axes, h, call).cast<Vertex>();
    } catch (const py::cast_error &) {
        throw py::type_error(call + ": " + py::repr(h).cast<std::string>() + " is no point; give (h, v), or (x, y, z) on the plane");
    }
}

SplineStyle StyleArg(const py::handle &h, const std::string &call);

std::map<size_t, TangentSetting<double>> TangentSettingsArg(const std::optional<std::map<size_t, double>> &tensions, const std::optional<std::map<size_t, double>> &radii,
                                                            const std::string &call);

inline const char *StyleName(SplineStyle s) { return s == SplineStyle::Quintic ? "quintic" : s == SplineStyle::Clamped ? "clamped" : "averaged"; }

AxisSystem AxesArg(const py::handle &h, const std::string &call);

AxisSystem SharedAxes(const py::iterable &items, const std::string &call, const std::string &whole, const std::string &parts);

inline AxisSystem MirrorPlane(const std::array<double, 3> &p, const std::array<double, 3> &n) {
    IsFinite("mirror plane", {p[0], p[1], p[2], n[0], n[1], n[2]});
    if (!(Norm(Vec3(n)) > 0.0))
        throw std::invalid_argument("mirror normal must not be zero");
    return AxesAlong(Pnt3(p), Vec3(n));
}

inline py::tuple PlaneOf(const Wire &w) {
    const AxisSystem &a = w.Axes();
    const Vector3 o = a.ToXYZ({0.0, 0.0});
    return py::make_tuple(Tuple3(o), Tuple3(a.ToXYZ({1.0, 0.0}) - o), Tuple3(a.ToXYZ({0.0, 1.0}) - o));
}

std::string CallText(const std::string &function, const std::vector<std::pair<const char *, py::handle>> &args);

inline void CheckJoinTol(OptDouble tol, const std::string &call) {
    if (tol && !(std::isfinite(*tol) && *tol >= 0.0))
        throw py::value_error(call + "(tol=" + std::to_string(*tol) + "): tol is a length >= 0, the largest gap between "
                              "ends that is joined");
}

inline bool IsEdgeLike(const py::handle &h) {
    return IsA<planex2d::Line>(h) || IsA<planex2d::Arc>(h) || IsA<planex2d::Spline>(h) ||
           IsA<EdgeView>(h) || IsA<Wire>(h);
}

void CollectEdges(const py::handle &h, const std::string &call, EdgeList &out, std::optional<AxisSystem> &axes);

EdgeList EdgesArg(const py::object &edges, const std::string &call, std::optional<AxisSystem> &axes);

void CheckArcRadii(const std::string &call, const Vertex &start, const Vertex &end, const Vertex &center, OptDouble tol);

inline void CheckCircleRadius(const std::string &call, double r) {
    if (!(std::isfinite(r) && r > 0.0)) {
        std::ostringstream text;
        text << std::setprecision(12) << call << ": the radius is " << r << ", it must be positive and finite; give the circle's "
             << "radius as a length > 0";
        throw py::value_error(text.str());
    }
}

template <class T>
inline T Or(const std::optional<T> &v, T fallback) {
    return v ? *v : fallback;
}

inline ArcFit Arcs(OptBool on, OptDouble length, OptDouble angle, OptDouble tol, OptDouble ignore) {
    const ArcFit &d = config::state->arc_fit;
    return {Or(on, d.enabled), length ? length : d.max_edge_length, Or(angle, d.max_edge_angle), tol ? tol : d.tol,
            ignore ? ignore : d.ignore_edges_smaller};
}
template <class Fit>
inline Fit FitOf(const py::object &o, Fit f, const std::string &key) {
    if (o.is_none())
        return f;
    if (py::isinstance<py::bool_>(o)) {
        f.enabled = o.cast<bool>();
        return f;
    }
    const std::string options = " (tol, max_edge_length, max_edge_angle, ignore_edges_smaller)";
    if (!py::isinstance<py::dict>(o))
        throw py::type_error(key + "=: True, False or a dict of the fit's options" + options);
    f.enabled = true;
    for (const auto &[k, v] : o.cast<py::dict>()) {
        const std::string name = py::str(k);
        if (name == "max_edge_angle") {
            f.max_edge_angle = v.cast<double>();
            continue;
        }
        const OptDouble x = v.is_none() ? std::nullopt : OptDouble(v.cast<double>());
        if (name == "max_edge_length")
            f.max_edge_length = x;
        else if (name == "tol")
            f.tol = x;
        else if (name == "ignore_edges_smaller")
            f.ignore_edges_smaller = x;
        else
            throw py::type_error(key + "=: no option '" + name + "'" + options);
    }
    return f;
}
inline py::dict AutoConstraintDict(const AutoConstraint &o) {
    py::dict out;
    for (const auto &[name, on] : autoConstraintKeys)
        out[name] = o.*on;
    return out;
}

AutoConstraint AutoConstraintOptions(const py::dict &d, AutoConstraint base, const std::string &key);

inline std::optional<AutoConstraint> AutoConstraintOf(const py::object &o, const std::string &call) {
    const config::State &c = *config::state;
    if (o.is_none())
        return c.auto_constraint ? std::optional(c.auto_constraint_options) : std::nullopt;
    if (py::isinstance<py::bool_>(o))
        return o.cast<bool>() ? std::optional(c.auto_constraint_options) : std::nullopt;
    if (!py::isinstance<py::dict>(o))
        throw py::type_error(call + "(auto_constraint=): True, False or a dict of relations, not " + py::repr(o).cast<std::string>());
    return AutoConstraintOptions(o.cast<py::dict>(), c.auto_constraint_options, call + "(auto_constraint=)");
}
inline bool AsksAutoConstraint(const py::object &o) { return !o.is_none() && !(py::isinstance<py::bool_>(o) && !o.cast<bool>()); }

struct AutoValue {
    std::vector<std::pair<std::string, bool>> switched;
};
inline bool IsAuto(const py::handle &h) { return IsA<AutoValue>(h); }
// the module's one px.Auto: a sentinel is compared with `is`
inline py::object PlainAuto() { return py::module_::import("planex._planex").attr("Auto"); }
inline AutoConstraint AutoRelationsOf(const AutoValue &a) {
    AutoConstraint o = config::state->auto_constraint_options;
    for (const auto &[name, on] : a.switched)
        for (const auto &[key, member] : autoConstraintKeys)
            if (name == key)
                o.*member = on;
    return o;
}
inline bool IsPlainAuto(const py::handle &h, const std::string &call, const std::string &name) {
    if (h.is_none())
        throw py::type_error(call + "(" + name + "=None): None is no value here; the default px.Auto works it out");
    if (!IsAuto(h))
        return false;
    if (!h.cast<const AutoValue &>().switched.empty())
        throw py::type_error(call + "(" + name + "=px.Auto({...})): there is nothing to switch here; px.Auto alone works it out");
    return true;
}
struct MinimalValue {};
inline py::object PlainMinimal() { return py::module_::import("planex._planex").attr("Minimal"); }

std::optional<Continuity> PropagationOf(const py::handle &h, const std::string &call);

inline std::optional<double> NumberOrAuto(const py::handle &h, const std::string &call, const std::string &name) {
    if (IsPlainAuto(h, call, name))
        return std::nullopt;
    if (!IsNumber(h))
        throw py::type_error(call + "(" + name + "=): a number or px.Auto, not " + py::repr(h).cast<std::string>());
    return h.cast<double>();
}
inline std::optional<bool> BoolOrAuto(const py::handle &h, const std::string &call, const std::string &name) {
    if (IsPlainAuto(h, call, name))
        return std::nullopt;
    if (!py::isinstance<py::bool_>(h))
        throw py::type_error(call + "(" + name + "=): True, False or px.Auto, not " + py::repr(h).cast<std::string>());
    return h.cast<bool>();
}
inline bool TieArg(const py::handle &h, const std::string &call, const std::string &name) {
    if (py::isinstance<py::bool_>(h))
        return h.cast<bool>();
    throw py::type_error(call + "(" + name + "=" + py::repr(h).cast<std::string>() + "): True or False" +
                         (py::isinstance<py::dict>(h) ? "; a tie has no relations to switch" : ""));
}
inline Cleanup Cleaning(OptDouble edges, OptDouble gaps, OptBool free, OptDouble collinear, OptDouble faces = {}, OptBool merge = {},
                        OptDouble holes = {}, OptDouble slivers = {}) {
    const Cleanup &d = config::state->cleanup;
    return {Or(edges, d.remove_edges_smaller), Or(gaps, d.close_gaps_smaller), Or(free, d.remove_free_edges),
            Or(collinear, d.merge_collinear_closer), Or(faces, d.remove_faces_smaller), Or(merge, d.merge_small_faces_instead),
            Or(holes, d.remove_holes_smaller), Or(slivers, d.merge_slivers_thinner)};
}

#define FIT_ARGS py::arg("fit_splines") = py::none()

#define ARC_ARGS py::arg("fit_arcs") = py::none()

#define FEATURE_ARGS py::arg("remove_holes_smaller") = py::none(), py::arg("merge_slivers_thinner") = py::none()

#define CLEANUP_ARGS                                                                                                   \
    py::arg("remove_edges_smaller") = py::none(), py::arg("close_gaps_smaller") = py::none(),                        \
    py::arg("remove_free_edges") = py::none(), py::arg("merge_collinear_closer") = py::none(),                       \
    py::arg("remove_faces_smaller") = py::none(), py::arg("merge_small_faces_instead") = py::none()

#define EXPORT_PARAMS                                                                                                  \
    const py::object &fs, OptDouble re, OptDouble cg, OptBool rf, OptDouble mc, OptDouble rfs, OptBool msf,             \
        const py::object &fa, OptDouble rhs, OptDouble mst
#define EXPORT_OPTIONS                                                                                                 \
    FitOf(fa, config::state->arc_fit, "fit_arcs"), FitOf(fs, config::state->spline_fit, "fit_splines"),                \
        Cleaning(re, cg, rf, mc, rfs, msf, rhs, mst)
// a wire's open ends are legitimate: free edges stay unless asked
#define WIRE_EXPORT_OPTIONS                                                                                            \
    FitOf(fa, config::state->arc_fit, "fit_arcs"), FitOf(fs, config::state->spline_fit, "fit_splines"),                \
        Cleaning(re, cg, rf ? rf : OptBool(false), mc, rfs, msf, rhs, mst)
#define EXPORT_ARGS FIT_ARGS, CLEANUP_ARGS, ARC_ARGS, FEATURE_ARGS

#define ARCFIT_ARGS                                                                                                    \
    py::arg("tol") = py::none(), py::arg("max_edge_length") = py::none(), py::arg("max_edge_angle") = py::none(),    \
    py::arg("ignore_edges_smaller") = py::none()

struct Thing {
    AxisSystem axes;
    FaceList faces;
    Wire edges;
    std::optional<Vertex> point;

    Planar AsPlanar() const {
        Planar s{Wire(axes), edges};
        for (const Face &f : faces)
            for (const Edge &e : f.Edges())
                s.region.Add(e);
        return s;
    }
};

inline Thing ThingOf(const Face &f) { return {f.Axes(), {f}, Wire(f.Axes()), {}}; }
inline Thing ThingOf(const Shape &s) { return {s.Axes(), s.Faces(), s.WireEdges(), {}}; }
inline Thing ThingOf(const Wire &w) { return {w.Axes(), {}, w, {}}; }
inline Thing ThingOf(const VertexView &v) { return {v.wire->Axes(), {}, Wire(v.wire->Axes()), v.Point()}; }

std::optional<Thing> AsThing(const py::handle &h);

std::optional<std::vector<double>> AsPoint(const py::handle &h);

using PointRows = py::array_t<double, py::array::c_style | py::array::forcecast>;

inline std::string ShapeText(const py::array &a) {
    std::string text = "(";
    for (py::ssize_t k = 0; k < a.ndim(); ++k)
        text += (k > 0 ? ", " : "") + std::to_string(a.shape(k));
    return text + (a.ndim() == 1 ? ",)" : ")");
}

inline void CheckRowsFinite(const PointRows &a, const std::string &call) {
    const double *x = a.data();
    for (py::ssize_t i = 0; i < a.shape(0) * a.shape(1); ++i)
        if (!std::isfinite(x[i]))
            throw py::value_error(call + ": row " + std::to_string(i / a.shape(1)) + " is not finite");
}

std::optional<PointRows> PointBatch(const py::handle &h, const std::string &call);

inline PointRows PlaneRows(const py::handle &h, const std::string &call) {
    PointRows a = PointRows::ensure(h);
    if (!a)
        throw py::value_error(call + ": the points are an (n, 2) array of numbers or a sequence of (h, v)");
    if (a.ndim() != 2 || a.shape(1) != 2)
        throw py::value_error(call + ": the points are an (n, 2) array, not shape " + ShapeText(a));
    CheckRowsFinite(a, call);
    return a;
}

PointRows PlaneRows(const AxisSystem &axes, const py::handle &h, const std::string &call);

inline std::vector<Vertex> PlanePoints(const AxisSystem &axes, const py::iterable &items, const std::string &call) {
    std::vector<Vertex> out;
    for (const py::handle &item : items)
        out.push_back(HVArg(axes, item, call));
    return out;
}

Wire CutterIn(const AxisSystem &axes, const py::handle &h);

TrimRule RuleOf(const AxisSystem &axes, const py::object &keepClosest, const py::object &removeClosest, bool largest, bool smallest, bool left, bool right);

#define TRIM_PARAMS const py::object &cutter, const py::object &kc, const py::object &rc, bool kl, bool ks, bool kleft, bool kright
#define TRIM_ARGS                                                                                                      \
    py::arg("cutter"), py::kw_only(), py::arg("keep_closest_to") = py::none(), py::arg("remove_closest_to") = py::none(), \
        py::arg("keep_largest") = false, py::arg("keep_smallest") = false, py::arg("keep_left") = false,             \
        py::arg("keep_right") = false
