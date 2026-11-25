// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <exception>
#include <string>

#include "bindingbase.hpp"
#include "bindingviews.hpp"

// a new reference that is never released: the category outlives every warning
inline PyObject *planexWarning = nullptr;
inline bool quickSketchDefault = true;
inline std::string drawingStandard = "DIN";

int CallerStackLevel();

// called from any thread, also from taskflow workers while the caller released the GIL; an "error" filter turns the
// warning into the Python exception, unless a C++ exception is already unwinding
inline void WarnPython(const std::string &text) {
    py::gil_scoped_acquire gil;
    if (PyErr_WarnEx(planexWarning, text.c_str(), CallerStackLevel()) == 0)
        return;
    if (std::uncaught_exceptions() == 0)
        throw py::error_already_set();
    PyErr_Clear();
}

template <class T>
inline py::class_<T> CoreClass(const py::module_ &core, const char *name) {
    return py::reinterpret_borrow<py::class_<T>>(core.attr(name));
}

template <class F, class... Extra>
inline void AddMethod(py::module_ &m, const char *cls, const char *name, F f, const Extra &...extra) {
    py::object c = m.attr(cls);
    py::setattr(c, name, py::cpp_function(f, py::name(name), py::is_method(c), py::sibling(py::getattr(c, name, py::none())), extra...));
}

void BindConfig(py::module_ &m);
void BindVertex(py::module_ &m);
void BindEdgeGeometry(py::module_ &m);
void BindEdges(py::module_ &m);
void BindWire(py::module_ &m, py::class_<Wire> &wire);
void BindFace(py::class_<Face> &face);
void BindShape(py::module_ &m, py::class_<Shape> &shape, py::class_<Face> &face, py::class_<FaceView> &faceView,
               py::class_<FacesView> &facesView);
void BindDeformable(py::module_ &m);
void BindPlane(py::module_ &m);
void BindConstraintKinds(py::module_ &m);
void BindOffsetForms(py::module_ &m);
void BindSketch(py::module_ &m);
void BindSolver(py::module_ &m);
void BindConstraintSketch(py::module_ &m);
void BindIdentify(py::module_ &m);
void BindStep(py::module_ &m);
