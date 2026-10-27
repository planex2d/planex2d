// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding_occ.hpp"
#include <gp_Ax2d.hxx>

#include "binding_sketch.hpp"
#include "integrations/netgenocc/section.hpp"
#ifdef PLANEX_CONSTRAINTS
#include "constraint_sketch/constraintsketch.hpp"
#endif

namespace {

// px.OCCGeometry of a sketch
template <class S>
void BindOCCGeometry(py::module_ &m) {
    m.def("OCCGeometry", [](S &s, EXPORT_PARAMS) {
        s.RequireFull("OCCGeometry()");
        const Shape shape = s.Shape();
        return py::module_::import("netgen.occ").attr("OCCGeometry")(ToOCCShape(shape, EXPORT_OPTIONS),
                                                                      py::arg("dim") = shape.HasFaces() ? FaceDim(shape.Axes()) : 1);
    }, py::arg("sketch"), EXPORT_ARGS, "");
}

// the OCC methods of a sketch class
template <class S>
void AddSketchOCCMethods(py::class_<S> sketch) {
    const std::string sk = NameOf(sketch);
    const std::string exportSignature = ExportSignature();
    const std::string exportArgs = ExportArgs("None takes px.config.remove_free_edges, False for a wire export");
    sketch
        // the caller's own object back, not a new wrapper of it (the OCC bindings' registry may be another one)
        .def("Project", [](const py::object &self, const py::object &shape, bool construction, const std::optional<std::string> &name,
                           const std::optional<std::string> &id) {
            S &s = self.cast<S &>();
            s.AddProjection(Project(ShapeArg(shape, "Project"), s.Axes(), true), construction, name, "Projection", UserId(id));
            return self;
        }, py::arg("shape"), py::pos_only(), py::arg("construction") = true, py::arg("name") = py::none(), py::arg("id") = py::none(),
             Doc("Project(shape: occ.TopoDS_Shape | list[occ.TopoDS_Shape], /, construction: bool = True, name: str | None = None, "
                 "id: str | None = None) -> " + sk + "\n\n"
                 "Add the edges and points of an OCC shape projected straight onto the sketch's plane, fixed where they are.\n\n"
                 "A curve becomes a spline through samples of it, within the spline tolerance. Its elements are "
                 "Projection.N.Edge.X, X the edge's index in shape.edges (the number the webgui shows), and "
                 "Projection.N.Vertex.X; pieces of one edge Edge.X.1, Edge.X.2.\n\n"
                 "Args:\n"
                 "    shape: the OCC shape, or a list of them (one compound: X numbers the edges of the compound).\n"
                 "    construction: references; False adds drawn edges.\n"
                 "    name: the name of the drawn edges.\n"
                 "    id: replaces Projection.N.\n\n"
                 "Examples:\n"
                 "    sk.Project(box)\n"
                 "    sk.Project(box, construction=False, name=\"outline\")"))
        .def("ToOCCWire", [](S &s, bool approximation, EXPORT_PARAMS, bool unify) {
            s.RequireFull("ToOCCWire()");
            return ToOCCWire(OneSketchWire(s, unify, unify ? "ToOCCWire()" : "ToOCCWire(unify=False)"), approximation, WIRE_EXPORT_OPTIONS);
        }, py::arg("approximation") = false, EXPORT_ARGS, py::arg("unify") = true,
           Doc("ToOCCWire(approximation: bool = False, " + exportSignature + ", unify: bool = True) -> occ.Wire\n\n"
               "The one wire of the drawn edges, Wire(unify=unify), as an OCC wire, made from a cleaned copy.\n\n"
               "Args:\n"
               "    approximation: splines as their arcs.\n" + exportArgs +
               "    unify: as for Wire().\n\n"
               "Raises:\n"
               "    ValueError: as Wire().\n\n"
               "Examples:\n"
               "    sk.ToOCCWire()\n"
               "    sk.ToOCCWire(unify=False, fit_arcs=True)"))
        .def("ToOCCWires", [](S &s, bool approximation, EXPORT_PARAMS, bool unify) {
            s.RequireFull("ToOCCWires()");
            Wire all(s.Axes());
            for (const Wire &w : s.Wires(unify, unify ? "ToOCCWires()" : "ToOCCWires(unify=False)"))
                for (const Edge &e : w)
                    all.Add(e);
            return CompoundOf(ToOCCWires(all, approximation, WIRE_EXPORT_OPTIONS));
        }, py::arg("approximation") = false, EXPORT_ARGS, py::arg("unify") = true,
           Doc("ToOCCWires(approximation: bool = False, " + exportSignature + ", unify: bool = True) -> occ.Compound\n\n"
               "The compound of the OCC wires of Wires(unify=unify), made from a cleaned copy.\n\n"
               "Args:\n"
               "    approximation: splines as their arcs.\n" + exportArgs +
               "    unify: as for Wires().\n\n"
               "Raises:\n"
               "    ValueError: as Wires().\n\n"
               "Examples:\n"
               "    sk.ToOCCWires()\n"
               "    sk.ToOCCWires(unify=False)"))
        .def("ToOCCFace", [](S &s, EXPORT_PARAMS) {
            s.RequireFull("ToOCCFace()");
            return TypedShape(ToOCCShape(s.Shape(), EXPORT_OPTIONS));
        },
             EXPORT_ARGS,
             Doc("ToOCCFace(" + exportSignature + ") -> occ.Face | occ.Compound\n\n"
                 "The region of the drawn elements as an OCC face, a compound of faces if it has several parts, made "
                 "from a cleaned copy.\n\n"
                 "Args:\n" + exportArgs + "\n"
                 "Raises:\n"
                 "    ValueError: in mode='strict' while the sketch can still move.\n\n"
                 "Examples:\n"
                 "    sk.ToOCCFace()\n"
                 "    sk.ToOCCFace(fit_arcs=True)"))
        .def("ToOCCShape", [](S &s, EXPORT_PARAMS) {
            s.RequireFull("ToOCCShape()");
            return TypedShape(ToOCCShape(s.Shape(), EXPORT_OPTIONS));
        },
             EXPORT_ARGS,
             Doc("ToOCCShape(" + exportSignature + ") -> occ.Face | occ.Compound\n\n"
                 "The same as ToOCCFace: the region as an OCC face, a compound of faces if it has several parts.\n\n"
                 "Args:\n" + exportArgs + "\n"
                 "Raises:\n"
                 "    ValueError: in mode='strict' while the sketch can still move.\n\n"
                 "Examples:\n"
                 "    sk.ToOCCShape()\n"
                 "    sk.ToOCCShape(remove_faces_smaller=0.01)"))
        .def("Intersect3d", [](const py::object &self, const py::object &shape, bool construction, const std::optional<std::string> &name,
                               const std::optional<std::string> &id) {
            S &s = self.cast<S &>();
            PlaneSection section = SectionOf(s.Axes(), ShapeArg(shape, "Intersect3d"));
            s.AddProjection(Projection{section.shape.Edges(), std::move(section.points)}, construction, name, "Intersect3d", UserId(id));
            return self;
        }, py::arg("shape"), py::pos_only(), py::arg("construction") = true, py::arg("name") = py::none(), py::arg("id") = py::none(),
             Doc("Intersect3d(shape: occ.TopoDS_Shape | list[occ.TopoDS_Shape], /, construction: bool = True, name: str | None = None, "
                 "id: str | None = None) -> " + sk + "\n\n"
                 "Add where the sketch's plane meets an OCC shape, as px.Intersect3d, as fixed elements under "
                 "Intersect3d.N, with the points where 3d wires cross or touch the plane.\n\n"
                 "Args:\n"
                 "    shape: the OCC shape, or a list of them (one compound: X numbers the edges of the compound).\n"
                 "    construction: references; False adds drawn edges.\n"
                 "    name: the name of them all; default they keep the names of the shape's faces and edges.\n"
                 "    id: replaces Intersect3d.N.\n\n"
                 "Examples:\n"
                 "    sk.Intersect3d(box)\n"
                 "    sk.Intersect3d(box, construction=False, name=\"cut\")"));
}

}  // namespace

void BindSketchOCC(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    m.def("_Ax2d", [](const gp_Ax2d &a) {
        return py::make_tuple(py::make_tuple(a.Location().X(), a.Location().Y()), py::make_tuple(a.Direction().X(), a.Direction().Y()));
    }, py::arg("pos"), "for px.WorkPlane: the place and direction of netgen's gp_Ax2d");
    BindOCCGeometry<Sketch>(m);
    AddSketchOCCMethods(CoreClass<Sketch>(m, "QuickSketch"));
#ifdef PLANEX_CONSTRAINTS
    BindOCCGeometry<ConstraintSketch>(m);
    AddSketchOCCMethods(CoreClass<ConstraintSketch>(m, "Sketch"));
#endif
}
