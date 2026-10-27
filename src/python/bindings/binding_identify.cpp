// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include "shape/identification.hpp"

static Affine2 AffineOf(const std::array<std::array<double, 2>, 2> &m, const std::array<double, 2> &v) {
    return {{m[0][0], m[0][1], m[1][0], m[1][1]}, {v[0], v[1]}};
}

// px.Auto (none: found at every export), a Transformation (px.Translation, px.Rotation, ...), or (matrix 2x2, vector 2)
static std::optional<Affine2> TrafoArg(const py::object &t, const std::string &call) {
    if (IsPlainAuto(t, call, "trafo"))
        return std::nullopt;
    if (py::isinstance<Affine2>(t))
        return t.cast<Affine2>();
    try {
        if (py::len(t) == 2)
            return AffineOf(t[py::int_(0)].cast<std::array<std::array<double, 2>, 2>>(), t[py::int_(1)].cast<std::array<double, 2>>());
    } catch (const py::error_already_set &) {
    } catch (const py::cast_error &) {
    }
    throw py::type_error(call + ": trafo is a Transformation (px.Translation, px.Rotation, px.GetTransformation) or "
                                "(matrix 2x2, vector 2)");
}

// netgen's IdentificationType, default PERIODIC
static int TypeArg(const py::object &type) {
    if (type.is_none())
        return 2;
    const int k = py::int_(type);
    if (k < 1 || k > 5)
        throw py::value_error("Identify: type is a netgen.meshing.IdentificationType");
    return k;
}

static void AddEdgesOf(const py::handle &h, EdgeList &out) {
    if (py::isinstance<EdgeView>(h))
        out.push_back(h.cast<const EdgeView &>().Get());
    else if (py::isinstance<EdgesView>(h)) {
        const EdgesView &l = h.cast<const EdgesView &>();
        for (size_t i : l.Indices())
            out.push_back((*l.wire)[i]);
    } else if (py::isinstance<Wire>(h))
        for (const Edge &e : h.cast<const Wire &>())
            out.push_back(e);
    else if (py::isinstance<Shape>(h))
        for (const Edge &e : h.cast<const Shape &>().Edges())
            out.push_back(e);
    else if (py::isinstance<Face>(h))
        for (const Edge &e : h.cast<const Face &>().Edges())
            out.push_back(e);
    else if (py::isinstance<Line>(h))
        out.push_back(h.cast<Line>());
    else if (py::isinstance<Arc>(h))
        out.push_back(h.cast<Arc>());
    else if (py::isinstance<py::iterable>(h) && !py::isinstance<py::str>(h))
        for (const py::handle &x : h)
            AddEdgesOf(x, out);
    else
        throw py::type_error("GetTransformation: " + py::repr(h).cast<std::string>() + " has no edges");
}

template <class T>
static T &IdentifyOn(T &x, const std::string &from, const std::string &to, const py::object &trafo, const std::optional<std::string> &name,
                     const py::object &type) {
    if (from == to)
        throw py::value_error("Identify('" + from + "', '" + to + "'): an edge name is identified with another");
    EdgeList a, b;
    for (const Edge &e : x.Edges())
        if (const Properties p = PropsOf(e); p && p->name) {
            if (*p->name == from)
                a.push_back(e);
            else if (*p->name == to)
                b.push_back(e);
        }
    if (a.empty() || b.empty())
        throw py::value_error("Identify('" + from + "', '" + to + "'): no edge is named '" + (a.empty() ? from : to) + "'");
    Identification id{name.value_or(from + "-" + to), from, to, TrafoArg(trafo, "Identify"), TypeArg(type)};
    if (!id.trafo)
        GetTransformation(a, b, false, "'" + from + "'", "'" + to + "'");
    std::erase_if(x.Ids(), [&](const Identification &i) { return i.name == id.name; });
    x.Ids().push_back(std::move(id));
    return x;
}

template <class T>
static T &RemoveIdentification(T &x, const std::string &name) {
    if (std::erase_if(x.Ids(), [&](const Identification &i) { return i.name == name; }) == 0)
        throw py::key_error("RemoveIdentification: no identification named '" + name + "'");
    return x;
}

static py::dict IdsDict(const Identifications &ids) {
    const py::object types = py::module_::import("netgen.meshing").attr("IdentificationType");
    py::dict d;
    for (const Identification &i : ids)
        d[py::str(i.name)] = py::make_tuple(i.from, i.to, i.trafo ? py::cast(*i.trafo) : PlainAuto(), types(i.type));
    return d;
}

