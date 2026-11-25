// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/placement.hpp"

namespace planex2d {

    TopoDS_Shape Placed(const TopoDS_Shape &s, const AxisSystem &axes) {
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

}
