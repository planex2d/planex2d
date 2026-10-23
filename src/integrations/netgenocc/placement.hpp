// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <BRepBuilderAPI_Transform.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include "core/axissystem.hpp"
#include "core/timer.hpp"
#include "integrations/netgenocc/convert.hpp"
#include "integrations/netgenocc/occprops.hpp"

namespace planex2d {

    // the shape drawn in the xy plane, moved onto the plane of axes
    inline TopoDS_Shape Placed(const TopoDS_Shape &s, const AxisSystem &axes) {
        if (axes.IsClose(AxisSystem()))
            return s;
        static Timer timer("planex Place");
        Region reg(timer);
        gp_Trsf t;
        t.SetDisplacement(gp_Ax3(), ToOCC(axes));
        BRepBuilderAPI_Transform transform(s, t, true);
        PropagateProperties(transform, s);
        return transform.Shape();
    }

    inline void Place(Handle(TopTools_HSequenceOfShape) &shapes, const AxisSystem &axes) {
        for (int i = 1; i <= shapes->Length(); ++i)
            shapes->SetValue(i, Placed(shapes->Value(i), axes));
    }

}
