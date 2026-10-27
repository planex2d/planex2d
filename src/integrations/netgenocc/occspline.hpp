// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include <Geom2d_BSplineCurve.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <gp_Pnt2d.hxx>

#include "core/vertex.hpp"
#include "curves/curve.hpp"

namespace planex2d {

    // OCC's B-spline curves and planex's: reading OCC curves in, px.BSpline's input, the export

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

    // an OCC curve as planex's, a periodic one in its clamped form on the same parameters
    inline Curve<double, Vertex> CurveOf(const Handle(Geom2d_BSplineCurve) &occ) {
        Handle(Geom2d_BSplineCurve) c = occ;
        if (c->IsPeriodic()) {
            c = Handle(Geom2d_BSplineCurve)::DownCast(occ->Copy());
            c->SetNotPeriodic();
        }
        Curve<double, Vertex> f;
        f.degree = (size_t)c->Degree();
        for (int i = 1; i <= c->NbKnots(); ++i)
            f.knots.insert(f.knots.end(), (size_t)c->Multiplicity(i), c->Knot(i));
        for (int i = 1; i <= c->NbPoles(); ++i) {
            f.poles.push_back({c->Pole(i).X(), c->Pole(i).Y()});
            if (c->IsRational())
                f.weights.push_back(c->Weight(i));
        }
        return f;
    }
    inline Handle(Geom2d_BSplineCurve) OccCurveOf(const Curve<double, Vertex> &c) {
        return MakeBSpline(BSplineData{c.poles, c.knots, c.weights, (int)c.degree}, false);
    }

}
