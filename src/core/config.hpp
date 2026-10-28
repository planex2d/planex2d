// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace planex2d {

    struct Vertex;
    struct PrimitiveEdge;
    template <class S, class P>
    struct Curve;

    // max_edge_length: none takes 1/20 of the length of the loop or open wire an edge lies in
    struct SplineFit {
        bool enabled = false;
        std::optional<double> max_edge_length;
        double max_edge_angle = 30.0;  // degrees
        double tol = 1e-5;
        double ignore_edges_smaller = 1e-5;
    };

    struct ArcFit {
        bool enabled = false;
        std::optional<double> max_edge_length;
        double max_edge_angle = 30.0;  // degrees
        double tol = 1e-5;
        double ignore_edges_smaller = 1e-5;
    };

    // how a spline interpolates its points: knots averaged from the parameters (The NURBS Book), or clamped with a knot at
    // every point as OCC's Geom2dAPI_Interpolate (netgen's splines)
    enum class SplineStyle { Averaged, Clamped };

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

    // the relations auto constraints find where they hold to rounding, each on or off; named_helpers: the drawing helpers
    // a call names (parallel=, tangent=, a to target, ...) become their constraints
    struct AutoConstraint {
        bool coincident = true, tangent = true, concentric = true, point_on_curve = true;
        bool horizontal = false, vertical = false, perpendicular = false, parallel = false;
        bool equal_radius = false, equal_length = false, collinear = false;
        bool named_helpers = true;
    };
    inline constexpr std::pair<const char *, bool AutoConstraint::*> autoConstraintKeys[] = {
        {"coincident", &AutoConstraint::coincident},       {"tangent", &AutoConstraint::tangent},
        {"concentric", &AutoConstraint::concentric},       {"point_on_curve", &AutoConstraint::point_on_curve},
        {"horizontal", &AutoConstraint::horizontal},       {"vertical", &AutoConstraint::vertical},
        {"perpendicular", &AutoConstraint::perpendicular}, {"parallel", &AutoConstraint::parallel},
        {"equal_radius", &AutoConstraint::equal_radius},   {"equal_length", &AutoConstraint::equal_length},
        {"collinear", &AutoConstraint::collinear},         {"named_helpers", &AutoConstraint::named_helpers}};

}

namespace planex2d::config {

    // the spline fit through pts within tol of the chain, with the end tangents if given: the OCC integration's, which
    // sets it when it loads; none without it
    using SplineFitter = std::optional<Curve<double, Vertex>> (*)(std::span<const PrimitiveEdge>, const std::vector<Vertex> &, double,
                                                                 const std::optional<Vertex> &, const std::optional<Vertex> &);

    struct State {
        // Tol(S) = k * eps * S, S the largest coordinate magnitude involved
        double eps = 1e-15;
        double k = 10.0;
        // max deviation of a spline's arc approximation, relative to the diagonal of its points' bounding box
        double spline_tol = 1e-6;
        // the style of a spline drawn without style=
        SplineStyle spline_style = SplineStyle::Averaged;
        // the most edges a notebook picture of an object draws; above it the object shows its text repr
        int max_preview_edges = 5000;
        // constraint solver: solved when every residual is below solve_tol * the size of the geometry
        double solve_tol = 1e-12;
        // singular values of the constraints' Jacobian below rank_tol * the largest count as zero
        double rank_tol = 1e-9;
        // constraint solver: the most damped Gauss-Newton steps for one group of constraints
        size_t solve_max_iterations = 200;
        // a constraint sketch warns when a small change of its dimensions can move its geometry more than this many
        // times as much (nearly singular)
        double sensitivity_warning = 1e3;
        // a line shorter than this times the sketch's size has no direction for the branch check of a constraint sketch
        double branch_check_min_length = 1e-6;
        // two axis systems are the same (operands of a boolean, parts of one shape, a sketch's export at the xy plane)
        // when their origins are this close and their normals and x directions this many radians apart: OCC's
        // Precision::Confusion() and Precision::Angular()
        double axes_distance = 1e-7;
        double axes_angle = 1e-12;
        // receives the warnings instead of std::cerr when set; may throw
        void (*warning_handler)(const std::string &) = nullptr;
        // a new constraint sketch's auto_constraint= when it is not given, and the relations auto_constraint=True finds
        bool auto_constraint = false;
        AutoConstraint auto_constraint_options;
        // defaults of the OCC exports, overridden by their arguments
        ArcFit arc_fit;
        SplineFit spline_fit;
        Cleanup cleanup;
        SplineFitter spline_fitter = nullptr;
        // the counters of the auto ids, one per kind ("Sketch", "Shape", "Mirror")
        std::map<std::string, size_t> id_counters;
    };

    // every extension module has its own copy of a header's inline variables: the OCC module points state at the core
    // module's when it loads, so both read and set the same settings
    inline State moduleState;
    inline State *state = &moduleState;

}

namespace planex2d {

    // relative to the magnitude S, and at least k * eps: below 1 a length of about 1e-14 is rounding
    inline double Tol(double S) { return config::state->k * config::state->eps * std::max(S, 1.0); }

    struct Warning {
        std::ostringstream text;

        template <class T>
        Warning &operator<<(const T &x) {
            text << x;
            return *this;
        }
        // a destructor is noexcept unless declared otherwise; the handler may throw
        ~Warning() noexcept(false) {
            if (config::state->warning_handler)
                config::state->warning_handler(text.str());
            else
                std::cerr << "planex warning: " << text.str() << std::endl;
        }
    };

}
