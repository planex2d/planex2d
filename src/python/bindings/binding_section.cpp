// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding_occ.hpp"
#include "integrations/netgenocc/section.hpp"

void BindSection(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    m.def("Intersect3d", [](const py::object &plane, const py::object &shape) {
        return ToCore("Shape", Intersect3d(AxesArg(plane, "Intersect3d"), ShapeArg(shape, "Intersect3d")));
    },
          py::arg("plane"), py::arg("shape"),
          "Intersect3d(plane: Plane | None, shape: TopoDS_Shape | list[TopoDS_Shape]) -> Shape\n\n"
          "Where the plane meets the OCC shape, as a Shape in the plane's coordinates.\n\n"
          "The cross-sections of its solids become faces with the solids' names and properties (several solids glued as "
          "by px.Glue, an overlap a face of its own; a face of a solid lying in the plane belongs to its cross-section), "
          "its other faces become faces where they lie in the plane, else the wires where they meet it, and its edges on "
          "no face (3d wires) the stretches lying in the plane; every edge is named after the face or edge of the shape "
          "it lies on. The points where such edges cross or touch the plane are left out with a warning; a sketch's "
          "Intersect3d keeps them. A plane that misses the shape gives an empty Shape and a warning.\n\n"
          "Args:\n"
          "    plane: the cutting plane: a Plane, netgen Axes, a sketch, a planar face or shape, a compound on one plane; "
          "None the xy plane.\n"
          "    shape: an OCC shape, or a list of them cut as one compound.\n\n"
          "Raises:\n"
          "    TypeError: plane has another form.\n\n"
          "Examples:\n"
          "    px._planex.Intersect3d(px.XY, occ_shape)\n"
          "    px._planex.Intersect3d(px.Plane(origin=(0, 0, 5)), occ_shape)");
    m.def("_FromOCC", [](const TopoDS_Shape &shape) { return ToCore("Shape", FromOCC(shape)); }, py::arg("shape"), py::pos_only(),
          "px.FromOCC of an OCC shape");
}
