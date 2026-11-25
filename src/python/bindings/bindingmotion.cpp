// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

void AddCorners(const Wire &w, const py::handle &at, const std::string &call, std::vector<Vertex> &out) {
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
