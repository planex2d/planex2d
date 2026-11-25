// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bindingbase.hpp"
#include "bindingdocs.hpp"
#include "bindingprops.hpp"

inline Wire SingleEdge(Edge e, const AxisSystem &axes) {
    Wire w(axes);
    w.Add(std::move(e));
    return w;
}

inline void CheckFresh(bool fresh, const char *view, const char *from) {
    if (!fresh)
        throw std::runtime_error(std::string("this ") + view + " view is stale: the edges changed since it was taken, take it again from ." + from);
}

// a view on edge i of a wire; it holds while the wire's edge list is unchanged (moving the edges keeps it)
struct EdgeView {
    Wire *wire;
    size_t i;
    uint64_t changes;

    EdgeView(Wire *w, size_t i) : wire(w), i(i), changes(w->Changes()) {}
    Edge &Get() const {
        CheckFresh(wire->Changes() == changes && i < wire->Size(), "edge", "edges");
        return (*wire)[i];
    }
    Wire AsWire() const { return SingleEdge(Get(), wire->Axes()); }
};

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
        CheckFresh(wire->Changes() == changes, "edges", "edges");
        return *subset;
    }
};

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
template <class G>
inline auto Common(const Wire &w, const std::vector<size_t> &index, G get) {
    return CommonOf(index.size(), [&](size_t k) { return PropsOf(w[index[k]]); }, get);
}
template <class G>
inline auto Common(const Wire &w, G get) {
    return CommonOf(w.Size(), [&](size_t k) { return PropsOf(w[k]); }, get);
}

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

struct VertexView {
    Wire *wire;
    size_t end;
    uint64_t changes;

    VertexView(Wire *w, size_t end) : wire(w), end(end), changes(w->Changes()) {}
    std::vector<size_t> Endpoints() const {
        CheckFresh(wire->Changes() == changes, "vertex", "vertices");
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

struct VerticesView {
    Wire *wire;
    std::optional<std::vector<size_t>> subset = std::nullopt;
    uint64_t changes = 0;

    std::vector<std::vector<size_t>> Vertices() const {
        std::vector<std::vector<size_t>> all = wire->VertexEndpoints();
        if (!subset)
            return all;
        CheckFresh(wire->Changes() == changes, "vertices", "vertices");
        std::vector<bool> in(wire->Size(), false);
        for (size_t i : *subset)
            in[i] = true;
        std::erase_if(all, [&](const std::vector<size_t> &at) {
            return std::none_of(at.begin(), at.end(), [&](size_t k) { return in[k / 2]; });
        });
        return all;
    }
};

struct FaceIndex {
    uint64_t changes, groupChanges;
    std::vector<std::pair<uint32_t, std::vector<size_t>>> faces;

    explicit FaceIndex(const Shape &s) : changes(s.Edges().Changes()), groupChanges(s.GroupChanges()), faces(s.FaceIndices()) {}
};

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

// the sizes of what the core and the OCC bindings both read: equal when both are built from the same sources (with the
// same version, a development build of other sources still differs here)
inline py::tuple Layout() {
    return py::make_tuple(sizeof(config::State), sizeof(OCCHooks), sizeof(CoreServices), sizeof(Vertex), sizeof(AxisSystem),
                          sizeof(Wire), sizeof(Face), sizeof(Shape), sizeof(FaceView), sizeof(EdgeView), sizeof(VertexView));
}
