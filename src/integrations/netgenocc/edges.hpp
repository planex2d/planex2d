// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <numbers>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>
#include <GeomAPI.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include "curves/arc.hpp"
#include "curves/circle.hpp"
#include "curves/line.hpp"
#include "curves/spline.hpp"

namespace planex2d {

    // an edge between the OCC vertices vs and ve, in the xy plane
    inline TopoDS_Edge ToOCC(const Line &, const TopoDS_Vertex &vs, const TopoDS_Vertex &ve) { return BRepBuilderAPI_MakeEdge(vs, ve).Edge(); }

    inline TopoDS_Edge ToOCC(const Arc &a, const TopoDS_Vertex &vs, const TopoDS_Vertex &ve) {
        const gp_Pnt s(a.Start().x, a.Start().y, 0.0), e(a.End().x, a.End().y, 0.0);
        const gp_Circ c(gp_Ax2(gp_Pnt(a.Center().x, a.Center().y, 0.0), gp::DZ()), a.Radius());
        // Sense=false still trims the ccw arc from P1 to P2 and only reverses it
        auto arc = a.CCW() ? GC_MakeArcOfCircle(c, s, e, true) : GC_MakeArcOfCircle(c, e, s, false);
        return BRepBuilderAPI_MakeEdge(arc.Value(), vs, ve).Edge();
    }

    // the closed edge of the whole circle on the seam vertex v: its curve starts at the origin and runs the circle's way
    inline TopoDS_Edge ToOCC(const Circle &c, const TopoDS_Vertex &v) {
        const Vertex m = c.Center(), o = c.Origin() - m;
        const gp_Ax2 axes(gp_Pnt(m.x, m.y, 0.0), c.CCW() ? gp::DZ() : gp::DZ().Reversed(), gp_Dir(o.x, o.y, 0.0));
        Handle(Geom_Circle) curve = new Geom_Circle(axes, c.Radius());
        return BRepBuilderAPI_MakeEdge(curve, v, v, 0.0, 2.0 * std::numbers::pi).Edge();
    }

    inline TopoDS_Edge ToOCC(const Spline &s, const TopoDS_Vertex &vs, const TopoDS_Vertex &ve) {
        return BRepBuilderAPI_MakeEdge(GeomAPI::To3d(s.Segment(), gp_Pln(gp::XOY())), vs, ve).Edge();
    }

}
