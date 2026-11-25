// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/splinefit.hpp"

#include <algorithm>
#include <stdexcept>

namespace planex2d {

    // OCC's interpolation with the tangents made unit, the derivative for chord length parameters: OCC's own scaling
    // lets uneven points overshoot far past the end
    Handle(Geom2d_BSplineCurve) OccInterpolate(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, double tol) {
        CheckSpline(pts, tangents, false, tol);
        const int n = (int)pts.size();
        Geom2dAPI_Interpolate builder(PointArray(pts), false, tol);
        if (!tangents.empty()) {
            // OCC reads only the flagged tangents
            const Vertex &dummy = tangents.begin()->second;
            TColgp_Array1OfVec2d tv(1, n);
            Handle(TColStd_HArray1OfBoolean) tf = new TColStd_HArray1OfBoolean(1, n);
            for (int i = 0; i < n; ++i) {
                const auto it = tangents.find((size_t)i);
                const Vertex t = it == tangents.end() ? dummy : Normalized(it->second);
                tv.SetValue(i + 1, gp_Vec2d(t.x, t.y));
                tf->SetValue(i + 1, it != tangents.end());
            }
            builder.Load(tv, tf, false);
        }
        builder.Perform();
        if (!builder.IsDone())
            throw std::runtime_error("spline interpolation failed");
        return builder.Curve();
    }

    // Geom2dAPI_PointsToBSpline cannot take end tangents
    Handle(Geom2d_BSplineCurve) Approximate(const std::vector<Vertex> &pts, double tol,
                                            const std::optional<Vertex> &startTangent,
                                            const std::optional<Vertex> &endTangent) {
        int n = (int)pts.size();
        AppDef_Array1OfMultiPointConstraint arr(1, n);
        for (int i = 0; i < n; ++i) {
            AppDef_MultiPointConstraint p(0, 1);
            p.SetPoint2d(1, gp_Pnt2d(pts[i].x, pts[i].y));
            arr.SetValue(i + 1, p);
        }
        if (startTangent)
            arr.ChangeValue(1).SetTang2d(1, gp_Vec2d(startTangent->x, startTangent->y));
        if (endTangent)
            arr.ChangeValue(n).SetTang2d(1, gp_Vec2d(endTangent->x, endTangent->y));
        // few points (e.g. a chain of two lines) cannot carry a cubic
        int degmin = std::min(3, n - 1);
        int continuity = degmin >= 3 ? 2 : degmin == 2 ? 1 : 0;
        try {
            // as Geom2dAPI_PointsToBSpline: 2 iterations, cutting, least squares below a tolerance of 1e-3
            AppDef_BSplineCompute computer(degmin, std::max(degmin, 8), 0.0, tol, 2, true, Approx_ChordLength,
                                           tol <= 1e-3);
            computer.SetContinuity(continuity);
            computer.SetConstraints(startTangent ? AppParCurves_TangencyPoint : AppParCurves_PassPoint,
                                    endTangent ? AppParCurves_TangencyPoint : AppParCurves_PassPoint);
            computer.Perform(AppDef_MultiLine(arr));
            const AppParCurves_MultiBSpCurve &c = computer.Value();
            TColgp_Array1OfPnt2d poles(1, c.NbPoles());
            c.Curve(1, poles);
            return new Geom2d_BSplineCurve(poles, c.Knots(), c.Multiplicities(), c.Degree());
        } catch (const Standard_Failure &) {
            return nullptr;
        }
    }

    std::optional<Curve<double, Vertex>> OccFittedCurve(std::span<const PrimitiveEdge> chain, const std::vector<Vertex> &pts, double tol,
                                                        const std::optional<Vertex> &startTangent,
                                                        const std::optional<Vertex> &endTangent) {
        if (const Handle(Geom2d_BSplineCurve) occ = Approximate(pts, tol, startTangent, endTangent); !occ.IsNull())
            if (Curve<double, Vertex> c = CurveOf(occ); Fits(c, chain, tol))
                return c;
        std::map<size_t, Vertex> tangents;
        if (startTangent)
            tangents[0] = *startTangent;
        if (endTangent)
            tangents[pts.size() - 1] = *endTangent;
        double S = 0.0;
        for (const Vertex &p : pts)
            S = std::max(S, NormInf(p));
        Curve<double, Vertex> c = CurveOf(OccInterpolate(pts, tangents, Tol(S)));
        return Fits(c, chain, tol) ? std::optional(std::move(c)) : std::nullopt;
    }

}
