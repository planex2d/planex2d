// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <string>

#include <BRepAdaptor_Surface.hxx>
#include <BRepLib_FindSurface.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>

#include "planex/core/axissystem.hpp"
#include "planex/integrations/netgenocc/convert.hpp"

namespace planex2d {

    gp_Pln PlaneOf(const TopoDS_Shape &shape, const std::string &call);

    // the plane of a planar OCC shape, its origin the global origin projected onto it
    inline AxisSystem SketchAxes(const TopoDS_Shape &shape, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis,
                                 const std::string &call) {
        const gp_Pln plane = PlaneOf(shape, call);
        return SketchAxes(AxesAlong(Vector3Of(plane.Location().XYZ()), Vector3Of(plane.Axis().Direction().XYZ())),
                          origin.value_or(Vector3{}), axis, call);
    }

}
