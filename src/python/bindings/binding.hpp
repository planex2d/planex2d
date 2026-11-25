// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <cctype>
#include <deque>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <numbers>
#include <numeric>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#ifdef PLANEX_THREADS
#include <taskflow/taskflow.hpp>
#endif

#include "planex/curves/interpolate.hpp"
#include "planex/shape/shape.hpp"
#include "planex/shape/shapeops.hpp"
#include "planex/offset/offset.hpp"
#include "planex/shape/measure.hpp"
#include "planex/shape/relate.hpp"
#include "planex/shape/split.hpp"
#include "planex/shape/deformable.hpp"
#include "planex/sketch/arcconstruction.hpp"
#include "planex/sketch/circleconstruction.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/sketch/lineconstruction.hpp"
#include "planex/sketch/projection.hpp"
#include "planex/sketch/symmetryaxis.hpp"
#include "planex/core/axissystem.hpp"
#include "planex/core/parallel.hpp"
#include "planex/core/timer.hpp"
#include "planex/shape/thickness.hpp"
#include "planex/shape/hull.hpp"
#include "planex/wire/chain.hpp"
#include "planex/wire/propagation.hpp"
#include "planex/wire/along.hpp"
#include "planex/wire/polyline.hpp"
#include "planex/shape/fillet.hpp"

namespace py = pybind11;
// Windows' headers (through OCC) declare a global function Arc: at global scope planex's Arc is planex2d::Arc
using namespace planex2d;

namespace pybind11::detail {
    template <class T>
    constexpr bool IsPlanexList = std::is_same_v<T, EdgeList> || std::is_same_v<T, WireList> || std::is_same_v<T, FaceList> ||
                                  std::is_same_v<T, ShapeList>;
    template <class T>
    struct type_caster<T, std::enable_if_t<IsPlanexList<T>>> : list_caster<T, typename T::value_type> {};
}

// a docstring made at run time (with a class's name): pybind11 keeps the pointer, not a copy, so the text is kept here
inline const char *Doc(std::string text) {
    static std::deque<std::string> kept;
    return kept.emplace_back(std::move(text)).c_str();
}
inline std::string NameOf(const py::handle &cls) { return py::str(cls.attr("__name__")).cast<std::string>(); }

// The OCC bindings are built with netgen's pybind11, the core with its own (any 3.x), whose type registry can be another
// one. There the core's classes are those its module holds (CoreTypes, filled by Shadow in the OCC bindings, empty in
// the core): IsA checks against them; a cast reads the core's object through pybind11's conduit, which needs a class of
// the C++ type in the OCC bindings' own registry (the bare shadow); and what the OCC bindings make goes back through the
// core's _adopt (ToCore)
inline std::unordered_map<std::type_index, py::handle> &CoreTypes() {
    static std::unordered_map<std::type_index, py::handle> types;
    return types;
}

template <class T>
bool IsA(const py::handle &h) {
    if (const auto c = CoreTypes().find(typeid(T)); c != CoreTypes().end())
        return py::isinstance(h, c->second);
    return py::isinstance<T>(h);
}

template <class T>
bool Shadow(py::module_ &m, const py::module_ &core, const char *name) {
    CoreTypes()[typeid(T)] = py::object(core.attr(name)).release();
    if (py::detail::get_type_info(typeid(T)))
        return false;
    py::class_<T>(m, Doc("_" + std::string(name)));
    return true;
}

// the Face h is, or a face view's copy kept in copy: the core converts a view to a Face implicitly, which pybind11
// cannot do for the core's objects when the OCC bindings keep another registry (defined after FaceView)
inline const Face &AsFace(const py::handle &h, std::optional<Face> &copy);

template <class T>
py::object ToCore(const char *kind, T made) {
    return py::module_::import("planex._planex").attr("_adopt")(kind, py::capsule(new T(std::move(made)), [](void *p) {
        delete static_cast<T *>(p);
    }));
}

inline Vertex Point2(const std::array<double, 2> &a) {
    IsFinite("point", {a[0], a[1]});
    return {a[0], a[1]};
}
inline py::tuple Tuple2(const Vertex &v) { return py::make_tuple(v.x, v.y); }
inline py::tuple Tuple3(const Vector3 &v) { return py::make_tuple(v.x, v.y, v.z); }
inline py::object BoxTuple(const BBox &b) {
    if (b.IsEmpty())
        return py::none();
    return py::make_tuple(py::make_tuple(b.x0, b.y0), py::make_tuple(b.x1, b.y1));
}
inline Vector3 Vec3(const std::array<double, 3> &a) {
    IsFinite("vector", {a[0], a[1], a[2]});
    return {a[0], a[1], a[2]};
}
inline Vector3 Pnt3(const std::array<double, 3> &a) {
    IsFinite("point", {a[0], a[1], a[2]});
    return {a[0], a[1], a[2]};
}
inline Axis AxisOf(const std::array<double, 3> &p, const std::array<double, 3> &d) {
    IsFinite("axis", {p[0], p[1], p[2], d[0], d[1], d[2]});
    if (!(Norm(Vec3(d)) > 0.0))
        throw std::invalid_argument("axis direction must not be zero");
    return {Pnt3(p), Normalized(Vec3(d))};
}
struct Pair3 {
    double d;
    Vector3 a, b;
};

