// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <TopAbs.hxx>

#include "integrations/netgenocc/prism.hpp"


namespace {

bool IsProfile(const py::handle &h) {
    return py::isinstance<Face>(h) || py::isinstance<Shape>(h) || (py::hasattr(h, "_axes") && !py::isinstance<AxisSystem>(h));
}

TopoDS_Shape ProfileOCC(const py::object &profile, const std::string &call) {
    if (!IsProfile(profile))
        throw py::type_error(call + "(profile): the profile is a Face, Shape, Sketch or QuickSketch");
    return profile.attr("ToOCCShape")().cast<TopoDS_Shape>();
}

PrismEnd PlanexEnd(const py::object &object, const std::string &call) {
    PrismEnd end;
    const AxisSystem axes = AxesArg(object, call);
    end.plane = gp_Pln(gp_Pnt(ToOCC(axes.Origin())), gp_Dir(ToOCC(axes.Normal())));
    for (TopExp_Explorer f(ProfileOCC(object, call), TopAbs_FACE); f.More(); f.Next())
        end.faces.push_back(TopoDS::Face(f.Current()));
    if (end.faces.empty())
        throw py::value_error(call + "(until=): the object has no face to end on; give a Face, a Shape with faces, a Sketch with a closed "
                                     "region or a px.Plane");
    return end;
}

PrismEnd FaceEnd(const TopoDS_Shape &shape, const std::string &call) {
    if (shape.ShapeType() != TopAbs_FACE)
        throw py::value_error(call + "(until=): an OCC " + TopAbs::ShapeTypeToString(shape.ShapeType()) +
                              " is no end; until= takes flat faces, a px.Plane or a planex object, not solids");
    if (BRepAdaptor_Surface(TopoDS::Face(shape)).GetType() != GeomAbs_Plane)
        throw py::value_error(call + "(until=): the OCC face is not flat; until= ends on flat faces only, give a px.Plane or a flat face");
    PrismEnd end;
    end.plane = PlaneOf(shape, call);
    end.faces.push_back(TopoDS::Face(shape));
    return end;
}

PrismEnd PrismEndArg(const py::object &until, const std::string &call) {
    if (until.is_none())
        return {};
    if (py::isinstance<Until>(until)) {
        PrismEnd end;
        end.reach = until.cast<Until>();
        return end;
    }
    if (py::isinstance<AxisSystem>(until)) {
        const AxisSystem &axes = until.cast<const AxisSystem &>();
        PrismEnd end;
        end.plane = gp_Pln(gp_Pnt(ToOCC(axes.Origin())), gp_Dir(ToOCC(axes.Normal())));
        return end;
    }
    if (py::isinstance<TopoDS_Shape>(until))
        return FaceEnd(until.cast<const TopoDS_Shape &>(), call);
    if (IsProfile(until))
        return PlanexEnd(until, call);
    throw py::type_error(call + "(until=): until is px.first, px.last (a Pocket), a px.Plane, a flat OCC face or a planex Face, Shape or "
                                "Sketch");
}

// the call of Pad or Pocket from its arguments; the target is null for a Pad
PrismCall PrismCallOf(const std::string &call, const py::object &profile, const std::optional<double> &length, double length2,
                      const py::object &dir, bool reverse, const py::object &until, const TopoDS_Shape &target) {
    if (!IsProfile(profile))
        throw py::type_error(call + "(profile): the profile is a Face, Shape, Sketch or QuickSketch");
    PrismCall c;
    c.call = call;
    const Vector3 normal = AxesArg(profile, call).Normal();
    c.normal = gp_Dir(ToOCC(normal));
    c.dir = PrismDirection(normal, XYZArg(dir, call + "(dir)"), reverse, !target.IsNull(), call);
    c.length = length;
    c.length2 = length2;
    c.until = PrismEndArg(until, call);
    c.target = target;
    return c;
}

const char *prismArgs =
    "    profile: a closed planar profile: a Face, a Shape with faces, a Sketch or QuickSketch (the faces its ToOCCShape() "
    "gives, on its plane).\n"
    "    length: the extent along dir from the profile's plane; None with until=.\n"
    "    length2: the extent against dir, so the solid reaches across the plane; default 0.\n"
    "    dir: the direction of the extrusion, e.g. (0, 0, 1), any direction that leaves the profile's plane (it may be tilted); "
    "default the plane's normal, for a Pocket its negative.\n"
    "    reverse: True turns dir around.\n"
    "    until: instead of length: a px.Plane, a flat OCC face, or a planex Face, Shape or Sketch (up to its plane, the "
    "end of the prism lies inside it); a Pocket also px.first or px.last (the first or last flat face of the target the "
    "prism crosses along dir). Solids and curved faces are not allowed.\n\n";

const char *prismProperties =
    "The solid has the name, maxh and colour of the profile's face; each side face the properties of its profile edge; "
    "both end faces the colour and maxh of the face, no name; the edges along dir the properties of the profile vertex they "
    "start at.";

}  // namespace

