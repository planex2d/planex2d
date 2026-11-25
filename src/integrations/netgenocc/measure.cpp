// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/measure.hpp"

namespace planex2d {

    TopoDS_Shape OCCOf(const Planar &s, const AxisSystem &axes) {
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        Wire region = s.region, curves = s.curves;
        region.SetAxes(axes);
        curves.SetAxes(axes);
        if (!region.Empty())
            builder.Add(all, ToOCCFace(region));
        if (!curves.Empty()) {
            const Cleanup &c = config::state->cleanup;
            auto wires = ToOCCWires(curves, false, config::state->arc_fit, config::state->spline_fit,
                                    Cleanup{c.remove_edges_smaller, c.close_gaps_smaller, false, c.merge_collinear_closer});
            for (int i = 1; i <= wires->Length(); ++i)
                builder.Add(all, wires->Value(i));
        }
        return all;
    }

}