template <class T>
static void BindIdentifications(py::module_ &m, const char *cls) {
    const auto ref = py::return_value_policy::reference_internal;
    const std::string var = VariableOf(cls);
    AddMethod(m, cls, "Identify", [](T &x, const std::string &a, const std::string &b, const py::object &trafo,
                                     const std::optional<std::string> &name, const py::object &type) -> T & {
        return IdentifyOn(x, a, b, trafo, name, type);
    }, py::arg("a"), py::arg("b"), py::pos_only(), py::arg("trafo") = PlainAuto(), py::kw_only(), py::arg("name") = py::none(),
              py::arg("type") = py::none(), ref,
              Doc("Identify(a: str, b: str, /, trafo: Transformation | tuple | Auto = px.Auto, *, name: str | None = None, "
                  "type: IdentificationType | None = None) -> " + std::string(cls) + "\n\n"
                  "Identifies the edges named a with those named b for the mesh: b's mesh is copied from a's (netgen's "
                  "Identify).\n\n"
                  "The identification is stored by name and applied at every OCC export, which raises unless every "
                  "exported piece of one side pairs with one of the other. Booleans, Glue, Split, Trim and copies keep it "
                  "(the first operand wins where names clash), motions move it.\n\n"
                  "Args:\n"
                  "    a: the name of the edges mapped onto b.\n"
                  "    b: the name of the edges that get a's mesh.\n"
                  "    trafo: the map of a onto b in the plane's coordinates: a Transformation (px.Translation, "
                  "px.Rotation) or (matrix 2x2, vector 2); px.Auto finds it from the geometry at every export "
                  "(px.GetTransformation), so it follows later changes.\n"
                  "    name: the identification's name; default 'a-b'; an identification of the same name is replaced.\n"
                  "    type: a netgen.meshing.IdentificationType; None: PERIODIC.\n\n"
                  "Returns:\n"
                  "    this " + std::string(cls) + ".\n\n"
                  "Raises:\n"
                  "    ValueError: a and b are the same; no edge has the name a or b; type is not an IdentificationType; "
                  "without trafo, no map takes a onto b (as px.GetTransformation).\n"
                  "    TypeError: trafo has another form.\n\n"
                  "Examples:\n"
                  "    " + var + ".Identify(\"left\", \"right\", px.Translation(10, 0))\n"
                  "    " + var + ".Identify(\"inner\", \"outer\", name=\"ring\")"));
    AddMethod(m, cls, "RemoveIdentification", [](T &x, const std::string &name) -> T & { return RemoveIdentification(x, name); },
              py::arg("name"), ref,
              Doc("RemoveIdentification(name: str) -> " + std::string(cls) + "\n\n"
                  "Removes the identification called name.\n\n"
                  "Args:\n"
                  "    name: the identification's name, a key of identifications.\n\n"
                  "Returns:\n"
                  "    this " + std::string(cls) + ".\n\n"
                  "Raises:\n"
                  "    KeyError: no identification has that name.\n\n"
                  "Examples:\n"
                  "    " + var + ".RemoveIdentification(\"left-right\")"));
    py::object c = m.attr(cls);
    py::setattr(c, "identifications",
                py::module_::import("builtins").attr("property")(
                    py::cpp_function([](const T &x) { return IdsDict(x.Ids()); }, py::is_method(c)), py::none(), py::none(),
                    "identifications: dict\n\nThe identifications as a dict {name: (a, b, trafo or px.Auto, type)}."));
}

