// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

namespace planex2d {

    template <class S>
    TopoDS_Shape OneOrCompound(const std::vector<S> &shapes) {
        if (shapes.size() == 1)
            return shapes.front();
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (const S &s : shapes)
            builder.Add(all, s);
        return all;
    }

}
