// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding_sketch.hpp"

// px.QuickSketch(sketch, copy=True): a copy of a QuickSketch, the geometry of a Sketch
static Sketch CopiedQuickSketch(const py::tuple &args, const py::object &autoConstraint, const std::optional<bool> &errorOnBranchChange,
                                const std::string &mode, const std::optional<std::string> &id, const py::object &dir) {
    const py::object source = SketchToCopy("QuickSketch", args, autoConstraint, errorOnBranchChange, mode, dir);
    py::object quick = py::isinstance<Sketch>(source) ? source : quickOfConstraintSketch(source);
    Sketch s = quick.is_none() ? QuickSketchOfObjects(source, "QuickSketch") : quick.cast<const Sketch &>();
    GiveId(s, id);
    return s;
}

// before any method: px.Tangent is the default continuity of Fillet on wires, faces and shapes
void BindConstraintKinds(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    py::enum_<sketch::Kind> kinds(m, "Constraint",
                                  "The kinds of constraints (px.Distance, ...); the geometric ones are also flags of the drawing calls.");
    for (sketch::Kind k : {sketch::Kind::Coincident, sketch::Kind::Horizontal, sketch::Kind::Vertical, sketch::Kind::Parallel, sketch::Kind::Perpendicular,
                       sketch::Kind::Tangent, sketch::Kind::Curvature, sketch::Kind::Equal, sketch::Kind::Symmetric, sketch::Kind::Concentric,
                       sketch::Kind::Fix, sketch::Kind::Distance, sketch::Kind::Radius, sketch::Kind::Diameter, sketch::Kind::Angle,
                       sketch::Kind::SymmetricCarriers, sketch::Kind::RigidGroup})
        kinds.value(sketch::KindName(k), k);
    kinds.export_values();
}

