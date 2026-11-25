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

#include "planex/offset/offsetform.hpp"
#include "planex/sketch/idbook.hpp"
#include "planex/sketch/offsetsides.hpp"


namespace planex2d::cs {

    struct PatternInfo {
        std::vector<std::string> originals;
        std::string ref;
        bool linear = true, equal = false;
        size_t ties = 0;
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> copies;
        std::vector<std::pair<std::map<std::string, size_t>, size_t>> before;
        std::vector<std::string> given;
        bool internal = true;
        // per copy its steps (along ref, along ref2)
        std::string ref2;
        size_t count = 0, count2 = 0;
        std::vector<std::pair<size_t, size_t>> steps;
        // the copies go against the line ref (ref2)
        bool reverse = false, reverse2 = false;

        bool IsGrid() const { return !ref2.empty(); }
        size_t CountNow(bool second) const {
            if (!IsGrid())
                return second ? 1 : copies.size() + 1;
            return second ? count2 : count;
        }
        std::string CountText(const std::string &id, const std::array<size_t, 2> &n) const {
            return id + ".count = " + std::to_string(n[0]) + (IsGrid() ? ", " + id + ".count2 = " + std::to_string(n[1]) : "");
        }

        bool HasOriginal(const std::string &id) const { return std::find(originals.begin(), originals.end(), id) != originals.end(); }
    };

    struct OffsetInfo {
        std::vector<std::string> sources, elements, ties;
        std::vector<char> forward;
        bool closed = false;
        std::vector<int> towards;
        std::string shape;
        std::set<std::tuple<int, size_t, int>> pieces;
        std::vector<std::pair<std::string, int>> rounds;
        mutable std::vector<double> checked;
        std::vector<std::pair<std::string, std::string>> curves;
        OffsetForms forms;

        sketch::OffsetChain Chain(const sketch::IdBook &book) const {
            sketch::OffsetChain source{{}, forward, closed};
            for (const std::string &id : sources)
                source.elements.push_back(book.byId.at(id).index);
            return source;
        }
        bool HasSource(const std::string &id) const { return std::find(sources.begin(), sources.end(), id) != sources.end(); }
        bool Owns(const std::string &id) const {
            return std::find(elements.begin(), elements.end(), id) != elements.end() || std::find(ties.begin(), ties.end(), id) != ties.end();
        }
    };

    struct ChamferInfo {
        std::array<std::string, 2> arm, cut, corner;
        bool reverse, angled;
    };

    struct MirrorInfo {
        std::vector<std::string> given;
        std::string axis;
        bool tie, internal;
    };

}