void BindOCC(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    const std::string exportSignature = ExportSignature();
    const std::string exportArgs = ExportArgs("None takes px.config.remove_free_edges, False for a wire");
    m.def("CheckShape", [](const TopoDS_Shape &shape) {
        planex2d::ShapeCheck c = planex2d::CheckShape(shape);
        py::dict d;
        d["valid"] = c.valid;
        d["closed"] = c.closed;
        d["free_edges"] = c.freeEdges;
        return d;
    }, py::arg("shape"), py::pos_only(),
          "CheckShape(shape: TopoDS_Shape, /) -> dict\n\n"
          "The OCC shape's BRepCheck validity, closedness and free edges, as a dict.\n\n"
          "Args:\n"
          "    shape: an OCC shape.\n\n"
          "Returns:\n"
          "    {valid: BRepCheck's verdict, closed: every edge used twice with opposite orientations, free_edges: the "
          "number of edges with fewer than two faces}.\n\n"
          "Examples:\n"
          "    px._planex.CheckShape(occ_shape)");

    m.def("_StackedPrisms", [](const ShapeList &interfaces, const Shape &region, const std::vector<double> &z,
                               const std::vector<std::string> &names, EXPORT_PARAMS) {
        TopoDS_Shape stack;
        {
            py::gil_scoped_release release;
            stack = StackedPrisms(interfaces, region, z, names, EXPORT_OPTIONS);
        }
        return TypedShape(stack);
    }, py::arg("interfaces"), py::arg("region"), py::arg("z"), py::arg("names"), EXPORT_ARGS,
          "the region extruded between the interfaces at heights z (from the top), the interfaces' faces as the caps");

    m.def("Project", [](const TopoDS_Shape &shape, const py::object &axes) { return ProjectedWires(shape, AxesArg(axes, "Project")); },
          py::arg("shape"), py::pos_only(), py::arg("plane") = py::none(),
          "Project(shape: TopoDS_Shape, /, plane: Plane | None = None) -> list[Wire]\n\n"
          "The edges of the OCC shape projected straight onto the plane, as a list of clean wires.\n\n"
          "Lines stay lines, circles parallel to the plane stay arcs, other curves become exact splines; points (edges "
          "seen end-on, lone vertices) are left out with a warning, Sketch.Project keeps them.\n\n"
          "Args:\n"
          "    shape: an OCC shape.\n"
          "    plane: the plane projected onto: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC "
          "shape; default the xy plane.\n\n"
          "Raises:\n"
          "    TypeError: plane has another form.\n\n"
          "Examples:\n"
          "    px.Project(occ_shape)\n"
          "    px.Project(occ_shape, plane=px.YZ)");

    auto geometry = [](const TopoDS_Shape &shape, int dim) {
        return py::module_::import("netgen.occ").attr("OCCGeometry")(shape, py::arg("dim") = dim);
    };
    m.def("OCCGeometry", [geometry](const Face &face, EXPORT_PARAMS) {
        return geometry(ToOCCFace(face, EXPORT_OPTIONS), FaceDim(face.Axes()));
    }, py::arg("face"), EXPORT_ARGS,
          Doc("OCCGeometry(face: Face, " + exportSignature + ") -> occ.OCCGeometry\n"
              "OCCGeometry(wire: Wire, " + exportSignature + ") -> occ.OCCGeometry\n"
              "OCCGeometry(shape: Shape, /, " + exportSignature + ") -> occ.OCCGeometry\n"
              "OCCGeometry(sketch: Sketch | QuickSketch, " + exportSignature + ") -> occ.OCCGeometry\n\n"
              "The netgen OCCGeometry of a face, wire, shape or sketch, exported with the given cleanup and fits.\n\n"
              "A face: dim=2 in the xy plane (z = 0), else dim=3 (a surface mesh). A wire: dim=1, a mesh of segments "
              "along the edges, also of a closed wire; px.OCCGeometry(px.Face([wire])) meshes the region inside it. A "
              "shape: its faces and wires, dim=2 in the xy plane (z = 0), else dim=3 (a surface mesh); dim=1 without "
              "faces. A sketch: netgen OCCGeometry of the region: dim=2 in the xy plane (z = 0), else dim=3 (a surface "
              "mesh); dim=1 without faces.\n\n"
              "Args:\n"
              "    face: a Face.\n"
              "    wire: a Wire.\n"
              "    shape: a Shape.\n"
              "    sketch: a Sketch or QuickSketch.\n" + exportArgs + "\n"
              "Raises:\n"
              "    ValueError: a sketch in mode='strict' while it can still move.\n"
              "    TypeError: fit_splines or fit_arcs has another form or names an unknown option.\n\n"
              "Examples:\n"
              "    px.OCCGeometry(face)\n"
              "    px.OCCGeometry(shape, fit_arcs=True, remove_edges_smaller=1e-6)"));
    m.def("OCCGeometry", [geometry](const Wire &w, EXPORT_PARAMS) {
        return geometry(CompoundOf(ToOCCWires(w, false, WIRE_EXPORT_OPTIONS)), 1);
    }, py::arg("wire"), EXPORT_ARGS, "");
    m.def("OCCGeometry", [geometry](const Shape &s, EXPORT_PARAMS) {
        return geometry(ToOCCShape(s, EXPORT_OPTIONS), s.HasFaces() ? FaceDim(s.Axes()) : 1);
    }, py::arg("shape"), py::pos_only(), EXPORT_ARGS, "");
    m.def("Pad", [](const py::object &profile, const std::optional<double> &length, double length2, const py::object &dir, bool reverse,
                    const py::object &until) {
        const PrismCall c = PrismCallOf("Pad", profile, length, length2, dir, reverse, until, TopoDS_Shape());
        const TopoDS_Shape faces = ProfileOCC(profile, "Pad");
        TopoDS_Shape solid;
        {
            py::gil_scoped_release release;
            solid = PadSolid(faces, c);
        }
        return TypedShape(solid);
    }, py::arg("profile"), py::arg("length") = py::none(), py::arg("length2") = 0.0, py::arg("dir") = py::none(),
          py::arg("reverse") = false, py::arg("until") = py::none(),
          Doc(std::string("Pad(profile: Face | Shape | Sketch | QuickSketch, length: float | None = None, length2: float = 0.0, "
              "dir: Point3 | None = None, reverse: bool = False, until: Plane | Face | Shape | Sketch | QuickSketch | "
              "TopoDS_Shape | None = None) -> TopoDS_Shape\n\n"
              "The profile extruded into an OCC solid, as CATIA's Pad.\n\n") + prismProperties + "\n\n"
              "Args:\n" + prismArgs +
              "Raises:\n"
              "    TypeError: profile or until has another form.\n"
              "    ValueError: neither length nor until, or both; a negative or infinite length; dir zero or parallel to the "
              "profile's plane; until=px.first or px.last (a Pad has no target); until a solid, a curved face, or a plane that "
              "does not lie ahead of the whole profile, or a face that does not hold the end of the prism.\n\n"
              "Examples:\n"
              "    px.Pad(face, 3)\n"
              "    px.Pad(sketch, 2, length2=1, dir=(0, 0, 1), reverse=True)\n"
              "    px.Pad(face, until=px.Plane(origin=(0, 0, 5)))"));

    m.def("Pocket", [](const TopoDS_Shape &target, const py::object &profile, const std::optional<double> &length, double length2,
                       const py::object &dir, bool reverse, const py::object &until) {
        const PrismCall c = PrismCallOf("Pocket", profile, length, length2, dir, reverse, until, target);
        const TopoDS_Shape faces = ProfileOCC(profile, "Pocket");
        TopoDS_Shape solid;
        {
            py::gil_scoped_release release;
            solid = PocketSolid(faces, c);
        }
        return TypedShape(solid);
    }, py::arg("target"), py::arg("profile"), py::arg("length") = py::none(), py::arg("length2") = 0.0, py::arg("dir") = py::none(),
          py::arg("reverse") = false, py::arg("until") = py::none(),
          Doc(std::string("Pocket(target: TopoDS_Shape, profile: Face | Shape | Sketch | QuickSketch, length: float | None = None, "
              "length2: float = 0.0, dir: Point3 | None = None, reverse: bool = False, until: SketchUntil | Plane | Face | "
              "Shape | Sketch | QuickSketch | TopoDS_Shape | None = None) -> TopoDS_Shape\n\n"
              "The target solid minus the profile's prism, as CATIA's Pocket.\n\n"
              "The prism is the one px.Pad makes, by default against the profile's normal, into the material. The target's faces "
              "keep their properties; the prism's walls, floor and edges bring those of the profile as in px.Pad.\n\n"
              "Args:\n"
              "    target: an OCC solid.\n") + prismArgs +
              "Raises:\n"
              "    TypeError: profile or until has another form.\n"
              "    ValueError: the target has no solid; neither length nor until, or both; a negative or infinite length; dir zero or "
              "parallel to the profile's plane; until a solid, a curved face, a plane that does not lie ahead of the whole profile, or a "
              "face that does not hold the end of the prism; px.first or px.last reaching no face or a curved one; a prism that "
              "removes nothing or everything.\n\n"
              "Examples:\n"
              "    px.Pocket(box, circle_face, 2)\n"
              "    px.Pocket(box, circle_face, until=px.last)"));
}
