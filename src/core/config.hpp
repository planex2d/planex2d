// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>

namespace planex2d {

    struct SplineFit {
        bool enabled = false;
        double max_edge_length = 1e-4;
        double max_edge_angle = 30.0;  // degrees
        double tol = 1e-5;
        double ignore_edges_smaller = 1e-5;
    };

    struct ArcFit {
        bool enabled = false;
        double max_edge_length = 1e-4;
        double max_edge_angle = 30.0;  // degrees
        double tol = 1e-5;
        double ignore_edges_smaller = 1e-5;
    };

    // how a spline interpolates its points: planex's rules, or OCC's Geom2dAPI_Interpolate (netgen's splines)
    enum class SplineStyle { Px, Occ };

    struct Cleanup {
        double remove_edges_smaller = 1e-7;  // OCC's Precision::Confusion()
        double close_gaps_smaller = 1e-7;
        bool remove_free_edges = true;
        double merge_collinear_closer = 1e-7;
        // faces of less area than this are thrown away (0: none), or with merge_small_faces_instead united with the
        // face they share the longest boundary with
        double remove_faces_smaller = 0.0;
        bool merge_small_faces_instead = false;
    };

}

namespace planex2d::config {

    // Tol(S) = k * eps * S, S the largest coordinate magnitude involved
    inline double eps = 1e-15;
    inline double k = 10.0;
    // max deviation of a spline's arc approximation, relative to the diagonal of its points' bounding box
    inline double spline_tol = 1e-6;
    // the style of a spline drawn without style=
    inline SplineStyle spline_style = SplineStyle::Px;
    // the most edges a notebook picture of an object draws; above it the object shows its text repr
    inline int max_preview_edges = 5000;
    // constraint solver: solved when every residual is below solve_tol * the size of the geometry
    inline double solve_tol = 1e-12;
    // singular values of the constraints' Jacobian below rank_tol * the largest count as zero
    inline double rank_tol = 1e-9;
    // constraint solver: the most damped Gauss-Newton steps for one group of constraints
    inline size_t solve_max_iterations = 200;
    // a constraint sketch warns when a small change of its dimensions can move its geometry more than this many times
    // as much (nearly singular)
    inline double sensitivity_warning = 1e3;
    // a line shorter than this times the sketch's size has no direction for the branch check of a constraint sketch
    inline double branch_check_min_length = 1e-6;
    // two axis systems are the same (operands of a boolean, parts of one shape, a sketch's export at the xy plane) when
    // their origins are this close and their normals and x directions this many radians apart: OCC's
    // Precision::Confusion() and Precision::Angular()
    inline double axes_distance = 1e-7;
    inline double axes_angle = 1e-12;
    // receives the warnings instead of std::cerr when set; may throw
    inline void (*warning_handler)(const std::string &) = nullptr;
    // defaults of the OCC exports, overridden by their arguments
    inline ArcFit arc_fit;
    inline SplineFit spline_fit;
    inline Cleanup cleanup;

}

namespace planex2d {

    // relative to the magnitude S, and at least k * eps: below 1 a length of about 1e-14 is rounding
    inline double Tol(double S) { return config::k * config::eps * std::max(S, 1.0); }

    struct Warning {
        std::ostringstream text;

        template <class T>
        Warning &operator<<(const T &x) {
            text << x;
            return *this;
        }
        // a destructor is noexcept unless declared otherwise; the handler may throw
        ~Warning() noexcept(false) {
            if (config::warning_handler)
                config::warning_handler(text.str());
            else
                std::cerr << "planex warning: " << text.str() << std::endl;
        }
    };

}
