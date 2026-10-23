// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "core/vertex.hpp"
#include "offset/offsetpieces.hpp"

namespace planex2d::sketch {

    // the source of an offset: its lines and arcs as one chain in order, each running along it (forward) or against
    // it; or one circle (closed)
    struct OffsetChain {
        std::vector<size_t> elements;
        std::vector<char> forward;
        bool closed = false;
    };

    // the sides of an offset: of a closed chain outer and inner, named; of an open chain its band or one side
    using OffsetSides = std::vector<std::pair<std::string, std::vector<OffsetLoop>>>;

    // the cap round a keyhole's spine start becomes the hole of radius r: the sides start where their lines meet it,
    // so the cap's joints cross (no longer tangent)
    inline void OpenHole(OffsetSides &sides, double r, double ad) {
        const double reach = std::sqrt(r * r - ad * ad);
        for (auto &[fixed, loops] : sides)
            for (OffsetLoop &loop : loops) {
                const size_t m = loop.pieces.size();
                const size_t i = (size_t)(std::find_if(loop.pieces.begin(), loop.pieces.end(), [](const OffsetPiece &p) {
                                              return p.kind == OffsetPiece::Kind::Vertex && p.side == 0 && p.k == 0;
                                          }) - loop.pieces.begin());
                OffsetPiece &before = loop.pieces[(i + m - 1) % m], &cap = loop.pieces[i], &after = loop.pieces[(i + 1) % m];
                before.end = before.end - Normalized(before.end - before.start) * reach;
                after.start = after.start + Normalized(after.end - after.start) * reach;
                cap.start = before.end;
                cap.end = after.start;
                cap.r = r;
                loop.joints[i].tangent = false;
                loop.joints[(i + 1) % m].tangent = false;
            }
    }
    // the shape of an offset's sides: per loop its pieces (kind, edge or vertex, side) and how they meet (tangent at a
    // vertex, crossing), the loops in a fixed order
    inline std::string ShapeOf(const OffsetSides &sides) {
        std::string out;
        for (const auto &[fixed, loops] : sides) {
            std::vector<std::string> keys;
            for (const OffsetLoop &loop : loops) {
                std::string key = loop.closed ? "o" : "-";
                for (size_t i = 0; i < loop.joints.size(); ++i) {
                    key += loop.joints[i].tangent ? " t" + std::to_string(loop.joints[i].vertex) : std::string(" x");
                    if (i < loop.pieces.size()) {
                        const OffsetPiece &p = loop.pieces[i];
                        key += " " + std::to_string((int)p.kind) + "." + std::to_string(p.k) + "." + std::to_string(p.side);
                    }
                }
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            out += fixed + "{";
            for (const std::string &k : keys)
                out += k + ";";
            out += "}";
        }
        return out;
    }
    inline std::set<std::tuple<int, size_t, int>> SidePieces(const OffsetSides &sides) {
        std::set<std::tuple<int, size_t, int>> out;
        for (const auto &[fixed, loops] : sides)
            for (const OffsetLoop &loop : loops)
                for (const OffsetPiece &p : loop.pieces)
                    out.insert({(int)p.kind, p.k, p.side});
        return out;
    }

}
