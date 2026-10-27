// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <pybind11/pybind11.h>

#include <BRep_Builder.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

// netgen's headers (through occprops.hpp) before binding.hpp's using namespace planex2d: both have a Timer
#include "integrations/netgenocc/convert.hpp"
#include "integrations/netgenocc/measure.hpp"
#include "integrations/netgenocc/plane.hpp"
#include "integrations/netgenocc/project.hpp"
#include "integrations/netgenocc/shapeexport.hpp"

#include "binding.hpp"

inline TopoDS_Compound CompoundOf(const Handle(TopTools_HSequenceOfShape) &wires) {
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (int i = 1; i <= wires->Length(); ++i)
        builder.Add(compound, wires->Value(i));
    return compound;
}

// an OCC shape, or a list or tuple of them as one compound
inline TopoDS_Shape ShapeArg(const py::handle &h, const std::string &call) {
    const auto shape = [&](const py::handle &x) {
        try {
            return x.cast<TopoDS_Shape>();
        } catch (const py::cast_error &) {
            throw py::type_error(call + ": " + py::repr(x).cast<std::string>() + " is no OCC shape; give a shape or a list of shapes");
        }
    };
    if (!py::isinstance<py::list>(h) && !py::isinstance<py::tuple>(h))
        return shape(h);
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const py::handle &x : h)
        builder.Add(compound, shape(x));
    return compound;
}

// the shape as netgen's class of its type (Face, Solid, Compound)
inline py::object TypedShape(const TopoDS_Shape &s) {
    if (s.ShapeType() == TopAbs_FACE)
        return py::cast(TopoDS::Face(s));
    if (s.ShapeType() == TopAbs_SOLID)
        return py::cast(TopoDS::Solid(s));
    if (s.ShapeType() == TopAbs_COMPOUND)
        return py::cast(TopoDS::Compound(s));
    return py::cast(s);
}

inline Wire ProjectedWire(const TopoDS_Shape &shape, const AxisSystem &axes) {
    Projection p = Project(shape, axes);
    if (!p.points.empty())
        Warning() << "Project: " << p.points.size() << " point" << (p.points.size() > 1 ? "s" : "")
                  << " left out (edges seen end-on, lone vertices); Sketch.Project keeps them as construction points";
    return p.wire;
}

inline WireList ProjectedWires(const TopoDS_Shape &shape, const AxisSystem &axes) { return ProjectedWire(shape, axes).OrderedChains(); }

inline const char *ProjectDoc(const std::string &x) {
    return Doc("Project(shape: TopoDS_Shape | list[TopoDS_Shape], /) -> list[Wire]\n\n"
               "The edges of the OCC shape projected straight onto this plane, as a list of wires, as px.Project.\n\n"
               "Args:\n"
               "    shape: an OCC shape, or a list of them (projected as one compound).\n\n"
               "Examples:\n"
               "    " + x + ".Project(box)");
}

// the dim of netgen's OCCGeometry of faces in the plane: a dim=2 mesh keeps only x and y, so faces elsewhere than in
// the plane z = 0 get a surface mesh in 3d
inline int FaceDim(const AxisSystem &a) {
    const Vector3 &n = a.Normal();
    return n.x == 0.0 && n.y == 0.0 && a.Origin().z == 0.0 ? 2 : 3;
}

inline TopoDS_Shape TargetOCC(const py::handle &target) {
    if (std::optional<std::vector<double>> p = AsPoint(target)) {
        if (p->size() != 3)
            throw py::value_error("a point in another plane needs three coordinates");
        return OCCPoint(gp_Pnt((*p)[0], (*p)[1], (*p)[2]));
    }
    if (std::optional<Thing> t = AsThing(target))
        return OCCOf(t->AsPlanar(), t->axes);
    if (py::isinstance<TopoDS_Shape>(target))
        return target.cast<TopoDS_Shape>();
    throw py::type_error("a target is a point, a Face, Shape, Wire, Edge, Vertex or an OCC shape");
}

inline std::optional<Vector3> OCCPointOf(const py::handle &h) {
    if (py::isinstance<gp_Pnt>(h))
        return Vector3Of(h.cast<gp_Pnt>().XYZ());
    return std::nullopt;
}
inline std::optional<Vector3> OCCDirectionOf(const py::handle &h) {
    if (py::isinstance<gp_Vec>(h))
        return Vector3Of(h.cast<gp_Vec>().XYZ());
    if (py::isinstance<gp_Dir>(h))
        return Vector3Of(h.cast<gp_Dir>().XYZ());
    return std::nullopt;
}
inline std::optional<AxisSystem> OCCAxesOf(const py::handle &h, const std::string &call) {
    if (py::isinstance<gp_Ax3>(h))
        return AxisSystemOf(h.cast<gp_Ax3>());
    if (py::isinstance<TopoDS_Shape>(h))
        return SketchAxes(h.cast<const TopoDS_Shape &>(), std::nullopt, std::nullopt, call);
    return std::nullopt;
}
inline bool IsOCCShape(const py::handle &h) { return py::isinstance<TopoDS_Shape>(h); }
inline bool IsOCCAxes(const py::handle &h) { return py::isinstance<gp_Ax3>(h); }
inline std::optional<Projection> OCCProjected(const py::handle &h, const AxisSystem &axes, bool ids) {
    if (!py::isinstance<TopoDS_Shape>(h))
        return std::nullopt;
    return Project(h.cast<const TopoDS_Shape &>(), axes, ids);
}
inline Wire ProjectedAcross(const Planar &s, const AxisSystem &from, const AxisSystem &to) { return Project(OCCOf(s, from), to).wire; }
inline Pair3 NearestAcross(const Planar &s, const std::optional<Vertex> &point, const AxisSystem &axes, const py::handle &target) {
    const TopoDS_Shape mine = point ? OCCPoint(gp_Pnt(ToOCC(axes.ToXYZ(*point)))) : OCCOf(s, axes);
    const Nearest3 n = NearestOCC(mine, TargetOCC(target));
    return {n.d, Vector3Of(n.a.XYZ()), Vector3Of(n.b.XYZ())};
}

inline const OCCHooks occHooks{.point = OCCPointOf,
                               .direction = OCCDirectionOf,
                               .axes = OCCAxesOf,
                               .isShape = IsOCCShape,
                               .isAxes = IsOCCAxes,
                               .projected = OCCProjected,
                               .across = ProjectedAcross,
                               .nearest = NearestAcross};

// m is the core module: the OCC module adds its functions there and its methods to the core's classes, so planex reads
// the same as with one module
void BindOCC(py::module_ &m);
void BindSection(py::module_ &m);
void BindSketchOCC(py::module_ &m);
