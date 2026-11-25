// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <stdexcept>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>

#include "planex/integrations/netgenocc/convert.hpp"
#include "planex/boolean/arrangement.hpp"
#include "planex/integrations/netgenocc/plane.hpp"
#include "planex/integrations/netgenocc/project.hpp"
#include "planex/shape/shape.hpp"
#include "planex/shape/shapeops.hpp"

namespace planex2d {

    inline TopoDS_Shape PlaneCut(const TopoDS_Shape &shape, const AxisSystem &axes) {
        BRepAlgoAPI_Section section(shape, gp_Pln(ToOCC(axes)), false);
        section.Approximation(true);
        section.Build();
        if (!section.IsDone())
            throw std::runtime_error("Intersect3d: OCC's section failed");
        return section.Shape();
    }

    inline bool InPlane(const TopoDS_Face &face, const AxisSystem &axes) {
        BRepAdaptor_Surface s(face, false);
        if (s.GetType() != GeomAbs_Plane)
            return false;
        const gp_Pln p = s.Plane();
        const gp_Dir n(ToOCC(axes.Normal()));
        return p.Axis().Direction().IsParallel(n, Precision::Angular()) &&
               std::abs(gp_Vec(gp_Pnt(ToOCC(axes.Origin())), p.Location()).Dot(gp_Vec(n))) <= Precision::Confusion();
    }

    Wire SectionEdges(const TopoDS_Shape &shape, const AxisSystem &axes);

    Wire CrossSection(Wire w, const TopoDS_Shape &solid, const AxisSystem &axes);

    Projection FreeSection(const TopoDS_Shape &shape, const AxisSystem &axes);

    struct PlaneSection {
        Shape shape;
        std::vector<ProjectedPoint> points;
    };

    PlaneSection SectionOf(const AxisSystem &axes, const TopoDS_Shape &shape);

    inline Shape Intersect3d(const AxisSystem &axes, const TopoDS_Shape &shape) {
        PlaneSection s = SectionOf(axes, shape);
        if (!s.points.empty())
            Warning() << "Intersect3d: " << s.points.size() << " point(s) where edges cross or touch the plane are left out "
                      << "of the shape, a sketch's Intersect3d keeps them";
        else if (s.shape.Empty())
            Warning() << "Intersect3d: the plane misses the shape or only touches it, the result is empty";
        return std::move(s.shape);
    }

    Shape FromOCC(const TopoDS_Shape &shape);

}
