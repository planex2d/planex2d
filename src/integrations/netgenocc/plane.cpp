// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/plane.hpp"

#include <stdexcept>

namespace planex2d {

    // with faces the normal is that of the first face as oriented (outward on a solid)
    gp_Pln PlaneOf(const TopoDS_Shape &shape, const std::string &call) {
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

}
