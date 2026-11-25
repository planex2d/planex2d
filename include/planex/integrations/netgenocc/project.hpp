// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GeomConvert.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>

#include "planex/integrations/netgenocc/convert.hpp"
#include "planex/integrations/netgenocc/occprops.hpp"
#include "planex/integrations/netgenocc/occspline.hpp"
#include "planex/boolean/boolean.hpp"
#include "planex/sketch/projection.hpp"

namespace planex2d {

    // a name read in: "|" joins names in a selection, so it becomes "_"
    inline std::optional<std::string> ReadName(std::optional<std::string> name) {
        if (!name || name->find('|') == std::string::npos)
            return name;
        const std::string given = *name;
        std::replace(name->begin(), name->end(), '|', '_');
        Warning() << "name '" << given << "' read as '" << *name << "': '|' joins names in a selection";
        return name;
    }

    Properties PropertiesOf(const TopoDS_Shape &s);

    // the id netgen's numbers give an edge or a vertex (Edge.1): its first index in shape.edges or shape.vertices,
    // which list it once per face it bounds, as the webgui shows them
    struct Numbering {
        TopTools_IndexedMapOfShape map;
        std::vector<size_t> first;

        Numbering(const TopoDS_Shape &shape, TopAbs_ShapeEnum kind);
        std::string Id(const char *kind, const TopoDS_Shape &s) const {
            return std::string(kind) + "." + std::to_string(first[(size_t)map.FindIndex(s) - 1]);
        }
    };

    struct ProjectedEdge {
        EdgeList edges;
        std::optional<Vertex> point;
    };
    ProjectedEdge ProjectEdge(const TopoDS_Edge &edge, const AxisSystem &axes, const Properties &props,
                              const Numbering *vertices = nullptr);

    inline Wire ProjectedFaceEdges(const TopoDS_Face &face, const AxisSystem &axes) {
        Wire w(axes);
        for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next())
            if (!BRep_Tool::Degenerated(TopoDS::Edge(e.Current())))
                for (Edge &x : ProjectEdge(TopoDS::Edge(e.Current()), axes, PropertiesOf(e.Current())).edges)
                    w.Add(std::move(x));
        return w;
    }

    std::vector<ProjectedPoint> PointsOffEdges(std::vector<ProjectedPoint> points, const Wire &w, double tol);

    // what OCC counts as one point in shape: its largest edge or vertex tolerance, at least the model precision
    inline double PointTolerance(const TopoDS_Shape &shape) {
        double tol = Precision::Confusion();
        for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next())
            tol = std::max(tol, BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        for (TopExp_Explorer v(shape, TopAbs_VERTEX); v.More(); v.Next())
            tol = std::max(tol, BRep_Tool::Tolerance(TopoDS::Vertex(v.Current())));
        return tol;
    }

    inline void ArrangeProjected(Wire &w, double tol) {
        w.RemoveSmallGaps(tol);
        std::vector<CutState> state(w.Size());
        Arrange(w, state);
        // a crossing cut near an end leaves a piece shorter than OCC's tolerance: it goes, its neighbours meet again
        w.Erase(EpsEdges(w, tol, [](size_t) { return true; }));
        w.RemoveSmallGaps(tol);
        w.MergeOnCarriers();
    }

    Projection Project(const TopoDS_Shape &shape, const AxisSystem &axes, bool ids = false);

}
