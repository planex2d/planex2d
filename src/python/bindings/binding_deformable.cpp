// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

void BindDeformable(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    auto ref = py::return_value_policy::reference_internal;
    py::class_<DeformableShape>(m, "DeformableShape",
                                "A face as dense closed polylines whose vertices follow a deformation field; outer loops run "
                                "counterclockwise, holes clockwise.")
        .def(py::init([](const py::object &shp, double h, std::optional<double> minDistance, bool clean) {
            const Wire edges = py::isinstance<Face>(shp) ? shp.cast<const Face &>().Edges() : shp.cast<const Shape &>().FaceEdges();
            return DeformableShape(edges, h, minDistance.value_or(h), clean);
        }), py::arg("shape"), py::pos_only(), py::arg("h"), py::arg("min_distance") = py::none(), py::arg("clean") = false,
           "DeformableShape(shape: Face | Shape, /, h: float, min_distance: float | None = None, clean: bool = False)\n\n"
           "The faces of the shape as polylines with every edge sampled at most h apart.\n\n"
           "Boundary parts that move closer than min_distance to each other are cut out.\n\n"
           "Args:\n"
           "    shape: a Face, or a Shape whose faces are taken.\n"
           "    h: the largest segment length.\n"
           "    min_distance: the smallest distance between boundary parts; None: h.\n"
           "    clean: True cleans the given shape of parts closer than min_distance right away.\n\n"
           "Examples:\n"
           "    px.DeformableShape(face, 0.1)\n"
           "    px.DeformableShape(shape, 0.1, min_distance=0.05, clean=True)")
        .def_property_readonly("h", &DeformableShape::H, "h: float\n\nThe largest segment length.")
        .def_property_readonly("min_distance", &DeformableShape::MinDistance, "min_distance: float\n\nThe smallest distance between boundary parts.")
        .def_property_readonly("mass", &DeformableShape::Area, "mass: float\n\nThe area.")
        .def_property_readonly("bounding_box", [](const DeformableShape &s) { return BoxTuple(s.Box()); },
                               "bounding_box: tuple[tuple[float, float], tuple[float, float]]\n\nIts bounding box in the plane, ((hmin, vmin), (hmax, vmax)).")
        .def("__len__", &DeformableShape::Size, "The number of vertices.")
        .def("RingSizes", &DeformableShape::RingSizes,
             "RingSizes() -> list[int]\n\n"
             "The number of vertices of every loop, in the order of Vertices().\n\n"
             "Examples:\n"
             "    shape.RingSizes()")
        .def("VertexNames", &DeformableShape::VertexNames,
             "VertexNames() -> list[str]\n\n"
             "The name of the original edge every vertex lies on, in the order of Vertices().\n\n"
             "Examples:\n"
             "    shape.VertexNames()")
        .def("Vertices", [](const DeformableShape &s) { return VertexArray(s.Vertices()); },
             "Vertices() -> np.ndarray\n\n"
             "The current vertices, an (n, 2) array.\n\n"
             "Examples:\n"
             "    shape.Vertices()")
        .def("ReferenceVertices", [](const DeformableShape &s) { return VertexArray(s.ReferenceVertices()); },
             "ReferenceVertices() -> np.ndarray\n\n"
             "The vertices the displacements refer to, an (n, 2) array.\n\n"
             "Examples:\n"
             "    shape.ReferenceVertices()")
        .def("SetDeformation", [](DeformableShape &s, py::array_t<double, py::array::c_style | py::array::forcecast> d) {
            if (d.ndim() != 2 || d.shape(1) != 2)
                throw std::invalid_argument("displacements of shape (n, 2) expected");
            auto a = d.unchecked<2>();
            std::vector<Vertex> v(static_cast<size_t>(d.shape(0)));
            for (py::ssize_t i = 0; i < d.shape(0); ++i)
                v[static_cast<size_t>(i)] = {a(i, 0), a(i, 1)};
            return s.SetDeformation(v);
        }, py::arg("displacements"),
           "SetDeformation(displacements: np.ndarray) -> bool\n\n"
           "Sets the current vertices to the reference vertices plus the displacements, then refines and cleans.\n\n"
           "Args:\n"
           "    displacements: an (n, 2) array, one row per vertex in the order of Vertices().\n\n"
           "Returns:\n"
           "    True if the topology changed.\n\n"
           "Raises:\n"
           "    ValueError: the array is not (n, 2) with one row per vertex.\n\n"
           "Examples:\n"
           "    shape.SetDeformation(displacements)")
        .def("Refine", [](DeformableShape &s) -> DeformableShape & { s.Refine(); return s; }, ref,
             "Refine() -> DeformableShape\n\n"
             "Halves the segments longer than h and drops interior vertices closer than h/4.\n\n"
             "Returns:\n"
             "    this DeformableShape.\n\n"
             "Examples:\n"
             "    shape.Refine()")
        .def("Commit", [](DeformableShape &s) -> DeformableShape & { s.Commit(); return s; }, ref,
             "Commit() -> DeformableShape\n\n"
             "Makes the current vertices the reference.\n\n"
             "Returns:\n"
             "    this DeformableShape.\n\n"
             "Examples:\n"
             "    shape.Commit()")
        .def("Clean", &DeformableShape::Clean,
             "Clean() -> bool\n\n"
             "Cuts the loops against each other, keeps the boundary of the material (winding number >= 1) and removes "
             "parts closer than min_distance to each other.\n\n"
             "Returns:\n"
             "    True if anything changed; then the current vertices are the reference.\n\n"
             "Examples:\n"
             "    shape.Clean()")
        .def("Defeature", &DeformableShape::Defeature,
             "Defeature() -> bool\n\n"
             "Removes the boundary vertices closer than min_distance to another part of the boundary and joins the loose "
             "ends by straight segments, nearest first.\n\n"
             "Returns:\n"
             "    True if anything changed.\n\n"
             "Examples:\n"
             "    shape.Defeature()")
        .def("IsValid", &DeformableShape::IsValid, "IsValid() -> bool\n\n"
             "True if the polylines do not intersect themselves or each other.\n\n"
             "Examples:\n"
             "    shape.IsValid()")
        .def("Wires", [](const DeformableShape &s) { return s.Lines().OrderedChains(); },
             "Wires() -> list[Wire]\n\n"
             "The current polylines as wires of lines with the original edges' properties.\n\n"
             "Examples:\n"
             "    shape.Wires()")
        .def("Shape", [](const DeformableShape &s) { return Shape(s.Edges(), nullptr); },
             "Shape() -> Shape\n\n"
             "The current outline as a Shape, one edge per original edge: a line where its points are collinear, else "
             "the spline through them.\n\n"
             "Examples:\n"
             "    shape.Shape()");
    AddCopy<DeformableShape>(m, "DeformableShape");
}
