// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once


#include <Geom2d_BSplineCurve.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <gp_Pnt2d.hxx>

#include "planex/core/vertex.hpp"
#include "planex/curves/curve.hpp"

namespace planex2d {


    Handle(Geom2d_BSplineCurve) MakeBSpline(const BSplineData &b, bool periodic);

    Curve<double, Vertex> CurveOf(const Handle(Geom2d_BSplineCurve) &occ);
    inline Handle(Geom2d_BSplineCurve) OccCurveOf(const Curve<double, Vertex> &c) {
        return MakeBSpline(BSplineData{c.poles, c.knots, c.weights, (int)c.degree}, false);
    }

}
