// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/vertex.hpp"
#include "sketch/geometry.hpp"

// what the constraint sketch reports to Python: its constraints, the names an expression may use, the solver's
// Jacobian, its free motions

namespace planex2d::cs {

    // a listed constraint: its kind, its id and the arguments of the Constrain call that makes it
    struct ConstraintEntry {
        std::string kind, id, text;
    };

    // a name in an expression: a dimension's id, or a reference and its measure (length, angle, r, sweep, h, v)
    struct ExpressionName {
        std::string id, measure;
    };

    // what an expression may name: the dimensions (id, whether an angle); and the other ids of constraints with
    // their kind, "count" for a pattern's count
    struct ExpressionNames {
        std::vector<std::pair<std::string, bool>> dimensions;
        std::vector<std::pair<std::string, std::string>> others;
    };

    // a constraint with the ids of what it relates, its value, and whether it is redundant or a reference
    struct ConstraintInfo {
        std::string id, kind, text;
        std::vector<std::string> refs;
        std::optional<double> value;
        bool redundant, reference;
        // shown on a copy, not solved; a copy's one tie
        bool derived = false, tie = false;
        // suspended, or in the set of conflicting constraints of one that is
        bool over = false;
    };

    // a row of the solver's Jacobian: the id of its constraint and its row there; an arc's own equations: the arc's
    // id; no row: the equation that holds the value of the dimension id (one an expression uses)
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

    // one way the geometry can still move: drag, the value that leads it ("Line.2.end.h", "Circle.1.r"); moves, the
    // elements it moves; per unit of the drag, how far every point (h, v) and other value (an angle in degrees) moves
    struct FreeMotion {
        std::string drag;
        std::vector<std::string> moves;
        std::vector<std::pair<std::string, Vertex>> points;
        std::vector<std::pair<std::string, double>> values;
    };

    // a constraint to add: its kind, the names it relates (an axis by "H" or "V"), its value if a dimension, the
    // arguments of its Constrain call; removes: the drag of the free motion a dimension removes
    struct Suggestion {
        sketch::Kind kind;
        std::vector<std::string> refs;
        std::optional<double> value;
        std::string text, removes;
    };

}
