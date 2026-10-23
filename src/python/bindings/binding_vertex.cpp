// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

// a point of a sketch or shape as Vertex() takes it: an object with _hv() (a selected point, Line.1.end), nothing else,
// so the other overloads convert numbers, tuples and arrays as they did
struct PlanePoint {
    py::object point;
};
namespace pybind11::detail {
    template <>
    struct type_caster<PlanePoint> {
        PYBIND11_TYPE_CASTER(PlanePoint, const_name("point"));
        bool load(handle src, bool) {
            if (!hasattr(src, "_hv"))
                return false;
            value.point = reinterpret_borrow<object>(src);
            return true;
        }
        static handle cast(const PlanePoint &p, return_value_policy, handle) { return p.point.inc_ref(); }
    };
}

void BindVertex(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    auto vertex = py::class_<Vertex>(m, "Vertex",
                                     "A point (h, v) of the plane, the construction vertex; it acts as its coordinates: h, v = p, "
                                     "np.array(p), p == (h, v).");
    BindCopy<Vertex>(vertex);
    vertex
        .def(py::init([](std::array<double, 2> a) {
            IsFinite("point", {a[0], a[1]});
            return Vertex{a[0], a[1]};
        }), py::arg("point"), py::pos_only(),
             "Vertex(point: Point, /)\n\n"
             "A point of the plane.\n\n"
             "Args:\n"
             "    point: (h, v), or a point of a sketch or shape (a selected point, Line.1.end): where it is in its plane.\n\n"
             "Raises:\n"
             "    ValueError: a coordinate is not finite.\n\n"
             "Examples:\n"
             "    p = px.Vertex((1, 2))\n"
             "    q = px.Vertex(sk(\"Line.1.end\")[0])")
        .def(py::init([](const PlanePoint &point) {
            const auto hv = point.point.attr("_hv")().cast<std::array<double, 2>>();
            IsFinite("point", {hv[0], hv[1]});
            return Vertex{hv[0], hv[1]};
        }), py::arg("point"), py::pos_only(), "")
        .def_readwrite("x", &Vertex::x, "x: float\n\nIts first coordinate in the plane (h), writable.")
        .def_readwrite("y", &Vertex::y, "y: float\n\nIts second coordinate in the plane (v), writable.")
        .def("__repr__", [](const Vertex &v) {
            const auto f = [](double x) { return py::repr(py::float_(x)).cast<std::string>(); };
            return "Vertex((" + f(v.x) + ", " + f(v.y) + "))";
        });
    BindCoordinates(vertex, [](const Vertex &v) { return v; });
    py::implicitly_convertible<py::sequence, Vertex>();
}