void BindIdentify(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    py::class_<Affine2>(m, "Transformation", "A map of the plane: p -> matrix p + vector.")
        .def(py::init(&AffineOf), py::arg("matrix"), py::arg("vector") = std::array<double, 2>{0.0, 0.0},
             "Transformation(matrix: tuple[tuple[float, float], tuple[float, float]], vector: tuple[float, float] = (0, 0))\n\n"
             "The map p -> matrix p + vector of the plane.\n\n"
             "Args:\n"
             "    matrix: the 2x2 matrix, two rows.\n"
             "    vector: the shift (dh, dv) after the matrix.\n\n"
             "Examples:\n"
             "    px.Transformation(((0, -1), (1, 0)))\n"
             "    px.Transformation(((1, 0), (0, 1)), (10, 0))")
        .def_property_readonly("matrix", [](const Affine2 &a) { return py::make_tuple(py::make_tuple(a.m[0], a.m[1]), py::make_tuple(a.m[2], a.m[3])); },
                               "matrix: tuple[tuple[float, float], tuple[float, float]]\n\nThe 2x2 matrix, two rows.")
        .def_property_readonly("vector", [](const Affine2 &a) { return Tuple2(a.t); }, "vector: tuple[float, float]\n\nThe shift (dh, dv) after the matrix.")
        .def("__call__", [](const Affine2 &a, const Vertex &p) { return a(p); }, py::arg("point"), py::pos_only(),
             "__call__(point: Point, /) -> Vertex\n\n"
             "The image of the point (h, v).\n\n"
             "Examples:\n"
             "    px.Translation(10, 0)((1, 2))")
        .def("Inverse", &Affine2::Inverse,
             "Inverse() -> Transformation\n\n"
             "The inverse map.\n\n"
             "Raises:\n"
             "    ValueError: the transformation is singular.\n\n"
             "Examples:\n"
             "    px.Rotation(90).Inverse()")
        .def("__mul__", [](const Affine2 &a, const Affine2 &b) { return a * b; },
             "__mul__(other: Transformation) -> Transformation\n\n"
             "The composition a * b: first b, then a.\n\n"
             "Examples:\n"
             "    px.Translation(10, 0) * px.Rotation(90)")
        .def("__repr__", [](const Affine2 &a) {
            const auto f = [](double x) { return py::repr(py::float_(x)).cast<std::string>(); };
            return "Transformation(matrix=((" + f(a.m[0]) + ", " + f(a.m[1]) + "), (" + f(a.m[2]) + ", " + f(a.m[3]) + ")), vector=(" +
                   f(a.t.x) + ", " + f(a.t.y) + "))";
        });
    m.def("Translation", [](double dh, double dv) { return Affine2::Translation({dh, dv}); }, py::arg("dh"), py::arg("dv"),
          "Translation(dh: float, dv: float) -> Transformation\n"
          "Translation(d: tuple[float, float]) -> Transformation\n\n"
          "The move by (dh, dv) in the plane.\n\n"
          "Args:\n"
          "    dh: the move along h.\n"
          "    dv: the move along v.\n"
          "    d: the move (dh, dv).\n\n"
          "Examples:\n"
          "    px.Translation(10, 0)\n"
          "    px.Translation((10, 0))");
    m.def("Translation", [](std::array<double, 2> d) { return Affine2::Translation({d[0], d[1]}); }, py::arg("d"), "");
    m.def("Rotation", [](double angle, const Vertex &center) { return Affine2::Rotation(center, angle); }, py::arg("angle"),
          py::arg("center") = Vertex{0.0, 0.0},
          "Rotation(angle: float, center: Point = (0, 0)) -> Transformation\n\n"
          "The turn by angle counterclockwise about center in the plane.\n\n"
          "Args:\n"
          "    angle: the angle in degrees.\n"
          "    center: the center (h, v).\n\n"
          "Examples:\n"
          "    px.Rotation(90)\n"
          "    px.Rotation(45, center=(5, 0))");
    m.def("GetTransformation", [](const py::object &a, const py::object &b, bool reverse) {
        EdgeList ea, eb;
        AddEdgesOf(a, ea);
        AddEdgesOf(b, eb);
        return GetTransformation(ea, eb, reverse);
    }, py::arg("a"), py::arg("b"), py::pos_only(), py::arg("reverse") = false,
          "GetTransformation(a: Wire | Face | Shape | EdgeView | EdgesView | Selection | LineEdge | ArcEdge | list, b: Wire | "
          "Face | Shape | EdgeView | EdgesView | Selection | LineEdge | ArcEdge | list, /, reverse: bool = False) -> Transformation\n\n"
          "The map of the plane taking the chain of lines and arcs a onto b, vertex onto vertex.\n\n"
          "A straight chain, a single arc or a circle maps by a similarity (turn, uniform scale, move), other chains by "
          "the affine map of the vertices; every vertex and edge midpoint must land. Of the possible pairings it takes "
          "the one closest to a translation (orientation kept, the smallest turn).\n\n"
          "Args:\n"
          "    a: the edges mapped: a wire, face, shape, edge view, edges view, selection, LineEdge, ArcEdge, or a list of them.\n"
          "    b: the edges a lands on, in the same forms.\n"
          "    reverse: True takes the best pairing running the other way.\n\n"
          "Raises:\n"
          "    ValueError: a or b has no edges, has a spline, branches or is not one connected chain; they have "
          "different numbers of vertices; no map takes a onto b.\n"
          "    TypeError: a or b has no edges to take.\n\n"
          "Examples:\n"
          "    px.GetTransformation(face.edges[0], face.edges[2])\n"
          "    px.GetTransformation(left, right, reverse=True)");
    BindIdentifications<Shape>(m, "Shape");
    BindIdentifications<Face>(m, "Face");
    AddCopy<Affine2>(m, "Transformation");
}
