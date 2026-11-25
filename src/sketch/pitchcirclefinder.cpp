// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/pitchcirclefinder.hpp"

#include <algorithm>

namespace planex2d::sketch {

    PitchCircleFinder::PitchCircleFinder(const Geometry &g, const std::vector<char> &skip, std::optional<double> tol) {
        double size = 0.0;
        for (size_t k = 0; k < g.elements.size(); ++k) {
            const Element &e = g.elements[k];
            if (skip[k] || (e.kind != ElementKind::Circle && e.kind != ElementKind::Arc))
                continue;
            rounds.push_back({k, g.Coords(e.Center()), std::abs(g.values[e.Radius()]), e.kind == ElementKind::Circle});
            size = std::max(size, NormInf(rounds.back().c) + rounds.back().r);
        }
        nearby = tol ? *tol : Tol(size);
    }

    std::vector<FoundPitchCircle> PitchCircleFinder::Circles() const {
        std::vector<char> used(rounds.size(), 0);
        std::vector<FoundPitchCircle> out;
        for (size_t m = 0; m < rounds.size(); ++m) {
            std::vector<size_t> around;
            for (size_t i = 0; i < rounds.size(); ++i)
                if (rounds[i].circle && !used[i] && !rounds[i].c.IsClose(rounds[m].c, nearby))
                    around.push_back(i);
            const Vertex c = rounds[m].c;
            std::sort(around.begin(), around.end(), [&](size_t i, size_t j) {
                const long long x = std::llround(rounds[i].r / nearby), y = std::llround(rounds[j].r / nearby);
                return x != y ? x < y : Norm(rounds[i].c - c) < Norm(rounds[j].c - c);
            });
            for (size_t a = 0; a < around.size();) {
                size_t b = a + 1;
                while (b < around.size() && IsAlike(around[a], around[b], c))
                    ++b;
                std::vector<size_t> group(around.begin() + (std::ptrdiff_t)a, around.begin() + (std::ptrdiff_t)b);
                const bool alike = rounds[m].circle && std::abs(rounds[m].r - rounds[group.front()].r) <= nearby;
                if (group.size() >= 3 && !alike && IsEquallySpaced(group, c)) {
                    FoundPitchCircle found{rounds[m].e, c, Norm(rounds[group.front()].c - c), {}};
                    for (size_t i : group) {
                        found.holes.push_back(rounds[i].e);
                        used[i] = 1;
                    }
                    out.push_back(found);
                }
                a = b;
            }
        }
        return out;
    }

    bool PitchCircleFinder::IsEquallySpaced(std::vector<size_t> &group, const Vertex &c) const {
        std::sort(group.begin(), group.end(), [&](size_t i, size_t j) {
            const Vertex u = rounds[i].c - c, v = rounds[j].c - c;
            return std::atan2(u.y, u.x) < std::atan2(v.y, v.x);
        });
        const size_t n = group.size();
        std::vector<double> chords(n);
        for (size_t k = 0; k < n; ++k)
            chords[k] = Norm(rounds[group[(k + 1) % n]].c - rounds[group[k]].c);
        for (size_t gap = 0; gap < n; ++gap) {
            const double step = chords[(gap + 1) % n];
            bool equal = true;
            for (size_t k = 0; k < n && equal; ++k)
                equal = k == gap || std::abs(chords[k] - step) <= nearby;
            if (equal) {
                std::rotate(group.begin(), group.begin() + (std::ptrdiff_t)((gap + 1) % n), group.end());
                return true;
            }
        }
        return false;
    }

}
