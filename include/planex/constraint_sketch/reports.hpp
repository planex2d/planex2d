// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/sketch/geometry.hpp"


namespace planex2d::cs {

    struct ConstraintEntry {
        std::string kind, id, text;
    };

    struct ExpressionName {
        std::string id, measure;
    };

    struct ExpressionNames {
        std::vector<std::pair<std::string, bool>> dimensions;
        std::vector<std::pair<std::string, std::string>> others;
    };

    struct ConstraintInfo {
        std::string id, kind, text;
        std::vector<std::string> refs;
        std::optional<double> value;
        bool redundant, reference;
        bool derived = false, tie = false;
        bool over = false;
    };

    struct JacobianRow {
        std::string id;
        std::optional<size_t> row;
    };

    // the solver's Jacobian at the geometry now, its rows compressed: per residual row (the constraints, then the
    // arcs' own and the value equations) and free unknown (reference, coordinate) the derivative with respect to
    // the solver's scaled unknown; scale: the unknown (an angle in degrees) per scaled one; row_scale: the change of
    // a row's residual per unit of its value (System::RowScale)
    struct SketchJacobian {
        std::vector<JacobianRow> rows;
        std::vector<std::pair<std::string, std::string>> columns;
        std::vector<double> scale, row_scale, data;
        std::vector<size_t> indices, indptr;
    };

    struct FreeMotion {
        std::string drag;
        std::vector<std::string> moves;
        std::vector<std::pair<std::string, Vertex>> points;
        std::vector<std::pair<std::string, double>> values;
    };

    struct Suggestion {
        sketch::Kind kind;
        std::vector<std::string> refs;
        std::optional<double> value;
        std::string text, removes;
    };

}
