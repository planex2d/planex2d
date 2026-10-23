// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

// the shortest text of each coordinate that reads back as it (-0 as 0)
static std::string Text3(const Vector3 &v) {
    const auto number = [](double x) { return NumberText(x == 0.0 ? 0.0 : x); };
    return "(" + number(v.x) + ", " + number(v.y) + ", " + number(v.z) + ")";
}

void BindPlane(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    py::class_<AxisSystem>(m, "Plane", "A plane in 3d for construction: an origin, the h and v directions and the normal, "
                                       "always right-handed (v = normal x h); its operations give new planes.")
        .def(py::init([](const py::object &axes, const py::object &origin, const py::object &axis) {
            return SketchAxes(AxesArg(axes, "Plane"), XYZArg(origin, "Plane(origin)"), XYZArg(axis, "Plane(axis)"), "Plane");
        }), py::arg("plane"), py::arg("origin") = py::none(), py::arg("axis") = py::none(),
             "Plane(plane: Plane | None, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(*, origin: Point3 = (0, 0, 0), normal: Point3 = (0, 0, 1), axis: Point3 | None = None)\n\n"
             "The plane of something that has one, or the plane through origin with that normal.\n\n"
             "With plane, origin and axis replace its own, projected onto it. Without plane, h runs along axis projected "
             "onto the plane, else along the global x axis (y if the normal is x). Netgen Axes convert to a Plane "
             "wherever planex takes a plane.\n\n"
             "Args:\n"
             "    plane: anything with a plane: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC "
             "shape; None the xy plane.\n"
             "    origin: the origin in 3d, (x, y, z), a netgen Pnt.\n"
             "    normal: the normal in 3d, (dx, dy, dz), a netgen Vec or Dir.\n"
             "    axis: the direction of h in 3d (not the normal), projected onto the plane.\n\n"
             "Raises:\n"
             "    ValueError: the normal is zero; the axis is perpendicular to the plane.\n"
             "    TypeError: plane, origin, normal or axis has another form.\n\n"
             "Examples:\n"
             "    px.Plane(px.XY, origin=(0, 0, 2), axis=(1, 1, 0))\n"
             "    px.Plane(origin=(0, 0, 2), normal=(0, 0, 1), axis=(1, 1, 0))")
        .def(py::init([](const py::object &origin, const py::object &normal, const py::object &axis) {
            const Vector3 n = *XYZArg(normal, "Plane(normal)");
            if (!(Norm(n) > 0.0))
                throw py::value_error("Plane: the normal must not be zero");
            return SketchAxes(AxesAlong(*XYZArg(origin, "Plane(origin)"), n), std::nullopt, XYZArg(axis, "Plane(axis)"), "Plane");
        }), py::kw_only(), py::arg("origin") = py::make_tuple(0.0, 0.0, 0.0), py::arg("normal") = py::make_tuple(0.0, 0.0, 1.0),
             py::arg("axis") = py::none(), "")
        .def_property_readonly("origin", [](const AxisSystem &a) { return Tuple3(a.Origin()); }, "origin: tuple[float, float, float]\n\nThe origin in 3d, (x, y, z).")
        .def_property_readonly("normal", [](const AxisSystem &a) { return Tuple3(a.Normal()); }, "normal: tuple[float, float, float]\n\nThe unit normal in 3d.")
        .def_property_readonly("h", [](const AxisSystem &a) { return Tuple3(a.XDirection()); }, "h: tuple[float, float, float]\n\nThe unit direction of h in 3d.")
        .def_property_readonly("v", [](const AxisSystem &a) { return Tuple3(a.YDirection()); }, "v: tuple[float, float, float]\n\nThe unit direction of v in 3d.")
        .def("ToXYZ", [](const AxisSystem &a, const Vertex &p) { return Tuple3(a.ToXYZ(p)); }, py::arg("point"), py::pos_only(),
             "ToXYZ(point: Point, /) -> tuple[float, float, float]\n\n"
             "The 3d coordinates of the point (h, v) of the plane.\n\n"
             "Args:\n"
             "    point: (h, v) in the plane.\n\n"
             "Examples:\n"
             "    plane.ToXYZ((1, 2))")
        .def("ToHV", [](const AxisSystem &a, const std::array<double, 3> &p) { return Tuple2(a.ToHV({p[0], p[1], p[2]})); },
             py::arg("point"), py::pos_only(),
             "ToHV(point: Point3, /) -> tuple[float, float]\n"
             "ToHV(points: list[Point3], /) -> list[tuple[float, float]]\n\n"
             "The plane coordinates (h, v) of the projection of a 3d point onto the plane, or of a list of points.\n\n"
             "Args:\n"
             "    point: (x, y, z).\n"
             "    points: a list of (x, y, z).\n\n"
             "Examples:\n"
             "    plane.ToHV((1, 2, 0))\n"
             "    plane.ToHV([(0, 0, 0), (1, 2, 0)])")
        .def("ToHV", [](const AxisSystem &a, const std::vector<std::array<double, 3>> &xyz) {
            py::list out;
            for (const auto &p : xyz)
                out.append(Tuple2(a.ToHV({p[0], p[1], p[2]})));
            return out;
        }, py::arg("points"), py::pos_only(), "")
        .def("Offset", [](AxisSystem a, double d) { return a.Offset(d); }, py::arg("d"),
             "Offset(d: float) -> Plane\n\n"
             "The parallel plane at distance d along the normal.\n\n"
             "Args:\n"
             "    d: the distance along the normal, negative against it.\n\n"
             "Examples:\n"
             "    plane.Offset(5)\n"
             "    plane.Offset(-0.5)")
        .def("Translated", [](AxisSystem a, std::array<double, 3> d) { return a.Translate(Vec3(d)); }, py::arg("d"),
             "Translated(d: tuple[float, float, float]) -> Plane\n"
             "Translated(dx: float, dy: float, dz: float) -> Plane\n\n"
             "A copy moved by d in 3d; this Plane is not changed.\n\n"
             "Args:\n"
             "    d: the displacement, (dx, dy, dz).\n"
             "    dx: the displacement along x.\n"
             "    dy: the displacement along y.\n"
             "    dz: the displacement along z.\n\n"
             "Examples:\n"
             "    plane.Translated((0, 0, 5))\n"
             "    plane.Translated(0, 0, 5)")
        .def("Translated", [](AxisSystem a, double dx, double dy, double dz) { return a.Translate(Vec3({dx, dy, dz})); },
             py::arg("dx"), py::arg("dy"), py::arg("dz"), "")
        .def("Rotated", [](AxisSystem a, std::array<double, 3> point, std::array<double, 3> dir, double angle) {
            return a.Rotate(AxisOf(point, dir), angle);
        }, py::arg("point"), py::pos_only(), py::arg("dir"), py::arg("angle"),
           "Rotated(point: Point3, /, dir: tuple[float, float, float], angle: float) -> Plane\n\n"
           "A copy rotated by angle around the axis through point along dir; this Plane is not changed.\n\n"
           "Args:\n"
           "    point: a point of the axis, (x, y, z).\n"
           "    dir: the direction of the axis, (dx, dy, dz).\n"
           "    angle: the angle in degrees.\n\n"
           "Examples:\n"
           "    plane.Rotated((0, 0, 0), (1, 0, 0), 90)")
        .def("Mirrored", [](AxisSystem a, std::array<double, 3> point, std::array<double, 3> normal) {
            return a.Mirror(MirrorPlane(point, normal));
        }, py::arg("point"), py::pos_only(), py::arg("normal"),
           "Mirrored(point: Point3, /, normal: tuple[float, float, float]) -> Plane\n\n"
           "A copy mirrored at the plane through point with that normal; v is reversed, so it stays right-handed.\n\n"
           "Args:\n"
           "    point: a point of the mirror plane, (x, y, z).\n"
           "    normal: the normal of the mirror plane, (dx, dy, dz).\n\n"
           "Raises:\n"
           "    ValueError: the normal is zero, or a value is not finite.\n\n"
           "Examples:\n"
           "    plane.Mirrored((0, 0, 0), (1, 0, 0))")
        .def("ToOCCAxes", [](const AxisSystem &a) { return ToOCC(a); }, "ToOCCAxes() -> occ.Axes\n\n"
             "The plane as netgen Axes, e.g. for occ.WorkPlane.\n\n"
             "Examples:\n"
             "    plane.ToOCCAxes()")
        .def("__repr__", [](const AxisSystem &a) {
            return "Plane(origin=" + Text3(a.Origin()) + ", normal=" + Text3(a.Normal()) + ", axis=" + Text3(a.XDirection()) + ")";
        });
    py::implicitly_convertible<gp_Ax3, AxisSystem>();
    m.attr("XY") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    m.attr("YZ") = AxisSystem({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    m.attr("ZX") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0});

    m.def("AxesOf", [](const py::object &axes, const std::string &call) { return AxesArg(axes, call); }, py::arg("axes"), py::arg("call") = "AxesOf",
          "AxesOf(axes: Plane | None, call: str = \"AxesOf\") -> Plane\n\n"
          "The plane of anything a plane= argument takes.\n\n"
          "Args:\n"
          "    axes: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape; None the xy plane.\n"
          "    call: the public call the errors name.\n\n"
          "Raises:\n"
          "    ValueError: the parts of a compound lie on different planes, or it is empty.\n"
          "    TypeError: axes has another form.\n\n"
          "Examples:\n"
          "    px._planex.AxesOf(face)");
    AddCopy<AxisSystem>(m, "Plane");
}
