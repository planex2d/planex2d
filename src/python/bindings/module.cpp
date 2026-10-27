// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

#include <pybind11/eval.h>

// the metaclass of Face: isinstance and issubclass also accept the classes in its _views, as abc does for virtual
// subclasses, so a FaceView counts as a Face
static py::object ViewAwareType() {
    py::dict scope;
    scope["base"] = py::reinterpret_borrow<py::object>(reinterpret_cast<PyObject *>(py::detail::get_internals().default_metaclass));
    py::exec(R"(
class ViewAwareType(base):
    def __instancecheck__(cls, obj):
        return super().__instancecheck__(obj) or isinstance(obj, getattr(cls, "_views", ()))

    def __subclasscheck__(cls, sub):
        return super().__subclasscheck__(sub) or issubclass(sub, getattr(cls, "_views", ()))
)", scope);
    return scope["ViewAwareType"];
}

PYBIND11_MODULE(_planex, m) {
    planexWarning = PyErr_NewExceptionWithDoc("planex.PlanexWarning", "A warning of the planex kernel.", PyExc_UserWarning, nullptr);
    m.attr("PlanexWarning") = py::handle(planexWarning);
    config::state->warning_handler = WarnPython;
    // the OCC module shares these, see config::state
    m.attr("_state") = py::capsule(config::state, "planex._state");
    m.attr("_occ_hooks") = py::capsule(occ, "planex._occ_hooks");
    m.attr("_services") = py::capsule(&coreServices, "planex._services");
    m.attr("_version") = PLANEX_VERSION;
    m.attr("_layout") = Layout();

    BindConfig(m);
    BindVertex(m);
    BindPlane(m);
    BindEdgeGeometry(m);
    BindConstraintKinds(m);
    BindOffsetForms(m);

    // every class before any method: a signature names the classes it takes and returns
    auto wire = py::class_<Wire>(m, "Wire",
                                 "One clean chain of edges, open or closed: consecutive edges share an end bit-exactly, "
                                 "no branch, crossing or overlap; the edges are in chain order, each with its own direction.");
    const py::object faceType = ViewAwareType();
    auto face = py::class_<Face>(m, "Face", py::metaclass(faceType),
                                 "One region: an outer loop and its holes, edges meeting only at shared endpoints.");
    auto shape = py::class_<Shape>(m, "Shape",
                                   "Faces that do not overlap and wires on one plane, cut against each other, every edge "
                                   "stored once; the result of every boolean.");
    auto faceView = py::class_<FaceView>(m, "FaceView",
                                         "A face of a shape, a px.Face for isinstance: its properties, edges and vertices are "
                                         "the shape's, a change is made there; what only reads works on Copy(). It holds "
                                         "while the shape's edges are unchanged.");
    auto facesView = py::class_<FacesView>(m, "FacesView",
                                           "The faces of a shape, a list-like view: a property set here is set on all of them.");
    face.attr("_views") = py::make_tuple(faceView);

    BindEdges(m);
    BindWire(m, wire);
    BindFace(face);
    BindShape(m, shape, face, faceView, facesView);
    BindDeformable(m);
    BindSketch(m);
#ifdef PLANEX_CONSTRAINTS
    BindSolver(m);
    BindConstraintSketch(m);
#endif
    BindIdentify(m);
    BindStep(m);
    // what the OCC bindings made, taken over from their capsule as the core's own object (binding.hpp ToCore); the
    // capsule's destructor frees what the move leaves
    py::options options;
    options.disable_function_signatures();
    m.def("_adopt", [](const std::string &kind, const py::capsule &made) -> py::object {
        if (kind == "Shape")
            return py::cast(std::move(*made.get_pointer<Shape>()));
        if (kind == "WireList")
            return py::cast(std::move(*made.get_pointer<WireList>()));
        throw std::invalid_argument("_adopt: no " + kind);
    }, py::arg("kind"), py::arg("made"), "_adopt(kind: str, made: object) -> Any\n\nfor planex's OCC bindings: an object they made, as the core's");
}
