// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "offset/offsetform.hpp"

// what the constraint sketch keeps of the calls whose results follow their source: patterns, offsets, chamfers, mirrors

namespace planex2d::cs {

    // a kept pattern: the elements it copies, its centre or direction, whether it spreads over the full turn, the
    // ties made so far, and per copy (k = 1, 2, ...) the element and constraint ids it made and the pattern's id
    // counters and ties before it (a count that shrinks and grows again gives the same ids)
    struct PatternInfo {
        std::vector<std::string> originals;
        std::string ref;
        bool linear = true, equal = false;
        size_t ties = 0;
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> copies;
        std::vector<std::pair<std::map<std::string, size_t>, size_t>> before;
        // the ids given, for the wires among them, which the copies copy
        std::vector<std::string> given;
        // the constraints among the originals shown on every copy (derived)
        bool internal = true;
        // a pattern of two directions: the second's line, its count, and per copy its steps (along ref, along ref2)
        std::string ref2;
        size_t count = 0, count2 = 0;
        std::vector<std::pair<size_t, size_t>> steps;

        bool IsGrid() const { return !ref2.empty(); }

        bool HasOriginal(const std::string &id) const { return std::find(originals.begin(), originals.end(), id) != originals.end(); }
    };

    // an offset with the constraints on: the ids of its source elements, of the elements it made (points too) and of
    // its ties, which it owns; what it was made as: its source's direction along the chain and whether it closes, its
    // sides (closed: +1 outer, -1 inner; open: 0 the band, +1 left, -1 right), its shape and pieces then
    // (sketch::ShapeOf), the source arcs and circles with the sign of their offsets' radii (r + d: +1, r - d: -1); the
    // source and distance at its last check
    struct OffsetInfo {
        std::vector<std::string> sources, elements, ties;
        std::vector<char> forward;
        bool closed = false;
        std::vector<int> towards;
        std::string shape;
        std::set<std::tuple<int, size_t, int>> pieces;
        std::vector<std::pair<std::string, int>> rounds;
        mutable std::vector<double> checked;
        // its offset curves, each with the spline it is the offset of
        std::vector<std::pair<std::string, std::string>> curves;
        OffsetForms forms;

        bool HasSource(const std::string &id) const { return std::find(sources.begin(), sources.end(), id) != sources.end(); }
        // id is one of its elements or ties
        bool Owns(const std::string &id) const {
            return std::find(elements.begin(), elements.end(), id) != elements.end() || std::find(ties.begin(), ties.end(), id) != ties.end();
        }
    };

    // a chamfer: the elements it cuts in drawn order, the cut points and the sharp corners as references (ids with
    // .start / .end), whether the first (the one taking d) is the one drawn second, and angle instead of d2
    struct ChamferInfo {
        std::array<std::string, 2> arm, cut, corner;
        bool reverse, angled;
    };

    // the mirror calls: the ids given, the axis, whether the copies are tied and show the constraints among the originals
    struct MirrorInfo {
        std::vector<std::string> given;
        std::string axis;
        bool tie, internal;
    };

}