void BindSketch(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    m.def("_Interpolate", [](const std::vector<Vertex> &points, const std::map<size_t, Vertex> &tangents, bool closed, const py::object &style) {
        const auto [at, vectors] = TangentsOf(tangents);
        const Curve<double, Vertex> f = Interpolate(points, at, vectors, closed, StyleArg(style, "_Interpolate"));
        py::dict out;
        out["degree"] = f.degree;
        out["knots"] = f.knots;
        out["params"] = f.params;
        out["poles"] = f.poles;
        return out;
    }, py::arg("points"), py::pos_only(), py::arg("tangents") = std::map<size_t, Vertex>{}, py::arg("closed") = false, py::arg("style") = "averaged",
       "test hook: our B-spline interpolation (degree, flattened knots, chord length parameters, poles)");
    m.def("_InterpolatedAt", [](const std::vector<Vertex> &points, const std::map<size_t, Vertex> &tangents, bool closed, double u, size_t k,
                                const py::object &style) {
        const auto [at, vectors] = TangentsOf(tangents);
        return Tuple2(Interpolate(points, at, vectors, closed, StyleArg(style, "_InterpolatedAt")).At(u, k));
    }, py::arg("points"), py::pos_only(), py::arg("tangents"), py::arg("closed"), py::arg("u"), py::arg("k") = 0, py::arg("style") = "averaged",
       "test hook: the k-th derivative of the interpolating B-spline at the chord length parameter u");
    py::enum_<SketchAxis>(m, "SketchAxis", "The horizontal and vertical axis of a sketch (px.H, px.V), and the global origin (px.origin).")
        .value("H", SketchAxis::H)
        .value("V", SketchAxis::V)
        .value("origin", SketchAxis::Origin)
        .export_values();
    py::enum_<Until>(m, "SketchUntil", "The until= of a sketch's Line and Arc: px.first goes until the first element ahead, "
                                       "px.last until the last (the start not counted).")
        .value("first", Until::First)
        .value("last", Until::Last)
        .export_values();
    py::class_<SketchPen>(m, "SketchPen", "A sketch's pen when read (sk.pen): a drawing call takes it where a point (its "
                                         "position) or a direction (its direction) is asked for.")
        .def_property_readonly("position", [](const SketchPen &p) { return Tuple2(p.position); }, "position: tuple[float, float]\n\nWhere the pen is, (h, v).")
        .def_property_readonly("direction", [](const SketchPen &p) { return Tuple2(p.direction); },
                               "direction: tuple[float, float]\n\nThe direction the pen draws in, (dh, dv).")
        .def("__repr__", [](const SketchPen &p) {
            return "SketchPen(position=" + py::repr(Tuple2(p.position)).cast<std::string>() +
                   ", direction=" + py::repr(Tuple2(p.direction)).cast<std::string>() + ")";
        });
    py::class_<Sketch> quick(m, "QuickSketch", "A sketch without constraints: drawing without solves or analysis, as fast as it gets.");
    BindDrawing(quick);
    quick.def(py::init([](const py::args &args, const py::object &autoConstraint, const py::object &branch,
                          const std::string &mode, const std::optional<std::string> &id, const py::object &dir, bool copy) {
        const std::optional<bool> errorOnBranchChange = BoolOrAuto(branch, "QuickSketch", "error_on_branch_change");
        if (copy)
            return CopiedQuickSketch(args, autoConstraint, errorOnBranchChange, mode, id, dir);
        return NewSketch<Sketch>("QuickSketch", args, py::none(), autoConstraint, errorOnBranchChange, mode, id, dir);
    }), py::arg("auto_constraint") = false, py::arg("error_on_branch_change") = PlainAuto(),
              py::arg("mode") = "normal", py::arg("id") = py::none(), py::arg("dir") = py::none(), py::arg("copy") = false,
              "QuickSketch(plane: Plane | Axes | Sketch | Shape | Face | Wire | Compound | TopoDS_Shape | list | None = None, /, *, "
              "copy: bool = False, id: str | None = None)\n"
              "QuickSketch(start: Point, /, *flags: Constraint, dir: tuple[float, float] | None = None, "
              "id: str | None = None)\n\n"
              "A sketch without constraints: empty on a plane or with its pen at a start point, or a copy of a sketch or "
              "of geometry.\n\n"
              "The first argument decides by its type. A plane (a Plane, netgen Axes), a planex object (sketch, Shape, "
              "Face, Wire, Compound), a planar OCC shape or a list of these gives the plane of the sketch: the object's own "
              "origin and axes, for OCC shape the normal as oriented and the global origin and x axis projected; the items "
              "of a list must lie on one plane. A start (h, v) is on the xy plane, the pen moved there. Nothing is the "
              "empty sketch on the xy plane.\n\n"
              "Args:\n"
              "    plane: the object or list of objects giving the plane.\n"
              "    start: the pen's start (h, v), or (x, y, z) on the xy plane (z = 0), as Move does; not an id.\n"
              "    flags: as for Move.\n"
              "    dir: the drawing direction (dh, dv) from the start.\n"
              "    auto_constraint: only False (or None): a QuickSketch has no constraints.\n"
              "    error_on_branch_change: ignored: a QuickSketch does not solve.\n"
              "    mode: only 'normal'.\n"
              "    copy: True copies the geometry in, the object's edges with their names and properties (a QuickSketch of a "
              "Sketch has its geometry at the solved values, the same ids, properties, construction flags, groups, pen and id "
              "counters, and no constraints, dimensions or expressions, without a warning; OCC shapes are projected as by "
              "Project(shape, construction=False)); only id= goes with it. A Plane or Axes has nothing to copy: a PlanexWarning "
              "and the empty sketch. Default False: the sketch is empty on the plane of the object.\n"
              "    id: the sketch's id; default an automatic one.\n\n"
              "Raises:\n"
              "    ValueError: auto_constraint=True (or a dict) or mode='strict'; dir has no length; an invalid id; a list or a "
              "compound whose items lie on different planes, or an empty one.\n"
              "    TypeError: an unexpected keyword (plane= does not exist, the plane is the first argument); positional "
              "arguments other than a start and flags, dir= without a start, a string or 3d point as the start; copy=True "
              "without exactly one argument, or with dir=, auto_constraint=, error_on_branch_change= or mode=.\n\n"
              "Examples:\n"
              "    sk = px.QuickSketch((0, 0), dir=(1, 0))\n"
              "    sk = px.QuickSketch(face)\n"
              "    quick = px.QuickSketch(occ_wires, copy=True)");
    m.def("_CheckUserId", &CheckUserId, py::arg("id"), "raises ValueError unless id follows the rule of a user-given id");
    m.def("_DidYouMean", [](const std::string &text, const std::vector<std::string> &ids) { return DidYouMean(text, ids); },
          py::arg("text"), py::arg("ids"), "' (did you mean ...?)' for the ids closest to text, or ''; the rule of every id error");
    m.def("_FindObject", [](const std::string &id) {
        py::list found;
        std::set<PyObject *> seen;
        for (const auto &[ptr, inst] : py::detail::get_internals().registered_instances) {
            py::handle h(reinterpret_cast<PyObject *>(inst));
            if (!seen.insert(h.ptr()).second)
                continue;
            auto check = [&](auto *type) {
                using T = std::remove_pointer_t<decltype(type)>;
                if (py::isinstance<T>(h) && IdOwner(h.cast<T &>()).AssignedId() == id)
                    found.append(h);
            };
            check(static_cast<Shape *>(nullptr));
            check(static_cast<Face *>(nullptr));
            check(static_cast<Wire *>(nullptr));
            for (const auto owns : sketchOwnsId)
                if (owns(h, id))
                    found.append(h);
        }
        return found;
    }, py::arg("id"), "for px.Find: the live objects whose id is this (only ids already given)");
    m.def("Move", [](const py::args &args, const py::object &axes, const py::object &autoConstraint, const py::object &branch,
                       const std::string &mode, const std::optional<std::string> &id, const py::object &dir) {
        const std::optional<bool> errorOnBranchChange = BoolOrAuto(branch, "Move", "error_on_branch_change");
        py::list items;
        size_t k = 0;
        if (args.size() >= 2 && IsNumber(args[0]) && IsNumber(args[1])) {
            items.append(py::make_tuple(args[0], args[1]));
            k = 2;
        }
        for (; k < args.size(); ++k)
            items.append(args[k]);
        const py::tuple start(items);
        if (start.empty() || !StartArg(start[0], "Move"))
            throw py::type_error("Move((h, v)), then flags: a new sketch starts at a point; Sketch(plane) or "
                                 "QuickSketch(plane) draws on a plane, also plane=");
        if (quickSketchDefault)
            return py::cast(NewSketch<Sketch>("Move", start, axes, autoConstraint, errorOnBranchChange, mode, id, dir));
        return constraintSketchAt(start, axes, autoConstraint, errorOnBranchChange, mode, id, dir);
    }, py::arg("plane") = py::none(), py::arg("auto_constraint") = py::none(), py::arg("error_on_branch_change") = PlainAuto(),
       py::arg("mode") = "normal", py::arg("id") = py::none(), py::arg("dir") = py::none(),
       "Move(start: Point, /, *flags: Constraint, plane: Plane | None = None, dir: tuple[float, float] | None = None, "
       "auto_constraint: bool | dict | None = None, error_on_branch_change: bool | Auto = px.Auto, mode: str = \"normal\", "
       "id: str | None = None) -> Sketch | QuickSketch\n"
       "Move(h: float, v: float, /, *flags: Constraint, plane: Plane | None = None, dir: tuple[float, float] | None = None, "
       "auto_constraint: bool | dict | None = None, error_on_branch_change: bool | Auto = px.Auto, mode: str = \"normal\", "
       "id: str | None = None) -> Sketch | QuickSketch\n\n"
       "A new sketch with its pen at a start point: of px.config.default_sketch_type, a QuickSketch by default.\n\n"
       "Args:\n"
       "    start: the pen's start (h, v), or (x, y, z) on the xy plane (z = 0).\n"
       "    h: the start's h; with v.\n"
       "    v: the start's v.\n"
       "    flags: as for the sketch's Move.\n"
       "    plane: the plane it draws on, as for Sketch(); default the xy plane.\n"
       "    dir: the drawing direction (dh, dv) from the start.\n"
       "    auto_constraint: as for Sketch().\n"
       "    error_on_branch_change: as for Sketch().\n"
       "    mode: as for Sketch().\n"
       "    id: the sketch's id.\n\n"
       "Raises:\n"
       "    TypeError: no start point.\n"
       "    ValueError: auto_constraint=True (or a dict) or mode='strict' for a QuickSketch; dir has no length.\n\n"
       "Examples:\n"
       "    px.Move((0, 0)).Line(10).Turn(90).Line(5)\n"
       "    px.Move(0, 0, dir=(0, 1), id=\"rib\")");
}
