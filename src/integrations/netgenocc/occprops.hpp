// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include <BRepBuilderAPI_MakeShape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <occ/occgeom.hpp>

#include "curves/edge.hpp"

namespace planex2d {

    // only netgen versions that have a minh property get it
    template <class P>
    void SetMinH(P &prop, double minh) {
        if constexpr (requires { prop.minh; })
            prop.minh = minh;
    }

    inline void SetProperties(const TopoDS_Shape &s, const Properties &p) {
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

    // fractions of the OCC curve's parameter range, as netgen's edge division reads them
    inline void SetPartition(const TopoDS_Shape &edge, const std::vector<double> &params) {
        if (params.empty())
            return;
        netgen::Array<double> a(params.size());
        for (size_t i = 0; i < params.size(); ++i)
            a[i] = params[i];
        netgen::OCCGeometry::GetProperties(edge).partition = std::move(a);
    }

    // netgen::PropagateProperties also carries identifications, which needs netgen symbols that are not exported
    inline void PropagateProperties(BRepBuilderAPI_MakeShape &builder, const TopoDS_Shape &original) {
        for (TopAbs_ShapeEnum type : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX})
            for (TopExp_Explorer e(original, type); e.More(); e.Next()) {
                if (!netgen::OCCGeometry::HaveProperties(e.Current()))
                    continue;
                netgen::ShapeProperties prop = netgen::OCCGeometry::GetProperties(e.Current());
                for (const TopoDS_Shape &m : builder.Modified(e.Current()))
                    netgen::OCCGeometry::GetProperties(m).Merge(prop);
            }
    }

}
