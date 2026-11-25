// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include "bindingargs.hpp"
#include "bindingmotion.hpp"

// the shortest text of each coordinate that reads back as it (-0 as 0)
static std::string Text3(const Vector3 &v) {
    const auto number = [](double x) { return NumberText(x == 0.0 ? 0.0 : x); };
    return "(" + number(v.x) + ", " + number(v.y) + ", " + number(v.z) + ")";
}

namespace {

bool IsXYZ(const py::handle &h) {
    if (occHooks->point(h) || occHooks->direction(h))
        return true;
    if (py::isinstance<py::str>(h) || py::isinstance<Wire>(h))
        return false;
    try {
        (void)h.cast<std::array<double, 3>>();
        return true;
    } catch (const py::cast_error &) {
        return false;
    }
}

std::optional<Axis> StraightLine(const py::handle &h) {
    return py::isinstance<Wire>(h) ? h.cast<const Wire &>().StraightLine() : std::nullopt;
}

AxisSystem PlaneFrom(const py::args &args, const py::kwargs &kw) {
    std::optional<Vector3> origin, axis, normal;
    for (const auto &[k, v] : kw) {
        const std::string key = k.cast<std::string>();
        if (key == "origin")
            origin = XYZArg(v, "Plane(origin)");
        else if (key == "axis")
            axis = XYZArg(v, "Plane(axis)");
        else if (key == "normal")
            normal = XYZArg(v, "Plane(normal)");
        else
            throw py::type_error("Plane() got an unexpected keyword argument '" + key + "'");
    }
    const size_t count = args.size();
    if (normal && count > 0)
        throw py::type_error("Plane(..., normal=): normal= goes with origin= alone, Plane(origin=..., normal=...); the plane "
                             "through a point with a normal is Plane(point, normal)");
    if (count == 0) {
        const Vector3 n = normal.value_or(Vector3{0.0, 0.0, 1.0});
        if (!(Norm(n) > 0.0))
            throw py::value_error("Plane: the normal must not be zero");
        return SketchAxes(AxesAlong(origin.value_or(Vector3{0.0, 0.0, 0.0}), n), std::nullopt, axis, "Plane");
    }
    if (count == 1)
        return SketchAxes(AxesArg(args[0], "Plane"), origin, axis, "Plane");
    if (count == 3) {
        if (!IsXYZ(args[0]) || !IsXYZ(args[1]) || !IsXYZ(args[2]))
            throw py::type_error("Plane(p1, p2, p3): three points (x, y, z)");
        return AxesThroughPoints(*XYZArg(args[0], "Plane(p1)"), *XYZArg(args[1], "Plane(p2)"), *XYZArg(args[2], "Plane(p3)"), origin, axis);
    }
    if (count != 2)
        throw py::type_error("Plane: at most three positional arguments: Plane(plane), Plane(plane, point), Plane(plane, offset), "
                             "Plane(line, line), Plane(line, point), Plane(p1, p2, p3), Plane(point, normal)");
    const py::handle a = args[0], b = args[1];
    if (const std::optional<Axis> line = StraightLine(a)) {
        if (py::isinstance<Wire>(b)) {
            const std::optional<Axis> other = StraightLine(b);
            if (!other)
                throw py::value_error("Plane(line, line): the second wire is not one straight line");
            return AxesThroughLines(*line, *other, origin, axis);
        }
        if (IsXYZ(b))
            return AxesThroughLineAndPoint(*line, *XYZArg(b, "Plane(point)"), origin, axis);
        if (IsNumber(b))
            throw py::type_error("Plane(line, offset): a straight wire is a line; the plane of the wire moved by the offset is "
                                 "Plane(px.Plane(wire), offset)");
        throw py::type_error("Plane(line, ...): the second is a line (a straight wire) or a point (x, y, z)");
    }
    if (IsXYZ(a)) {
        if (!IsXYZ(b))
            throw py::type_error("Plane(point, normal): the normal is a direction (dx, dy, dz)");
        const Vector3 n = *XYZArg(b, "Plane(normal)");
        if (!(Norm(n) > 0.0))
            throw py::value_error("Plane(point, normal): the normal must not be zero");
        return AxesThrough(*XYZArg(a, "Plane(point)"), n, origin, axis);
    }
    if (py::isinstance<Wire>(a) && py::isinstance<Wire>(b))
        throw py::value_error("Plane(line, line): the first wire is not one straight line");
    const AxisSystem plane = AxesArg(a, "Plane");
    if (IsNumber(b))
        return AxesMoved(plane, b.cast<double>(), origin, axis);
    if (IsXYZ(b))
        return AxesMoved(plane, Dot(*XYZArg(b, "Plane(point)") - plane.Origin(), plane.Normal()), origin, axis);
    throw py::type_error("Plane(plane, ...): the second is a point (x, y, z) or a number, the offset");
}

}

