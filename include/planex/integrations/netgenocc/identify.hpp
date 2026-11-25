// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once


#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Mat.hxx>

#include "planex/integrations/netgenocc/convert.hpp"
#include "planex/integrations/netgenocc/project.hpp"
#include "planex/shape/identification.hpp"

namespace planex2d {

    inline gp_GTrsf GlobalTrafo(const Affine2 &trafo, const AxisSystem &axes) {
        const gp_Mat r(ToOCC(axes.XDirection()), ToOCC(axes.YDirection()), ToOCC(axes.Normal()));
        const gp_Mat local(trafo.m[0], trafo.m[1], 0.0, trafo.m[2], trafo.m[3], 0.0, 0.0, 0.0, 1.0);
        const gp_Mat g = r * local * r.Transposed();
        const gp_XYZ o = ToOCC(axes.Origin());
        return gp_GTrsf(g, gp_XYZ(trafo.t.x, trafo.t.y, 0.0).Multiplied(r) + o - o.Multiplied(g));
    }

    void ApplyIdentifications(const TopoDS_Shape &shape, const AxisSystem &axes, const Identifications &ids);

}
