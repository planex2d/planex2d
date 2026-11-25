// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <map>
#include <optional>
#include <span>
#include <vector>

#include <AppDef_Array1OfMultiPointConstraint.hxx>
#include <AppDef_BSplineCompute.hxx>
#include <AppDef_MultiLine.hxx>
#include <AppDef_MultiPointConstraint.hxx>
#include <AppParCurves_Constraint.hxx>
#include <AppParCurves_MultiBSpCurve.hxx>
#include <Approx_ParametrizationType.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_HArray1OfBoolean.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array1OfVec2d.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>

#include "planex/core/vertex.hpp"
#include "planex/curves/curve.hpp"
#include "planex/curves/curvefit.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/curves/primitiveedge.hpp"
#include "planex/integrations/netgenocc/occspline.hpp"

namespace planex2d {

    inline Handle(TColgp_HArray1OfPnt2d) PointArray(const std::vector<Vertex> &pts) {
        Handle(TColgp_HArray1OfPnt2d) arr = new TColgp_HArray1OfPnt2d(1, (int)pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            arr->SetValue((int)i + 1, gp_Pnt2d(pts[i].x, pts[i].y));
        return arr;
    }

    Handle(Geom2d_BSplineCurve) OccInterpolate(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, double tol);

    Handle(Geom2d_BSplineCurve) Approximate(const std::vector<Vertex> &pts, double tol,
                                            const std::optional<Vertex> &startTangent,
                                            const std::optional<Vertex> &endTangent);

    std::optional<Curve<double, Vertex>> OccFittedCurve(std::span<const PrimitiveEdge> chain, const std::vector<Vertex> &pts, double tol,
                                                        const std::optional<Vertex> &startTangent,
                                                        const std::optional<Vertex> &endTangent);

}