void BindPlane(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    py::class_<AxisSystem>(m, "Plane", "A plane in 3d for construction: an origin, the h and v directions and the normal, "
                                       "always right-handed (v = normal x h); its operations give new planes.")
        .def(py::init([](const py::args &args, const py::kwargs &kw) { return PlaneFrom(args, kw); }),
             "Plane(plane: Plane | None, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(plane: Plane, point: Point3, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(plane: Plane, offset: float, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(line: Wire, line2: Wire, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(line: Wire, point: Point3, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(p1: Point3, p2: Point3, p3: Point3, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(point: Point3, normal: Point3, /, *, origin: Point3 | None = None, axis: Point3 | None = None)\n"
             "Plane(*, origin: Point3 = (0, 0, 0), normal: Point3 = (0, 0, 1), axis: Point3 | None = None)\n\n"
             "A plane: of something that has one, parallel to a plane through a point or at an offset, through two lines, "
             "a line and a point or three points, or through a point with a normal.\n\n"
             "A plane given as the first argument keeps its h and, moved onto the new plane, its origin. The others take "
             "the global origin and the global x axis projected onto the plane (y if the normal is x); their normal "
             "points toward +z, in the xy plane toward +x, along y toward +y. origin and axis replace them, projected "
             "onto the plane. A wire whose edges lie on one straight line is a line; any other wire gives its plane. "
             "Netgen Axes convert to a Plane wherever planex takes a plane.\n\n"
             "Args:\n"
             "    plane: anything with a plane: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC "
             "shape; None the xy plane.\n"
             "    point: a point (x, y, z) the plane runs through, a netgen Pnt.\n"
             "    offset: the distance along the plane's normal, negative against it.\n"
             "    line: a straight line: a Wire whose edges lie on one line.\n"
             "    line2: a second line that meets line or runs parallel to it.\n"
             "    p1: a point (x, y, z) of the plane.\n"
             "    p2: a second point.\n"
             "    p3: a third point, not on the line through p1 and p2.\n"
             "    normal: the normal in 3d, (dx, dy, dz), a netgen Vec or Dir.\n"
             "    origin: the origin in 3d, (x, y, z), a netgen Pnt; projected onto the plane.\n"
             "    axis: the direction of h in 3d (not the normal), projected onto the plane.\n\n"
             "Raises:\n"
             "    ValueError: the normal is zero; the axis is perpendicular to the plane; two lines that neither meet nor "
             "are parallel, or lie on one line; a point on the line; three points on one line.\n"
             "    TypeError: an argument of another form; normal= with positional arguments.\n\n"
             "Examples:\n"
             "    px.Plane(px.XY, 2)\n"
             "    px.Plane((0, 0, 0), (1, 0, 0), (0, 1, 1), axis=(1, 0, 0))")
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
        .def("ToHV", [](const AxisSystem &a, const py::object &p) { return ToHVOf(a, p); }, py::arg("point"), py::pos_only(),
             "ToHV(point: Point3, /) -> tuple[float, float]\n"
             "ToHV(points: list[Point3], /) -> list[tuple[float, float]]\n\n"
             "The plane coordinates (h, v) of the projection of a 3d point onto the plane, or of a list of points.\n\n"
             "Args:\n"
             "    point: (x, y, z), or netgen's Pnt (a face's center).\n"
             "    points: a list of them.\n\n"
             "Examples:\n"
             "    plane.ToHV((1, 2, 0))\n"
             "    plane.ToHV([(0, 0, 0), (1, 2, 0)])")
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
        .def("__repr__", [](const AxisSystem &a) {
            return "Plane(origin=" + Text3(a.Origin()) + ", normal=" + Text3(a.Normal()) + ", axis=" + Text3(a.XDirection()) + ")";
        });
    m.attr("XY") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    m.attr("YZ") = AxisSystem({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    m.attr("ZX") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0});
    // the front plane: h = x, v = z, the normal h x v = -y
    m.attr("XZ") = AxisSystem({0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0});
    // the planes of the standard views, seen from the side their normal points to, v up where the view stands beside the
    // front view in first-angle projection (the bottom view above it: v toward the front)
    m.attr("Front") = AxisSystem({0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0});
    m.attr("Top") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    m.attr("Right") = AxisSystem({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    m.attr("Left") = AxisSystem({0.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, -1.0, 0.0});
    m.attr("Rear") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    m.attr("Bottom") = AxisSystem({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0});

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
    // netgen's Axes where a Plane is taken, as py::implicitly_convertible<gp_Ax3, AxisSystem> would do: the OCC bindings
    // tell them (occHooks->isAxes), the conversion stays in the core's own type registry
    py::detail::get_type_info(typeid(AxisSystem))->implicit_conversions.push_back([](PyObject *obj, PyTypeObject *type) -> PyObject * {
        static bool busy = false;
        if (busy || !occHooks->isAxes(obj))
            return nullptr;
        busy = true;
        PyObject *plane = PyObject_CallOneArg(reinterpret_cast<PyObject *>(type), obj);
        busy = false;
        if (!plane)
            PyErr_Clear();
        return plane;
    });
}
