// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "binding.hpp"
#include "constraint_sketch/expression.hpp"
#include "curves/parametricfit.hpp"
#include "sketch/curveformula.hpp"

// the nodes _expressions._Parse gives: (operation, number), ("val", k) the k-th value
inline std::vector<cs::Node> NodesOf(const std::vector<std::pair<std::string, double>> &nodes) {
    constexpr size_t last = (size_t)cs::lastOp;
    std::vector<cs::Node> out;
    for (const auto &[name, number] : nodes) {
        size_t k = 0;
        while (k <= last && name != cs::OpName(static_cast<cs::Op>(k)))
            ++k;
        if (k > last)
            throw py::value_error("expression: no operation '" + name + "'");
        const cs::Op op = static_cast<cs::Op>(k);
        out.push_back({op, op == cs::Op::Value ? 1.0 : number, op == cs::Op::Value ? (size_t)number : 0});
    }
    cs::CheckNodes(out);
    return out;
}

// Curve(point, range) as the messages name it
inline std::string CurveText(const std::string &call, const py::handle &point, const py::handle &range) {
    return call + "(" + py::repr(point).cast<std::string>() + ", " + py::repr(range).cast<std::string>() + ")";
}
// the point of Curve given as a callable t -> (h, v), not as two expressions
inline bool IsCurveCallable(const py::handle &point) {
    return !py::isinstance<py::str>(point) && !py::isinstance<py::tuple>(point) && !py::isinstance<py::list>(point) && py::hasattr(point, "__call__");
}
// the range (t0, t1): two numbers, or (strings: a formula's) expressions
inline py::tuple CurveRange(const py::handle &range, const std::string &text, bool numbers) {
    if (!(py::isinstance<py::tuple>(range) || py::isinstance<py::list>(range)) || py::len(range) != 2)
        throw py::type_error(text + ": the range is (t0, t1), two numbers" + (numbers ? std::string() : " or expressions"));
    const py::tuple out = py::tuple(py::reinterpret_borrow<py::sequence>(range));
    for (const py::handle &end : out)
        if (!IsNumber(end) && (numbers || !py::isinstance<py::str>(end)))
            throw py::type_error(text + ": the range's " + py::repr(end).cast<std::string>() + " is no number" +
                                 (numbers ? " (a callable's range is numbers)" : " or expression"));
    return out;
}

// a curve's formula, its point (h, v) two expressions in t, read by _expressions._ParseCurve with names (those of
// the sketch it is drawn in; numbers_only for a sketch or a call without dimensions)
inline sketch::CurveFormula ParsedCurve(const py::handle &point, const py::handle &range, const std::string &text, const py::dict &names) {
    if (!(py::isinstance<py::tuple>(point) || py::isinstance<py::list>(point)) || py::len(point) != 2 || !py::isinstance<py::str>(point[py::int_(0)]) ||
        !py::isinstance<py::str>(point[py::int_(1)]))
        throw py::type_error(text + ": the point is (h, v) as two expressions in t, (\"t\", \"0.1*t^2\"), or a callable t -> (h, v)");
    const py::object parsed = py::module_::import("planex._expressions").attr("_ParseCurve")(text, point, CurveRange(range, text, false), **names);
    using Nodes = std::vector<std::pair<std::string, double>>;
    sketch::CurveFormula f;
    f.text = text;
    f.h = NodesOf(parsed["h"].cast<Nodes>());
    f.v = NodesOf(parsed["v"].cast<Nodes>());
    f.from = NodesOf(parsed["from"].cast<Nodes>());
    f.to = NodesOf(parsed["to"].cast<Nodes>());
    f.ids = parsed["dimensions"].cast<std::vector<std::string>>();
    return f;
}

// the shape of the curve the callable point gives over the range (numbers): SampledFit, the callable called once per t;
// a reversed range runs from t0 to t1 as a formula's does, sampled along s = -t
inline Curve<double, Vertex> CallableShape(const py::object &point, const py::handle &range, const std::string &text) {
    const py::tuple t = CurveRange(range, text, true);
    const double t0 = t[0].cast<double>(), t1 = t[1].cast<double>(), way = t1 < t0 ? -1.0 : 1.0;
    return SampledFit([&](double s) {
        const double at = way * s;
        const py::object p = point(at);
        try {
            const std::array<double, 2> a = p.cast<std::array<double, 2>>();
            IsFinite(text.c_str(), {a[0], a[1]});
            return Vertex{a[0], a[1]};
        } catch (const py::cast_error &) {
            throw py::type_error(text + ": the callable gives " + py::repr(p).cast<std::string>() + " at t = " + NumberText(at) +
                                 ", no point (h, v): a tuple, list or array of two numbers");
        }
    }, way * t0, way * t1, text);
}
