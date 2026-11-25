// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "bindingsketchdraw.hpp"
#include "bindingargs.hpp"
#include "bindingconstruction.hpp"
#include "bindingsketchargs.hpp"

std::string HelpedCall(const std::string &call, const std::vector<py::object> &ends, const std::vector<double> &numbers, const py::dict &helpers) {
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

// Arc(r, tangent=(a, b)) is Arc(a, b, r, tangent=(a, b)): the targets as the ends
std::vector<py::object> FilletEnds(const std::vector<py::object> &ends, const std::vector<double> &sizes, const py::dict &helpers) {
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