[[noreturn]] inline void NeedsOCC(const std::string &what) {
    throw py::import_error(what + " needs planex's OCC integration (netgen and OCC), which is not loaded; pip install planex2d[occ] "
                                  "(planex2d-occ, with netgen)");
}

struct OCCHooks {
    std::optional<Vector3> (*point)(const py::handle &) = [](const py::handle &) -> std::optional<Vector3> { return std::nullopt; };
    std::optional<Vector3> (*direction)(const py::handle &) = [](const py::handle &) -> std::optional<Vector3> { return std::nullopt; };
    std::optional<AxisSystem> (*axes)(const py::handle &, const std::string &) =
        [](const py::handle &, const std::string &) -> std::optional<AxisSystem> { return std::nullopt; };
    bool (*isShape)(const py::handle &) = [](const py::handle &) { return false; };
    bool (*isAxes)(const py::handle &) = [](const py::handle &) { return false; };
    std::optional<Projection> (*projected)(const py::handle &, const AxisSystem &, bool) =
        [](const py::handle &, const AxisSystem &, bool) -> std::optional<Projection> { return std::nullopt; };
    Wire (*across)(const Planar &, const AxisSystem &, const AxisSystem &) =
        [](const Planar &, const AxisSystem &, const AxisSystem &) -> Wire { NeedsOCC("a projection into another plane"); };
    Pair3 (*nearest)(const Planar &, const std::optional<Vertex> &, const AxisSystem &, const py::handle &) =
        [](const Planar &, const std::optional<Vertex> &, const AxisSystem &, const py::handle &) -> Pair3 {
            NeedsOCC("a distance to something in another plane");
        };
};
// as config::state: the OCC module points occHooks at the core module's hooks, which it fills
inline OCCHooks moduleHooks;
inline OCCHooks *occHooks = &moduleHooks;

// the core's own thread pool and profiler for the OCC bindings, whose statics are their own (capsule _services)
struct CoreServices {
#ifdef PLANEX_THREADS
    tf::Executor &(*executor)();
#endif
    Profiler &(*profiler)();
};
#ifdef PLANEX_THREADS
inline const CoreServices coreServices{OwnExecutor, Profiler::Own};
#else
inline const CoreServices coreServices{Profiler::Own};
#endif

inline std::string VariableOf(const std::string &cls) {
    if (cls.ends_with("Sketch"))
        return "sk";
    std::string x = cls;
    x[0] = (char)std::tolower(x[0]);
    return x;
}

struct Config {};

template <class T>
inline void ConfigValue(py::class_<Config> &cls, const char *name, T &value, const char *doc) {
    cls.def_property_static(name, [&value](py::object) { return value; }, [&value](py::object, T v) { value = v; }, doc);
}

using OptBool = std::optional<bool>;
using OptDouble = std::optional<double>;

#include "bindingdocs.hpp"
#include "bindingprops.hpp"
#include "bindingviews.hpp"
#include "bindingargs.hpp"
#include "bindingmotion.hpp"
#include "bindingmeasure.hpp"
#include "bindingconstruction.hpp"

// a new reference that is never released: the category outlives every warning
inline PyObject *planexWarning = nullptr;
inline bool quickSketchDefault = true;
inline std::string drawingStandard = "DIN";

int CallerStackLevel();

// called from any thread, also from taskflow workers while the caller released the GIL; an "error" filter turns the
// warning into the Python exception, unless a C++ exception is already unwinding
inline void WarnPython(const std::string &text) {
    py::gil_scoped_acquire gil;
    if (PyErr_WarnEx(planexWarning, text.c_str(), CallerStackLevel()) == 0)
        return;
    if (std::uncaught_exceptions() == 0)
        throw py::error_already_set();
    PyErr_Clear();
}

template <class T>
inline py::class_<T> CoreClass(const py::module_ &core, const char *name) {
    return py::reinterpret_borrow<py::class_<T>>(core.attr(name));
}

template <class F, class... Extra>
inline void AddMethod(py::module_ &m, const char *cls, const char *name, F f, const Extra &...extra) {
    py::object c = m.attr(cls);
    py::setattr(c, name, py::cpp_function(f, py::name(name), py::is_method(c), py::sibling(py::getattr(c, name, py::none())), extra...));
}

void BindConfig(py::module_ &m);
void BindVertex(py::module_ &m);
void BindEdgeGeometry(py::module_ &m);
void BindEdges(py::module_ &m);
void BindWire(py::module_ &m, py::class_<Wire> &wire);
void BindFace(py::class_<Face> &face);
void BindShape(py::module_ &m, py::class_<Shape> &shape, py::class_<Face> &face, py::class_<FaceView> &faceView,
               py::class_<FacesView> &facesView);
void BindDeformable(py::module_ &m);
void BindPlane(py::module_ &m);
void BindConstraintKinds(py::module_ &m);
void BindOffsetForms(py::module_ &m);
void BindSketch(py::module_ &m);
void BindSolver(py::module_ &m);
void BindConstraintSketch(py::module_ &m);
void BindIdentify(py::module_ &m);
void BindStep(py::module_ &m);
