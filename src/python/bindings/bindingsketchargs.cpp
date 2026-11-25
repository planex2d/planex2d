// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "bindingsketchargs.hpp"
#include "bindingargs.hpp"
#include <iterator>

CallArgs Split(const py::tuple &args, const std::string &call) {
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

Vertex PointArg(const py::handle &h, const std::string &call) {
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

std::optional<Vertex> StartArg(const py::handle &h, const std::string &call) {
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
    if (occHooks->point(h) || (sequence && py::len(h) == 3))
        return PointArg(AxisSystem(), h, call);
    if (sequence || IsA<Vertex>(h))
        return PointArg(h, call);
    return std::nullopt;
}

std::vector<std::string> CornerIds(const py::object &id, const std::string &call) {
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

std::vector<Properties> CornerProps(const py::kwargs &kw, const std::string &call) {
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
        out.push_back(PropsOf(one));
    }
    return out;
}
