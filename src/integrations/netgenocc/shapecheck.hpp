// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>

#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS_Shape.hxx>

namespace planex2d {

    struct ShapeCheck {
        bool valid;
        bool closed;
        size_t freeEdges;
    };

    // BRepCheck's verdict, closedness and the number of edges with fewer than two faces
    inline ShapeCheck CheckShape(const TopoDS_Shape &shape) {
        TopTools_IndexedDataMapOfShapeListOfShape faces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
        size_t free = 0;
        for (int i = 1; i <= faces.Extent(); ++i)
            if (faces(i).Extent() < 2)
                ++free;
        return {BRepCheck_Analyzer(shape).IsValid() == Standard_True, BRep_Tool::IsClosed(shape) == Standard_True, free};
    }

}
