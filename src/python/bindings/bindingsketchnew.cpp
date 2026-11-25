// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "bindingsketchnew.hpp"
#include "bindingviews.hpp"
#include "bindingargs.hpp"

py::object SketchToCopy(const std::string &call, const py::tuple &args, const SketchSetup &setup) {
    const auto &[autoConstraint, errorOnBranchChange, mode, id, dir] = setup;
    if (args.size() != 1)
        throw py::type_error(call + "(sketch, copy=True): got " + std::to_string(args.size()) + " arguments; copy=True takes the one sketch or geometry to copy, " +
                             call + "(sketch, copy=True)");
    const bool sketch = call == "Sketch";
    if (!dir.is_none() || (AsksAutoConstraint(autoConstraint) && !sketch) || errorOnBranchChange || mode != "normal")
        throw py::type_error(call + (sketch ? "(sketch, copy=True): only id= and auto_constraint= go with copy=True; dir=, "
                                              "error_on_branch_change= and mode= belong to a new sketch, leave them out"
                                            : "(sketch, copy=True): only id= goes with copy=True; dir=, auto_constraint=, "
                                              "error_on_branch_change= and mode= belong to a new sketch, leave them out"));
    return py::reinterpret_borrow<py::object>(args[0]);
}

void AddCopiedGeometry(const py::handle &h, const AxisSystem &axes, std::vector<CopiedGeometry> &out) {
    if (IsA<Wire>(h))
        out.push_back({{h.cast<const Wire &>(), {}}, false});
    else if (std::optional<Face> copy; IsA<Face>(h))
        out.push_back({{AsFace(h, copy).Edges(), {}}, false});
    else if (IsA<Shape>(h))
        out.push_back({{h.cast<const Shape &>().Edges(), {}}, false});
    else if (std::optional<Projection> p = occHooks->projected(h, axes, true))
        out.push_back({std::move(*p), true});
    else if (py::hasattr(h, "parts"))
        for (const py::handle &part : h.attr("parts"))
            AddCopiedGeometry(part, axes, out);
    else if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h))
        for (const py::handle &item : h)
            AddCopiedGeometry(item, axes, out);
}

Wire OneGeometry(const py::object &geometry, const std::string &call) {
    if (IsA<Wire>(geometry))
        return geometry.cast<const Wire &>();
    if (std::optional<Face> copy; IsA<Face>(geometry))
        return AsFace(geometry, copy).Edges();
    if (IsA<Shape>(geometry))
        return geometry.cast<const Shape &>().Edges();
    if (py::isinstance<py::list>(geometry) || py::isinstance<py::tuple>(geometry) || py::hasattr(geometry, "parts"))
        throw py::type_error(call + ": one Wire, Face or Shape; the edges of a list or compound may overlap, glue them into one "
                                    "shape first: sk." + call + "(px.Glue(parts))");
    if (occHooks->isShape(geometry))
        throw py::type_error(call + ": an OCC shape is projected in: sk.Project(shape, construction=False)");
    throw py::type_error(call + ": a Wire, Face or Shape, not " + py::str(py::type::of(geometry).attr("__name__")).cast<std::string>());
}
