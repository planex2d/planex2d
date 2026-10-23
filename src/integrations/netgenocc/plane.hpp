// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <BRepAdaptor_Surface.hxx>
#include <BRepLib_FindSurface.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>

#include "core/axissystem.hpp"
#include "integrations/netgenocc/convert.hpp"

namespace planex2d {

    // with faces the normal is that of the first face as oriented (outward on a solid)
    inline gp_Pln PlaneOf(const TopoDS_Shape &shape, const std::string &call) {
        BRepLib_FindSurface find(shape, -1.0, true);
        if (!find.Found())
            throw std::invalid_argument(call + ": the shape does not lie in one plane");
        GeomAdaptor_Surface surface(find.Surface());
        if (surface.GetType() != GeomAbs_Plane)
            throw std::invalid_argument(call + ": the shape does not lie in one plane");
        gp_Pln plane = surface.Plane().Transformed(find.Location().Transformation());
        if (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More()) {
            const TopoDS_Face &f = TopoDS::Face(faces.Current());
            if (BRepAdaptor_Surface s(f); s.GetType() == GeomAbs_Plane) {
                gp_Dir n = s.Plane().Axis().Direction();
                if (f.Orientation() == TopAbs_REVERSED)
                    n.Reverse();
                if (n.Dot(plane.Axis().Direction()) < 0.0)
                    plane = gp_Pln(plane.Location(), plane.Axis().Direction().Reversed());
            }
        }
        return plane;
    }

    // the plane of a planar OCC shape, its origin the global origin projected onto it
    inline AxisSystem SketchAxes(const TopoDS_Shape &shape, const std::optional<Vector3> &origin, const std::optional<Vector3> &axis,
                                 const std::string &call) {
        const gp_Pln plane = PlaneOf(shape, call);
        return SketchAxes(AxesAlong(Vector3Of(plane.Location().XYZ()), Vector3Of(plane.Axis().Direction().XYZ())),
                          origin.value_or(Vector3{}), axis, call);
    }

}
