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

#include "shape/shape.hpp"
#include "shape/measure.hpp"
#include "shape/relate.hpp"
#include "shape/split.hpp"
#include "shape/deformable.hpp"
#include "sketch/arcconstruction.hpp"
#include "sketch/circleconstruction.hpp"
#include "sketch/geometry.hpp"
#include "sketch/lineconstruction.hpp"
#include "sketch/projection.hpp"
#include "sketch/symmetryaxis.hpp"
#include "core/axissystem.hpp"
#include "core/parallel.hpp"
#include "core/timer.hpp"
#include "shape/thickness.hpp"
#include "wire/chain.hpp"
#include "shape/fillet.hpp"

namespace py = pybind11;
// Windows' headers (through OCC) declare a global function Arc: at global scope planex's Arc is planex2d::Arc
using namespace planex2d;

namespace pybind11::detail {
    template <class T>
    constexpr bool IsPlanexList = std::is_same_v<T, EdgeList> || std::is_same_v<T, WireList> || std::is_same_v<T, FaceList> ||
                                  std::is_same_v<T, ShapeList>;
    // the list structs reach Python as lists
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

// the core's class name of T, and a shadow of it when the OCC bindings keep another registry; true if it made one
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

// what the OCC bindings made as the core's own object: kind names it for the core's _adopt
template <class T>
py::object ToCore(const char *kind, T made) {
    return py::module_::import("planex._planex").attr("_adopt")(kind, py::capsule(new T(std::move(made)), [](void *p) {
        delete static_cast<T *>(p);
    }));
}

inline Shape WireShape(const Wire &w) { return Shape(w.Axes(), {}, {w}); }

// the edges of every face, one between two faces or through one twice: the ray parity is that of the union of the faces
inline Wire FaceRegion(const Shape &s) {
    Wire w(s.Axes());
    for (const Wire &g : s.Groups())
        for (const Edge &e : g)
            w.Add(e);
    return w;
}

inline constexpr const char *NoCompoundBooleans = "a Compound has no booleans, merge it first with px.Unify(comp)";

// a Face, Shape or Wire as a shape; a Python Compound gives its parts where allowed
inline void AddShapes(const py::handle &h, ShapeList &out, bool compounds = true) {
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

inline ShapeList ShapesArg(const py::iterable &parts, bool compounds = true) {
    if (!compounds && py::hasattr(parts, "parts"))
        throw py::type_error(NoCompoundBooleans);
    ShapeList list;
    for (const py::handle &h : parts)
        AddShapes(h, list, compounds);
    return list;
}

// the operands of an operator: no compounds
inline ShapeList Operands(Shape a, const py::handle &b) {
    ShapeList list{std::move(a)};
    AddShapes(b, list, false);
    return list;
}

// a new shape made of others' edges: a Shape of its own, its edges numbered anew
inline Shape Fresh(Shape s) {
    s.Edges().SetIdKind("Shape");
    s.Edges().AssignIds(true);
    s.Edges().ClearVertexIds();
    return s;
}

inline Shape Combined(ShapeList parts, Shape (*f)(const ShapeList &, bool), bool multithreaded) {
    Identifications ids = parts.MergedIds();
    py::gil_scoped_release release;
    Shape r = f(parts, multithreaded);
    r.Ids() = std::move(ids);
    return Fresh(std::move(r));
}

// the forms of an offset's ends and corners as Python names them (px.Round, px.Flat, ...), one enum for both
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

// the faces with the band of width |d| along their boundaries added (d > 0) or taken away (d < 0), its corners as given
inline Shape OffsetFaces(const Shape &s, double d, OffsetCorner corners) {
    if (s.HasSharedFaces())
        throw py::value_error("Offset: the faces of the shape share edges (glued); px.Unify(shape) merges them first");
    if (d == 0.0)
        return s;
    ShapeList parts{s, Shape(s.FaceEdges().Offset(std::abs(d), OffsetEnd::Round, corners), nullptr)};
    if (d > 0.0)
        return Combined(std::move(parts), &Unify, false);
    return Combined(std::move(parts), &Subtract, false);
}

inline const char *OffsetDoc(const std::string &cls, const std::string &returns, const std::string &raises, const std::string &x) {
    return Doc("Offset(d: float, inplace: bool = True, corners: OffsetForm = px.Round) -> " + returns + "\n\n"
               "Grows the faces by d: the band of width abs(d) along their boundaries added for d > 0, taken away for d < 0.\n\n"
               "The band is Wire.Offset of their edges; the faces keep their properties, the new edges those of the edges they "
               "come from. A corner that the offset goes round on its outside (a convex one growing, a reflex one shrinking) "
               "becomes an arc of radius abs(d) about it (px.Round), the two offsets extended along their end tangents until they "
               "meet (px.Sharp, without a limit: a corner of angle theta between its edges reaches abs(d) / sin(theta / 2) out from "
               "it), or the line between their ends (px.Chamfer).\n\n"
               "Args:\n"
               "    d: the width of the band, a length; d > 0 grows, d < 0 shrinks, 0 changes nothing.\n"
               "    inplace: True changes this " + cls + " and returns it; False returns the offset as a new Shape and "
               "leaves this one.\n"
               "    corners: px.Round, px.Sharp or px.Chamfer.\n\n"
               "Raises:\n"
               "    ValueError: " + raises + " corners is px.Flat or px.Square.\n\n"
               "Examples:\n"
               "    " + x + ".Offset(0.5)\n"
               "    band = " + x + ".Offset(0.5, inplace=False, corners=px.Sharp)");
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

using Name = std::optional<std::string>;
using Size = std::optional<double>;
using Layer = std::optional<int>;
using Col = std::optional<std::vector<double>>;
using Partition = std::optional<std::vector<double>>;
using QuadDominated = std::optional<bool>;

inline std::optional<std::array<double, 4>> ColOf(const Col &col) {
    if (!col)
        return std::nullopt;
    if (col->size() != 3 && col->size() != 4)
        throw py::value_error("col needs 3 or 4 values");
    return std::array<double, 4>{(*col)[0], (*col)[1], (*col)[2], col->size() == 4 ? (*col)[3] : 1.0};
}

inline std::optional<std::vector<double>> PartitionOf(const Partition &partition) {
    if (!partition)
        return std::nullopt;
    std::vector<double> f = *partition;
    std::sort(f.begin(), f.end());
    for (size_t i = 0; i < f.size(); ++i)
        if (!(f[i] > 0.0 && f[i] < 1.0) || (i > 0 && f[i] == f[i - 1]))
            throw py::value_error("partition needs distinct fractions of the edge length in (0, 1)");
    return f;
}

// a mesh size: positive (hpref may be 0) and finite
inline Size SizeOf(const Size &v, const char *what, bool zero = false) {
    if (v && (!std::isfinite(*v) || *v < 0.0 || (*v == 0.0 && !zero)))
        throw py::value_error(std::string(what) + " = " + std::to_string(*v) + ": a mesh size is " + (zero ? "at least 0" : "positive") + " and finite");
    return v;
}

// "|" joins names in a selection (name="a|b")
inline Name CheckedName(const Name &name) {
    if (name && name->find('|') != std::string::npos)
        throw py::value_error("name '" + *name + "': a name has no '|', it joins names in a selection (name=\"a|b\")");
    return name;
}

inline Properties Props(const Name &name, const Size &maxh, const Col &col, const Size &minh, const Size &hpref,
                        const Layer &layer, const Partition &partition) {
    return ChangedProperties(nullptr, [&](ShapeProperties &p) {
        p.name = CheckedName(name);
        p.maxh = SizeOf(maxh, "maxh");
        p.col = ColOf(col);
        p.minh = SizeOf(minh, "minh");
        p.hpref = SizeOf(hpref, "hpref", true);
        p.layer = layer;
        p.partition = PartitionOf(partition);
    });
}

inline ShapeProperties PropsOrDefault(const Properties &p) { return p ? *p : ShapeProperties{}; }
inline ShapeProperties PropsOrDefault(const Edge &e) { return PropsOrDefault(PropsOf(e)); }

template <class F>
inline void UpdateProps(Edge &e, F f) {
    SetProps(e, ChangedProperties(PropsOf(e), f));
}

// the Python value type of a property, from its setter's second parameter
template <class F>
struct SetterValue;
template <class C, class R, class P, class V>
struct SetterValue<R (C::*)(P, V) const> {
    using type = std::decay_t<V>;
};
template <class F>
using SetterValueOf = typename SetterValue<decltype(&F::operator())>::type;

// every property as (Python name, getter, setter); edges have partition, faces quad_dominated
template <class F>
inline void SharedFields(F bind) {
    bind("name", [](const ShapeProperties &p) { return p.name; }, [](ShapeProperties &p, const Name &v) { p.name = CheckedName(v); });
    bind("maxh", [](const ShapeProperties &p) { return p.maxh; }, [](ShapeProperties &p, const Size &v) { p.maxh = SizeOf(v, "maxh"); });
    bind("minh", [](const ShapeProperties &p) { return p.minh; }, [](ShapeProperties &p, const Size &v) { p.minh = SizeOf(v, "minh"); });
    bind("hpref", [](const ShapeProperties &p) { return p.hpref; }, [](ShapeProperties &p, const Size &v) { p.hpref = SizeOf(v, "hpref", true); });
    bind("layer", [](const ShapeProperties &p) { return p.layer; }, [](ShapeProperties &p, const Layer &v) { p.layer = v; });
    bind("col", [](const ShapeProperties &p) { return p.col; }, [](ShapeProperties &p, const Col &v) { p.col = ColOf(v); });
}
template <class F>
inline void EdgeFields(F bind) {
    SharedFields(bind);
    bind("partition", [](const ShapeProperties &p) { return p.partition; },
         [](ShapeProperties &p, const Partition &v) { p.partition = PartitionOf(v); });
}
template <class F>
inline void FaceFields(F bind) {
    SharedFields(bind);
    bind("quad_dominated", [](const ShapeProperties &p) { return p.quad_dominated; },
         [](ShapeProperties &p, const QuadDominated &v) { p.quad_dominated = v; });
}

// a property's docstring: its type line, what it is, then whose (" of the face; None if not set.")
inline const char *FieldDoc(const std::string &field, const std::string &whose) {
    static const std::map<std::string, std::string> type{
        {"name", "str | None"},         {"maxh", "float | None"},
        {"minh", "float | None"},       {"hpref", "float | None"},
        {"layer", "int | None"},        {"col", "tuple[float, float, float, float] | None"},
        {"partition", "list[float] | None"}, {"quad_dominated", "bool | None"}};
    static const std::map<std::string, std::string> what{
        {"name", "The name"},
        {"maxh", "The largest mesh size (netgen's maxh)"},
        {"minh", "The smallest mesh size (netgen's minh)"},
        {"hpref", "Netgen's hpref"},
        {"layer", "Netgen's layer number"},
        {"col", "The color (r, g, b, a), set as (r, g, b) or (r, g, b, a)"},
        {"partition", "The partition (the mesh points along an edge as increasing fractions of its length in (0, 1))"},
        {"quad_dominated", "The quad_dominated flag (True: netgen meshes with mostly quadrilaterals)"}};
    return Doc(field + ": " + type.at(field) + "\n\n" + what.at(field) + whose);
}

inline Wire SingleEdge(Edge e, const AxisSystem &axes) {
    Wire w(axes);
    w.Add(std::move(e));
    return w;
}

// a view on edge i of a wire; it holds while the wire's edge list is unchanged (moving the edges keeps it)
struct EdgeView {
    Wire *wire;
    size_t i;
    uint64_t changes;

    EdgeView(Wire *w, size_t i) : wire(w), i(i), changes(w->Changes()) {}
    Edge &Get() const {
        if (wire->Changes() != changes || i >= wire->Size())
            throw std::runtime_error("this edge view is stale: the edges changed since it was taken, take it again from .edges");
        return (*wire)[i];
    }
    Wire AsWire() const { return SingleEdge(Get(), wire->Axes()); }
};

// all edges of a wire, or the edges of one face of a shape by their indices (these hold while the edge list is unchanged)
struct EdgesView {
    Wire *wire;
    std::optional<std::vector<size_t>> subset = std::nullopt;
    uint64_t changes = 0;

    std::vector<size_t> Indices() const {
        if (!subset) {
            std::vector<size_t> all(wire->Size());
            std::iota(all.begin(), all.end(), (size_t)0);
            return all;
        }
        if (wire->Changes() != changes)
            throw std::runtime_error("this edges view is stale: the edges changed since it was taken, take it again from .edges");
        return *subset;
    }
};

// the value all n properties share, none otherwise
template <class P, class G>
inline auto CommonOf(size_t n, P propsAt, G get) -> decltype(get(ShapeProperties{})) {
    decltype(get(ShapeProperties{})) value;
    for (size_t k = 0; k < n; ++k) {
        auto v = get(PropsOrDefault(propsAt(k)));
        if (k > 0 && v != value)
            return {};
        value = v;
    }
    return value;
}
// the value the edges share, none otherwise
template <class G>
inline auto Common(const Wire &w, const std::vector<size_t> &index, G get) {
    return CommonOf(index.size(), [&](size_t k) { return PropsOf(w[index[k]]); }, get);
}
template <class G>
inline auto Common(const Wire &w, G get) {
    return CommonOf(w.Size(), [&](size_t k) { return PropsOf(w[k]); }, get);
}

// item index of a list view of n items: an integer (negative from the end) gives one view, a slice a Python list of
// them; every view keeps self, and through it the owner, alive
template <class F>
inline py::object ListItem(const py::object &self, const py::object &index, size_t n, F at) {
    auto view = [&](size_t i) {
        py::object v = at(i);
        py::detail::keep_alive_impl(v, self);
        return v;
    };
    const auto size = (py::ssize_t)n;
    if (PyIndex_Check(index.ptr())) {
        py::ssize_t i = PyNumber_AsSsize_t(index.ptr(), PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            throw py::error_already_set();
        if (i < 0)
            i += size;
        if (i < 0 || i >= size)
            throw py::index_error("index out of range");
        return view((size_t)i);
    }
    if (py::isinstance<py::slice>(index)) {
        py::ssize_t start, stop, step, length;
        if (!index.cast<py::slice>().compute(size, &start, &stop, &step, &length))
            throw py::error_already_set();
        py::list out;
        for (py::ssize_t k = 0; k < length; ++k, start += step)
            out.append(view((size_t)start));
        return out;
    }
    throw py::type_error("indices must be integers or slices, not " + py::str(py::type::handle_of(index).attr("__name__")).cast<std::string>());
}

// a view on one vertex of a wire: its properties are those of the edge ends there, merged; a change sets the changed
// merged properties on all of them
struct VertexView {
    Wire *wire;
    // an edge end at the vertex: the view follows it when the edges move
    size_t end;
    uint64_t changes;

    VertexView(Wire *w, size_t end) : wire(w), end(end), changes(w->Changes()) {}
    std::vector<size_t> Endpoints() const {
        if (wire->Changes() != changes)
            throw std::runtime_error("this vertex view is stale: the edges changed since it was taken, take it again from .vertices");
        for (std::vector<size_t> &at : wire->VertexEndpoints())
            if (std::find(at.begin(), at.end(), end) != at.end())
                return std::move(at);
        throw std::runtime_error("this vertex view is stale: its vertex is gone");
    }
    Vertex Point() const {
        Endpoints();
        return wire->Endpoint(end);
    }
};

template <class F>
inline void UpdateVertex(Wire &w, const std::vector<size_t> &at, F change) {
    const Properties changed = ChangedProperties(w.VertexProps(at), change);
    for (size_t k : at)
        w.SetEndpointProps(k, changed);
}

// all vertices of a wire, or those of the edges of one face of a shape
struct VerticesView {
    Wire *wire;
    std::optional<std::vector<size_t>> subset = std::nullopt;
    uint64_t changes = 0;

    // the edge ends at every vertex
    std::vector<std::vector<size_t>> Vertices() const {
        std::vector<std::vector<size_t>> all = wire->VertexEndpoints();
        if (!subset)
            return all;
        if (wire->Changes() != changes)
            throw std::runtime_error("this vertices view is stale: the edges changed since it was taken, take it again from .vertices");
        std::vector<bool> in(wire->Size(), false);
        for (size_t i : *subset)
            in[i] = true;
        std::erase_if(all, [&](const std::vector<size_t> &at) {
            return std::none_of(at.begin(), at.end(), [&](size_t k) { return in[k / 2]; });
        });
        return all;
    }
};

// the faces of a shape as the views see them: the group and edge indices of every face, in the order of Shape::Faces
struct FaceIndex {
    uint64_t changes, groupChanges;
    std::vector<std::pair<uint32_t, std::vector<size_t>>> faces;

    explicit FaceIndex(const Shape &s) : changes(s.Edges().Changes()), groupChanges(s.GroupChanges()), faces(s.FaceIndices()) {}
};

// a view on face i of a shape: its properties are those of its group (a change gives the face a group of its own, the
// faces keep their order), its edges and vertices are the shape's; it holds while the shape's edge list is unchanged
struct FaceView {
    Shape *shape;
    size_t i;
    std::shared_ptr<FaceIndex> index;

    const std::pair<uint32_t, std::vector<size_t>> &Get() const {
        if (shape->Edges().Changes() != index->changes)
            throw std::runtime_error("this face view is stale: the shape's edges changed since it was taken, take it again from .faces");
        if (shape->GroupChanges() != index->groupChanges)
            *index = FaceIndex(*shape);
        if (i >= index->faces.size())
            throw std::runtime_error("this face view is stale: its face is gone");
        return index->faces[i];
    }
    Face Copy() const {
        const auto &[k, edges] = Get();
        return shape->FaceOf(k, edges);
    }
    const Properties &Props() const { return shape->GroupProps(Get().first); }
    void SetProps(Properties p) {
        const uint32_t k = Get().first;
        if (SameProperties(p, shape->GroupProps(k)))
            return;
        shape->SeparateFaces(k);
        shape->SetGroupProps(Get().first, std::move(p));
    }
    // each edge once, in the face's order
    std::vector<size_t> EdgeIndices() const {
        std::vector<size_t> out;
        std::vector<bool> seen(shape->Edges().Size(), false);
        for (size_t e : Get().second)
            if (!seen[e]) {
                seen[e] = true;
                out.push_back(e);
            }
        return out;
    }
};

inline const Face &AsFace(const py::handle &h, std::optional<Face> &copy) {
    if (IsA<FaceView>(h))
        return copy.emplace(h.cast<const FaceView &>().Copy());
    return h.cast<const Face &>();
}

// the faces of a shape; it follows the shape
struct FacesView {
    Shape *shape;
    std::shared_ptr<FaceIndex> index = nullptr;

    const std::shared_ptr<FaceIndex> &Current() {
        if (!index || shape->Edges().Changes() != index->changes)
            index = std::make_shared<FaceIndex>(*shape);
        else if (shape->GroupChanges() != index->groupChanges)
            *index = FaceIndex(*shape);
        return index;
    }
};

template <class F>
inline void VertexFields(F bind) {
    bind("name", [](const ShapeProperties &p) { return p.name; }, [](ShapeProperties &p, const Name &v) { p.name = CheckedName(v); });
    bind("maxh", [](const ShapeProperties &p) { return p.maxh; }, [](ShapeProperties &p, const Size &v) { p.maxh = v; });
    bind("hpref", [](const ShapeProperties &p) { return p.hpref; }, [](ShapeProperties &p, const Size &v) { p.hpref = v; });
}

// set on every edge of wireOf(t), read as the common value
template <class T, class C, class W>
inline void BindEdgeProperties(C &cls, W wireOf) {
    EdgeFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        cls.def_property(name, [wireOf, get](T &t) { return Common(wireOf(t), get); }, [wireOf, set](T &t, const V &v) {
            for (Edge &e : wireOf(t))
                UpdateProps(e, [&](ShapeProperties &p) { set(p, v); });
        }, FieldDoc(name, " all edges share, None if they differ; set on all edges."));
    });
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

// no chord lies closer to a curve than the rounding of its coordinates
template <class E>
inline void CheckSampleLimit(const char *call, const E &x, const char *kind, double tol) {
    const double limit = Tol(x.LengthScale());
    if (tol < limit) {
        std::ostringstream text;
        text << call << "(tol): tol " << tol << " is below the rounding of this " << kind << ", " << limit;
        throw py::value_error(text.str());
    }
}

// points along an arc from its start to its end, every chord within tol of it
inline std::vector<Vertex> ArcPoints(const planex2d::Arc &a, double tol, const char *call, const char *kind) {
    CheckSampleLimit(call, a, kind, tol);
    // the sagitta r (1 - cos(t / 2)) as 2 r sin^2(t / 4): no cancellation for a small tol
    const double step = std::min(std::numbers::pi, 4.0 * std::asin(std::min(1.0, std::sqrt(tol / (2.0 * a.Radius())))));
    const double chords = std::ceil(a.Sweep() / step);
    std::vector<Vertex> pts{a.Start()};
    // config.eps and config.k may lower the limit without bound
    if (!(chords < (double)pts.max_size()))
        throw py::value_error(std::string(call) + "(tol): too many points for this " + kind);
    const size_t n = std::max<size_t>(1, (size_t)chords);
    for (size_t k = 1; k < n; ++k)
        pts.push_back(a.PointAt(a.Sweep() * (double)k / (double)n));
    pts.push_back(a.End());
    return pts;
}

// points along the edge from its start to its end, every chord within tol of it: a line its ends, an arc by its sagitta,
// a circle by those of its two halves, a spline as Spline::Sample
inline std::vector<Vertex> SamplePoints(const Edge &e, double tol, const char *call) {
    if (const planex2d::Arc *a = std::get_if<planex2d::Arc>(&e))
        return ArcPoints(*a, tol, call, "arc");
    if (const Circle *c = std::get_if<Circle>(&e)) {
        const std::array<planex2d::Arc, 2> halves = c->Halves();
        std::vector<Vertex> pts = ArcPoints(halves[0], tol, call, "circle");
        const std::vector<Vertex> rest = ArcPoints(halves[1], tol, call, "circle");
        pts.insert(pts.end(), rest.begin() + 1, rest.end());
        return pts;
    }
    if (const Spline *s = std::get_if<Spline>(&e)) {
        CheckSampleLimit(call, *s, "spline", tol);
        return s->Sample(tol);
    }
    const Line &l = std::get<Line>(e);
    return {l.Start(), l.End()};
}

// a loop or chain of w within tol, every edge as the walk runs it (reversed: the loop backwards); a point where two
// edges meet once, a closed loop ending at its first point
inline std::vector<Vertex> LoopPoints(const Wire &w, const Loop &loop, bool reversed, double tol) {
    std::vector<Vertex> out;
    const size_t m = loop.edges.size();
    for (size_t k = 0; k < m; ++k) {
        const auto [i, forward] = loop.edges[reversed ? m - 1 - k : k];
        std::vector<Vertex> pts = SamplePoints(w[i], tol, "Polylines");
        if (forward == reversed)
            std::reverse(pts.begin(), pts.end());
        out.insert(out.end(), out.empty() ? pts.begin() : pts.begin() + 1, pts.end());
    }
    if (loop.closed && !out.empty())
        out.back() = out.front();
    return out;
}

inline bool CounterClockwise(const Wire &w, const Loop &loop) {
    return w.LoopArea(loop.edges) > 0.0;
}

// the loops of a face: the outer one counterclockwise, then the holes clockwise
inline void AddFaceLoops(const Wire &edges, double tol, py::list &out) {
    const std::vector<NestedLoop> nested = edges.Nesting();
    for (size_t depth = 0; depth < 2; ++depth)
        for (const NestedLoop &n : nested)
            if (n.depth == depth)
                out.append(VertexArray(LoopPoints(edges, n.loop, CounterClockwise(edges, n.loop) == (depth == 1), tol)));
}

// a loop runs the way its first edge (lowest index) runs, start to end
inline bool RunsBackwards(const Loop &loop) {
    const auto first = std::min_element(loop.edges.begin(), loop.edges.end());
    return !first->second;
}

inline void AddWireLoops(const Wire &w, double tol, py::list &out) {
    for (const Loop &loop : w.Loops())
        out.append(VertexArray(LoopPoints(w, loop, RunsBackwards(loop), tol)));
}

inline const char *PolylinesDoc(const std::string &x) {
    return Doc("Polylines(tol: float) -> list[np.ndarray]\n\n"
               "The loops and open chains as (n, 2) arrays of plane coordinates, each chord within tol of its edge.\n\n"
               "A face gives its outer loop counterclockwise, then its holes clockwise; a shape its faces in turn, then its "
               "free wires; a wire's loop runs the way the wire's first edge runs; a closed loop ends at its first point.\n\n"
               "Args:\n"
               "    tol: the largest distance of a chord from its edge, a length > 0.\n\n"
               "Raises:\n"
               "    ValueError: tol is not positive and finite, or below the rounding of an arc's or a spline's "
               "coordinates.\n\n"
               "Examples:\n"
               "    " + x + ".Polylines(0.01)");
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
// the nearest points in 3d, a on the measured thing and b on the target
struct Pair3 {
    double d;
    Vector3 a, b;
};

[[noreturn]] inline void NeedsOCC(const std::string &what) {
    throw py::import_error(what + " needs planex's OCC integration (netgen and OCC), which is not loaded; pip install planex2d[occ] "
                                  "(planex2d-occ, with netgen)");
}

// what the OCC module adds when it loads: netgen's OCC objects as arguments (gp_Pnt a point, gp_Vec and gp_Dir a
// direction, gp_Ax3 and a planar shape a plane, a TopoDS_Shape a target projected into the plane) and what is measured
// or projected between planes; without it nothing is an OCC object and the rest raises
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
// as config::state: the OCC module points occ at the core module's hooks, which it fills
inline OCCHooks moduleHooks;
inline OCCHooks *occ = &moduleHooks;

// the core's own thread pool and profiler for the OCC bindings, whose statics are their own (capsule _services)
struct CoreServices {
    tf::Executor &(*executor)();
    Profiler &(*profiler)();
};
inline const CoreServices coreServices{OwnExecutor, Profiler::Own};

// the sizes of what the core and the OCC bindings both read: equal when both are built from the same sources (with the
// same version, a development build of other sources still differs here)
inline py::tuple Layout() {
    return py::make_tuple(sizeof(config::State), sizeof(OCCHooks), sizeof(CoreServices), sizeof(Vertex), sizeof(AxisSystem),
                          sizeof(Wire), sizeof(Face), sizeof(Shape), sizeof(FaceView), sizeof(EdgeView), sizeof(VertexView));
}

// a 3d point or direction: a tuple, or netgen's Pnt, Vec or Dir; none for None
inline std::optional<Vector3> XYZArg(const py::handle &h, const std::string &call) {
    if (h.is_none())
        return std::nullopt;
    if (std::optional<Vector3> p = occ->point(h))
        return p;
    if (std::optional<Vector3> d = occ->direction(h))
        return d;
    try {
        const std::array<double, 3> a = h.cast<std::array<double, 3>>();
        return Vector3{a[0], a[1], a[2]};
    } catch (const py::cast_error &) {
        throw py::type_error(call + ": expected a 3d point or direction, e.g. (x, y, z) or occ.X");
    }
}

inline bool IsNumber(const py::handle &h) {
    return (py::isinstance<py::float_>(h) || py::isinstance<py::int_>(h)) && !py::isinstance<py::bool_>(h);
}

// one 3d point (three numbers, or netgen's Pnt, Vec or Dir), not a list of them
inline bool IsOneXYZ(const py::handle &h) {
    if (occ->point(h) || occ->direction(h))
        return true;
    if (!py::isinstance<py::sequence>(h) || py::isinstance<py::str>(h) || py::len(h) != 3)
        return false;
    for (const py::handle x : h)
        if (py::isinstance<py::sequence>(x) || occ->point(x) || occ->direction(x))
            return false;
    return true;
}
// the plane coordinates of a 3d point in every form XYZArg takes, or a list of them for a list
inline py::object ToHVOf(const AxisSystem &axes, const py::object &p) {
    if (IsOneXYZ(p))
        return Tuple2(axes.ToHV(*XYZArg(p, "ToHV")));
    py::list out;
    for (const py::handle q : p)
        out.append(Tuple2(axes.ToHV(*XYZArg(q, "ToHV"))));
    return out;
}
// a 3d point (x, y, z) as the plane's (h, v); anything else as it is
inline py::object InPlane(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    std::optional<Vector3> p = occ->point(h);
    if (!p && (py::isinstance<py::tuple>(h) || py::isinstance<py::list>(h)) && py::len(h) == 3 && IsNumber(h[py::int_(0)]) &&
             IsNumber(h[py::int_(1)]) && IsNumber(h[py::int_(2)]))
        p = Vector3{h[py::int_(0)].cast<double>(), h[py::int_(1)].cast<double>(), h[py::int_(2)].cast<double>()};
    if (!p)
        return py::reinterpret_borrow<py::object>(h);
    const double d = Dot(*p - axes.Origin(), axes.Normal());
    if (std::abs(d) > Tol(Norm(*p) + Norm(axes.Origin())))
        throw py::value_error(call + ": the point " + py::repr(h).cast<std::string>() + " is " +
                              py::repr(py::float_(std::abs(d))).cast<std::string>() +
                              " from the plane; take it into the plane with plane.ToHV(point)");
    return Tuple2(axes.ToHV(*p));
}

inline Vertex HVArg(const AxisSystem &axes, const py::handle &h, const std::string &call) {
    try {
        return InPlane(axes, h, call).cast<Vertex>();
    } catch (const py::cast_error &) {
        throw py::type_error(call + ": " + py::repr(h).cast<std::string>() + " is no point; give (h, v), or (x, y, z) on the plane");
    }
}

// style= of a spline: "averaged" or "clamped", None the config's; the old names raise naming the new
inline SplineStyle StyleArg(const py::handle &h, const std::string &call) {
    if (h.is_none())
        return config::state->spline_style;
    const std::string v = py::isinstance<py::str>(h) ? h.cast<std::string>() : "";
    if (v == "averaged")
        return SplineStyle::Averaged;
    if (v == "clamped")
        return SplineStyle::Clamped;
    if (v == "px" || v == "occ")
        throw py::value_error(call + ": style '" + v + "' is now '" + (v == "px" ? "averaged" : "clamped") + "'");
    throw py::value_error(call + ": style " + py::repr(h).cast<std::string>() + "; it is 'averaged' or 'clamped'");
}
inline const char *StyleName(SplineStyle s) { return s == SplineStyle::Clamped ? "clamped" : "averaged"; }

inline AxisSystem AxesArg(const py::handle &h, const std::string &call);

// the one plane the items share: the first item's, an item off it is an error
inline AxisSystem SharedAxes(const py::iterable &items, const std::string &call, const std::string &whole, const std::string &parts) {
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

// a plane: a Plane, netgen Axes, a Sketch, Shape, Face or Wire (their plane), a planar OCC shape (as Sketch(shape)), a
// Compound whose parts lie on one plane, or a list of these on one plane; None is the xy plane
inline AxisSystem AxesArg(const py::handle &h, const std::string &call) {
    if (h.is_none())
        return AxisSystem();
    if (IsA<AxisSystem>(h))
        return h.cast<AxisSystem>();
    if (std::optional<AxisSystem> a = occ->axes(h, call))
        return *a;
    if (IsA<Shape>(h))
        return h.cast<const Shape &>().Axes();
    if (std::optional<Face> copy; IsA<Face>(h))
        return AsFace(h, copy).Axes();
    if (IsA<Wire>(h))
        return h.cast<const Wire &>().Axes();
    // a sketch
    if (py::hasattr(h, "_axes"))
        return h.attr("_axes").cast<AxisSystem>();
    if (py::hasattr(h, "parts"))
        return SharedAxes(h.attr("parts"), call, "compound", "parts");
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h))
        return SharedAxes(py::reinterpret_borrow<py::iterable>(h), call, "list", "items");
    throw py::type_error(call + ": plane is a Plane, netgen Axes, a Sketch, Shape, Face, Wire or Compound, a planar OCC shape, or a list of these");
}

// the mirror plane through p with normal n (only its origin and normal count)
inline AxisSystem MirrorPlane(const std::array<double, 3> &p, const std::array<double, 3> &n) {
    IsFinite("mirror plane", {p[0], p[1], p[2], n[0], n[1], n[2]});
    if (!(Norm(Vec3(n)) > 0.0))
        throw std::invalid_argument("mirror normal must not be zero");
    return AxesAlong(Pnt3(p), Vec3(n));
}

// the plane of a wire as (origin, u, v) in 3d: for the selectors' coordinates
inline py::tuple PlaneOf(const Wire &w) {
    const AxisSystem &a = w.Axes();
    const Vector3 o = a.ToXYZ({0.0, 0.0});
    return py::make_tuple(Tuple3(o), Tuple3(a.ToXYZ({1.0, 0.0}) - o), Tuple3(a.ToXYZ({0.0, 1.0}) - o));
}

// what holds an object's id: the edges of a wire, face or shape, a sketch itself
template <class T>
inline auto &IdOwner(T &t) {
    if constexpr (std::is_same_v<T, Face>)
        return t.IdWire();
    else if constexpr (std::is_same_v<T, Shape>)
        return t.Edges();
    else
        return t;
}
template <class T>
constexpr bool HasObjectId = requires(T &t) { IdOwner(t).SetIdKind(std::string()); };

// a copy is another object, of the kind that made it (Copy, Mirror, ...); a copied sketch is a sketch
template <class T>
inline T CopyOf(const T &t, const char *kind) {
    T r(t);
    if constexpr (HasObjectId<T>)
        if (IdOwner(r).IdKind() != "Sketch")
            IdOwner(r).SetIdKind(kind);
    return r;
}

// a view's Copy (and copy.copy, copy.deepcopy): what it shows as an independent object, never the view
template <class V, class F>
inline void BindViewCopy(py::class_<V> &cls, F copy, const char *doc) {
    cls.def("Copy", copy, doc)
        .def("__copy__", copy)
        .def("__deepcopy__", [copy](const V &v, const py::dict &) { return copy(v); }, py::arg("memo"));
}

// a point that acts as its coordinates in the plane: .h, .v, h, v = p, np.array(p), p == (h, v)
template <class T, class F>
inline void BindCoordinates(py::class_<T> &cls, F hv) {
    cls.def_property_readonly("h", [hv](const T &t) { return hv(t).x; }, "h: float\n\nWhere it is along the plane's first axis.")
        .def_property_readonly("v", [hv](const T &t) { return hv(t).y; }, "v: float\n\nWhere it is along the plane's second axis.")
        .def("__len__", [](const T &) { return 2; })
        .def("__getitem__", [hv](const T &t, py::ssize_t k) {
            if (k < -2 || k > 1)
                throw py::index_error("a point has two coordinates, h and v");
            const Vertex p = hv(t);
            return k % 2 == 0 ? p.x : p.y;
        }, py::arg("k"))
        .def("__iter__", [hv](const T &t) {
            const Vertex p = hv(t);
            return py::iter(py::make_tuple(p.x, p.y));
        })
        .def("__array__", [hv](const T &t, const py::object &dtype, const py::object &) {
            const Vertex p = hv(t);
            return py::module_::import("numpy").attr("array")(py::make_tuple(p.x, p.y), dtype);
        }, py::arg("dtype") = py::none(), py::arg("copy") = py::none())
        .def("__eq__", [hv](const T &t, const Vertex &o) { return hv(t) == o; }, py::is_operator(), py::arg("other"));
}

// t becomes r in place: the same object, its id kept
template <class T>
inline void ReplaceKeepingId(T &t, T r) {
    const std::string id = IdOwner(t).AssignedId(), kind = IdOwner(t).IdKind();
    t = std::move(r);
    IdOwner(t).SetIdKind(kind);
    IdOwner(t).SetId(id);
}

template <class T, class C>
inline void BindCopy(C &cls) {
    const std::string name = NameOf(cls);
    cls.def("Copy", [](const T &t) { return CopyOf(t, "Copy"); },
            Doc("Copy() -> " + name + "\n\nAn independent copy; copy.copy and copy.deepcopy give the same.\n\nExamples:\n    b = a.Copy()"))
        .def("__copy__", [](const T &t) { return CopyOf(t, "Copy"); })
        .def("__deepcopy__", [](const T &t, const py::dict &) { return CopyOf(t, "Copy"); }, py::arg("memo"));
    if constexpr (HasObjectId<T>)
        cls.def_property("id", [](T &t) { return IdOwner(t).Id(); }, [](T &t, const std::string &id) {
            CheckUserId(id);
            IdOwner(t).SetId(id);
        }, "id: str\n\nThe object's id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set "
           "(an ASCII letter, then letters, digits, _ and single inner spaces); its elements' ids follow it (Shape.3.Line.2).")
            .def("_SetId", [](T &t, const std::string &id) { IdOwner(t).SetId(id); }, py::arg("id"),
                 "for the Python layer: a scoped id it makes from a user's (board.F.Cu), not checked as a user id")
            .def("_SetIdKind", [](T &t, const std::string &kind) { IdOwner(t).SetIdKind(kind); }, py::arg("kind"),
                 "for the Python layer: the kind its auto id counts in (DXF, KiCad, ...)")
            .def("_Same", [](T &t) {
                T r(t);
                IdOwner(r).SetId(IdOwner(t).AssignedId());
                return r;
            }, "for the Python layer: a copy that keeps the id (a Compound's part)");
    if constexpr (requires(T &t) { IdOwner(t).AssignIds(true); })
        cls.def("_RenumberIds", [](T &t) {
            IdOwner(t).AssignIds(true);
            IdOwner(t).AssignVertexIds(true);
        }, "for the Python layer: its edges and vertices numbered anew");
}

// .Copy() and the copy hooks for a class bound elsewhere under name
template <class T>
inline void AddCopy(py::module_ &m, const char *name) {
    auto cls = py::reinterpret_borrow<py::class_<T>>(m.attr(name));
    BindCopy<T>(cls);
}

// the variable an example calls on: sk for the sketches, else the class's name in lower case
inline std::string VariableOf(const std::string &cls) {
    if (cls.ends_with("Sketch"))
        return "sk";
    std::string x = cls;
    x[0] = (char)std::tolower(x[0]);
    return x;
}

// a motion, in place (call) and on a copy (copyCall): its forms' arguments, what it does, its Args: and Raises: lines
struct MotionDoc {
    std::string call, copyCall, verb, past, how, more, args, raises;
    std::vector<std::string> forms, examples;

    const char *Of(const std::string &cls, bool copy) const {
        const std::string c = copy ? copyCall : call, x = VariableOf(cls);
        std::string text;
        for (const std::string &f : forms)
            text += c + "(" + f + ") -> " + cls + "\n";
        text += copy ? "\nA copy " + past + " " + how + "; this " + cls + " is not changed.\n\n" : "\n" + verb + " it in place " + how + ".\n\n";
        text += more + "Args:\n" + args + "\n" + (copy ? "" : "Returns:\n    this " + cls + ".\n\n") + "Raises:\n" + raises + "\nExamples:";
        for (const std::string &e : examples)
            text += "\n    " + x + "." + c + e;
        return Doc(text);
    }
};

inline MotionDoc RotateDoc() {
    return {"Rotate", "Rotated", "Rotates", "rotated",
            "by angle around center in its plane, or around the axis through point along dir in 3d",
            "In 3d only its plane moves; its coordinates in the plane stay.\n\n",
            "    center: the center of the rotation in the plane, (h, v).\n"
            "    angle: the angle in degrees.\n"
            "    point: a point of the axis in 3d, (x, y, z).\n"
            "    dir: the direction of the axis in 3d, (dx, dy, dz).\n",
            "    ValueError: a value is not finite.\n",
            {"center: Point, angle: float", "point: Point3, /, dir: tuple[float, float, float], angle: float"},
            {"((0, 0), 90)", "((0, 0, 0), (0, 0, 1), 45)"}};
}

inline MotionDoc TranslateDoc() {
    return {"Translate", "Translated", "Moves", "moved",
            "by d: in the plane by (dh, dv), in 3d by (dx, dy, dz)",
            "In 3d only its plane moves; its coordinates in the plane stay.\n\n",
            "    d: the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.\n"
            "    dh: the displacement along the plane's first axis.\n"
            "    dv: the displacement along the plane's second axis.\n"
            "    dx: the displacement along x in 3d.\n"
            "    dy: the displacement along y in 3d.\n"
            "    dz: the displacement along z in 3d.\n",
            "    ValueError: a value is not finite.\n",
            {"d: tuple[float, float]", "dh: float, dv: float", "d: tuple[float, float, float]", "dx: float, dy: float, dz: float"},
            {"((10, 0))", "(0, 0, 5)"}};
}

inline MotionDoc MirrorDoc() {
    return {"Mirror", "Mirrored", "Mirrors", "mirrored",
            "at the line through point along dir in its plane, or at the plane through point with normal in 3d",
            "In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are "
            "mirrored at the plane's first axis.\n\n",
            "    point: a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).\n"
            "    dir: the direction of the mirror line in the plane, (dh, dv).\n"
            "    normal: the normal of the mirror plane in 3d, (dx, dy, dz).\n",
            "    ValueError: a value is not finite.\n",
            {"point: Point, /, dir: tuple[float, float]", "point: Point3, /, normal: tuple[float, float, float]"},
            {"((0, 0), (0, 1))", "((0, 0, 0), (1, 0, 0))"}};
}

inline MotionDoc ScaleDoc() {
    return {"Scale", "Scaled", "Scales", "scaled",
            "by factor around center in its plane, or around point in 3d",
            "The mesh sizes maxh and minh stay unless asked for.\n\n",
            "    center: the center of the scaling in the plane, (h, v).\n"
            "    point: the center of the scaling in 3d, (x, y, z).\n"
            "    factor: the scale factor, > 0.\n"
            "    scale_maxh: True also scales maxh.\n"
            "    scale_minh: True also scales minh.\n",
            "    ValueError: factor is not positive and finite, or a coordinate is not finite.\n",
            {"center: Point, factor: float, scale_maxh: bool = False, scale_minh: bool = False",
             "point: Point3, /, factor: float, scale_maxh: bool = False, scale_minh: bool = False"},
            {"((0, 0), 2)", "((0, 0), 0.5, scale_maxh=True)"}};
}

// in the plane by center and angle, in 3d around an axis: then only the plane moves; mirror: with Mirror(ed)
template <class T, bool mirror = true, class C>
inline void BindMotion(C &cls) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string name = NameOf(cls), x = VariableOf(name);
    const MotionDoc rotate = RotateDoc(), translate = TranslateDoc(), scale = ScaleDoc();
    cls.def("Rotate", [](T &t, std::array<double, 2> c, double angle) -> T & { t.Rotate(Point2(c), angle); return t; },
            py::arg("center"), py::arg("angle"), ref, rotate.Of(name, false))
        .def("Rotate", [](T &t, std::array<double, 3> p, std::array<double, 3> d, double angle) -> T & {
            t.Rotate(AxisOf(p, d), angle);
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), py::arg("angle"), ref, "")
        .def("Rotated", [](const T &t, std::array<double, 2> c, double angle) { T r = CopyOf(t, "Rotate"); return r.Rotate(Point2(c), angle), r; },
             py::arg("center"), py::arg("angle"), rotate.Of(name, true))
        .def("Rotated", [](const T &t, std::array<double, 3> p, std::array<double, 3> d, double angle) {
            T r = CopyOf(t, "Rotate");
            r.Rotate(AxisOf(p, d), angle);
            return r;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), py::arg("angle"), "")
        .def("Translate", [](T &t, std::array<double, 2> d) -> T & { t.Translate(Point2(d)); return t; }, py::arg("d"), ref,
             translate.Of(name, false))
        .def("Translate", [](T &t, double dh, double dv) -> T & { t.Translate(Vertex{dh, dv}); return t; }, py::arg("dh"),
             py::arg("dv"), ref, "")
        .def("Translate", [](T &t, std::array<double, 3> d) -> T & { t.Translate(Vec3(d)); return t; }, py::arg("d"), ref, "")
        .def("Translate", [](T &t, double dx, double dy, double dz) -> T & { t.Translate(Vec3({dx, dy, dz})); return t; },
             py::arg("dx"), py::arg("dy"), py::arg("dz"), ref, "")
        .def("Translated", [](const T &t, std::array<double, 2> d) { T r = CopyOf(t, "Translate"); return r.Translate(Point2(d)), r; }, py::arg("d"),
             translate.Of(name, true))
        .def("Translated", [](const T &t, double dh, double dv) { T r = CopyOf(t, "Translate"); return r.Translate(Vertex{dh, dv}), r; },
             py::arg("dh"), py::arg("dv"), "")
        .def("Translated", [](const T &t, std::array<double, 3> d) { T r = CopyOf(t, "Translate"); return r.Translate(Vec3(d)), r; }, py::arg("d"), "")
        .def("Translated", [](const T &t, double dx, double dy, double dz) {
            T r = CopyOf(t, "Translate");
            return r.Translate(Vec3({dx, dy, dz})), r;
        }, py::arg("dx"), py::arg("dy"), py::arg("dz"), "");
    if constexpr (mirror)
        cls.def("Mirror", [](T &t, std::array<double, 2> p, std::array<double, 2> d) -> T & {
            t.Mirror(Point2(p), Point2(d));
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), ref, MirrorDoc().Of(name, false))
        .def("Mirror", [](T &t, std::array<double, 3> p, std::array<double, 3> n) -> T & {
            t.Mirror(MirrorPlane(p, n));
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("normal"), ref, "")
        .def("Mirrored", [](const T &t, std::array<double, 2> p, std::array<double, 2> d) {
            T r = CopyOf(t, "Mirror");
            return r.Mirror(Point2(p), Point2(d)), r;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), MirrorDoc().Of(name, true))
        .def("Mirrored", [](const T &t, std::array<double, 3> p, std::array<double, 3> n) {
            T r = CopyOf(t, "Mirror");
            return r.Mirror(MirrorPlane(p, n)), r;
        }, py::arg("point"), py::pos_only(), py::arg("normal"), "");
    cls.def("Scale", [](T &t, std::array<double, 2> c, double s, bool scaleMaxh, bool scaleMinh) -> T & {
            t.Scale(Point2(c), s, scaleMaxh, scaleMinh);
            return t;
        }, py::arg("center"), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, ref, scale.Of(name, false))
        .def("Scale", [](T &t, std::array<double, 3> p, double s, bool scaleMaxh, bool scaleMinh) -> T & {
            t.Scale(Pnt3(p), s, scaleMaxh, scaleMinh);
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, ref, "")
        .def("Scaled", [](const T &t, std::array<double, 2> c, double s, bool scaleMaxh, bool scaleMinh) {
            T r = CopyOf(t, "Scale");
            return r.Scale(Point2(c), s, scaleMaxh, scaleMinh), r;
        }, py::arg("center"), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, scale.Of(name, true))
        .def("Scaled", [](const T &t, std::array<double, 3> p, double s, bool scaleMaxh, bool scaleMinh) {
            T r = CopyOf(t, "Scale");
            return r.Scale(Pnt3(p), s, scaleMaxh, scaleMinh), r;
        }, py::arg("point"), py::pos_only(), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, "")
        .def("Normal", [](const T &t) { return Tuple3(t.Axes().Normal()); },
             Doc("Normal() -> tuple[float, float, float]\n\nThe normal of its plane in 3d.\n\nExamples:\n    " + x + ".Normal()"))
        .def_property_readonly("bounding_box", [](const T &t) { return BoxTuple(t.Box()); },
                               "bounding_box: tuple[tuple[float, float], tuple[float, float]] | None\n\nIts bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.")
        .def("ToXYZ", [](const T &t, const Vertex &p) { return Tuple3(t.Axes().ToXYZ(p)); }, py::arg("point"), py::pos_only(),
             Doc("ToXYZ(point: Point, /) -> tuple[float, float, float]\n"
                 "ToXYZ(points: list[Point], /) -> list[tuple[float, float, float]]\n\n"
                 "The 3d coordinates of a point (h, v) of its plane, or of a list of points.\n\n"
                 "Args:\n"
                 "    point: (h, v) in the plane.\n"
                 "    points: a list of (h, v).\n\n"
                 "Examples:\n"
                 "    " + x + ".ToXYZ((1, 2))\n"
                 "    " + x + ".ToXYZ([(0, 0), (1, 2)])"))
        .def("ToXYZ", [](const T &t, const std::vector<Vertex> &hv) {
            py::list out;
            for (const Vertex &p : hv)
                out.append(Tuple3(t.Axes().ToXYZ(p)));
            return out;
        }, py::arg("points"), py::pos_only(), "")
        .def("ToHV", [](const T &t, const py::object &p) { return ToHVOf(t.Axes(), p); }, py::arg("point"), py::pos_only(),
             Doc("ToHV(point: Point3, /) -> tuple[float, float]\n"
                 "ToHV(points: list[Point3], /) -> list[tuple[float, float]]\n\n"
                 "The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.\n\n"
                 "Args:\n"
                 "    point: (x, y, z), or netgen's Pnt (a face's center).\n"
                 "    points: a list of them.\n\n"
                 "Examples:\n"
                 "    " + x + ".ToHV((1, 2, 0))\n"
                 "    " + x + ".ToHV([(0, 0, 0), (1, 2, 0)])"));
}

#define DRAW_PARAMS const Name &n, const Size &mh, const Col &c, const Size &mi, const Size &hp, const Layer &la, const Partition &pa, bool construction
#define PROPS Props(n, mh, c, mi, hp, la, pa)
#define DRAW_ARGS                                                                                                       \
    py::arg("name") = py::none(), py::arg("maxh") = py::none(), py::arg("col") = py::none(), py::arg("minh") = py::none(), \
        py::arg("hpref") = py::none(), py::arg("layer") = py::none(), py::arg("partition") = py::none(),                    \
        py::arg("construction") = false

template <class S>
inline void CheckPlane(const S &s, const Wire &w, const std::string &call) {
    if (!s.Axes().IsClose(w.Axes()))
        throw py::value_error(call + ": the target lies in another plane");
}

inline bool IsTarget(const py::handle &t) {
    return py::isinstance<py::str>(t) || IsA<EdgeView>(t) || IsA<Wire>(t) || IsA<Face>(t) || IsA<Shape>(t) ||
           occ->isShape(t);
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

// the edges with that name in the sketch, an Edge, a Wire, a Face, a Shape, or an OCC shape (projected into the sketch as
// construction elements)
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
    } else if (std::optional<Projection> p = occ->projected(t, s.Axes(), false)) {
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

// a target, or (target, point): the point fixes the choice among the target's solutions
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

// an end (from or to): a target, (target, point), or a point (coordinates or a named point) to pass through exactly
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

// one target, (target, point), or two of them
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

// until=: px.first, px.last, or a target it goes up to (target then holds it)
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

// one edge: of several with the name the one nearest to near, with a warning unless near was given or they all lie
// on one carrier
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

// a point, or the name of a construction point
template <class S>
inline std::optional<Vertex> PointArg(S &s, const py::handle &t, const std::string &call) {
    if (t.is_none())
        return std::nullopt;
    if (py::isinstance<py::str>(t))
        return s.PointNamed(t.cast<std::string>(), call);
    return InPlane(s.Axes(), t, call).template cast<Vertex>();
}

// center (a point or a named point) or concentric (a named point, else the center of an arc, or a point)
template <class S>
inline std::optional<Vertex> CenterArg(S &wp, const py::object &center, const py::object &concentric, const Vertex &at,
                                       bool nearGiven, const std::string &call) {
    if (!center.is_none() && !concentric.is_none())
        throw py::value_error(call + ": give center or concentric, not both");
    if (concentric.is_none())
        return PointArg(wp, center, call);
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

// the call as written, for the messages
inline std::string CallText(const std::string &function, const std::vector<std::pair<const char *, py::handle>> &args) {
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

// a helped Line, Arc or Circle call's references, looked up in wp
// call: the call as written, for the messages; from, to: the positional ends (none if not given)
template <class S>
inline sketch::LineRefs LineRefsOf(S &wp, const std::string &call, std::optional<double> l, const py::object &until, const py::object &from,
                           const py::object &to, const py::object &parallel, const py::object &perpendicular, const py::object &angle,
                           const py::object &congruent, const py::object &tangent, const py::object &distance,
                           const py::object &through, const py::object &nearPoint, bool reverse) {
    sketch::LineRefs r;
    r.call = call;
    r.reverse = reverse;
    r.until = UntilArg(wp, until, r.untilTarget, r.call);
    r.nearPoint = PointArg(wp, nearPoint, r.call);
    r.through = PointArg(wp, through, r.call);
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
        // (d, point): d on the side of the point, -d on the other
        py::sequence d = distance;
        const std::optional<Vertex> p = d.size() == 2 ? PointArg(wp, d[1], r.call) : std::nullopt;
        if (!p)
            throw py::value_error(r.call + ": distance is a length or (length, point)");
        const Line *ref = r.parallel ? std::get_if<Line>(&*r.parallel) : nullptr;
        if (!ref)
            throw py::value_error(r.call + ": distance=(d, point) needs a line for parallel");
        const Vertex u = ref->End() - ref->Start(), w = *p - ref->Start();
        const double side = u.x * w.y - u.y * w.x;
        if (side == 0.0)
            throw py::value_error(r.call + ": the point of distance=(d, point) lies on the line it is parallel to");
        r.distance = side > 0.0 ? d[0].cast<double>() : -d[0].cast<double>();
    } else if (!distance.is_none()) {
        const Line *ref = r.parallel ? std::get_if<Line>(&*r.parallel) : nullptr;
        r.distance = distance.cast<double>();
        if (ref) {
            const Vertex u = ref->End() - ref->Start(), w = at - ref->Start();
            const double side = (u.x * w.y - u.y * w.x) / Norm(u);
            if (side < -Tol(NormInf(at)))
                r.distance = -*r.distance;
        }
    }
    return r;
}

// call: the call as written, for the messages; from, to: the positional ends (none if not given); tangent: one target,
// two, or the direction at the start (dh, dv)
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
    a.nearPoint = PointArg(wp, nearPoint, a.call);
    a.through = PointArg(wp, through, a.call);
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
    refs.nearPoint = PointArg(wp, nearPoint, refs.call);
    const Vertex at = refs.nearPoint.value_or(wp.Position());
    refs.center = CenterArg(wp, center, concentric, at, refs.nearPoint.has_value(), refs.call);
    if (!congruent.is_none())
        refs.congruent = ReferenceEdge(wp, congruent, at, refs.nearPoint.has_value(), refs.call);
    refs.through = PointArg(wp, through, refs.call);
    refs.tangent = TangentArg(wp, tangent, refs.call);
    return refs;
}

struct Config {};

template <class T>
inline void ConfigValue(py::class_<Config> &cls, const char *name, T &value, const char *doc) {
    cls.def_property_static(name, [&value](py::object) { return value; }, [&value](py::object, T v) { value = v; }, doc);
}

// export options not given take the config defaults
using OptBool = std::optional<bool>;
using OptDouble = std::optional<double>;

inline void CheckJoinTol(OptDouble tol, const std::string &call) {
    if (tol && !(std::isfinite(*tol) && *tol >= 0.0))
        throw py::value_error(call + "(tol=" + std::to_string(*tol) + "): tol is a length >= 0, the largest gap between "
                              "ends that is joined");
}

inline bool IsEdgeLike(const py::handle &h) {
    return IsA<planex2d::Line>(h) || IsA<planex2d::Arc>(h) || IsA<planex2d::Spline>(h) ||
           IsA<EdgeView>(h) || IsA<Wire>(h);
}

// the edges of an edge, edge view or wire into out; a view's or non-empty wire's axes into axes, which must agree
inline void CollectEdges(const py::handle &h, const std::string &call, EdgeList &out, std::optional<AxisSystem> &axes) {
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

// one edge-like object or an iterable of them
inline EdgeList EdgesArg(const py::object &edges, const std::string &call, std::optional<AxisSystem> &axes) {
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
inline void CheckArcRadii(const std::string &call, const Vertex &start, const Vertex &end, const Vertex &center, OptDouble tol) {
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
    return {Or(on, d.enabled), length ? length : d.max_edge_length, Or(angle, d.max_edge_angle), Or(tol, d.tol),
            Or(ignore, d.ignore_edges_smaller)};
}
// fit_arcs= / fit_splines=: None the config's, True or False on or off, a dict of FitArcs' keywords (on)
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
        if (name == "max_edge_length") {
            f.max_edge_length = v.is_none() ? std::nullopt : std::optional<double>(v.cast<double>());
            continue;
        }
        const double x = v.cast<double>();
        if (name == "tol")
            f.tol = x;
        else if (name == "max_edge_angle")
            f.max_edge_angle = x;
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
// the relations a dict switches, the others as in base
inline AutoConstraint AutoConstraintOptions(const py::dict &d, AutoConstraint base, const std::string &key) {
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
// auto_constraint=: None the config's, True the config's relations, False none, a dict the config's relations with
// the ones it names switched
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
// a QuickSketch's auto_constraint=: whether it asks for constraints (True or a dict)
inline bool AsksAutoConstraint(const py::object &o) { return !o.is_none() && !(py::isinstance<py::bool_>(o) && !o.cast<bool>()); }

// px.Auto: a value worked out from the situation; px.Auto({...}) carries the auto-constraint relations it switches
struct AutoValue {
    std::vector<std::pair<std::string, bool>> switched;
};
inline bool IsAuto(const py::handle &h) { return IsA<AutoValue>(h); }
// the module's one px.Auto: a sentinel is compared with `is`
inline py::object PlainAuto() { return py::module_::import("planex._planex").attr("Auto"); }
// the config's relations with the ones px.Auto({...}) names switched
inline AutoConstraint AutoRelationsOf(const AutoValue &a) {
    AutoConstraint o = config::state->auto_constraint_options;
    for (const auto &[name, on] : a.switched)
        for (const auto &[key, member] : autoConstraintKeys)
            if (name == key)
                o.*member = on;
    return o;
}
// an argument whose default is px.Auto: true for px.Auto; None and px.Auto({...}) raise
inline bool IsPlainAuto(const py::handle &h, const std::string &call, const std::string &name) {
    if (h.is_none())
        throw py::type_error(call + "(" + name + "=None): None is no value here; the default px.Auto works it out");
    if (!IsAuto(h))
        return false;
    if (!h.cast<const AutoValue &>().switched.empty())
        throw py::type_error(call + "(" + name + "=px.Auto({...})): there is nothing to switch here; px.Auto alone works it out");
    return true;
}
// a number, or px.Auto (none)
inline std::optional<double> NumberOrAuto(const py::handle &h, const std::string &call, const std::string &name) {
    if (IsPlainAuto(h, call, name))
        return std::nullopt;
    if (!IsNumber(h))
        throw py::type_error(call + "(" + name + "=): a number or px.Auto, not " + py::repr(h).cast<std::string>());
    return h.cast<double>();
}
// True, False or px.Auto (none)
inline std::optional<bool> BoolOrAuto(const py::handle &h, const std::string &call, const std::string &name) {
    if (IsPlainAuto(h, call, name))
        return std::nullopt;
    if (!py::isinstance<py::bool_>(h))
        throw py::type_error(call + "(" + name + "=): True, False or px.Auto, not " + py::repr(h).cast<std::string>());
    return h.cast<bool>();
}
// a copy tool's auto_constraint= or internal_constraints=: True or False
inline bool TieArg(const py::handle &h, const std::string &call, const std::string &name) {
    if (py::isinstance<py::bool_>(h))
        return h.cast<bool>();
    throw py::type_error(call + "(" + name + "=" + py::repr(h).cast<std::string>() + "): True or False" +
                         (py::isinstance<py::dict>(h) ? "; a tie has no relations to switch" : ""));
}
inline Cleanup Cleaning(OptDouble edges, OptDouble gaps, OptBool free, OptDouble collinear, OptDouble faces = {}, OptBool merge = {}) {
    const Cleanup &d = config::state->cleanup;
    return {Or(edges, d.remove_edges_smaller), Or(gaps, d.close_gaps_smaller), Or(free, d.remove_free_edges),
            Or(collinear, d.merge_collinear_closer), Or(faces, d.remove_faces_smaller), Or(merge, d.merge_small_faces_instead)};
}

#define FIT_ARGS py::arg("fit_splines") = py::none()

#define ARC_ARGS py::arg("fit_arcs") = py::none()

#define CLEANUP_ARGS                                                                                                   \
    py::arg("remove_edges_smaller") = py::none(), py::arg("close_gaps_smaller") = py::none(),                        \
    py::arg("remove_free_edges") = py::none(), py::arg("merge_collinear_closer") = py::none(),                       \
    py::arg("remove_faces_smaller") = py::none(), py::arg("merge_small_faces_instead") = py::none()

// the export options as lambda parameters, in the order of EXPORT_ARGS
#define EXPORT_PARAMS                                                                                                  \
    const py::object &fs, OptDouble re, OptDouble cg, OptBool rf, OptDouble mc, OptDouble rfs, OptBool msf,             \
        const py::object &fa
#define EXPORT_OPTIONS                                                                                                 \
    FitOf(fa, config::state->arc_fit, "fit_arcs"), FitOf(fs, config::state->spline_fit, "fit_splines"),                \
        Cleaning(re, cg, rf, mc, rfs, msf)
// a wire's open ends are legitimate: free edges stay unless asked
#define WIRE_EXPORT_OPTIONS                                                                                            \
    FitOf(fa, config::state->arc_fit, "fit_arcs"), FitOf(fs, config::state->spline_fit, "fit_splines"),                \
        Cleaning(re, cg, rf ? rf : OptBool(false), mc, rfs, msf)
#define EXPORT_ARGS FIT_ARGS, CLEANUP_ARGS, ARC_ARGS

#define ARCFIT_ARGS                                                                                                    \
    py::arg("tol") = py::none(), py::arg("max_edge_length") = py::none(), py::arg("max_edge_angle") = py::none(),    \
    py::arg("ignore_edges_smaller") = py::none()

// the EXPORT_ARGS in a signature line and under Args:; freeDefault: what remove_free_edges=None does
inline std::string ExportSignature() {
    return "fit_splines: bool | dict | None = None, remove_edges_smaller: float | None = None, close_gaps_smaller: float | "
           "None = None, remove_free_edges: bool | None = None, merge_collinear_closer: float | None = None, "
           "remove_faces_smaller: float | None = None, merge_small_faces_instead: bool | None = None, fit_arcs: bool | "
           "dict | None = None";
}

inline std::string ExportArgs(const std::string &freeDefault) {
    const std::string config = "; None takes px.config.";
    return "    fit_splines: fits splines to chains of short edges: True, False or a dict of the fit's options (tol, "
           "max_edge_length, max_edge_angle, ignore_edges_smaller)" + config + "fit_splines.\n"
           "    remove_edges_smaller: removes lines and arcs no longer than this" + config + "remove_edges_smaller.\n"
           "    close_gaps_smaller: closes gaps between ends up to this" + config + "close_gaps_smaller.\n"
           "    remove_free_edges: removes the edges that touch no other edge at either end; " + freeDefault + ". In a Shape "
           "only the edges of its faces go, its wires stay (a wire of one isolated edge too).\n"
           "    merge_collinear_closer: merges two lines whose common vertex is this close to the line through their "
           "far ends" + config + "merge_collinear_closer.\n"
           "    remove_faces_smaller: removes the faces of less area than this (0: none)" + config + "remove_faces_smaller.\n"
           "    merge_small_faces_instead: True unites such a face with the face it shares the longest boundary with "
           "instead" + config + "merge_small_faces_instead.\n"
           "    fit_arcs: fits arcs to chains of short edges: True, False or a dict of FitArcs' options" + config +
           "fit_arcs.\n";
}

// a call with EXPORT_ARGS (after approximation= if it has it): its docstring, examples on x; wire: free edges stay unless asked
inline const char *ExportDoc(const std::string &call, const std::string &returns, const std::string &text, const std::string &x,
                             bool wire, bool approximation = false) {
    return Doc(call + "(" + (approximation ? "approximation: bool = False, " : "") + ExportSignature() + ") -> " + returns +
               "\n\n" + text + "\n\n"
               "Args:\n" +
               (approximation ? "    approximation: True gives the splines as their arc approximation.\n" : "") +
               ExportArgs(wire ? "None: False for a wire" : "None takes px.config.remove_free_edges") + "\n"
               "Raises:\n"
               "    TypeError: fit_splines or fit_arcs has another form or names an unknown option.\n\n"
               "Examples:\n"
               "    " + x + "." + call + "()\n"
               "    " + x + "." + call + "(fit_arcs=True, remove_edges_smaller=1e-6)");
}

inline const char *FitArcsDoc(const std::string &cls, const std::string &text, const std::string &x) {
    const std::string config = "; None takes px.config.fit_arcs[\"";
    return Doc("FitArcs(tol: float | None = None, max_edge_length: float | None = None, max_edge_angle: float | None = None, "
               "ignore_edges_smaller: float | None = None) -> " + cls + "\n\n" + text + "\n\n"
               "Args:\n"
               "    tol: the largest distance of the arc from the chain" + config + "tol\"].\n"
               "    max_edge_length: only edges shorter than this form a chain" + config + "max_edge_length\"], whose "
               "default None takes 1/20 of the length of the loop or open wire an edge lies in.\n"
               "    max_edge_angle: the largest turn between neighboring edges of a chain, in degrees" + config +
               "max_edge_angle\"].\n"
               "    ignore_edges_smaller: edges shorter than this do not end a chain by their direction" + config +
               "ignore_edges_smaller\"].\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".FitArcs()\n"
               "    " + x + ".FitArcs(tol=1e-4, max_edge_length=0.5)");
}

inline const char *CloseGapsDoc(const std::string &cls, const std::string &x, const std::string &more) {
    return Doc("CloseGaps(tol: float) -> " + cls + "\n\n"
               "Joins every endpoint without an exact match on another edge with all endpoints of other edges within tol "
               "at their midpoint, in place.\n\n"
               "The ends meet halfway between where they were, whatever the order of the edges; ends that already "
               "coincide count once.\n\n" + more +
               "Args:\n"
               "    tol: the largest gap closed, a length.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".CloseGaps(1e-6)");
}

inline const char *RemoveSmallEdgesDoc(const std::string &cls, const std::string &x) {
    return Doc("RemoveSmallEdges(tol: float) -> " + cls + "\n\n"
               "Removes the lines and arcs no longer than tol in place; both ends of each merge into its midpoint.\n\n"
               "Lengths are measured before any merge; splines and the edges of a spline (fitted at export) stay.\n\n"
               "Args:\n"
               "    tol: the largest length removed.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".RemoveSmallEdges(1e-4)");
}

inline const char *MergeCollinearEdgesDoc(const std::string &cls, const std::string &lines, const std::string &x) {
    return Doc("MergeCollinearEdges(tol: float | None = None) -> " + cls + "\n\n"
               "Merges lines" + lines + " that meet alone at their vertices into fewer lines, in place: a vertex goes while "
               "every vertex removed between its neighbors stays within tol of the line through them.\n\n"
               "The result stays within tol of the edges the call was given; a second call may merge more. The edges of a "
               "spline stay.\n\n"
               "Args:\n"
               "    tol: the largest distance of a removed vertex from the new line; None takes "
               "px.config.merge_collinear_closer.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".MergeCollinearEdges()\n"
               "    " + x + ".MergeCollinearEdges(1e-6)");
}

inline const char *OpenEndsDoc(const std::string &x) {
    return Doc("OpenEnds() -> list[tuple[int, bool]]\n\n"
               "The endpoints without an exact match on another edge, as (edge index, True at its end, False at its "
               "start).\n\n"
               "Examples:\n"
               "    " + x + ".OpenEnds()");
}

inline const char *IsClosedDoc(const std::string &x) {
    return Doc("IsClosed() -> bool\n\nTrue if every endpoint has an exact match on another edge (OpenEnds() is empty).\n\n"
               "Examples:\n"
               "    " + x + ".IsClosed()");
}

inline const char *SmallEdgesDoc(const std::string &x) {
    return Doc("SmallEdges(tol: float) -> list[int]\n\n"
               "The indices of the edges no longer than tol.\n\n"
               "Args:\n"
               "    tol: the largest length counted.\n\n"
               "Examples:\n"
               "    " + x + ".SmallEdges(1e-3)");
}

inline const char *IntersectionsDoc(const std::string &x, const std::string &whose) {
    return Doc("Intersections(other: Wire, /) -> list[Vertex]\n\n"
               "The points where " + whose + " meet the edges of other, each once, in the plane.\n\n"
               "Args:\n"
               "    other: a Wire on the same plane.\n\n"
               "Raises:\n"
               "    ValueError: other lies on a different plane.\n\n"
               "Examples:\n"
               "    " + x + ".Intersections(px.Line((0, -5), (0, 5)))");
}

inline const char *ContainsDoc(const std::string &x, const std::string &where) {
    return Doc("Contains(other: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, boundary: bool = True) -> "
               "np.ndarray\n"
               "Contains(other: Point | Point3 | Face | Shape | Wire, /, boundary: bool = True) -> bool\n\n"
               "True if the point lies inside " + where + "; for a Face, Shape or Wire, True if it lies in " + where +
               " and their interiors meet (DE-9IM, see Relate).\n\n"
               "A point (x, y, z) off the plane is not inside.\n\n"
               "Args:\n"
               "    other: (h, v) in the plane or (x, y, z); many points as an (n, 2) or (n, 3) array give a bool array "
               "(n,); or a Face, Shape or Wire.\n"
               "    boundary: the answer for a point on the boundary (points only).\n\n"
               "Raises:\n"
               "    TypeError: other is not a point, Face, Shape or Wire.\n\n"
               "Examples:\n"
               "    " + x + ".Contains((1, 2))\n"
               "    " + x + ".Contains([(0, 0), (1, 2)], boundary=False)\n"
               "    " + x + ".Contains(hole)");
}

// what a measure looks at: faces, edges or a named point, on a plane
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

inline std::optional<Thing> AsThing(const py::handle &h) {
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

// a point: two plane coordinates or three in 3d; a point of a sketch or shape where it is in 3d, a vertex view as a
// thing of its plane
inline std::optional<std::vector<double>> AsPoint(const py::handle &h) {
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

// many points: an array or a sequence of sequences of shape (n, 2) for (h, v) in the plane or (n, 3) for (x, y, z), every
// row finite; none for a single point or anything else, an array of another shape raises
inline std::optional<PointRows> PointBatch(const py::handle &h, const std::string &call) {
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

// the rows of an (n, 2) array or of a sequence of (h, v), every row finite
inline PointRows PlaneRows(const py::handle &h, const std::string &call) {
    PointRows a = PointRows::ensure(h);
    if (!a)
        throw py::value_error(call + ": the points are an (n, 2) array of numbers or a sequence of (h, v)");
    if (a.ndim() != 2 || a.shape(1) != 2)
        throw py::value_error(call + ": the points are an (n, 2) array, not shape " + ShapeText(a));
    CheckRowsFinite(a, call);
    return a;
}

// the rows of an (n, 2) or (n, 3) array or a sequence of (h, v) or (x, y, z) on the plane, as (h, v)
inline PointRows PlaneRows(const AxisSystem &axes, const py::handle &h, const std::string &call) {
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

// the points (h, v) or (x, y, z) on the plane, as (h, v)
inline std::vector<Vertex> PlanePoints(const AxisSystem &axes, const py::iterable &items, const std::string &call) {
    std::vector<Vertex> out;
    for (const py::handle &item : items)
        out.push_back(HVArg(axes, item, call));
    return out;
}

// the points of the corners at: a vertex of w, a point (h, v), a vertex name (every vertex with that name), or any list or
// selection of them
inline void AddCorners(const Wire &w, const py::handle &at, const std::string &call, std::vector<Vertex> &out) {
    if (py::isinstance<py::str>(at)) {
        const std::string name = at.cast<std::string>();
        const std::vector<Vertex> named = w.VerticesNamed(name);
        if (named.empty())
            throw py::value_error(call + ": no vertex is named '" + name + "'; name it (vertex.name = ...) or give its point");
        out.insert(out.end(), named.begin(), named.end());
    } else if (IsA<VertexView>(at)) {
        const VertexView &v = at.cast<const VertexView &>();
        if (v.wire != &w)
            throw py::value_error(call + ": a vertex belongs to another object; take the corners from the object that is cut");
        out.push_back(v.Point());
    } else if (const std::optional<std::vector<double>> p = AsPoint(at)) {
        if (p->size() != 2)
            throw py::value_error(call + ": a corner is a point of the plane (h, v), not " + std::to_string(p->size()) + " coordinates");
        out.push_back(Point2({(*p)[0], (*p)[1]}));
    } else if (const std::optional<PointRows> rows = PointBatch(at, call)) {
        if (rows->shape(1) != 2)
            throw py::value_error(call + ": the corners are points of the plane (h, v), not rows of " + std::to_string(rows->shape(1)));
        const auto r = rows->unchecked<2>();
        for (py::ssize_t i = 0; i < rows->shape(0); ++i)
            out.push_back({r(i, 0), r(i, 1)});
    } else if (py::isinstance<py::iterable>(at)) {
        for (const py::handle &item : at)
            AddCorners(w, item, call, out);
    } else {
        throw py::type_error(call + ": at is a vertex, a point (h, v), a vertex name or a list of them, not " +
                             py::str(py::type::handle_of(at).attr("__name__")).cast<std::string>());
    }
}

inline std::vector<Vertex> CornerPoints(const Wire &w, const py::handle &at, const std::string &call) {
    std::vector<Vertex> corners;
    AddCorners(w, at, call, corners);
    if (corners.empty())
        throw py::value_error(call + ": no corner given; at is a vertex, a point (h, v), a vertex name or a list of them");
    return corners;
}

inline const char *CornerDoc(bool chamfer, bool copy, const std::string &cls) {
    const std::string kind = chamfer ? "Chamfer" : "Fillet", call = kind + (copy ? "ed" : ""), x = VariableOf(cls),
                      sizes = chamfer ? "d: float, /, d2: float | None = None, angle: float | None = None, reverse: bool = False"
                                      : "r: float, continuity: Constraint = px.Tangent, /";
    const std::string at = "VertexView | Point | str | list[VertexView | Point | str]";
    const std::string result = chamfer ? "new line" : "arc of radius r";
    std::string text = call + "(at: " + at + ", " + sizes + ") -> " + cls + "\n\n";
    if (copy)
        text += "A copy with the corners " + std::string(chamfer ? "chamfered" : "rounded") + " as " + kind + " does; this " + cls +
                " is not changed.\n\n";
    else
        text += std::string(chamfer ? "Cuts corners with a straight line" : "Rounds corners with an arc tangent to both edges") +
                ", in place.\n\n"
                "A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or "
                "faces share, an open end, a point on no vertex and a tangent joint are not corners). The " + result +
                " is a new edge with the id " + kind + ".k (k counts per object, in the order the corners are given) and the "
                "properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner "
                "going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and "
                "properties, shortened to the " + (chamfer ? "cut" : "touch") + " points; the corner vertex and its properties are "
                "gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the "
                "spline is cut there.\n\n" +
                (chamfer ? "A length on a curved edge is the straight distance from the corner to the cut point.\n\n"
                         : "With px.Curvature a curvature-continuous blend takes the arc's place between the same touch points: a "
                           "quintic spline whose curvature runs from the first edge's to the second's without a jump. It bends "
                           "tighter than the arc inside (its smallest radius about 0.67 r to 0.71 r for corners turning 60 to "
                           "120 degrees) and passes closer to the corner (0.33 r instead of the arc's 0.41 r at a right angle).\n\n") +
                "A size that does not fit (the point on or beyond the far end of an edge, or past a neighboring cut of the "
                "same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that "
                "fits. If no corner is viable the call raises.\n\n";
    text += "Args:\n"
            "    at: the corners: a vertex (" + x + ".vertices[0]), a point (h, v) on a vertex, a vertex name (every vertex with "
            "that name), or a list or selection of these; there is no default.\n";
    if (chamfer)
        text += "    d: the length on the first edge, a length > 0; also on the second if d2 and angle are None.\n"
                "    d2: the length on the second edge, a length > 0; None: not given.\n"
                "    angle: with d, the angle in degrees between the chamfer and the first edge, measured at its cut point "
                "towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.\n"
                "    reverse: True makes the second edge the first.\n";
    else
        text += "    r: the radius, a length > 0.\n"
                "    continuity: px.Tangent the arc, px.Curvature the curvature-continuous blend.\n";
    text += "\nReturns:\n    " + std::string(copy ? "the copy." : "this " + cls + ", so calls chain.") + "\n\n"
            "Raises:\n"
            "    TypeError: at is none of the forms above.\n"
            "    ValueError: a size is not positive and finite" + std::string(chamfer ? ", the angle is not between 0 and 180 or d2 and "
            "angle are both given" : ", continuity is neither px.Tangent nor px.Curvature") + ", no corner is given, a name is no vertex's name, a vertex belongs to another "
            "object, or no corner can be cut (the message names each corner and why); nothing changes then.\n\n"
            "Examples:\n";
    if (chamfer)
        text += "    " + x + "." + call + "((4, 0), 1)\n    " + x + "." + call + "([(4, 0), (4, 4)], 1, d2=2)";
    else
        text += "    " + x + "." + call + "((4, 0), 0.5)\n    " + x + "." + call + "(" + x + ".vertices[0], 0.5, px.Curvature)";
    return Doc(text);
}

// Fillet's continuity: px.Tangent (the arc) or px.Curvature (the blend in its place); true for px.Curvature
inline bool CurvatureOf(sketch::Kind continuity, const std::string &call) {
    if (continuity != sketch::Kind::Tangent && continuity != sketch::Kind::Curvature)
        throw py::value_error(call + ": continuity is px.Tangent or px.Curvature, not px." + sketch::KindName(continuity));
    return continuity == sketch::Kind::Curvature;
}
inline void FilletCorners(Wire &w, const std::vector<Vertex> &corners, double r, bool curvature, const std::string &call) {
    if (curvature)
        w.CutCorners(corners, CurvatureFilletRule{{r}}, call);
    else
        w.CutCorners(corners, FilletRule{r}, call);
}

// the four calls of a class: Fillet and Chamfer in place, Filleted and Chamfered on a copy; wireOf gives the wire whose
// vertices are the corners
template <class T, class C, class W>
inline void BindCorners(C &cls, W wireOf) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string name = NameOf(cls);
    cls.def("Fillet", [wireOf](T &t, const py::object &at, double r, sketch::Kind continuity) -> T & {
            const bool curvature = CurvatureOf(continuity, "Fillet");
            Wire &w = wireOf(t);
            FilletCorners(w, CornerPoints(w, at, "Fillet"), r, curvature, "Fillet");
            return t;
        }, py::arg("at"), py::arg("r"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), ref, CornerDoc(false, false, name))
        .def("Filleted", [wireOf](T &t, const py::object &at, double r, sketch::Kind continuity) {
            const bool curvature = CurvatureOf(continuity, "Filleted");
            const std::vector<Vertex> corners = CornerPoints(wireOf(t), at, "Filleted");
            T copy = CopyOf(t, "Fillet");
            FilletCorners(wireOf(copy), corners, r, curvature, "Filleted");
            return copy;
        }, py::arg("at"), py::arg("r"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), CornerDoc(false, true, name))
        .def("Chamfer", [wireOf](T &t, const py::object &at, double d, OptDouble d2, OptDouble angle, bool reverse) -> T & {
            Wire &w = wireOf(t);
            w.CutCorners(CornerPoints(w, at, "Chamfer"), ChamferRule{d, d2, angle, reverse}, "Chamfer");
            return t;
        }, py::arg("at"), py::arg("d"), py::pos_only(), py::arg("d2") = py::none(), py::arg("angle") = py::none(),
             py::arg("reverse") = false, ref, CornerDoc(true, false, name))
        .def("Chamfered", [wireOf](T &t, const py::object &at, double d, OptDouble d2, OptDouble angle, bool reverse) {
            const std::vector<Vertex> corners = CornerPoints(wireOf(t), at, "Chamfered");
            T copy = CopyOf(t, "Chamfer");
            wireOf(copy).CutCorners(corners, ChamferRule{d, d2, angle, reverse}, "Chamfered");
            return copy;
        }, py::arg("at"), py::arg("d"), py::pos_only(), py::arg("d2") = py::none(), py::arg("angle") = py::none(),
             py::arg("reverse") = false, CornerDoc(true, true, name));
}

// f(state, i) for every row i < n, without the GIL; many rows on the executor's threads, each with its own copy of state
template <class S, class F>
inline void ForRows(size_t n, S &state, F f) {
    // rows per task: enough work to outweigh scheduling it
    constexpr size_t chunk = 4096;
    py::gil_scoped_release release;
    if (!threadsAvailable || n < 2 * chunk || Executor().num_workers() < 2 || Executor().this_worker_id() >= 0) {
        for (size_t i = 0; i < n; ++i)
            f(state, i);
        return;
    }
    tf::Executor &executor = Executor();
    std::vector<std::optional<S>> own(executor.num_workers());
    tf::Taskflow flow;
    for (size_t first = 0; first < n; first += chunk)
        flow.emplace([&, first] {
            std::optional<S> &s = own[(size_t)executor.this_worker_id()];
            if (!s)
                s.emplace(state);
            for (size_t i = first; i < std::min(n, first + chunk); ++i)
                f(*s, i);
        });
    executor.run(flow).get();
}

inline Pair3 Pair3Of(const AxisSystem &axes, const Nearest &n) { return {n.d, axes.ToXYZ(n.a), axes.ToXYZ(n.b)}; }

// a target in the plane: a point, a thing on the same plane, or an OCC shape or thing elsewhere projected into it
inline std::optional<Planar> TargetInPlane(const AxisSystem &axes, const py::handle &target, bool project, std::optional<Vertex> &point) {
    if (std::optional<std::vector<double>> p = AsPoint(target)) {
        point = p->size() == 2 ? Vertex{(*p)[0], (*p)[1]} : axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
        return Planar{Wire(axes), Wire(axes)};
    }
    if (std::optional<Thing> t = AsThing(target)) {
        if (t->point)
            point = *t->point;
        if (t->axes.IsClose(axes))
            return t->AsPlanar();
        if (project)
            return Planar{Wire(axes), occ->across(t->AsPlanar(), t->axes, axes)};
        return std::nullopt;
    }
    if (project)
        if (std::optional<Projection> p = occ->projected(target, axes, false)) {
            if (p->wire.Empty() && p->points.size() == 1)
                point = p->points.front().p;
            return Planar{Wire(axes), p->wire};
        }
    return std::nullopt;
}

inline Pair3 NearestPair(const Thing &self, const py::handle &target, bool project) {
    const Planar s = self.AsPlanar();
    if (s.Empty() && !self.point)
        throw py::value_error("Distance: nothing to measure from");
    // a 3d point off the plane: the nearest point of the plane set to its projection (project: measured in the plane)
    if (std::optional<std::vector<double>> p = AsPoint(target); p && p->size() == 3 && !project) {
        const Vector3 q{(*p)[0], (*p)[1], (*p)[2]};
        const Vector3 a = self.axes.ToXYZ(self.point ? *self.point : NearestTo(s, self.axes.ToHV(q)).a);
        return {Norm(a - q), a, q};
    }
    std::optional<Vertex> point;
    if (std::optional<Planar> t = TargetInPlane(self.axes, target, project, point)) {
        if (self.point && point)
            return Pair3Of(self.axes, Nearest{Norm(*point - *self.point), *self.point, *point});
        if (self.point)
            return Pair3Of(self.axes, NearestTo(*t, *self.point).Swapped());
        if (point)
            return Pair3Of(self.axes, NearestTo(s, *point));
        return Pair3Of(self.axes, NearestBetween(s, *t));
    }
    return occ->nearest(s, self.point, self.axes, target);
}

inline void CheckComponents(const std::string &components) {
    if (components.find_first_not_of("hvxyz") != std::string::npos)
        throw py::value_error("components are letters of 'hvxyz', got '" + components + "'");
}

// the component of d: h and v in the plane, x, y and z in 3d
inline double ComponentOf(const AxisSystem &axes, const Vector3 &d, char c) {
    switch (c) {
        case 'h': return Dot(d, axes.XDirection());
        case 'v': return Dot(d, axes.YDirection());
        case 'x': return d.x;
        case 'y': return d.y;
        default: return d.z;
    }
}

// the components of b - a; one value for one letter
inline py::object Components(const AxisSystem &axes, const Pair3 &p, const std::string &components) {
    CheckComponents(components);
    const Vector3 d = p.b - p.a;
    py::list out;
    for (char c : components)
        out.append(ComponentOf(axes, d, c));
    if (out.size() == 1)
        return out[0];
    return py::tuple(out);
}

// a set prepared for many points: the planar set, its edges and their grids
struct Prepared {
    Planar s;
    Wire all;
    PlanarIndex index;

    explicit Prepared(Planar p) : s(std::move(p)), all(s.All()), index(s.region, all, s.Tolerance()) {}
    Prepared(const Prepared &) = delete;
    Prepared &operator=(const Prepared &) = delete;
};

// NearestPair of the thing and the point in row r of c coordinates (project: a 3d point is measured in the plane)
inline Pair3 PairAt(const Thing &self, PlanarIndex &q, const double *r, size_t c, bool project) {
    if (c == 3 && !project) {
        const Vector3 t{r[0], r[1], r[2]};
        const Vector3 a = self.axes.ToXYZ(self.point ? *self.point : q.NearestTo(self.axes.ToHV(t)).a);
        return {Norm(a - t), a, t};
    }
    const Vertex p = c == 3 ? self.axes.ToHV({r[0], r[1], r[2]}) : Vertex{r[0], r[1]};
    const Nearest n = self.point ? Nearest{Norm(p - *self.point), *self.point, p} : q.NearestTo(p);
    return Pair3Of(self.axes, n);
}

// Distance to every row: (n,), with k components (n, k), one value per row for one letter
inline py::array_t<double> DistancesOf(const Thing &self, const PointRows &pts, bool project, const std::optional<std::string> &components) {
    Prepared set(self.AsPlanar());
    if (set.s.Empty() && !self.point)
        throw py::value_error("Distance: nothing to measure from");
    if (components)
        CheckComponents(*components);
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    const size_t k = components ? components->size() : 0;
    py::array_t<double> out = components && k != 1 ? py::array_t<double>({pts.shape(0), (py::ssize_t)k})
                                                   : py::array_t<double>(pts.shape(0));
    const double *x = pts.data();
    double *o = out.mutable_data();
    ForRows(n, set.index, [&](PlanarIndex &q, size_t i) {
        const Pair3 p = PairAt(self, q, x + c * i, c, project);
        if (!components) {
            o[i] = p.d;
            return;
        }
        const Vector3 d = p.b - p.a;
        for (size_t j = 0; j < k; ++j)
            o[k * i + j] = ComponentOf(self.axes, d, (*components)[j]);
    });
    return out;
}

inline py::object DistanceOf(const Thing &self, const py::handle &target, bool project, const std::optional<std::string> &components) {
    if (std::optional<PointRows> pts = PointBatch(target, "Distance"))
        return DistancesOf(self, *pts, project, components);
    Pair3 p = NearestPair(self, target, project);
    if (!components)
        return py::float_(p.d);
    return Components(self.axes, p, *components);
}

// Distance(signed=True) to (h, v) or to every row of an (n, 2) array: to the boundary edges, negative inside the region
// (its parity), 0 within the tolerance of Contains
inline py::object SignedDistance(const Wire &region, const Wire &boundary, const py::handle &target, const std::optional<std::string> &components) {
    if (components)
        throw py::value_error("Distance(signed=True): a signed distance has no components");
    const std::optional<PointRows> pts = PointBatch(target, "Distance");
    const std::optional<std::vector<double>> p = pts ? std::nullopt : AsPoint(target);
    if (!pts && !p)
        throw py::value_error("Distance(signed=True): the target is a point (h, v) or an (n, 2) array of them");
    if ((pts && pts->shape(1) == 3) || (p && p->size() == 3))
        throw py::value_error("Distance(signed=True): the points lie in the plane, (h, v) or an (n, 2) array");
    if (boundary.Empty())
        throw py::value_error("Distance: nothing to measure from");
    PlanarIndex index(region, boundary, region.Tol());
    if (p)
        return py::float_(index.SignedDistance({(*p)[0], (*p)[1]}));
    py::array_t<double> out(pts->shape(0));
    const double *x = pts->data();
    double *o = out.mutable_data();
    ForRows((size_t)(pts->shape(0)), index, [&](PlanarIndex &q, size_t i) { o[i] = q.SignedDistance({x[2 * i], x[2 * i + 1]}); });
    return out;
}

// the signed distance to the boundary of the union of the faces: an edge between two faces or through one does not bound
template <class T>
inline py::object SignedDistanceOf(const T &x, const py::handle &target, const std::optional<std::string> &components) {
    if constexpr (std::is_same_v<T, Face>) {
        return SignedDistance(x.Edges(), x.Edges(), target, components);
    } else if constexpr (std::is_same_v<T, Shape>) {
        if (!x.Empty() && !x.HasFaces())
            throw py::value_error("Distance(signed=True): the shape has no faces, so no inside");
        return SignedDistance(FaceRegion(x), x.Outline(), target, components);
    } else {
        throw py::value_error("Distance(signed=True): a wire has no inside");
    }
}

// the carrier of a line (a line long enough to reach over the box) or of an arc (the full circle)
inline Thing CarrierOver(const EdgeView &r, const BBox &box) {
    Wire carrier(r.wire->Axes());
    if (const Line *l = std::get_if<Line>(&r.Get())) {
        const Vertex mid = l->MidPoint(), dir = (l->End() - l->Start()) * (1.0 / l->Length());
        const Vertex center = box.Center();
        const double reach = Norm(box.Size()) + Norm(mid - center) + 1.0;
        carrier.Add(Line(mid - dir * reach, mid + dir * reach));
    } else if (const planex2d::Arc *a = std::get_if<planex2d::Arc>(&r.Get())) {
        const Vertex opposite = a->Center() * 2.0 - a->Start();
        carrier.Add(planex2d::Arc(a->Start(), opposite, a->Center(), true));
        carrier.Add(planex2d::Arc(opposite, a->Start(), a->Center(), true));
    } else if (const Circle *c = std::get_if<Circle>(&r.Get())) {
        for (const planex2d::Arc &half : c->Halves())
            carrier.Add(half);
    } else {
        throw py::value_error("on_carrier: a spline has no carrier");
    }
    return ThingOf(carrier);
}

// over the edge and the target too
inline Thing CarrierOf(const EdgeView &r, const py::handle &target, bool project) {
    const Wire &owner = *r.wire;
    std::optional<Vertex> point;
    std::optional<Planar> t = TargetInPlane(owner.Axes(), target, project, point);
    if (!t)
        throw py::value_error("on_carrier: the target must lie in the plane (a point, a thing on the same plane, or project=True)");
    BBox box = BoxOf(r.Get());
    box.Grow(t->region.Box());
    box.Grow(t->curves.Box());
    if (point)
        box.Grow(BBox::Of(*point, *point));
    return CarrierOver(r, box);
}

// over the edge and every row
inline Thing CarrierOf(const EdgeView &r, const PointRows &pts) {
    const AxisSystem &axes = r.wire->Axes();
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    const double *x = pts.data();
    BBox box = BoxOf(r.Get());
    for (size_t i = 0; i < n; ++i)
        box.Grow(c == 2 ? Vertex{x[2 * i], x[2 * i + 1]} : axes.ToHV({x[3 * i], x[3 * i + 1], x[3 * i + 2]}));
    return CarrierOver(r, box);
}

// ClosestPoint of every row: (n, 2) or (n, 3)
inline py::array_t<double> ClosestPointsOf(const Thing &self, const PointRows &pts) {
    Prepared set(self.AsPlanar());
    if (set.s.Empty())
        throw py::value_error("ClosestPoint: nothing to measure from");
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    py::array_t<double> out({pts.shape(0), pts.shape(1)});
    const double *x = pts.data();
    double *o = out.mutable_data();
    ForRows(n, set.index, [&](PlanarIndex &q, size_t i) {
        const double *r = x + c * i;
        double *w = o + c * i;
        if (c == 2) {
            const Vertex a = q.NearestTo({r[0], r[1]}).a;
            w[0] = a.x;
            w[1] = a.y;
            return;
        }
        const Vector3 a = self.axes.ToXYZ(q.NearestTo(self.axes.ToHV({r[0], r[1], r[2]})).a);
        w[0] = a.x;
        w[1] = a.y;
        w[2] = a.z;
    });
    return out;
}

inline py::object ClosestPointOf(const Thing &self, const py::handle &point) {
    if (std::optional<PointRows> pts = PointBatch(point, "ClosestPoint"))
        return ClosestPointsOf(self, *pts);
    std::optional<std::vector<double>> p = AsPoint(point);
    if (!p)
        throw py::type_error("ClosestPoint: a point of two plane coordinates or three in 3d");
    const Planar s = self.AsPlanar();
    if (s.Empty())
        throw py::value_error("ClosestPoint: nothing to measure from");
    if (p->size() == 2)
        return Tuple2(NearestTo(s, {(*p)[0], (*p)[1]}).a);
    Vertex uv = self.axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
    return Tuple3(self.axes.ToXYZ(NearestTo(s, uv).a));
}

// Contains of every row: (n,) of bool
inline py::array_t<bool> ContainsMany(const Planar &s, const AxisSystem &axes, const PointRows &pts, bool boundary) {
    Prepared set(s);
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    py::array_t<bool> out(pts.shape(0));
    const double *x = pts.data();
    bool *o = out.mutable_data();
    const double tol = set.s.Tolerance();
    ForRows(n, set.index, [&](PlanarIndex &q, size_t i) {
        const double *r = x + c * i;
        Vertex uv{r[0], r[1]};
        if (c == 3) {
            uv = axes.ToHV({r[0], r[1], r[2]});
            if (Norm(axes.ToXYZ(uv) - Vector3{r[0], r[1], r[2]}) > std::max(tol, Tol(NormInf(uv)))) {
                o[i] = false;
                return;
            }
        }
        const Where w = q.Locate(uv);
        o[i] = w == Where::Inside || (w == Where::Boundary && boundary);
    });
    return out;
}

inline py::object ContainsOf(const Planar &s, const AxisSystem &axes, const py::handle &point, bool boundary) {
    if (std::optional<PointRows> pts = PointBatch(point, "Contains"))
        return ContainsMany(s, axes, *pts, boundary);
    std::optional<std::vector<double>> p = AsPoint(point);
    if (!p)
        throw py::type_error("Contains: a point of two plane coordinates or three in 3d");
    Vertex uv{(*p)[0], (*p)[1]};
    if (p->size() == 3) {
        uv = axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
        if (Norm(axes.ToXYZ(uv) - Vector3{(*p)[0], (*p)[1], (*p)[2]}) > std::max(s.Tolerance(), Tol(NormInf(uv))))
            return py::bool_(false);
    }
    Where w = Locate(s, uv);
    return py::bool_(w == Where::Inside || (w == Where::Boundary && boundary));
}

inline RelateOperand ShapeOperand(const Shape &s, const std::string &call) {
    if (s.HasFaces() && s.WireCount() > 0)
        throw py::value_error(call + ": a shape of faces and wires has no relation, as it has no boolean; compare "
                                     "shape.faces or shape.wires");
    return s.HasFaces() ? RelateOperand{2, s.Outline(), {}} : RelateOperand{1, s.WireEdges(), {}};
}

// what the DE-9IM relations compare: a Face or a Shape of faces as a region, a Wire or a Shape of wires as lines, a
// point (h, v) or (x, y, z) on the plane
inline RelateOperand RelateOperandOf(const py::handle &h, const AxisSystem &axes, const std::string &call) {
    auto onPlane = [&](const AxisSystem &other) {
        if (!other.IsClose(axes))
            throw py::value_error(call + ": the two lie on different planes; move them onto one plane first");
    };
    if (std::optional<Face> copy; IsA<Face>(h)) {
        const Face &f = AsFace(h, copy);
        onPlane(f.Axes());
        return {2, f.Edges(), {}};
    }
    if (IsA<Shape>(h)) {
        const Shape &s = h.cast<const Shape &>();
        onPlane(s.Axes());
        return ShapeOperand(s, call);
    }
    if (IsA<Wire>(h)) {
        const Wire &w = h.cast<const Wire &>();
        onPlane(w.Axes());
        return {1, w, {}};
    }
    if (py::hasattr(h, "parts"))
        throw py::type_error(call + ": a Compound has no relations, merge it first with px.Unify(comp)");
    std::optional<std::vector<double>> p = AsPoint(h);
    if (!p)
        throw py::type_error(call + ": other is a Face, Shape, Wire or a point");
    Vertex uv{(*p)[0], (*p)[1]};
    if (p->size() == 3) {
        uv = axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
        const Vector3 q = axes.ToXYZ(uv);
        const double off = std::max({std::abs(q.x - (*p)[0]), std::abs(q.y - (*p)[1]), std::abs(q.z - (*p)[2])});
        if (off > Tol(NormInf(uv)))
            throw py::value_error(call + ": the point lies off the plane");
    }
    return {0, Wire(axes), uv};
}

inline bool IsGeometry(const py::handle &h) { return IsA<Face>(h) || IsA<Shape>(h) || IsA<Wire>(h); }

inline RelateMatrix RelationOf(const RelateOperand &a, const py::handle &other, const std::string &call) {
    const RelateOperand b = RelateOperandOf(other, a.edges.Axes(), call);
    py::gil_scoped_release release;
    return Relate(a, b);
}

// Relate and the named relations on a class whose objects give their operand by operandOf
template <class T, class C, class F>
inline void BindRelations(C &cls, F operandOf) {
    const std::string x = VariableOf(NameOf(cls));
    cls.def("Relate", [operandOf](const T &self, const py::object &other, const std::optional<std::string> &pattern) -> py::object {
        if (pattern)
            CheckRelatePattern(*pattern);
        const RelateMatrix m = RelationOf(operandOf(self), other, "Relate");
        return pattern ? py::object(py::bool_(m.Matches(*pattern))) : py::object(py::str(m.Text()));
    }, py::arg("other"), py::pos_only(), py::arg("pattern") = py::none(),
       // asterisks after a space as reST literals: Sphinx reads them as emphasis
       Doc("Relate(other: Face | Shape | Wire | Point, /, pattern: str | None = None) -> str | bool\n\n"
           "The DE-9IM matrix of this and other: nine characters for the intersections of the interiors, boundaries "
           "and exteriors (rows this, columns other), each the dimension 0, 1, 2 or F for none. A face is a region, a "
           "wire lines (the ends of its open chains the boundary), a point a point.\n\n"
           "Args:\n"
           "    other: a Face, Shape (of faces or of wires), Wire, or a point (h, v) or (x, y, z) on the plane.\n"
           "    pattern: nine characters of ``F``, ``T``, ``*``, ``0``, ``1``, ``2``: True if the matrix matches it.\n\n"
           "Raises:\n"
           "    TypeError: other is none of these.\n"
           "    ValueError: other lies on another plane; a shape has faces and wires; a pattern is not nine of ``F``, ``T``, ``*``, ``0``, ``1``, ``2``.\n\n"
           "Examples:\n"
           "    " + x + ".Relate(other)\n"
           "    " + x + ".Relate(other, 'T*F**F***')"));
    struct Named {
        const char *name, *text;
        bool (*test)(const RelateMatrix &, int, int);
    };
    static const Named named[] = {
        {"Intersects", "True if this and other have a point in common.",
         [](const RelateMatrix &m, int, int) { return !m.Matches("FF*FF****"); }},
        {"IsDisjoint", "True if this and other have no point in common.",
         [](const RelateMatrix &m, int, int) { return m.Matches("FF*FF****"); }},
        {"Touches", "True if this and other meet only on their boundaries.",
         [](const RelateMatrix &m, int, int) { return Touches(m); }},
        {"Crosses", "True if this and other cross: their interiors meet in a lower dimension than the larger of the two "
                    "has, and each has a part outside the other (lines crossing at points, a line through a face).",
         [](const RelateMatrix &m, int da, int db) { return Crosses(m, da, db); }},
        {"Overlaps", "True if this and other, of one dimension, share a part of that dimension and each has a part "
                     "outside the other.",
         [](const RelateMatrix &m, int da, int db) { return Overlaps(m, da, db); }},
        {"Covers", "True if no point of other lies outside this.",
         [](const RelateMatrix &m, int, int) { return Covers(m); }},
        {"IsCoveredBy", "True if no point of this lies outside other.",
         [](const RelateMatrix &m, int, int) { return Covers(m.Transposed()); }},
        {"IsWithin", "True if this lies in other and their interiors meet.",
         [](const RelateMatrix &m, int, int) { return m.Matches("T*F**F***"); }},
        {"Equals", "True if this and other are the same point set.",
         [](const RelateMatrix &m, int, int) { return m.Matches("T*F**FFF*"); }},
    };
    for (const Named &n : named)
        cls.def(n.name, [operandOf, test = n.test, name = std::string(n.name)](const T &self, const py::object &other) {
            const RelateOperand a = operandOf(self);
            const RelateOperand b = RelateOperandOf(other, a.edges.Axes(), name);
            py::gil_scoped_release release;
            return test(Relate(a, b), a.dim, b.dim);
        }, py::arg("other"), py::pos_only(),
           Doc(std::string(n.name) + "(other: Face | Shape | Wire | Point, /) -> bool\n\n" + n.text +
               " By the DE-9IM matrix, see Relate.\n\n"
               "Args:\n"
               "    other: a Face, Shape (of faces or of wires), Wire, or a point (h, v) or (x, y, z) on the plane.\n\n"
               "Raises:\n"
               "    TypeError: other is not a Face, Shape, Wire or point.\n"
               "    ValueError: other lies on another plane.\n\n"
               "Examples:\n"
               "    " + x + "." + n.name + "(other)"));
}

// Contains of a point (and arrays of points) as before; of a Face, Shape or Wire by the DE-9IM matrix
template <class F>
inline py::object ContainsAny(const Planar &s, const AxisSystem &axes, const py::handle &other, bool boundary, F operand) {
    if (IsGeometry(other))
        return py::bool_(RelationOf(operand(), other, "Contains").Matches("T*****FF*"));
    return ContainsOf(s, axes, other, boundary);
}

inline py::tuple MatrixTuple(const Matrix3 &m) {
    return py::make_tuple(py::make_tuple(m[0][0], m[0][1], m[0][2]), py::make_tuple(m[1][0], m[1][1], m[1][2]),
                          py::make_tuple(m[2][0], m[2][1], m[2][2]));
}

inline MassProps MassOfThing(const Thing &t) {
    Planar s = t.AsPlanar();
    return MassOf(s.region, s.curves);
}

inline py::object CenterOf(const Thing &t) {
    MassProps p = MassOfThing(t);
    if (p.mass == 0.0)
        return py::none();
    return Tuple3(t.axes.ToXYZ(p.center));
}

// a single circle the edges lie on, with the swept angle over all of them
struct OnCircle {
    Vertex center;
    double radius, sweep;
};
inline std::optional<OnCircle> CircleOfEdges(const Wire &w) {
    std::optional<OnCircle> c;
    for (const Edge &e : w) {
        const std::optional<CircleCarrier> k = CarrierOf(e);
        if (!k)
            return std::nullopt;
        const planex2d::Arc *a = std::get_if<planex2d::Arc>(&e);
        const double tol = Tol(LengthScaleOf(e));
        if (!c)
            c = OnCircle{k->center, k->radius, 0.0};
        else if (Norm(k->center - c->center) > tol || std::abs(k->radius - c->radius) > tol)
            return std::nullopt;
        c->sweep += a ? a->Sweep() : 2.0 * std::numbers::pi;
    }
    return c;
}

inline const char *KindOf(const Edge &e) {
    return std::holds_alternative<Line>(e) ? "line" : std::holds_alternative<planex2d::Arc>(e) ? "arc" : std::holds_alternative<Circle>(e) ? "circle" : "spline";
}

// the properties of one measured thing
inline py::dict MeasureDict(const Thing &t) {
    py::dict d;
    double area = 0.0;
    for (const Face &f : t.faces)
        area += f.Area();
    d["faces"] = t.faces.size();
    d["area"] = area;
    d["edges"] = t.edges.Size();
    d["length"] = t.edges.Length();
    if (t.point) {
        d["type"] = "point";
        d["center"] = Tuple3(t.axes.ToXYZ(*t.point));
        d["bounding_box"] = BoxTuple(BBox::Of(*t.point, *t.point));
        return d;
    }
    std::optional<OnCircle> circle = t.faces.empty() ? CircleOfEdges(t.edges) : std::nullopt;
    if (t.faces.size() == 1)
        d["type"] = "face";
    else if (!t.faces.empty())
        d["type"] = "faces";
    else if (t.edges.Empty())
        d["type"] = "none";
    else if (circle)
        d["type"] = circle->sweep >= 2.0 * std::numbers::pi * (1.0 - 1e-12) ? "circle" : "arc";
    else if (t.edges.Size() == 1)
        d["type"] = KindOf(t.edges[0]);
    else
        d["type"] = "edges";
    Planar s = t.AsPlanar();
    BBox box = s.region.Box();
    box.Grow(s.curves.Box());
    d["bounding_box"] = BoxTuple(box);
    MassProps p = MassOf(s.region, s.curves);
    if (p.mass == 0.0)
        return d;
    d["center"] = Tuple3(t.axes.ToXYZ(p.center));
    d["inertia"] = MatrixTuple(Inertia(p, t.axes));
    Section sec = SectionOf(p);
    d["Ih"] = sec.iu;
    d["Iv"] = sec.iv;
    d["Ihv"] = sec.iuv;
    d["I1"] = sec.i1;
    d["I2"] = sec.i2;
    d["principal_angle"] = sec.angle;
    if (circle) {
        d["center"] = Tuple3(t.axes.ToXYZ(circle->center));
        d["radius"] = circle->radius;
        d["angle"] = Degrees(std::min(circle->sweep, 2.0 * std::numbers::pi));
    }
    return d;
}

// the direction of a thing that is a single line, in 3d, not normalized
inline std::optional<Vector3> LineDirection(const Thing &t) {
    if (!t.faces.empty() || t.point || t.edges.Size() != 1)
        return std::nullopt;
    const Line *l = std::get_if<Line>(&t.edges[0]);
    if (!l)
        return std::nullopt;
    return t.axes.ToXYZ(l->End()) - t.axes.ToXYZ(l->Start());
}

// two things: their distance, its components and, for two lines, the angle between them in [0, 90] degrees
inline py::dict MeasureBetween(const Thing &a, const py::handle &target, const std::optional<Thing> &b, bool project) {
    Pair3 p = NearestPair(a, target, project);
    py::dict d;
    d["type"] = "between";
    d["distance"] = p.d;
    const Vector3 v = p.b - p.a;
    d["du"] = Dot(v, a.axes.XDirection());
    d["dv"] = Dot(v, a.axes.YDirection());
    d["dx"] = v.x;
    d["dy"] = v.y;
    d["dz"] = v.z;
    d["points"] = py::make_tuple(Tuple3(p.a), Tuple3(p.b));
    if (b)
        if (std::optional<Vector3> da = LineDirection(a), db = LineDirection(*b); da && db) {
            double angle = Degrees(AngleBetween(*da, *db));
            d["angle"] = std::min(angle, 180.0 - angle);
        }
    return d;
}

// the faces and edges with that name (all without one)
inline Thing NamedIn(const AxisSystem &axes, const FaceList &faces, const Wire &edges, const std::optional<std::string> &name) {
    Thing t{axes, {}, Wire(axes), {}};
    for (const Face &f : faces)
        if (!name || (f.Props() && f.Props()->name == *name))
            t.faces.push_back(f);
    for (const Edge &e : edges)
        if (!name || (PropsOf(e) && PropsOf(e)->name == *name))
            t.edges.Add(e);
    return t;
}

// a target of Measure(to=...): a name in the same object, or anything Distance takes
template <class F>
inline py::dict MeasureWith(const Thing &a, const py::object &to, bool project, F named) {
    if (to.is_none())
        return MeasureDict(a);
    if (!py::isinstance<py::str>(to))
        return MeasureBetween(a, to, AsThing(to), project);
    const std::string name = to.cast<std::string>();
    Thing b = named(name);
    if (b.point)
        return MeasureBetween(a, py::cast(std::vector<double>{b.point->x, b.point->y}), b, project);
    if (b.faces.empty() && b.edges.Empty())
        throw py::value_error("Measure(to='" + name + "'): nothing has that name");
    return MeasureBetween(a, py::cast(Shape(b.axes, b.faces, {b.edges})), b, project);
}

inline const char *DistanceDoc(const std::string &x) {
    // one signature line per form of the target, so a type checker knows the return (an @overload each in the stub)
    // the many points' form first: an OCC shape is Any to a type checker (netgen has no stubs) and would take them
    return Doc("Distance(target: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, project: bool = False, "
               "components: str | None = None, signed: bool = False) -> np.ndarray\n"
               "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, "
               "project: bool = False, components: None = None, signed: bool = False) -> float\n"
               "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, "
               "project: bool = False, *, components: str) -> float | tuple[float, ...]\n\n"
               "The smallest distance to the target; 0 inside a face.\n\n"
               "A target in another plane is measured in 3d, or projected into this plane with project.\n\n"
               "Args:\n"
               "    target: a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a "
               "shape or OCC shape; many points as an (n, 2) or (n, 3) array.\n"
               "    project: True projects a target in another plane into this one.\n"
               "    components: letters of 'hvxyz': the components of the shortest connection from here to the target "
               "instead of its length.\n"
               "    signed: True: the distance of points (h, v) to the boundary of the union of the faces (edges between "
               "faces do not count), negative inside, 0 on it.\n\n"
               "Returns:\n"
               "    a float; with components a tuple of them, one letter a float; many points an array (n,), with k "
               "components (n, k), one letter (n,).\n\n"
               "Raises:\n"
               "    ValueError: it is empty; components has other letters; signed with components, with points not "
               "(h, v), or without faces.\n"
               "    TypeError: the target has another form.\n\n"
               "Examples:\n"
               "    " + x + ".Distance((5, 0))\n"
               "    " + x + ".Distance(other, components=\"hv\")");
}

inline const char *ClosestPointDoc(const std::string &x) {
    return Doc(std::string("ClosestPoint(point: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /) -> np.ndarray\n"
               "ClosestPoint(point: Point | Point3, /) -> tuple[float, ...]\n\n"
               "The nearest point to point, in the plane for (h, v), in 3d for (x, y, z)") +
               (x == "face" || x == "shape" ? "; inside a face the point itself" : "") + ".\n\n"
               "Args:\n"
               "    point: (h, v) or (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.\n\n"
               "Raises:\n"
               "    ValueError: it is empty.\n"
               "    TypeError: point is not a point.\n\n"
               "Examples:\n"
               "    " + x + ".ClosestPoint((5, 0))\n"
               "    " + x + ".ClosestPoint([(0, 0), (5, 0)])");
}

inline const char *MeasureDoc(const std::string &x) {
    return Doc("Measure(name: str | FaceView | EdgeView | None = None, to: str | Point | Point3 | Face | Shape | Wire | None = None, project: bool = False) -> dict\n\n"
               "The measures of the faces and edges named name, or of the one with that id or selected, as a dict.\n\n"
               "Its keys: type, faces, area, edges, length, center (in 3d; of an arc or circle its center, with radius and "
               "angle), inertia, Ih, Iv, Ihv, I1, I2, principal_angle, bounding_box; with to also distance, du, dv, dx, "
               "dy, dz, points and the angle between two lines.\n\n"
               "Args:\n"
               "    name: a name, an id, or a selected face or edge; None measures all.\n"
               "    to: a name, or anything Distance takes: adds the distance to it.\n"
               "    project: True projects a target of to in another plane into this one.\n\n"
               "Raises:\n"
               "    ValueError: no face or edge has that name or id.\n"
               "    TypeError: name has another form.\n\n"
               "Examples:\n"
               "    " + x + ".Measure()\n"
               "    " + x + ".Measure(\"iron\", to=\"air\")");
}

template <class T, class C, class F, class N>
inline void BindMeasures(C &cls, F thingOf, N named) {
    const std::string var = VariableOf(NameOf(cls));
    cls.def_property_readonly("center", [thingOf](const T &x) { return CenterOf(thingOf(x)); },
                              "center: tuple[float, float, float] | None\n\nThe center of mass in 3d, (x, y, z): of the area for faces, of the length for wires; None if it is empty.")
        .def_property_readonly("inertia", [thingOf](const T &x) -> py::object {
            Thing t = thingOf(x);
            MassProps p = MassOfThing(t);
            if (p.mass == 0.0)
                return py::none();
            return MatrixTuple(Inertia(p, t.axes));
        }, "inertia: tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]] | None\n\nThe inertia tensor about the center in 3d for unit density (per area for faces, per length for wires), three rows "
           "of three; None if it is empty.")
        .def("Distance", [thingOf](const T &x, const py::object &target, bool project, const std::optional<std::string> &components,
                                   bool sign) {
            if (sign)
                return SignedDistanceOf(x, target, components);
            return DistanceOf(thingOf(x), target, project, components);
        }, py::arg("target"), py::pos_only(), py::arg("project") = false, py::arg("components") = py::none(), py::arg("signed") = false,
           DistanceDoc(var))
        .def("ClosestPoint", [thingOf](const T &x, const py::object &point) { return ClosestPointOf(thingOf(x), point); },
             py::arg("point"), py::pos_only(), ClosestPointDoc(var))
        .def("Measure", [thingOf, named](const T &x, const py::object &name, const py::object &to, bool project) {
            const auto byName = [&](const std::string &n) { return named(x, n); };
            if (name.is_none())
                return MeasureWith(named(x, std::nullopt), to, project, byName);
            if (py::isinstance<py::str>(name)) {
                const std::string n = name.cast<std::string>();
                Thing t = named(x, n);
                if (t.faces.empty() && t.edges.Empty())
                    throw py::value_error("Measure('" + n + "'): no face or edge has that name or id");
                return MeasureWith(t, to, project, byName);
            }
            const std::optional<Thing> t = AsThing(name);
            if (!t)
                throw py::type_error("Measure: name is a name, an id or a selected face or edge, not " + py::repr(name).cast<std::string>());
            return MeasureWith(*t, to, project, byName);
        }, py::arg("name") = py::none(), py::arg("to") = py::none(), py::arg("project") = false,
           MeasureDoc(var));
}


// the edges of a cutter in the plane: a Wire, Edge, Face, Shape or Sketch on it, or anything else projected onto it
inline Wire CutterIn(const AxisSystem &axes, const py::handle &h) {
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
    else if (std::optional<Projection> p = occ->projected(h, axes, false))
        return p->wire;
    else
        throw py::type_error("a cutter is a Wire, Edge, Face, Shape, Sketch or an OCC shape");
    if (w->Axes().IsClose(axes))
        return *w;
    return occ->across(Planar{Wire(w->Axes()), *w}, w->Axes(), axes);
}

struct TrimRule {
    enum Kind { KeepClosest, RemoveClosest, KeepLargest, KeepSmallest, KeepLeft, KeepRight } kind;
    Vertex point;
};

inline TrimRule RuleOf(const AxisSystem &axes, const py::object &keepClosest, const py::object &removeClosest, bool largest,
                       bool smallest, bool left, bool right) {
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

// the pieces a rule keeps: by distance (of the centre for faces, of the piece for wires), by size, or by side
template <class D, class S>
inline std::vector<size_t> Chosen(size_t n, const TrimRule &r, D distance, S size) {
    std::vector<size_t> all(n);
    std::iota(all.begin(), all.end(), 0);
    auto best = [&](auto key) { return *std::min_element(all.begin(), all.end(), [&](size_t a, size_t b) { return key(a) < key(b); }); };
    switch (r.kind) {
        case TrimRule::KeepClosest: return {best(distance)};
        case TrimRule::RemoveClosest: {
            size_t k = best(distance);
            all.erase(all.begin() + (long)k);
            return all;
        }
        case TrimRule::KeepLargest: return {best([&](size_t k) { return -size(k); })};
        case TrimRule::KeepSmallest: return {best(size)};
        default: return {};
    }
}

// the trimmed region of a face, nothing if the cutter does not separate it
inline std::optional<Wire> TrimmedFace(const Face &f, const Wire &cutter, const TrimRule &r) {
    FaceSplit s = SplitFace(f, cutter);
    if (s.pieces.empty())
        return std::nullopt;
    std::vector<MassProps> mass;
    for (const HalfEdges &piece : s.pieces)
        mass.push_back(MassOf(EdgesOf(s.edges, piece), Wire(f.Axes())));
    std::vector<size_t> chosen;
    if (r.kind == TrimRule::KeepLeft || r.kind == TrimRule::KeepRight) {
        for (size_t k = 0; k < s.pieces.size(); ++k)
            if (SideOf(s, s.pieces[k]) == (r.kind == TrimRule::KeepLeft ? 1 : -1))
                chosen.push_back(k);
        if (chosen.empty())
            throw py::value_error(std::string("Trim(...): no piece lies wholly on the ") + (r.kind == TrimRule::KeepLeft ? "left" : "right") +
                                  " of the cutter");
    } else {
        chosen = Chosen(s.pieces.size(), r, [&](size_t k) { return Norm(mass[k].center - r.point); },
                        [&](size_t k) { return mass[k].mass; });
    }
    return UnionOfPieces(s, chosen);
}

inline std::optional<Wire> TrimmedWire(const Wire &w, const Wire &cutter, const TrimRule &r) {
    if (r.kind == TrimRule::KeepLeft || r.kind == TrimRule::KeepRight)
        throw py::value_error("Trim(...): keep_left and keep_right apply to faces");
    WireSplit split = SplitWire(w, cutter);
    if (split.runs.empty())
        return std::nullopt;
    WireList runs;
    for (size_t k = 0; k < split.runs.size(); ++k)
        runs.push_back(split.Run(k));
    Wire out(w.Axes());
    for (size_t k : Chosen(runs.size(), r, [&](size_t k) { return NearestTo(runs[k], r.point).d; },
                           [&](size_t k) { return runs[k].Length(); }))
        for (const Edge &e : runs[k])
            out.Add(e);
    return out;
}

// every face and wire of the shape cut one by one: one shape per piece, the untouched parts as they are
inline ShapeList SplitShape(const Shape &s, const Wire &cutter) {
    ShapeList out;
    for (const Face &f : s.Faces()) {
        FaceSplit split = SplitFace(f, cutter);
        if (split.pieces.empty())
            out.emplace_back(f);
        for (const HalfEdges &piece : split.pieces)
            out.emplace_back(Face(EdgesOf(split.edges, piece), f.Props()));
    }
    for (const Wire &w : s.WireParts()) {
        WireSplit split = SplitWire(w, cutter);
        if (split.runs.empty())
            out.push_back(Shape(s.Axes(), {}, {w}));
        for (size_t k = 0; k < split.runs.size(); ++k)
            out.push_back(Shape(s.Axes(), {}, {split.Run(k)}));
    }
    for (Shape &piece : out) {
        piece.Edges().AssignIds(true);
        piece.Edges().ClearVertexIds();
        piece.Ids() = s.Ids();
    }
    return out;
}

inline Shape TrimShape(const Shape &s, const Wire &cutter, const TrimRule &r) {
    FaceList faces;
    WireList wires;
    for (const Face &f : s.Faces()) {
        std::optional<Wire> t = TrimmedFace(f, cutter, r);
        faces.push_back(t ? Face(std::move(*t), f.Props()) : f);
    }
    // the sides are those of faces: a shape's free wires stay as they are
    const bool side = r.kind == TrimRule::KeepLeft || r.kind == TrimRule::KeepRight;
    if (side && !s.HasFaces())
        throw py::value_error("Trim(...): keep_left and keep_right apply to faces");
    for (const Wire &w : s.WireParts()) {
        std::optional<Wire> t = side ? std::nullopt : TrimmedWire(w, cutter, r);
        wires.push_back(t ? std::move(*t) : w);
    }
    Shape trimmed(s.Axes(), faces, wires);
    trimmed.Ids() = s.Ids();
    trimmed.Edges().AssignIds(true);
    trimmed.Edges().ClearVertexIds();
    return trimmed;
}

#define TRIM_PARAMS const py::object &cutter, const py::object &kc, const py::object &rc, bool kl, bool ks, bool kleft, bool kright
#define TRIM_ARGS                                                                                                      \
    py::arg("cutter"), py::kw_only(), py::arg("keep_closest_to") = py::none(), py::arg("remove_closest_to") = py::none(), \
        py::arg("keep_largest") = false, py::arg("keep_smallest") = false, py::arg("keep_left") = false,             \
        py::arg("keep_right") = false
inline const std::string cutterType = "Wire | EdgeView | Face | Shape | Sketch | QuickSketch | TopoDS_Shape";
inline const std::string cutterArg = "    cutter: the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into "
                                     "the plane if it lies elsewhere.\n";

inline const char *TrimDoc(const std::string &x) {
    return Doc("Trim(cutter: " + cutterType + ", *, keep_closest_to: Point | Point3 | None = None, remove_closest_to: Point | "
               "Point3 | None = None, keep_largest: bool = False, keep_smallest: bool = False, keep_left: bool = False, "
               "keep_right: bool = False) -> Shape\n\n"
               "Cuts every face and wire by the cutter and keeps the pieces the one option given chooses.\n\n"
               "What the cutter does not separate stays as it is.\n\n"
               "Args:\n" + cutterArg +
               "    keep_closest_to: keeps the piece closest to this point (a face piece by its center, a wire piece by "
               "itself), (h, v) or (x, y, z).\n"
               "    remove_closest_to: removes the piece closest to this point and keeps the others.\n"
               "    keep_largest: keeps the piece of the largest area (faces) or length (wires).\n"
               "    keep_smallest: keeps the piece of the smallest area or length.\n"
               "    keep_left: keeps the face pieces left of the cutter as drawn; a shape's free wires stay.\n"
               "    keep_right: keeps the face pieces right of the cutter as drawn; a shape's free wires stay.\n\n"
               "Returns:\n"
               "    a new Shape; this one is not changed.\n\n"
               "Raises:\n"
               "    ValueError: not exactly one option is given; keep_left or keep_right without faces, or no piece "
               "lies wholly on that side.\n"
               "    TypeError: the cutter or the point has the wrong form.\n\n"
               "Examples:\n"
               "    " + x + ".Trim(px.Line((0, -5), (0, 5)), keep_left=True)\n"
               "    " + x + ".Trim(cutter, keep_closest_to=(2, 1))");
}

inline const char *SplitDoc(const std::string &x) {
    return Doc("Split(cutter: " + cutterType + ", /) -> list[Shape]\n\n"
               "Cuts every face and wire by the cutter into one Shape per piece.\n\n"
               "What the cutter does not separate stays as it is; the pieces keep the face's properties, the new edges "
               "the cutter's.\n\n"
               "Args:\n" + cutterArg + "\n"
               "Raises:\n"
               "    TypeError: the cutter has the wrong form.\n\n"
               "Examples:\n"
               "    pieces = " + x + ".Split(px.Line((0, -5), (0, 5)))");
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

// a new reference that is never released: the category outlives every warning
inline PyObject *planexWarning = nullptr;
// px.config.default_sketch_type, what px.Move makes: px.QuickSketch, else px.Sketch
inline bool quickSketchDefault = true;

// called from any thread, also from taskflow workers while the caller released the GIL; an "error" filter turns the
// warning into the Python exception, unless a C++ exception is already unwinding
// the stack level of the first frame outside the planex package: the warning names the user's line, not a
// wrapper of the package (1 where no Python frame calls, as in a worker thread)
inline int CallerStackLevel() {
    try {
        static const std::string package = [] {
            const std::string f = py::module_::import("planex").attr("__file__").cast<std::string>();
            return f.substr(0, f.find_last_of("/\\") + 1);
        }();
        int level = 1;
        for (py::object frame = py::module_::import("sys").attr("_getframe")(0); !frame.is_none(); frame = frame.attr("f_back")) {
            if (frame.attr("f_code").attr("co_filename").cast<std::string>().rfind(package, 0) != 0)
                break;
            ++level;
        }
        return level;
    } catch (const py::error_already_set &) {
        return 1;
    }
}

inline void WarnPython(const std::string &text) {
    py::gil_scoped_acquire gil;
    if (PyErr_WarnEx(planexWarning, text.c_str(), CallerStackLevel()) == 0)
        return;
    if (std::uncaught_exceptions() == 0)
        throw py::error_already_set();
    PyErr_Clear();
}

// a class bound in another file or module, to add methods to
template <class T>
inline py::class_<T> CoreClass(const py::module_ &core, const char *name) {
    return py::reinterpret_borrow<py::class_<T>>(core.attr(name));
}

// a method on a class that is bound in another file
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
