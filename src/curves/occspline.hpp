// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <AppDef_Array1OfMultiPointConstraint.hxx>
#include <AppDef_BSplineCompute.hxx>
#include <AppDef_MultiLine.hxx>
#include <AppDef_MultiPointConstraint.hxx>
#include <AppParCurves_Constraint.hxx>
#include <AppParCurves_MultiBSpCurve.hxx>
#include <Approx_ParametrizationType.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_HArray1OfBoolean.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array1OfVec2d.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>

#include "core/vertex.hpp"

namespace planex2d {

    inline Vertex OccPoint(const Handle(Geom2d_BSplineCurve) &c, double u) {
        gp_Pnt2d p = c->Value(u);
        return {p.X(), p.Y()};
    }
    inline Vertex OccDerivative(const Handle(Geom2d_BSplineCurve) &c, double u) {
        gp_Pnt2d p;
        gp_Vec2d v;
        c->D1(u, p, v);
        return {v.X(), v.Y()};
    }
    inline double OccParam(const Handle(Geom2d_BSplineCurve) &c, const Vertex &x, double ua, double ub) {
        return Geom2dAPI_ProjectPointOnCurve(gp_Pnt2d(x.x, x.y), c, ua, ub).LowerDistanceParameter();
    }
    struct OccCurve {
        const Handle(Geom2d_BSplineCurve) &c;
        Vertex At(double u) const { return OccPoint(c, u); }
        Vertex Derivative(double u) const { return OccDerivative(c, u); }
        double Project(const Vertex &x, double ua, double ub) const { return OccParam(c, x, ua, ub); }
    };

    inline void CheckSpline(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents, bool periodic, double tol) {
        const auto point = [](const Vertex &p) { return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")"; };
        for (const Vertex &p : pts)
            IsFinite("Spline", {p.x, p.y});
        for (const auto &[i, t] : tangents)
            IsFinite("Spline tangent", {t.x, t.y});
        if (pts.size() < (periodic ? 3u : 2u))
            throw std::invalid_argument("Spline: " + std::to_string(pts.size()) + " points; a" + (periodic ? " periodic" : "") +
                                        " spline needs at least " + (periodic ? "3" : "2"));
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            if (Norm(pts[i + 1] - pts[i]) <= tol)
                throw std::invalid_argument("Spline: points " + std::to_string(i) + " and " + std::to_string(i + 1) + " coincide at " +
                                            point(pts[i]));
        if (periodic && Norm(pts.back() - pts.front()) <= tol)
            throw std::invalid_argument("Spline: the last point is the first; a periodic spline lists its start once");
        for (const auto &[i, t] : tangents) {
            if (i >= pts.size())
                throw std::invalid_argument("Spline: a tangent at point " + std::to_string(i) + ", but the points are 0 to " +
                                            std::to_string(pts.size() - 1));
            if (!(Norm(t) > 0.0))
                throw std::invalid_argument("Spline: the tangent at point " + std::to_string(i) + " is zero");
        }
    }

    inline Handle(TColgp_HArray1OfPnt2d) PointArray(const std::vector<Vertex> &pts) {
        Handle(TColgp_HArray1OfPnt2d) arr = new TColgp_HArray1OfPnt2d(1, (int)pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            arr->SetValue((int)i + 1, gp_Pnt2d(pts[i].x, pts[i].y));
        return arr;
    }

    // scaleTangents: OCC scales them by the point spacing (netgen's style "occ"); else they are made unit, the derivative
    // for chord length parameters, since OCC's scaling lets uneven points overshoot far past the end
    inline Handle(Geom2d_BSplineCurve) InterpolateOcc(const std::vector<Vertex> &pts, const std::map<size_t, Vertex> &tangents,
                                                      bool periodic, double tol, bool scaleTangents) {
        CheckSpline(pts, tangents, periodic, tol);
        const int n = (int)pts.size();
        Geom2dAPI_Interpolate builder(PointArray(pts), periodic, tol);
        if (!tangents.empty()) {
            // OCC reads only the flagged tangents
            const Vertex &dummy = tangents.begin()->second;
            TColgp_Array1OfVec2d tv(1, n);
            Handle(TColStd_HArray1OfBoolean) tf = new TColStd_HArray1OfBoolean(1, n);
            for (int i = 0; i < n; ++i) {
                const auto it = tangents.find((size_t)i);
                const Vertex t = it == tangents.end() ? dummy : scaleTangents ? it->second : Normalized(it->second);
                tv.SetValue(i + 1, gp_Vec2d(t.x, t.y));
                tf->SetValue(i + 1, it != tangents.end());
            }
            builder.Load(tv, tf, scaleTangents);
        }
        builder.Perform();
        if (!builder.IsDone())
            throw std::runtime_error("spline interpolation failed");
        return builder.Curve();
    }

    // Geom2dAPI_PointsToBSpline cannot take end tangents
    inline Handle(Geom2d_BSplineCurve) Approximate(const std::vector<Vertex> &pts, double tol,
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

    struct BSplineData {
        std::vector<Vertex> poles;
        std::vector<double> knots;
        std::vector<double> weights;  // empty if not rational
        int degree = 0;
    };

    inline Handle(Geom2d_BSplineCurve) MakeBSpline(const BSplineData &b, bool periodic) {
        std::vector<double> u;
        std::vector<int> mult;
        for (double k : b.knots) {
            if (!u.empty() && k == u.back()) {
                ++mult.back();
            } else {
                u.push_back(k);
                mult.push_back(1);
            }
        }
        for (const Vertex &p : b.poles)
            IsFinite("BSpline", {p.x, p.y});
        for (double x : b.knots)
            IsFinite("BSpline knot", {x});
        for (double x : b.weights)
            IsFinite("BSpline weight", {x});
        int n = (int)b.poles.size();
        int nu = (int)u.size();
        // OCC builds nothing sound from these and may crash on them
        if (n < 2 || nu < 2 || b.degree < 1 || (!b.weights.empty() && b.weights.size() != b.poles.size()))
            throw std::invalid_argument("invalid b-spline: " + std::to_string(n) + " poles, " + std::to_string(nu) + " distinct knots, "
                                        "degree " + std::to_string(b.degree) + (b.weights.empty() ? "" : ", " + std::to_string(b.weights.size()) + " weights") +
                                        "; it needs 2 poles and 2 distinct knots at least, degree 1 at least, a weight per pole");
        TColgp_Array1OfPnt2d poles(1, n);
        for (int i = 0; i < n; ++i)
            poles.SetValue(i + 1, gp_Pnt2d(b.poles[i].x, b.poles[i].y));
        TColStd_Array1OfReal knots(1, nu);
        TColStd_Array1OfInteger mults(1, nu);
        for (int i = 0; i < nu; ++i) {
            knots.SetValue(i + 1, u[i]);
            mults.SetValue(i + 1, mult[i]);
        }
        try {
            if (b.weights.empty())
                return new Geom2d_BSplineCurve(poles, knots, mults, b.degree, periodic);
            TColStd_Array1OfReal weights(1, n);
            for (int i = 0; i < n; ++i)
                weights.SetValue(i + 1, b.weights[i]);
            return new Geom2d_BSplineCurve(poles, weights, knots, mults, b.degree, periodic);
        } catch (const Standard_Failure &f) {
            throw std::invalid_argument(std::string("invalid b-spline: ") + f.GetMessageString());
        }
    }

}
