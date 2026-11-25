// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/occprops.hpp"

namespace planex2d {

    void SetProperties(const TopoDS_Shape &s, const Properties &p) {
        if (!p)
            return;
        netgen::ShapeProperties &prop = netgen::OCCGeometry::GetProperties(s);
        if (p->name)
            prop.name = *p->name;
        if (p->maxh)
            prop.maxh = *p->maxh;
        if (p->minh)
            SetMinH(prop, *p->minh);
        if (p->hpref)
            prop.hpref = *p->hpref;
        if (p->layer)
            prop.layer = *p->layer;
        if (p->col)
            prop.col = netgen::Vec<4>((*p->col)[0], (*p->col)[1], (*p->col)[2], (*p->col)[3]);
        if (p->quad_dominated)
            prop.quad_dominated = *p->quad_dominated;
    }

}
