// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <BRepBuilderAPI_Transform.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include "planex/core/axissystem.hpp"
#include "planex/core/timer.hpp"
#include "planex/integrations/netgenocc/convert.hpp"
#include "planex/integrations/netgenocc/occprops.hpp"

namespace planex2d {

    TopoDS_Shape Placed(const TopoDS_Shape &s, const AxisSystem &axes);

    inline void Place(Handle(TopTools_HSequenceOfShape) &shapes, const AxisSystem &axes) {
        for (int i = 1; i <= shapes->Length(); ++i)
            shapes->SetValue(i, Placed(shapes->Value(i), axes));
    }

}
