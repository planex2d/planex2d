// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding_occ.hpp"
#include "binding_sketch.hpp"
#ifdef PLANEX_CONSTRAINTS
#include "constraint_sketch/constraintsketch.hpp"
#endif

namespace {

// every core class the OCC bindings read: shadows in their own registry when the core's is another one, so that
// pybind11 reads the core's objects through its conduit (binding.hpp Shadow); a shadow Vertex also takes (h, v) as
// the core's Vertex does
void ShadowCore(py::module_ &m, const py::module_ &core) {
    Shadow<Face>(m, core, "Face");
    Shadow<Wire>(m, core, "Wire");
    Shadow<Shape>(m, core, "Shape");
    Shadow<AxisSystem>(m, core, "Plane");
    Shadow<EdgeView>(m, core, "EdgeView");
    Shadow<VertexView>(m, core, "VertexView");
    Shadow<FaceView>(m, core, "FaceView");
    Shadow<planex2d::Line>(m, core, "LineEdge");
    Shadow<planex2d::Arc>(m, core, "ArcEdge");
    Shadow<planex2d::Circle>(m, core, "CircleEdge");
    Shadow<planex2d::Spline>(m, core, "SplineEdge");
    Shadow<Until>(m, core, "SketchUntil");
    Shadow<AutoValue>(m, core, "AutoType");
    Shadow<SketchAxis>(m, core, "SketchAxis");
    Shadow<sketch::Kind>(m, core, "Constraint");
    Shadow<SketchPen>(m, core, "SketchPen");
    Shadow<Sketch>(m, core, "QuickSketch");
#ifdef PLANEX_CONSTRAINTS
    Shadow<ConstraintSketch>(m, core, "Sketch");
#endif
    if (Shadow<Vertex>(m, core, "Vertex")) {
        py::reinterpret_borrow<py::class_<Vertex>>(m.attr("_Vertex")).def(py::init([](std::array<double, 2> a) {
            IsFinite("point", {a[0], a[1]});
            return Vertex{a[0], a[1]};
        }));
        py::implicitly_convertible<py::sequence, Vertex>();
    }
}

}  // namespace

// planex's OCC bindings: they take the core module's settings and hooks, and add their functions to the core module and
// their methods to its classes; netgen.occ registers netgen's OCC types
PYBIND11_MODULE(_planex_occ, m) {
    py::module_ core = py::module_::import("planex._planex");
    // read only what the core was built from the same sources
    const std::string coreVersion = core.attr("_version").cast<std::string>();
    if (coreVersion != PLANEX_VERSION)
        throw py::import_error(std::string("planex's OCC bindings are built for planex2d ") + PLANEX_VERSION + ", this planex2d is " +
                               coreVersion + ": pip install --upgrade planex2d[occ] installs the OCC bindings for it");
    if (!py::object(core.attr("_layout")).equal(Layout()))
        throw py::import_error(std::string("planex's OCC bindings (planex2d ") + PLANEX_VERSION + ") are built from other sources than "
                               "this planex2d's core: build both from the same sources");
    m.attr("_core_version") = PLANEX_VERSION;
    config::state = core.attr("_state").cast<py::capsule>().get_pointer<config::State>();
    occ = core.attr("_occ_hooks").cast<py::capsule>().get_pointer<OCCHooks>();
    const auto *services = core.attr("_services").cast<py::capsule>().get_pointer<CoreServices>();
    executorOf = services->executor;
    Profiler::of = services->profiler;
    py::module_::import("netgen.occ");
    ShadowCore(m, core);
    BindOCC(core);
    BindSketchOCC(core);
    BindSection(core);
}
