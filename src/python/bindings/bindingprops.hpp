// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "bindingbase.hpp"

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

Properties Props(const Name &name, const Size &maxh, const Col &col, const Size &minh, const Size &hpref, const Layer &layer, const Partition &partition);

inline ShapeProperties PropsOrDefault(const Properties &p) { return p ? *p : ShapeProperties{}; }
inline ShapeProperties PropsOrDefault(const Edge &e) { return PropsOrDefault(PropsOf(e)); }

template <class F>
inline void UpdateProps(Edge &e, F f) {
    SetProps(e, ChangedProperties(PropsOf(e), f));
}

template <class F>
struct SetterValue;
template <class C, class R, class P, class V>
struct SetterValue<R (C::*)(P, V) const> {
    using type = std::decay_t<V>;
};
template <class F>
using SetterValueOf = typename SetterValue<decltype(&F::operator())>::type;

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

template <class F>
inline void VertexFields(F bind) {
    bind("name", [](const ShapeProperties &p) { return p.name; }, [](ShapeProperties &p, const Name &v) { p.name = CheckedName(v); });
    bind("maxh", [](const ShapeProperties &p) { return p.maxh; }, [](ShapeProperties &p, const Size &v) { p.maxh = SizeOf(v, "maxh"); });
    bind("hpref", [](const ShapeProperties &p) { return p.hpref; }, [](ShapeProperties &p, const Size &v) { p.hpref = SizeOf(v, "hpref", true); });
}

#define DRAW_PARAMS const Name &n, const Size &mh, const Col &c, const Size &mi, const Size &hp, const Layer &la, const Partition &pa, bool construction
#define PROPS Props(n, mh, c, mi, hp, la, pa)
#define DRAW_ARGS                                                                                                       \
    py::arg("name") = py::none(), py::arg("maxh") = py::none(), py::arg("col") = py::none(), py::arg("minh") = py::none(), \
        py::arg("hpref") = py::none(), py::arg("layer") = py::none(), py::arg("partition") = py::none(),                    \
        py::arg("construction") = false
